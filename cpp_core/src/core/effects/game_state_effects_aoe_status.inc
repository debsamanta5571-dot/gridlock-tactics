// game_state_effects_aoe_status.inc — shard of register_default_effects()
    static_cast<void>(stack_manager.register_effect("apply_barrier_self", [](GameState& game, const StackItem& item) {
        if (item.source_entity_id.empty()) {
            return ActionResult{false, "apply_barrier_self requires a source entity", {}};
        }
        const auto src_it = game.board.all_entities_map.find(item.source_entity_id);
        if (src_it == game.board.all_entities_map.end() || !src_it->second
                || src_it->second->current_health <= 0) {
            return ActionResult{true, "apply_barrier_self: source gone (fizzled)", {}};
        }
        int amount = 1;
        if (const auto it = item.payload.find(effect_keys::kPayloadAmount); it != item.payload.end()) {
            amount = it->second;
        }
        amount = std::max(0, amount);
        if (!add_entity_effect(*src_it->second, "barrier", amount, effect_keys::kBarrierOwnerTurnEndSource)) {
            return ActionResult{true, src_it->second->entity_id + " is immune to barrier (fizzled)", {}};
        }
        return ActionResult{true, src_it->second->entity_id + " gains " + std::to_string(amount)
            + " barrier until end of this turn", {}};
    }));

    static_cast<void>(stack_manager.register_effect("grant_cleave_self", [](GameState& game, const StackItem& item) {
        if (item.source_entity_id.empty()) {
            return ActionResult{false, "grant_cleave_self requires a source entity", {}};
        }
        const auto src_it = game.board.all_entities_map.find(item.source_entity_id);
        if (src_it == game.board.all_entities_map.end() || !src_it->second
                || src_it->second->current_health <= 0) {
            return ActionResult{true, "grant_cleave_self: source gone (fizzled)", {}};
        }
        TemporaryEntityEffect eff;
        eff.effect_id        = "grant_cleave_self_buff";
        eff.source_id        = item.source_entity_id;
        eff.name             = "Cleave";
        eff.rules_text       = "Cleave until end of turn.";
        eff.expire_on        = "owner_turn_end";
        eff.remaining_turns  = 1;
        eff.granted_attributes.push_back({"cleave", std::nullopt});
        game.add_temporary_effect(src_it->second, std::move(eff));
        return ActionResult{true, src_it->second->entity_id + " gains Cleave until end of turn", {}};
    }));

    static_cast<void>(stack_manager.register_effect("whirlwind_spray", [](GameState& game, const StackItem& item) {
        if (item.source_entity_id.empty()) {
            return ActionResult{false, "whirlwind_spray requires a source entity", {}};
        }
        const auto src_it = game.board.all_entities_map.find(item.source_entity_id);
        if (src_it == game.board.all_entities_map.end() || !src_it->second
                || src_it->second->current_health <= 0) {
            return ActionResult{true, "whirlwind_spray: source gone (fizzled)", {}};
        }
        const int range_bonus = payload_int(item, "range_bonus", -1);
        TemporaryEntityEffect eff;
        eff.effect_id        = "whirlwind_spray_buff";
        eff.source_id        = item.source_entity_id;
        eff.name             = "Whirlwind Spray";
        eff.rules_text       = "Whirlwind; ranged range "
            + std::to_string(range_bonus) + " until end of turn.";
        eff.expire_on        = "owner_turn_end";
        eff.remaining_turns  = 1;
        eff.granted_attributes.push_back({"whirlwind", std::nullopt});
        eff.stat_grants.bonus_ranged_range = range_bonus;
        game.add_temporary_effect(src_it->second, std::move(eff));
        return ActionResult{true,
            "Whirlwind Spray: whirlwind active, range "
            + std::to_string(range_bonus) + " until end of turn", {}};
    }));

    static_cast<void>(stack_manager.register_effect("grant_permanent_stat_growth_self", [](GameState& game, const StackItem& item) {
        if (item.source_entity_id.empty()) {
            return ActionResult{false, "grant_permanent_stat_growth_self requires a source unit", {}};
        }
        const auto src_it = game.board.all_entities_map.find(item.source_entity_id);
        if (src_it == game.board.all_entities_map.end() || !src_it->second) {
            return ActionResult{false, "Source unit is no longer on the board", {}};
        }
        auto unit = std::dynamic_pointer_cast<Unit>(src_it->second);
        if (!unit) {
            return ActionResult{false, "Source is not a unit", {}};
        }
        int health_gain = 1;
        int attack_gain = 1;
        if (const auto it = item.payload.find("health"); it != item.payload.end()) {
            health_gain = it->second;
        }
        if (const auto it = item.payload.find("attack"); it != item.payload.end()) {
            attack_gain = it->second;
        }
        if (const auto it = item.payload.find(effect_keys::kPayloadAmount); it != item.payload.end()) {
            if (!item.payload.contains("health") && !item.payload.contains("attack")) {
                health_gain = attack_gain = it->second;
            }
        }
        health_gain = std::max(0, health_gain);
        attack_gain = std::max(0, attack_gain);
        apply_permanent_stat_growth_to_unit(*unit, health_gain, attack_gain);
        game.refresh_passive_auras();
        return ActionResult{true,
            unit->entity_id + " gained +" + std::to_string(health_gain) + " max HP and +" + std::to_string(attack_gain) + " damage permanently",
            {}};
    }));

    // grant_permanent_aura_ability_damage: permanently increases bonus_ability_damage on all
    // allied_units passives on the source entity. Used by Press the Advantage (The Boss).
    static_cast<void>(stack_manager.register_effect("grant_permanent_aura_ability_damage", [](GameState& game, const StackItem& item) {
        if (item.source_entity_id.empty()) {
            return ActionResult{false, "grant_permanent_aura_ability_damage requires a source entity", {}};
        }
        const auto src_it = game.board.all_entities_map.find(item.source_entity_id);
        if (src_it == game.board.all_entities_map.end() || !src_it->second
                || src_it->second->current_health <= 0) {
            return ActionResult{true, "grant_permanent_aura_ability_damage: source gone (fizzled)", {}};
        }
        const int amount = payload_int(item, "amount", 1);
        int upgraded = 0;
        for (auto& passive : src_it->second->passive_abilities) {
            if (passive.applies_to == "allied_units") {
                passive.stat_grants.bonus_ability_damage += amount;
                ++upgraded;
            }
        }
        if (upgraded == 0) {
            return ActionResult{true, "grant_permanent_aura_ability_damage: no allied_units aura found (fizzled)", {}};
        }
        game.refresh_passive_auras();
        return ActionResult{true,
            src_it->second->entity_id + ": Command Presence permanently +" + std::to_string(amount) + " ability damage", {}};
    }));

    // Self-targeted overload: applies overload stacks directly to the source unit.
    // Used by abilities like Volt Surge. No board target required.
    static_cast<void>(stack_manager.register_effect("apply_overload_self", [](GameState& game, const StackItem& item) {
        if (item.source_entity_id.empty()) {
            return ActionResult{false, "apply_overload_self requires a source unit", {}};
        }
        const auto src_it = game.board.all_entities_map.find(item.source_entity_id);
        if (src_it == game.board.all_entities_map.end() || !src_it->second) {
            return ActionResult{false, "apply_overload_self: source not on board (fizzled)", {}};
        }
        const int amount = payload_int(item, effect_keys::kPayloadAmount, 2);
        const std::string msg = game.apply_overload_stacks(src_it->second, amount);
        return ActionResult{true, msg.empty() ? "apply_overload_self fizzled" : msg, {}};
    }));

    // Board-targeted version: applies permanent stat growth to the resolved target (ally or enemy).
    // Used by spells like Spark of Resonance. Shares payload keys with the _self variant.
    static_cast<void>(stack_manager.register_effect("grant_permanent_stat_growth", [](GameState& game, const StackItem& item) {
        const auto resolved = resolve_stack_board_target(game, item, "grant_permanent_stat_growth");
        if (!resolved.status.ok || resolved.status.message.find("(fizzled)") != std::string::npos) {
            return resolved.status;
        }
        auto unit = std::dynamic_pointer_cast<Unit>(resolved.target);
        if (!unit) {
            return ActionResult{false, "grant_permanent_stat_growth: target is not a unit", {}};
        }
        int health_gain = 1;
        int attack_gain = 1;
        if (const auto it = item.payload.find("health"); it != item.payload.end()) {
            health_gain = it->second;
        }
        if (const auto it = item.payload.find("attack"); it != item.payload.end()) {
            attack_gain = it->second;
        }
        if (const auto it = item.payload.find(effect_keys::kPayloadAmount); it != item.payload.end()) {
            if (!item.payload.contains("health") && !item.payload.contains("attack")) {
                health_gain = attack_gain = it->second;
            }
        }
        health_gain = std::max(0, health_gain);
        attack_gain = std::max(0, attack_gain);
        if (health_gain > 0) {
            unit->base_health += health_gain;
            unit->current_health += health_gain;
        }
        if (attack_gain > 0) {
            sync_unit_damage_ranges_from_nominal(*unit);
            unit->melee_damage_min += attack_gain;
            unit->melee_damage_max += attack_gain;
            unit->melee_damage += attack_gain;
            if (unit->is_ranged || unit->attack_type == AttackType::Ranged) {
                unit->ranged_damage_min += attack_gain;
                unit->ranged_damage_max += attack_gain;
                unit->ranged_damage += attack_gain;
            }
        }
        game.refresh_passive_auras();
        return ActionResult{true,
            unit->entity_id + " gained +" + std::to_string(health_gain) + " max HP and +" + std::to_string(attack_gain) + " damage permanently",
            {}};
    }));

    static_cast<void>(stack_manager.register_effect("grant_bonus_move", [](GameState& game, const StackItem& item) {
        const auto resolved = resolve_stack_board_target(game, item, "grant_bonus_move");
        if (!resolved.status.ok || resolved.status.message.find("(fizzled)") != std::string::npos) {
            return resolved.status;
        }
        // Crushing Advance: this unit cannot gain bonus move-points from any source.
        if (entity_has_attribute(*resolved.target, "crushes_on_move")) {
            return ActionResult{true, resolved.target->entity_id + " has Crushing Advance — bonus move fizzled", {}};
        }
        const int amount   = payload_int(item, "amount",   1);
        const int overload = payload_int(item, "overload", 0);
        TemporaryEntityEffect eff;
        eff.effect_id       = "bonus_move_grant";
        eff.source_id       = item.source_entity_id;
        eff.name            = "Bonus Move";
        eff.rules_text      = "Extra move action(s) that expire at end of owner's turn.";
        eff.expire_on       = "owner_turn_end";
        eff.remaining_turns = 1;
        eff.stat_grants.bonus_moves = amount;
        // add_temporary_effect immediately credits moves_remaining_this_turn.
        const std::string target_id = resolved.target->entity_id;
        game.add_temporary_effect(resolved.target, std::move(eff));
        if (overload > 0) {
            game.apply_overload_stacks(resolved.target, overload);
        }
        return ActionResult{true,
            target_id + " granted +" + std::to_string(amount) + " move(s) until end of turn"
            + (overload > 0 ? " and " + std::to_string(overload) + " overload" : ""), {}};
    }));

    // -----------------------------------------------------------------------
    // aoe_damage_surrounding — automated passive pulse (e.g. Macrowave).
    // Fired with source_entity_id set; damages all 8-way surrounding units by `amount`.
    // Hits allies and enemies alike (card text: "all surrounding units"). No structures, no bases.
    // -----------------------------------------------------------------------
    static_cast<void>(stack_manager.register_effect("aoe_damage_surrounding", [](GameState& game, const StackItem& item) {
        const int amount = payload_int(item, effect_keys::kPayloadAmount, 0);
        if (amount <= 0) {
            return ActionResult{true, "aoe_damage_surrounding: no damage", {}};
        }
        const auto src_it = game.board.all_entities_map.find(item.source_entity_id);
        if (src_it == game.board.all_entities_map.end() || !src_it->second || !src_it->second->position) {
            return ActionResult{true, "aoe_damage_surrounding: source gone (fizzled)", {}};
        }
        const Entity& source = *src_it->second;

        std::vector<std::shared_ptr<Entity>> victims;
        std::set<std::string> seen;
        for (const auto& [cx, cy] : entity_surrounding_cells(source)) {
            auto ent = game.board.entity_at(cx, cy);
            if (!ent || !seen.insert(ent->entity_id).second) continue;
            if (ent->entity_type != "unit") continue;  // "all units" means units only; structures and bases excluded
            victims.push_back(ent);
        }
        if (victims.empty()) {
            return ActionResult{true, "aoe_damage_surrounding: no units in range", {}};
        }

        std::vector<std::shared_ptr<Entity>> to_destroy;
        for (const auto& v : victims) {
            if (!v || !game.board.all_entities_map.contains(v->entity_id)) continue;
            apply_incoming_damage(*v, amount, /*pierce=*/false);
            if (v->current_health <= 0) to_destroy.push_back(v);
        }
        for (const auto& v : to_destroy) {
            if (game.board.all_entities_map.contains(v->entity_id)) game.destroy_board_entity(v);
        }
        game.refresh_passive_auras();

        return ActionResult{true,
            "Macrowave Pulse: dealt " + std::to_string(amount) + " damage to "
            + std::to_string(victims.size()) + " surrounding unit(s)", {}};
    }));

    // -----------------------------------------------------------------------
    // thundering_vale — focus spell: damage and push orthogonally adjacent units away from caster.
    // Payload: amount (damage), push (tiles). No board target; uses source_entity_id.
    // Uses entity_adjacent_cells (4-way), not entity_surrounding_cells (8-way).
    // -----------------------------------------------------------------------
    static_cast<void>(stack_manager.register_effect("thundering_vale", [](GameState& game, const StackItem& item) {
        if (item.source_entity_id.empty()) {
            return ActionResult{false, "thundering_vale: requires a focus caster", {}};
        }
        const auto src_it = game.board.all_entities_map.find(item.source_entity_id);
        if (src_it == game.board.all_entities_map.end() || !src_it->second
                || src_it->second->current_health <= 0) {
            return ActionResult{true, "thundering_vale: caster gone (fizzled)", {}};
        }
        const Entity& source = *src_it->second;
        const int damage_amount = payload_int(item, effect_keys::kPayloadAmount, 0);
        const int push_distance = payload_int(item, "push", 0);
        if (damage_amount <= 0 && push_distance <= 0) {
            return ActionResult{true, "thundering_vale: no effect", {}};
        }

        std::vector<std::shared_ptr<Entity>> victims;
        std::set<std::string> seen;
        for (const auto& [cx, cy] : entity_adjacent_cells(source)) {
            auto ent = game.board.entity_at(cx, cy);
            if (!ent || ent->entity_id == source.entity_id) {
                continue;
            }
            if (ent->entity_type != "unit") {
                continue;
            }
            if (!seen.insert(ent->entity_id).second) {
                continue;
            }
            victims.push_back(ent);
        }
        if (victims.empty()) {
            return ActionResult{true, "thundering_vale: no adjacent units", {}};
        }

        std::string msg;
        std::vector<std::shared_ptr<Entity>> to_destroy;
        for (const auto& victim : victims) {
            if (!victim || !game.board.all_entities_map.contains(victim->entity_id)) {
                continue;
            }
            if (damage_amount > 0) {
                DamagePacket packet;
                packet.source = src_it->second;
                packet.target = victim;
                packet.amount = damage_amount;
                packet.damage_type = damage_type_from_stack_payload(item.payload);
                packet.pierces_damage_prevention = item.pierces_damage_prevention;
                packet.ability_grants_lifesteal = item.heals_on_damage_dealt;
                packet.ability_grants_soul_steal = item.heals_allied_base_on_damage_dealt;
                packet.from_melee_attack = false;
                packet.defer_reactive = true;
                packet.source_label = item.source_name.empty() ? "thundering_vale" : item.source_name;
                const auto hit = apply_damage_packet(game, packet);
                if (!hit.ok) {
                    return hit;
                }
                if (!msg.empty()) {
                    msg += "; ";
                }
                msg += hit.message;
                if (victim->current_health <= 0) {
                    to_destroy.push_back(victim);
                }
            }
        }
        for (const auto& victim : to_destroy) {
            if (game.board.all_entities_map.contains(victim->entity_id)) {
                game.destroy_board_entity(victim);
            }
        }

        if (push_distance > 0) {
            PushDisplacementOptions opts;
            opts.collision_source_label = item.source_name.empty() ? "thundering_vale" : item.source_name;
            for (const auto& victim : victims) {
                if (!victim || !game.board.all_entities_map.contains(victim->entity_id)) {
                    continue;
                }
                if (entity_is_immovable(*victim)) {
                    continue;
                }
                const auto dir = push_direction_away_from_source(source, *victim);
                if (dir.first == 0 && dir.second == 0) {
                    continue;
                }
                const auto push_result =
                    resolve_push_displacement(game, victim, dir.first, dir.second, push_distance, opts);
                if (!msg.empty()) {
                    msg += "; ";
                }
                msg += push_result.message;
                if (push_result.tiles_moved > 0) {
                    game.cancel_stale_queued_batch_items_after_entity_move(victim->entity_id);
                    game.process_bleed_after_movement(victim);
                }
            }
        }
        game.refresh_passive_auras();
        return ActionResult{true, msg.empty() ? "thundering_vale resolved" : msg, {}};
    }));

    // -----------------------------------------------------------------------
    // grant_stockpile_to_unit — grant Stockpile charges on the target unit's source card instance.
    static_cast<void>(stack_manager.register_effect("grant_stockpile_to_unit", [](GameState& game, const StackItem& item) {
        const auto resolved = resolve_stack_board_target(game, item, "grant_stockpile_to_unit");
        if (!resolved.status.ok || resolved.status.message.find("(fizzled)") != std::string::npos) {
            return resolved.status;
        }
        if (!resolved.target || resolved.target->entity_type != "unit" || resolved.target->source_card_id.empty()
                || !resolved.target->owner) {
            return ActionResult{false, "grant_stockpile_to_unit: requires an allied unit with a source card", {}};
        }
        const int owner_id = *resolved.target->owner;
        auto deck_it = game.players_decks.find(owner_id);
        if (deck_it == game.players_decks.end()) {
            return ActionResult{false, "grant_stockpile_to_unit: owner deck missing", {}};
        }
        Deck& deck = deck_it->second;
        const CardInstanceId cid = deck.find_card_by_public_id(resolved.target->source_card_id);
        if (!cid.is_valid()) {
            return ActionResult{false, "grant_stockpile_to_unit: source card not found", {}};
        }
        const int amount = std::max(1, payload_int(item, effect_keys::kPayloadAmount, 2));
        CardInstance& inst = deck.pool.at(cid);
        inst.stockpile_amount = amount;
        inst.stockpile_remaining = amount;
        inst.stockpile_used_this_turn = false;
        inst.stockpile_double_play_used_this_turn = false;
        if (deck.contains_in_in_play(cid)) {
            deck.remove_card_from_hand_and_in_play(cid);
            deck.deck.push_back(cid);
        }
        return ActionResult{true,
            resolved.target->entity_id + " granted Stockpile " + std::to_string(amount), {}};
    }));

    // grant_passive_ability — spell effect that permanently adds a passive from the
    // catalog to the target unit.  string_payload["passive_key"] names the passive.
    // Duplicate grants (same key already on the unit) are silently ignored.
    // -----------------------------------------------------------------------
    static_cast<void>(stack_manager.register_effect("grant_passive_ability", [](GameState& game, const StackItem& item) {
        const auto resolved = resolve_stack_board_target(game, item, "grant_passive_ability");
        if (!resolved.status.ok || resolved.status.message.find("(fizzled)") != std::string::npos) {
            return resolved.status;
        }
        if (!resolved.target || resolved.target->entity_type != "unit") {
            return ActionResult{false, "grant_passive_ability: target must be a unit", {}};
        }
        const std::string& passive_key = payload_string(item, "passive_key");
        if (passive_key.empty()) {
            return ActionResult{false, "grant_passive_ability: missing passive_key in string payload", {}};
        }
        PassiveAbilitySpec spec;
        if (!try_get_passive_from_catalog(passive_key, spec)) {
            return ActionResult{false, "grant_passive_ability: passive '" + passive_key + "' not in catalog", {}};
        }
        // Idempotent: don't stack duplicates.
        for (const auto& existing : resolved.target->passive_abilities) {
            if (existing.key == spec.key) {
                return ActionResult{true,
                    "grant_passive_ability: '" + passive_key + "' already on target (fizzled)", {}};
            }
        }
        resolved.target->passive_abilities.push_back(std::move(spec));
        game.mark_passive_auras_dirty();
        return ActionResult{true, "Granted '" + passive_key + "' to " + resolved.target->entity_id, {}};
    }));

    // -----------------------------------------------------------------------
    // Unified directional damage handler — used by effect_key "directional_damage".
    // Reads shape from item.string_payload["shape"] (default "rectangle").
    // -----------------------------------------------------------------------
    auto directional_damage_handler = [](GameState& game, const StackItem& item) -> ActionResult {
        if (item.source_entity_id.empty()) {
            return ActionResult{false, "directional_damage requires a source unit", {}};
        }
        const auto src_it = game.board.all_entities_map.find(item.source_entity_id);
        if (src_it == game.board.all_entities_map.end() || !src_it->second) {
            return ActionResult{false, "Source unit is no longer on the board", {}};
        }
        const Entity& source = *src_it->second;
        const int max_range = [&]() -> int {
            const auto it = item.payload.find("max_range");
            return it != item.payload.end() ? it->second : 4;
        }();
        const bool allow_diagonals = [&]() -> bool {
            const auto it = item.payload.find("cardinal_only");
            return !(it != item.payload.end() && it->second != 0);
        }();
        const auto aim_vr = validate_directional_area_damage_ability_target(
            game, item.controller_id, source, item.targets, max_range, allow_diagonals);
        if (!aim_vr.ok) {
            return aim_vr;
        }
        const int tx = item.targets.at(effect_keys::kCellX);
        const int ty = item.targets.at(effect_keys::kCellY);
        std::optional<std::pair<int, int>> anchor;
        if (source.position) {
            anchor = *source.position;
        } else if (!source.occupied_positions.empty()) {
            anchor = source.occupied_positions.front();
        }
        if (!anchor) {
            return ActionResult{false, "Source unit has no position", {}};
        }
        const auto [ax, ay] = *anchor;
        const auto [dir_x, dir_y] = snap_octilinear_direction(ax, ay, tx, ty);
        const std::string shape_key = [&]() -> std::string {
            const auto it = item.string_payload.find("shape");
            return (it != item.string_payload.end() && !it->second.empty()) ? it->second : "rectangle";
        }();
        const auto params = aoe_shape_params_from_payload(item.payload);
        ensure_builtin_aoe_shapes_registered();
        const auto cells = compute_aoe_cells(shape_key, ax, ay, dir_x, dir_y, params);
        if (cells.empty() && aoe_shape_known(shape_key)) {
            // Shape produced no cells (e.g. zero-depth) — fizzle gracefully.
            return ActionResult{true, item.source_name + " found no blast cells", {}};
        }
        if (!aoe_shape_known(shape_key)) {
            return ActionResult{false, "Unknown AoE shape: " + shape_key, {}};
        }
        int amount = 0;
        if (const auto it = item.payload.find(effect_keys::kPayloadAmount); it != item.payload.end()) {
            amount = it->second;
        }
        amount = std::max(0, amount);
        // Pre-consume next_damage_bonus ONCE before the victims loop so the full bonus applies to
        // every entity in the AoE — not just the first one apply_damage_packet happens to hit.
        amount += consume_first_entity_effect(*src_it->second, "next_damage_bonus");
        auto victims = entities_in_cells(game, cells);
        // Remove the source itself (you can't hit yourself with your own AoE).
        victims.erase(
            std::remove_if(victims.begin(), victims.end(),
                [&](const std::shared_ptr<Entity>& ent) { return !ent || ent->entity_id == source.entity_id; }),
            victims.end());
        // If "enemy_only" is set, filter out allied entities (used by Fury Strikes, Beam Splitter).
        if (const auto fo_it = item.payload.find("enemy_only");
                fo_it != item.payload.end() && fo_it->second != 0) {
            victims.erase(
                std::remove_if(victims.begin(), victims.end(),
                    [&](const std::shared_ptr<Entity>& ent) {
                        return !ent || !ent->owner || !source.owner ||
                               !teams_hostile(game, *ent->owner, *source.owner);
                    }),
                victims.end());
        }
        if (amount <= 0 || victims.empty()) {
            const std::string name = item.source_name.empty() ? item.effect_key : item.source_name;
            return ActionResult{true, name + " found no targets", {}};
        }
        std::string msg;
        for (const auto& victim : victims) {
            if (!victim || !game.board.all_entities_map.contains(victim->entity_id)) {
                continue;
            }
            DamagePacket packet;
            packet.source = src_it->second;
            packet.target = victim;
            packet.amount = amount;
            packet.damage_type = damage_type_from_stack_payload(item.payload);
            packet.pierces_damage_prevention = item.pierces_damage_prevention;
            packet.ability_grants_lifesteal = item.heals_on_damage_dealt;
            packet.ability_grants_soul_steal = item.heals_allied_base_on_damage_dealt;
            packet.from_melee_attack = false;
            packet.defer_reactive = true;
            packet.source_label = item.source_name.empty() ? item.effect_key : item.source_name;
            auto hit = apply_damage_packet(game, packet);
            if (!hit.ok) {
                return hit;
            }
            if (!msg.empty()) msg += "; ";
            msg += hit.message;
        }
        const std::string name = item.source_name.empty() ? item.effect_key : item.source_name;
        return ActionResult{true, msg.empty() ? name + " resolved" : msg, {}};
    };
    static_cast<void>(stack_manager.register_effect("directional_damage", directional_damage_handler));

    // -----------------------------------------------------------------------
    // piercing_shot — fires in one octilinear direction, hitting every unit
    // and structure along the ray up to max_range.  Bases are skipped.
    // Targeting uses the same direction-cell picker as directional_damage.
    // -----------------------------------------------------------------------
    static_cast<void>(stack_manager.register_effect("piercing_shot",
        [](GameState& game, const StackItem& item) -> ActionResult {
            if (item.source_entity_id.empty()) {
                return ActionResult{false, "piercing_shot requires a source unit", {}};
            }
            const auto src_it = game.board.all_entities_map.find(item.source_entity_id);
            if (src_it == game.board.all_entities_map.end() || !src_it->second) {
                return ActionResult{false, "Source unit is no longer on the board", {}};
            }
            const Entity& source = *src_it->second;
            const int max_range = payload_int(item, "max_range", 8);
            // Reuse directional validation — target cell must be on an octilinear ray.
            const auto aim_vr = validate_directional_area_damage_ability_target(
                game, item.controller_id, source, item.targets, max_range, /*allow_diagonals=*/true);
            if (!aim_vr.ok) { return aim_vr; }

            std::optional<std::pair<int, int>> anchor;
            if (source.position) {
                anchor = *source.position;
            } else if (!source.occupied_positions.empty()) {
                anchor = source.occupied_positions.front();
            }
            if (!anchor) { return ActionResult{false, "Source unit has no position", {}}; }
            const auto [ax, ay] = *anchor;
            const int tx = item.targets.at(effect_keys::kCellX);
            const int ty = item.targets.at(effect_keys::kCellY);
            const auto [dir_x, dir_y] = snap_octilinear_direction(ax, ay, tx, ty);

            int amount = std::max(0, payload_int(item, effect_keys::kPayloadAmount, 0));
            // Pre-consume next_damage_bonus once — applies to every entity hit.
            amount += consume_first_entity_effect(*src_it->second, "next_damage_bonus");

            std::string msg;
            std::set<std::string> seen;
            for (int dist = 1; dist <= max_range; ++dist) {
                const int cx = ax + dist * dir_x;
                const int cy = ay + dist * dir_y;
                if (!game.board.get_square(cx, cy)) { break; } // off-board: ray ends
                auto ent = game.board.entity_at(cx, cy);
                if (!ent || !seen.insert(ent->entity_id).second) { continue; }
                if (entity_is_base(*ent)) { continue; } // piercing shot skips bases
                if (ent->entity_type != "unit" && !entity_is_structure(*ent)) { continue; }
                if (!game.board.all_entities_map.contains(ent->entity_id)) { continue; }

                DamagePacket packet;
                packet.source                      = src_it->second;
                packet.target                      = ent;
                packet.amount                      = amount;
                packet.damage_type                 = damage_type_from_stack_payload(item.payload);
                packet.pierces_damage_prevention   = item.pierces_damage_prevention;
                packet.ability_grants_lifesteal    = item.heals_on_damage_dealt;
                packet.ability_grants_soul_steal   = item.heals_allied_base_on_damage_dealt;
                packet.from_melee_attack           = false;
                packet.defer_reactive              = true;
                packet.source_label                = item.source_name.empty() ? "Piercing Shot" : item.source_name;
                auto hit = apply_damage_packet(game, packet);
                if (!hit.ok) { return hit; }
                if (!msg.empty()) msg += "; ";
                msg += hit.message;
            }
            const std::string name = item.source_name.empty() ? "Piercing Shot" : item.source_name;
            return ActionResult{true, msg.empty() ? name + ": no targets hit" : msg, {}};
        }));

    const auto register_status_effect = [this](const std::string& effect_key, const std::string& status_key, const std::string& display_name) {
        static_cast<void>(stack_manager.register_effect(effect_key, [status_key, display_name](GameState& game, const StackItem& item) {
            const auto resolved = resolve_stack_board_target(game, item, display_name);
            if (!resolved.status.ok || resolved.status.message.find("(fizzled)") != std::string::npos) {
                return resolved.status;
            }
            int amount = 1;
            if (const auto it = item.payload.find(effect_keys::kPayloadAmount); it != item.payload.end()) {
                amount = it->second;
            }
            amount = std::max(0, amount);
            if (!add_entity_effect(*resolved.target, status_key, amount)) {
                return ActionResult{true, resolved.target->entity_id + " is immune to " + status_key + " (fizzled)", {}};
            }
            return ActionResult{true, "Applied " + std::to_string(amount) + " " + status_key + " stack(s) to " + resolved.target->entity_id, {}};
        }));
    };
    register_status_effect("apply_poison", "poison", "apply_poison");
    register_status_effect("apply_fire", "fire", "apply_fire");
    register_status_effect("apply_bleed", "bleed", "apply_bleed");
    register_status_effect("apply_silenced", "silenced", "apply_silenced");
