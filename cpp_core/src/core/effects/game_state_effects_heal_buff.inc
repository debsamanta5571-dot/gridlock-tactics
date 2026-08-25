// game_state_effects_heal_buff.inc — shard of register_default_effects()
    static_cast<void>(stack_manager.register_effect("dynamic_deployable_replicate", [](GameState& game, const StackItem& item) {
        if (item.source_entity_id.empty()) {
            return ActionResult{false, "dynamic_deployable_replicate requires a focus caster", {}};
        }
        const auto caster_it = game.board.all_entities_map.find(item.source_entity_id);
        if (caster_it == game.board.all_entities_map.end() || !caster_it->second
                || caster_it->second->current_health <= 0) {
            return ActionResult{true, "dynamic_deployable_replicate: caster is gone (fizzled)", {}};
        }
        const auto caster = caster_it->second;

        const auto resolved = resolve_stack_board_target(game, item, "dynamic_deployable_replicate");
        if (!resolved.status.ok || resolved.status.message.find("(fizzled)") != std::string::npos) {
            return resolved.status;
        }
        if (!resolved.target) {
            return ActionResult{false, "dynamic_deployable_replicate: no valid target", {}};
        }

        // Roll the caster's attack damage (prefer melee, fall back to ranged) and halve it.
        DamageRange caster_range{0, 0};
        int crit_pct = kDefaultCritChancePercent;
        if (const auto* u = dynamic_cast<const Unit*>(caster.get())) {
            if (u->attack_type == AttackType::Melee) {
                caster_range = unit_base_melee_damage_range(*u);
            } else {
                caster_range = unit_base_ranged_damage_range(*u);
            }
            crit_pct = u->crit_chance_percent;
        }
        const auto roll = roll_attack_damage(game.rng(), caster_range, crit_pct);
        const int half_damage = round_down_half(roll.final_damage);  // 0 is allowed

        // Deal the halved damage to the target.
        DamagePacket packet;
        packet.source = caster;
        packet.target = resolved.target;
        packet.amount = half_damage;
        packet.damage_type = DamageType::Physical;
        packet.pierces_damage_prevention = item.pierces_damage_prevention;
        packet.ability_grants_lifesteal = item.heals_on_damage_dealt;
        packet.source_label = item.source_name.empty() ? "Dynamic Deployable Duplicator" : item.source_name;
        packet.defer_reactive = true;
        apply_damage_packet(game, packet);

        // Spawn bot adjacent to the target (even if target was killed by the damage).
        // Only skip the spawn if we dealt 0 damage — a 0-stat bot serves no purpose.
        if (half_damage > 0) {
            spawn_dynamic_deployable_duplicator_bot(game, *caster, *resolved.target, half_damage);
        }

        return ActionResult{true,
            "Dynamic Deployable Duplicator dealt " + std::to_string(half_damage) + " to "
                + resolved.target->entity_id + " and spawned a bot",
            {}};
    }));

    // AoE: enemies in the 4×4 area around the target cell receive overload stacks;
    // allied units (not structures, not bases) receive a temporary movement speed boost until owner_turn_end.
    static_cast<void>(stack_manager.register_effect("shocking_stimulus_aoe", [](GameState& game, const StackItem& item) {
        const auto cx_it = item.targets.find(effect_keys::kCellX);
        const auto cy_it = item.targets.find(effect_keys::kCellY);
        if (cx_it == item.targets.end() || cy_it == item.targets.end()) {
            return ActionResult{false, "shocking_stimulus_aoe: missing target cell", {}};
        }
        const int cx = cx_it->second;
        const int cy = cy_it->second;

        // Collect the 4×4 area around the center cell (dx/dy each span -1..+2).
        std::vector<std::pair<int,int>> cells;
        cells.reserve(16);
        for (int dx = -1; dx <= 2; ++dx) {
            for (int dy = -1; dy <= 2; ++dy) {
                cells.emplace_back(cx + dx, cy + dy);
            }
        }

        const int overload_stacks = payload_int(item, "overload",  1);
        const int move_bonus      = payload_int(item, "movement",  1);

        int buffed_allies = 0;

        for (const auto& ent : entities_in_cells(game, cells)) {
            if (!ent || !ent->owner || entity_is_base(*ent)) continue;
            if (entity_is_structure(*ent)) continue;
            if (teams_hostile(game, *ent->owner, item.controller_id)) continue;
            // Allied unit: apply overload stacks and temporary movement boost.
            if (overload_stacks > 0) {
                game.apply_overload_stacks(ent, overload_stacks);
            }
            if (move_bonus > 0) {
                TemporaryEntityEffect move_eff;
                move_eff.effect_id              = "shocking_stimulus_movement";
                move_eff.source_id              = item.source_entity_id;
                move_eff.name                   = "Shocking Stimulus";
                move_eff.rules_text             = "Temporary +" + std::to_string(move_bonus) + " movement from Shocking Stimulus.";
                move_eff.expire_on              = "owner_turn_end";
                move_eff.remaining_turns        = 1;
                move_eff.stat_grants.bonus_movement = move_bonus;
                game.add_temporary_effect(ent, std::move(move_eff));
            }
            ++buffed_allies;
        }

        if (buffed_allies == 0) {
            return ActionResult{true, "Shocking Stimulus: no allied units in area (fizzled)", {}};
        }
        return ActionResult{true,
            "Shocking Stimulus buffed " + std::to_string(buffed_allies) + " allied unit(s) in the area",
            {}};
    }));

    // AoE: deal flat damage to every entity in a square radius around the target cell.
    // Used by Outdated Grenade (focus spell, radius 1 = 3×3, amount 1).
    static_cast<void>(stack_manager.register_effect("aoe_damage_square", [](GameState& game, const StackItem& item) {
        const auto cx_it = item.targets.find(effect_keys::kCellX);
        const auto cy_it = item.targets.find(effect_keys::kCellY);
        if (cx_it == item.targets.end() || cy_it == item.targets.end()) {
            return ActionResult{false, "aoe_damage_square: missing target cell", {}};
        }
        const int cx     = cx_it->second;
        const int cy     = cy_it->second;
        const int radius = payload_int(item, "radius", 1);
        const int amount = payload_int(item, effect_keys::kPayloadAmount, 1) + item.ability_damage_bonus;

        std::vector<std::pair<int, int>> cells;
        cells.reserve(static_cast<std::size_t>((2 * radius + 1) * (2 * radius + 1)));
        for (int dx = -radius; dx <= radius; ++dx) {
            for (int dy = -radius; dy <= radius; ++dy) {
                cells.emplace_back(cx + dx, cy + dy);
            }
        }

        std::shared_ptr<Entity> caster_ent;
        if (!item.source_entity_id.empty()) {
            const auto it = game.board.all_entities_map.find(item.source_entity_id);
            if (it != game.board.all_entities_map.end()) {
                caster_ent = it->second;
            }
        }

        int hit_count = 0;
        for (const auto& ent : entities_in_cells(game, cells)) {
            if (!ent || ent->current_health <= 0) { continue; }
            DamagePacket packet;
            packet.source                   = caster_ent;
            packet.target                   = ent;
            packet.amount                   = amount;
            packet.damage_type              = DamageType::Physical;
            packet.pierces_damage_prevention = item.pierces_damage_prevention;
            packet.ability_grants_lifesteal  = item.heals_on_damage_dealt;
            packet.source_label             = item.source_name.empty() ? "Outdated Grenade" : item.source_name;
            packet.defer_reactive           = true;
            apply_damage_packet(game, packet);
            ++hit_count;
        }
        if (hit_count == 0) {
            return ActionResult{true, "aoe_damage_square: no entities in area (fizzled)", {}};
        }
        return ActionResult{true,
            "aoe_damage_square hit " + std::to_string(hit_count) + " entity/entities", {}};
    }));
    // grant_aoe_stat_buff_turn_end: +attack/+health to allied units in a square area until active turn end.
    static_cast<void>(stack_manager.register_effect("grant_aoe_stat_buff_turn_end", [](GameState& game, const StackItem& item) {
        const auto cx_it = item.targets.find(effect_keys::kCellX);
        const auto cy_it = item.targets.find(effect_keys::kCellY);
        if (cx_it == item.targets.end() || cy_it == item.targets.end()) {
            return ActionResult{false, "grant_aoe_stat_buff_turn_end: missing target cell", {}};
        }
        const int cx = cx_it->second;
        const int cy = cy_it->second;
        const int radius = payload_int(item, "radius", 1);
        const int atk_bonus = payload_int(item, "attack", 3);
        const int hp_bonus = payload_int(item, "health", 3);
        const auto turn_owner = game.turn_manager.current_player();
        if (!turn_owner) {
            return ActionResult{false, "grant_aoe_stat_buff_turn_end: no active turn", {}};
        }

        std::vector<std::pair<int, int>> cells;
        cells.reserve(static_cast<std::size_t>((2 * radius + 1) * (2 * radius + 1)));
        for (int dx = -radius; dx <= radius; ++dx) {
            for (int dy = -radius; dy <= radius; ++dy) {
                cells.emplace_back(cx + dx, cy + dy);
            }
        }

        int buffed = 0;
        for (const auto& ent : entities_in_cells(game, cells)) {
            if (!ent || !ent->owner || entity_is_base(*ent) || !entity_can_move(*ent)) {
                continue;
            }
            if (teams_hostile(game, *ent->owner, item.controller_id)) {
                continue;
            }
            const std::string effect_id = "active_turn_buff_" + ent->entity_id + "_" + std::to_string(cx) + "_"
                                          + std::to_string(cy) + "_" + std::to_string(buffed);
            TemporaryEntityEffect eff;
            eff.effect_id = effect_id;
            eff.source_id = item.source_entity_id;
            eff.name = item.source_name.empty() ? "Stat Buff" : item.source_name;
            eff.rules_text = "+" + std::to_string(atk_bonus) + "/+" + std::to_string(hp_bonus)
                             + " until end of this turn.";
            eff.expire_on = "never";
            eff.remaining_turns = 0;
            eff.stat_grants.bonus_attack = atk_bonus;
            eff.stat_grants.bonus_health = hp_bonus;
            game.add_temporary_effect(ent, std::move(eff));
            game.active_turn_buff_pending_.push_back(
                ActiveTurnBuffEntry{*turn_owner, ent->entity_id, effect_id});
            ++buffed;
        }
        if (buffed == 0) {
            return ActionResult{true, "grant_aoe_stat_buff_turn_end: no allied units in area (fizzled)", {}};
        }
        return ActionResult{true,
            "Granted +" + std::to_string(atk_bonus) + "/+" + std::to_string(hp_bonus) + " to "
            + std::to_string(buffed) + " allied unit(s) until end of turn", {}};
    }));

    // grant_damage_buff_turn_end: +attack to one allied unit until the active turn ends.
    static_cast<void>(stack_manager.register_effect("grant_damage_buff_turn_end", [](GameState& game, const StackItem& item) {
        const auto resolved = resolve_stack_board_target(game, item, "grant_damage_buff_turn_end");
        if (!resolved.status.ok || resolved.status.message.find("(fizzled)") != std::string::npos) {
            return resolved.status;
        }
        if (!resolved.target || resolved.target->entity_type != "unit" || !entity_can_move(*resolved.target)) {
            return ActionResult{false, "grant_damage_buff_turn_end: requires an allied unit target", {}};
        }
        if (!resolved.target->owner || teams_hostile(game, item.controller_id, *resolved.target->owner)) {
            return ActionResult{false, "grant_damage_buff_turn_end: target must be allied", {}};
        }
        const int atk_bonus = std::max(0, payload_int(item, effect_keys::kPayloadAmount, 1));
        if (atk_bonus <= 0) {
            return ActionResult{true, "grant_damage_buff_turn_end: no bonus applied (fizzled)", {}};
        }
        const auto turn_owner = game.turn_manager.current_player();
        if (!turn_owner) {
            return ActionResult{false, "grant_damage_buff_turn_end: no active turn", {}};
        }
        const std::string effect_id = "damage_buff_turn_" + resolved.target->entity_id + "_"
            + (item.item_id.empty() ? "spell" : item.item_id);
        TemporaryEntityEffect eff;
        eff.effect_id = effect_id;
        eff.source_id = item.source_entity_id;
        eff.name = item.source_name.empty() ? "Damage Buff" : item.source_name;
        eff.rules_text = "+" + std::to_string(atk_bonus) + " damage until end of this turn.";
        eff.expire_on = "never";
        eff.remaining_turns = 0;
        eff.stat_grants.bonus_attack = atk_bonus;
        game.add_temporary_effect(resolved.target, std::move(eff));
        game.active_turn_buff_pending_.push_back(
            ActiveTurnBuffEntry{*turn_owner, resolved.target->entity_id, effect_id});
        return ActionResult{true,
            resolved.target->entity_id + " gains +" + std::to_string(atk_bonus)
            + " damage until end of turn", {}};
    }));


    // extend_aura_range: temporarily extends this unit's aura_range.
    // Payload: `amount` (default 1) — range bonus.
    // Optional payload: `remaining_turns` (int, default 1), string_payload `expire_on` (default "owner_turn_start").
    static_cast<void>(stack_manager.register_effect("extend_aura_range", [](GameState& game, const StackItem& item) {
        if (item.source_entity_id.empty()) {
            return ActionResult{false, "extend_aura_range requires a source entity", {}};
        }
        const auto src_it = game.board.all_entities_map.find(item.source_entity_id);
        if (src_it == game.board.all_entities_map.end() || !src_it->second
                || src_it->second->current_health <= 0) {
            return ActionResult{true, "extend_aura_range: source gone (fizzled)", {}};
        }
        const int bonus     = payload_int(item, "amount", 1);
        const int remaining = payload_int(item, "remaining_turns", 1);
        const std::string& expire_raw = payload_string(item, "expire_on");
        const std::string expire = expire_raw.empty() ? "owner_turn_start" : expire_raw;
        TemporaryEntityEffect eff;
        eff.effect_id        = "aura_range_boost";
        eff.source_id        = item.source_entity_id;
        eff.name             = item.source_name.empty() ? "Amplify" : item.source_name;
        eff.rules_text       = "Aura range boosted by " + std::to_string(bonus) + ".";
        eff.expire_on        = expire;
        eff.remaining_turns  = remaining;
        eff.stat_grants.bonus_aura_range = bonus;
        game.add_temporary_effect(src_it->second, std::move(eff));
        game.mark_passive_auras_dirty();
        return ActionResult{true,
            "Aura range extended by " + std::to_string(bonus), {}};
    }));

    // amplify_aura_stats: temporarily boosts the attack and health grants of this unit's allied auras.
    // Payload: `attack` (default 1), `health` (default 1).
    // Expires at owner_turn_start (lasts through the opponent's turn then falls off at the start of the next own turn).
    static_cast<void>(stack_manager.register_effect("amplify_aura_stats", [](GameState& game, const StackItem& item) {
        if (item.source_entity_id.empty()) {
            return ActionResult{false, "amplify_aura_stats requires a source entity", {}};
        }
        const auto src_it = game.board.all_entities_map.find(item.source_entity_id);
        if (src_it == game.board.all_entities_map.end() || !src_it->second
                || src_it->second->current_health <= 0) {
            return ActionResult{true, "amplify_aura_stats: source gone (fizzled)", {}};
        }
        const int atk_bonus    = payload_int(item, "attack", 0);
        const int hp_bonus     = payload_int(item, "health", 0);
        const int ab_dmg_bonus = payload_int(item, "ability_damage", 0);
        TemporaryEntityEffect eff;
        eff.effect_id       = "aura_stats_boost";
        eff.source_id       = item.source_entity_id;
        eff.name            = item.source_name.empty() ? "Inspire" : item.source_name;
        eff.rules_text      = "Aura temporarily strengthened until start of next turn.";
        eff.expire_on       = "owner_turn_start";
        eff.remaining_turns = 1;
        eff.stat_grants.bonus_aura_attack          = atk_bonus;
        eff.stat_grants.bonus_aura_health          = hp_bonus;
        eff.stat_grants.bonus_aura_ability_damage  = ab_dmg_bonus;
        game.add_temporary_effect(src_it->second, std::move(eff));
        game.mark_passive_auras_dirty();
        return ActionResult{true,
            "Aura boosted by +" + std::to_string(atk_bonus) + " atk, +"
            + std::to_string(hp_bonus) + " HP, +"
            + std::to_string(ab_dmg_bonus) + " ability dmg until start of next turn", {}};
    }));

    // apply_covering_fire: mark a target allied unit so that the source unit reacts with an attack
    // if anything attacks that target this turn. The reaction fires after any counterattack.
    // The marker is a TemporaryEntityEffect (expire_on owner_turn_start) on the protected unit.
    static_cast<void>(stack_manager.register_effect("apply_covering_fire", [](GameState& game, const StackItem& item) {
        const auto resolved = resolve_stack_board_target(game, item, "apply_covering_fire");
        if (!resolved.status.ok || resolved.status.message.find("(fizzled)") != std::string::npos) {
            return resolved.status;
        }
        if (!resolved.target || resolved.target->entity_type != "unit") {
            return ActionResult{false, "apply_covering_fire: target must be a unit", {}};
        }
        if (item.source_entity_id.empty()) {
            return ActionResult{false, "apply_covering_fire: no covering unit (source_entity_id missing)", {}};
        }
        const auto cov_it = game.board.all_entities_map.find(item.source_entity_id);
        if (cov_it == game.board.all_entities_map.end() || !cov_it->second ||
                cov_it->second->current_health <= 0) {
            return ActionResult{false, "apply_covering_fire: covering unit is not on the board", {}};
        }
        // Each cast adds a new stack — covering fire is stackable. Multiple stacks from the same
        // source each represent a separate queued reaction (one consumed per incoming attack).
        // Unused stacks expire at owner_turn_start via the TemporaryEntityEffect duration.
        TemporaryEntityEffect eff;
        eff.effect_id       = "covering_fire";
        eff.source_id       = item.source_entity_id;
        eff.name            = "Covering Fire";
        eff.rules_text      = "A covering unit will react and attack any enemy that attacks this unit.";
        eff.expire_on       = "owner_turn_start";
        eff.remaining_turns = 1;
        game.add_temporary_effect(resolved.target, std::move(eff));
        return ActionResult{true,
            cov_it->second->entity_id + " is now covering " + resolved.target->entity_id, {}};
    }));

    // Cover Shot (Callahan): apply covering fire to a target ally AND grant the caster +1 ranged
    // range until the start of their next turn. Both effects share the same owner_turn_start expiry
    // so they fall off together.
    static_cast<void>(stack_manager.register_effect("apply_covering_fire_range_buff",
            [](GameState& game, const StackItem& item) -> ActionResult {
        const auto resolved = resolve_stack_board_target(game, item, "apply_covering_fire_range_buff");
        if (!resolved.status.ok || resolved.status.message.find("(fizzled)") != std::string::npos) {
            return resolved.status;
        }
        if (!resolved.target || resolved.target->entity_type != "unit") {
            return ActionResult{false, "apply_covering_fire_range_buff: target must be a unit", {}};
        }
        if (item.source_entity_id.empty()) {
            return ActionResult{false, "apply_covering_fire_range_buff: no source entity", {}};
        }
        const auto cov_it = game.board.all_entities_map.find(item.source_entity_id);
        if (cov_it == game.board.all_entities_map.end() || !cov_it->second
                || cov_it->second->current_health <= 0) {
            return ActionResult{false, "apply_covering_fire_range_buff: source not on board", {}};
        }
        // Covering fire on target, with bonus_ranged_range encoded on the effect itself.
        // The extra range applies ONLY when the covering fire reaction fires — it is read
        // in combat_resolver.cpp by the covering fire loop before erasing the effect.
        TemporaryEntityEffect cf;
        cf.effect_id                   = "covering_fire";
        cf.source_id                   = item.source_entity_id;
        cf.name                        = "Covering Fire";
        cf.rules_text                  = "A covering unit will react and attack any enemy that attacks this unit.";
        cf.expire_on                   = "owner_turn_start";
        cf.remaining_turns             = 1;
        cf.stat_grants.bonus_ranged_range = 1;  // consumed as cover_bonus_range at reaction time
        game.add_temporary_effect(resolved.target, std::move(cf));
        return ActionResult{true,
            cov_it->second->entity_id + " covers " + resolved.target->entity_id + " (with +1 reaction range)", {}};
    }));

    // apply_covering_fire_stacks: apply N covering fire stacks to an allied target in one cast.
    // Each stack is an independent TemporaryEntityEffect — consumed one per incoming attack,
    // identical to the single stacks placed by apply_covering_fire.
    static_cast<void>(stack_manager.register_effect("apply_covering_fire_stacks",
            [](GameState& game, const StackItem& item) -> ActionResult {
        const auto resolved = resolve_stack_board_target(game, item, "apply_covering_fire_stacks");
        if (!resolved.status.ok || resolved.status.message.find("(fizzled)") != std::string::npos) {
            return resolved.status;
        }
        if (!resolved.target || resolved.target->entity_type != "unit") {
            return ActionResult{false, "apply_covering_fire_stacks: target must be a unit", {}};
        }
        if (item.source_entity_id.empty()) {
            return ActionResult{false, "apply_covering_fire_stacks: no source entity", {}};
        }
        const auto cov_it = game.board.all_entities_map.find(item.source_entity_id);
        if (cov_it == game.board.all_entities_map.end() || !cov_it->second
                || cov_it->second->current_health <= 0) {
            return ActionResult{false, "apply_covering_fire_stacks: covering unit not on board", {}};
        }
        const int stacks = payload_int(item, "amount", 1);
        for (int i = 0; i < stacks; ++i) {
            TemporaryEntityEffect cf;
            cf.effect_id       = "covering_fire";
            cf.source_id       = item.source_entity_id;
            cf.name            = "Covering Fire";
            cf.rules_text      = "A covering unit will react and attack any enemy that attacks this unit.";
            cf.expire_on       = "owner_turn_start";
            cf.remaining_turns = 1;
            game.add_temporary_effect(resolved.target, std::move(cf));
        }
        return ActionResult{true,
            cov_it->second->entity_id + " covers " + resolved.target->entity_id
            + " (" + std::to_string(stacks) + " stacks)", {}};
    }));

    // grant_stealth_self: add stealth stacks to the source unit without requiring a board target.
    // One stealth stack = one attack or reveal before the unit loses stealth.
    static_cast<void>(stack_manager.register_effect("grant_stealth_self",
            [](GameState& game, const StackItem& item) -> ActionResult {
        if (item.source_entity_id.empty()) {
            return ActionResult{false, "grant_stealth_self: requires source unit", {}};
        }
        const auto src_it = game.board.all_entities_map.find(item.source_entity_id);
        if (src_it == game.board.all_entities_map.end() || !src_it->second
                || src_it->second->current_health <= 0) {
            return ActionResult{true, "grant_stealth_self: source gone (fizzled)", {}};
        }
        const int amount = payload_int(item, "amount", 1);
        if (!add_entity_effect(*src_it->second, "stealth", amount)) {
            return ActionResult{true, src_it->second->entity_id + " is immune to stealth (fizzled)", {}};
        }
        return ActionResult{true, src_it->second->entity_id + " goes dark (stealth x" + std::to_string(amount) + ")", {}};
    }));


    static_cast<void>(stack_manager.register_effect("grant_evasive_self",
            [](GameState& game, const StackItem& item) -> ActionResult {
        if (item.source_entity_id.empty()) {
            return ActionResult{false, "grant_evasive_self: requires source unit", {}};
        }
        const auto src_it = game.board.all_entities_map.find(item.source_entity_id);
        if (src_it == game.board.all_entities_map.end() || !src_it->second
                || src_it->second->current_health <= 0) {
            return ActionResult{true, "grant_evasive_self: source gone (fizzled)", {}};
        }
        const int amount = payload_int(item, "amount", 1);
        if (!add_entity_effect(*src_it->second, "evasive", amount)) {
            return ActionResult{true, src_it->second->entity_id + " cannot gain Evasive (fizzled)", {}};
        }
        return ActionResult{true, src_it->second->entity_id + " gains Evasive for " + std::to_string(amount)
            + " turn(s)", {}};
    }));

    // High Explosive Round (Voltcrusher Dreadnought): fire a shell at a target enemy using LOS
    // (validated before this handler runs). The shell may be body-intercepted by units along its
    // path. Wherever it ends up hitting, detonate for `amount` damage in a 3×3 area (ignores LOS).
    static_cast<void>(stack_manager.register_effect("high_explosive_round", [](GameState& game, const StackItem& item) {
        const auto cx_it = item.targets.find(effect_keys::kCellX);
        const auto cy_it = item.targets.find(effect_keys::kCellY);
        if (cx_it == item.targets.end() || cy_it == item.targets.end()) {
            return ActionResult{false, "high_explosive_round: missing target cell", {}};
        }
        const int target_x = cx_it->second;
        const int target_y = cy_it->second;
        const int damage = payload_int(item, effect_keys::kPayloadAmount, 4) + item.ability_damage_bonus;

        std::shared_ptr<Entity> caster_ent;
        if (!item.source_entity_id.empty()) {
            const auto it = game.board.all_entities_map.find(item.source_entity_id);
            if (it != game.board.all_entities_map.end()) {
                caster_ent = it->second;
            }
        }

        // Resolve the actual explosion center. If a unit is in the shot path, it may intercept
        // (50% per blocking unit-cell) and the shell detonates at the interceptor instead.
        std::pair<int, int> explosion_center = {target_x, target_y};
        const auto intended_target_ptr = game.board.entity_at(target_x, target_y);
        if (caster_ent && intended_target_ptr && intended_target_ptr->entity_type == "unit") {
            DamagePacket probe;
            probe.source = caster_ent;
            probe.target = intended_target_ptr;
            probe.amount = 0; // Not applied — used only to identify the actual hit entity.
            probe.damage_type = DamageType::Physical;
            const bool intercepted = maybe_redirect_ranged_attack_to_blocking_unit(
                game, *caster_ent, {target_x, target_y}, *intended_target_ptr, probe, nullptr);
            if (intercepted && probe.target && probe.target->position) {
                explosion_center = *probe.target->position;
            }
        }

        // AoE explosion — 3×3 around explosion_center, ignores LOS.
        constexpr int kRadius = 1;
        std::vector<std::pair<int, int>> cells;
        cells.reserve(9);
        for (int dx = -kRadius; dx <= kRadius; ++dx) {
            for (int dy = -kRadius; dy <= kRadius; ++dy) {
                cells.emplace_back(explosion_center.first + dx, explosion_center.second + dy);
            }
        }

        int hit_count = 0;
        for (const auto& ent : entities_in_cells(game, cells)) {
            if (!ent || ent->current_health <= 0) { continue; }
            DamagePacket packet;
            packet.source                    = caster_ent;
            packet.target                    = ent;
            packet.amount                    = damage;
            packet.damage_type               = DamageType::Physical;
            packet.pierces_damage_prevention = item.pierces_damage_prevention;
            packet.ability_grants_lifesteal  = item.heals_on_damage_dealt;
            packet.source_label              = item.source_name.empty() ? "High Explosive Round" : item.source_name;
            packet.defer_reactive            = true;
            apply_damage_packet(game, packet);
            ++hit_count;
        }
        if (hit_count == 0) {
            return ActionResult{true, "high_explosive_round: no entities in blast area (fizzled)", {}};
        }
        return ActionResult{true,
            "High Explosive Round detonated at (" + std::to_string(explosion_center.first) + ","
            + std::to_string(explosion_center.second) + ") hitting "
            + std::to_string(hit_count) + " entity/entities", {}};
    }));

    // Artillery Mode (Voltcrusher Dreadnought): grant trueshot and +1 ranged range until end of turn.
    static_cast<void>(stack_manager.register_effect("artillery_mode", [](GameState& game, const StackItem& item) {
        if (item.source_entity_id.empty()) {
            return ActionResult{false, "artillery_mode requires a source entity", {}};
        }
        const auto src_it = game.board.all_entities_map.find(item.source_entity_id);
        if (src_it == game.board.all_entities_map.end() || !src_it->second
                || src_it->second->current_health <= 0) {
            return ActionResult{true, "artillery_mode: source gone (fizzled)", {}};
        }
        const int range_bonus    = payload_int(item, "range_bonus", 1);
        const int deadzone_bonus = payload_int(item, "deadzone_bonus", 1);
        TemporaryEntityEffect eff;
        eff.effect_id        = "artillery_mode_buff";
        eff.source_id        = item.source_entity_id;
        eff.name             = "Artillery Mode";
        eff.rules_text       = "Trueshot, +" + std::to_string(range_bonus) + " range, deadzone "
                               + std::to_string(deadzone_bonus) + " until start of next turn.";
        eff.expire_on        = "owner_turn_start";
        eff.remaining_turns  = 1;
        eff.granted_attributes.push_back({"trueshot", std::nullopt});
        eff.stat_grants.bonus_ranged_range   = range_bonus;
        eff.stat_grants.bonus_ranged_deadzone = deadzone_bonus;
        game.add_temporary_effect(src_it->second, std::move(eff));
        return ActionResult{true,
            "Artillery Mode: trueshot, +" + std::to_string(range_bonus) + " range, deadzone "
            + std::to_string(deadzone_bonus) + " until start of next turn", {}};
    }));


    // whirlwind_spray: grant Whirlwind and adjust ranged range until owner_turn_end.

    // grant_cleave_self: grant Cleave until owner_turn_end (Miasma Billow).
    static_cast<void>(stack_manager.register_effect("grant_first_strike_self", [](GameState& game, const StackItem& item) {
        if (item.source_entity_id.empty()) {
            return ActionResult{false, "grant_first_strike_self requires a source entity", {}};
        }
        const auto src_it = game.board.all_entities_map.find(item.source_entity_id);
        if (src_it == game.board.all_entities_map.end() || !src_it->second
                || src_it->second->current_health <= 0) {
            return ActionResult{true, "grant_first_strike_self: source gone (fizzled)", {}};
        }
        TemporaryEntityEffect eff;
        eff.effect_id        = "grant_first_strike_self_buff";
        eff.source_id        = item.source_entity_id;
        eff.name             = "First Strike";
        eff.rules_text       = "First Strike until end of turn.";
        eff.expire_on        = "owner_turn_end";
        eff.remaining_turns  = 1;
        eff.granted_attributes.push_back({"first_strike", std::nullopt});
        game.add_temporary_effect(src_it->second, std::move(eff));
        return ActionResult{true, src_it->second->entity_id + " gains First Strike until end of turn", {}};
    }));

    static_cast<void>(stack_manager.register_effect("apply_valiant_guard_self", [](GameState& game, const StackItem& item) {
        if (item.source_entity_id.empty()) {
            return ActionResult{false, "apply_valiant_guard_self requires a source entity", {}};
        }
        const auto src_it = game.board.all_entities_map.find(item.source_entity_id);
        if (src_it == game.board.all_entities_map.end() || !src_it->second
                || src_it->second->current_health <= 0) {
            return ActionResult{true, "apply_valiant_guard_self: source gone (fizzled)", {}};
        }
        TemporaryEntityEffect eff;
        eff.effect_id       = effect_keys::kValiantGuardEffectId;
        eff.source_id       = item.source_entity_id;
        eff.name            = "Valiant Guard";
        eff.rules_text      = "The next time a surrounding 1×1 ally is targeted by an attack, swap positions and take that attack.";
        eff.expire_on       = "owner_turn_start";
        eff.remaining_turns = 1;
        game.add_temporary_effect(src_it->second, std::move(eff));
        return ActionResult{true, src_it->second->entity_id + " stands ready to guard surrounding allies", {}};
    }));

