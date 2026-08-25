// game_state_effects_combat.inc — shard of register_default_effects()
    static_cast<void>(stack_manager.register_effect(effect_keys::kUnitDefendStanceActionKey, [](GameState& game, const StackItem& item) {
        return apply_defend_stance_from_stack(game, item);
    }));
    static_cast<void>(stack_manager.register_effect("deal_damage", [](GameState& game, const StackItem& item) {
        const auto resolved = resolve_stack_board_target(game, item, "deal_damage");
        if (!resolved.status.ok || resolved.status.message.find("(fizzled)") != std::string::npos) {
            return resolved.status;
        }
        int amount = 0;
        if (const auto it = item.payload.find(effect_keys::kPayloadAmount); it != item.payload.end()) {
            amount = it->second;
        }
        const bool convert_bonuses_to_health = payload_int(item, "convert_all_bonus_damage_to_health", 0) != 0;
        int unit_type_bonus = 0;
        if (resolved.target) {
            unit_type_bonus =
                unit_type_bonus_damage_for_target(*resolved.target, item.bonus_damage_unit_types, item.bonus_damage_amount);
        }
        if (!convert_bonuses_to_health) {
            amount += unit_type_bonus;
            amount += item.ability_damage_bonus;
        }
        std::shared_ptr<Entity> source;
        if (!item.source_entity_id.empty()) {
            const auto src_it = game.board.all_entities_map.find(item.source_entity_id);
            if (src_it != game.board.all_entities_map.end()) {
                source = src_it->second;
            }
        }
        if (convert_bonuses_to_health && source && source->current_health > 0) {
            int health_gain = item.ability_damage_bonus + unit_type_bonus;
            health_gain += consume_all_next_damage_bonus(*source);
            if (resolved.target) {
                health_gain += compute_keyword_damage_bonuses(game, *source, *resolved.target, false);
            }
            if (health_gain > 0) {
                add_entity_effect(*source, "bonus_health", health_gain, item.item_id);
            }
        }
        DamagePacket packet;
        packet.source = std::move(source);
        packet.target = resolved.target;
        packet.amount = amount;
        packet.suppress_source_damage_bonuses = convert_bonuses_to_health;
        packet.damage_type = damage_type_from_stack_payload(item.payload);
        packet.pierces_damage_prevention = item.pierces_damage_prevention;
        packet.ability_grants_lifesteal = item.heals_on_damage_dealt;
        packet.ability_grants_soul_steal = item.heals_allied_base_on_damage_dealt;
        if (item.heals_allied_base_on_damage_dealt) {
            if (!item.soul_steal_heal_base_entity_id.empty()) {
                const auto base_it = game.board.all_entities_map.find(item.soul_steal_heal_base_entity_id);
                if (base_it != game.board.all_entities_map.end()) {
                    packet.soul_steal_heal_base = base_it->second;
                }
            }
            if (!packet.soul_steal_heal_base) {
                packet.soul_steal_heal_base = resolve_soul_steal_heal_base(game, item.controller_id, item.targets);
            }
        }
        packet.source_label = item.source_name.empty() ? item.effect_key : item.source_name;
        packet.defer_reactive = true;
        bool evaded_to_cover = false;
        bool body_blocked = false;
        if (resolved.target && resolved.target->entity_type == "unit") {
            const int evasive_stacks = evasive_stack_count(*resolved.target);
            if (evasive_stacks > 0 && roll_evasive_whiff(game.rng(), evasive_stacks)) {
                return ActionResult{true, resolved.target->entity_id + " evaded (Evasive)", {}};
            }
        }
        if (source && resolved.target && resolved.target->entity_type == "unit") {
            std::optional<AbilitySpec> ability;
            if (stack_item_uses_ranged_low_cover_evasion(item, *source, ability)) {
                const auto xit = item.targets.find(effect_keys::kCellX);
                const auto yit = item.targets.find(effect_keys::kCellY);
                if (xit != item.targets.end() && yit != item.targets.end()) {
                    const AbilitySpec* ability_ptr = ability ? &*ability : nullptr;
                    body_blocked = maybe_redirect_ranged_attack_to_blocking_unit(
                        game, *source, {xit->second, yit->second}, *resolved.target, packet, ability_ptr);
                    if (!body_blocked) {
                        evaded_to_cover = maybe_redirect_ranged_attack_to_low_cover(
                            game, *source, {xit->second, yit->second}, *resolved.target, packet, ability_ptr);
                    }
                }
            }
        }
        auto damage_result = apply_damage_packet(game, packet);
        if (body_blocked && damage_result.ok) {
            damage_result.message += " (body-blocked in line of fire)";
        } else if (evaded_to_cover && damage_result.ok) {
            damage_result.message += " (evaded behind low cover)";
        }
        return damage_result;
    }));

    static_cast<void>(stack_manager.register_effect("heal", [](GameState& game, const StackItem& item) {
        const auto resolved = resolve_stack_board_target(game, item, "heal");
        if (!resolved.status.ok || resolved.status.message.find("(fizzled)") != std::string::npos) {
            return resolved.status;
        }
        int amount = 0;
        if (const auto it = item.payload.find(effect_keys::kPayloadAmount); it != item.payload.end()) {
            amount = it->second;
        }
        HealPacket packet;
        packet.target = resolved.target;
        packet.amount = std::max(0, amount);
        packet.source_label = item.source_name.empty() ? item.effect_key : item.source_name;
        return apply_heal_packet(game, packet);
    }));

    static_cast<void>(stack_manager.register_effect("grant_player_base_heal", [](GameState& game, const StackItem& item) {
        const std::string base_id = "base_p" + std::to_string(item.controller_id);
        const auto base_it = game.board.all_entities_map.find(base_id);
        if (base_it == game.board.all_entities_map.end() || !base_it->second) {
            return ActionResult{true, "grant_player_base_heal: base missing (fizzled)", {}};
        }
        const int amount = std::max(0, payload_int(item, effect_keys::kPayloadAmount, 0));
        if (amount <= 0) {
            return ActionResult{true, "grant_player_base_heal: amount 0", {}};
        }
        HealPacket packet;
        packet.target = base_it->second;
        packet.amount = amount;
        packet.source_label = item.source_name.empty() ? item.effect_key : item.source_name;
        return apply_heal_packet(game, packet);
    }));

    static_cast<void>(stack_manager.register_effect("grant_player_base_bonus_health", [](GameState& game, const StackItem& item) {
        const std::string base_id = "base_p" + std::to_string(item.controller_id);
        const auto base_it = game.board.all_entities_map.find(base_id);
        if (base_it == game.board.all_entities_map.end() || !base_it->second) {
            return ActionResult{true, "grant_player_base_bonus_health: base missing (fizzled)", {}};
        }
        const int amount = std::max(0, payload_int(item, effect_keys::kPayloadAmount, 0));
        if (amount <= 0) {
            return ActionResult{true, "grant_player_base_bonus_health: amount 0", {}};
        }
        Entity& base = *base_it->second;
        const std::string source_id = item.item_id.empty() ? item.source_name : item.item_id;
        for (EntityEffectInstance& effect : base.entity_effects) {
            if (effect.key == "bonus_health" && effect.source_id == source_id && effect.expire_on == "never") {
                effect.amount += amount;
                game.mark_passive_auras_dirty();
                return ActionResult{true, base.entity_id + " gained " + std::to_string(amount) + " bonus health", {}};
            }
        }
        EntityEffectInstance inst;
        inst.key = "bonus_health";
        inst.amount = amount;
        inst.source_id = source_id;
        inst.expire_on = "never";
        base.entity_effects.push_back(std::move(inst));
        game.mark_passive_auras_dirty();
        return ActionResult{true, base.entity_id + " gained " + std::to_string(amount) + " bonus health", {}};
    }));

    static_cast<void>(stack_manager.register_effect("grant_player_base_max_health", [](GameState& game, const StackItem& item) {
        const std::string base_id = "base_p" + std::to_string(item.controller_id);
        const auto base_it = game.board.all_entities_map.find(base_id);
        if (base_it == game.board.all_entities_map.end() || !base_it->second) {
            return ActionResult{true, "grant_player_base_max_health: base missing (fizzled)", {}};
        }
        const int amount = std::max(0, payload_int(item, effect_keys::kPayloadAmount, 0));
        if (amount <= 0) {
            return ActionResult{true, "grant_player_base_max_health: amount 0", {}};
        }
        auto& base = *base_it->second;
        base.base_health += amount;
        base.current_health += amount;
        game.mark_passive_auras_dirty();
        return ActionResult{true, base.entity_id + " gained +" + std::to_string(amount) + " max HP", {}};
    }));

    static_cast<void>(stack_manager.register_effect("grant_bonus_health", [](GameState& game, const StackItem& item) {
        const auto resolved = resolve_stack_board_target(game, item, "grant_bonus_health");
        if (!resolved.status.ok || resolved.status.message.find("(fizzled)") != std::string::npos) {
            return resolved.status;
        }
        if (!resolved.target) {
            return ActionResult{false, "grant_bonus_health requires a target unit", {}};
        }
        const int amount = payload_int(item, effect_keys::kPayloadAmount, 1);
        if (amount <= 0) {
            return ActionResult{true, "grant_bonus_health: amount 0", {}};
        }
        if (!entity_status_allowed(*resolved.target, "bonus_health")) {
            return ActionResult{true, resolved.target->entity_id + " cannot receive bonus health", {}};
        }
        const std::string source_id = item.item_id.empty() ? item.source_name : item.item_id;
        add_entity_effect(*resolved.target, "bonus_health", amount, source_id);
        return ActionResult{true,
            resolved.target->entity_id + " gained " + std::to_string(amount) + " bonus health",
            {}};
    }));

    // heal_boosted: heal for amount + ability_damage_bonus + next_damage_bonus.
    // Scales with passive aura bonuses (Command Presence) and one-shot bonuses (Tune-Up, Overcharge).
    // Used by Jacquelynne's Mending Shot.
    static_cast<void>(stack_manager.register_effect("heal_boosted", [](GameState& game, const StackItem& item) {
        const auto resolved = resolve_stack_board_target(game, item, "heal_boosted");
        if (!resolved.status.ok || resolved.status.message.find("(fizzled)") != std::string::npos) {
            return resolved.status;
        }
        // Consume all pending next-damage Boost stacks from the caster (Tune-Up, Overcharge Burst, etc.).
        const auto src_it = item.source_entity_id.empty()
            ? game.board.all_entities_map.end()
            : game.board.all_entities_map.find(item.source_entity_id);
        const int one_shot_bonus = (src_it != game.board.all_entities_map.end() && src_it->second)
            ? std::max(0, consume_all_next_damage_bonus(*src_it->second))
            : 0;
        const int base   = payload_int(item, effect_keys::kPayloadAmount, 0);
        const int amount = std::max(0, base + item.ability_damage_bonus + one_shot_bonus);
        HealPacket packet;
        packet.target       = resolved.target;
        packet.amount       = amount;
        packet.source_label = item.source_name.empty() ? item.effect_key : item.source_name;
        return apply_heal_packet(game, packet);
    }));

    // grant_doubled_next_ability (Doublecast): tag the target so their next ability activation is
    // echoed — ActivateAbilityAction::execute() consumes this effect and pushes an identical
    // StackItem copy (Fast speed) immediately after the original, causing the ability to fire twice.
    static_cast<void>(stack_manager.register_effect("grant_doubled_next_ability", [](GameState& game, const StackItem& item) {
        const auto resolved = resolve_stack_board_target(game, item, "grant_doubled_next_ability");
        if (!resolved.status.ok || resolved.status.message.find("(fizzled)") != std::string::npos) {
            return resolved.status;
        }
        if (!resolved.target || (resolved.target->entity_type != "unit" && !entity_is_structure(*resolved.target))) {
            return ActionResult{false, "grant_doubled_next_ability: requires a unit or structure target", {}};
        }
        add_entity_effect(*resolved.target, "next_ability_doubled", 1, item.item_id);
        return ActionResult{true,
            "Doublecast: " + resolved.target->entity_id + "'s next ability will be echoed", {}};
    }));

    // apply_overload_ally_draw: apply N overload stacks to an allied unit and draw M cards.
    // Payload: "amount" = overload stacks (default 3), "draw" = cards drawn (default 2).
    // Desperate Scan spell.
    static_cast<void>(stack_manager.register_effect("apply_overload_ally_draw", [](GameState& game, const StackItem& item) {
        const auto resolved = resolve_stack_board_target(game, item, "apply_overload_ally_draw");
        if (!resolved.status.ok || resolved.status.message.find("(fizzled)") != std::string::npos) {
            return resolved.status;
        }
        if (!resolved.target || (resolved.target->entity_type != "unit" && !entity_is_structure(*resolved.target))) {
            return ActionResult{false, "apply_overload_ally_draw: requires a unit or structure target", {}};
        }
        const int overload_amount = std::max(0, payload_int(item, effect_keys::kPayloadAmount, 3));
        const int draw_amount     = std::max(0, payload_int(item, "draw", 2));
        const std::string overload_msg = game.apply_overload_stacks(resolved.target, overload_amount);
        if (draw_amount > 0) {
            game.draw_cards(item.controller_id, draw_amount);
        }
        return ActionResult{true,
            overload_msg + "; drew " + std::to_string(draw_amount) + " card(s)", {}};
    }));

    // grant_stat_growth_draw: permanently buff target allied unit (+attack, +health) then draw cards.
    // Payload: "attack" (default 1), "health" (default 1), "draw" (default 1).
    static_cast<void>(stack_manager.register_effect("grant_stat_growth_draw", [](GameState& game, const StackItem& item) {
        const auto resolved = resolve_stack_board_target(game, item, "grant_stat_growth_draw");
        if (!resolved.status.ok || resolved.status.message.find("(fizzled)") != std::string::npos) {
            return resolved.status;
        }
        auto unit = std::dynamic_pointer_cast<Unit>(resolved.target);
        if (!unit) {
            return ActionResult{false, "grant_stat_growth_draw: target is not a unit", {}};
        }
        const int attack_gain = std::max(0, payload_int(item, "attack", 1));
        const int health_gain = std::max(0, payload_int(item, "health", 1));
        const int draw_amount = std::max(0, payload_int(item, "draw", 1));
        if (health_gain > 0) {
            unit->base_health    += health_gain;
            unit->current_health += health_gain;
        }
        if (attack_gain > 0) {
            sync_unit_damage_ranges_from_nominal(*unit);
            unit->melee_damage_min += attack_gain;
            unit->melee_damage_max += attack_gain;
            unit->melee_damage     += attack_gain;
            if (unit->is_ranged || unit->attack_type == AttackType::Ranged) {
                unit->ranged_damage_min += attack_gain;
                unit->ranged_damage_max += attack_gain;
                unit->ranged_damage     += attack_gain;
            }
        }
        game.refresh_passive_auras();
        if (draw_amount > 0) {
            game.draw_cards(item.controller_id, draw_amount);
        }
        std::string msg = unit->entity_id + " gained +" + std::to_string(attack_gain) + " damage and +"
            + std::to_string(health_gain) + " max HP permanently";
        if (draw_amount > 0) {
            msg += "; drew " + std::to_string(draw_amount) + " card(s)";
        }
        return ActionResult{true, msg, {}};
    }));

    // liquid_data: if target has deployment fatigue, grant it the surge attribute (can
    // attack/use abilities this turn despite fatigue); otherwise, refresh all abilities
    // (refreshes ability uses and barrage cast counts).
    static_cast<void>(stack_manager.register_effect("liquid_data", [](GameState& game, const StackItem& item) {
        const auto resolved = resolve_stack_board_target(game, item, "liquid_data");
        if (!resolved.status.ok || resolved.status.message.find("(fizzled)") != std::string::npos) {
            return resolved.status;
        }
        const auto& target = resolved.target;
        if (entity_has_deployment_fatigue(*target)) {
            TemporaryEntityEffect eff;
            eff.effect_id   = "volatile_surge_buff";
            eff.source_id   = item.source_entity_id;
            eff.name        = "Volatile Surge";
            eff.rules_text  = "Granted surge by Volatile Surge — may attack and use abilities despite deployment fatigue.";
            eff.expire_on   = "owner_turn_start";
            eff.remaining_turns = 1;
            eff.granted_attributes.push_back({"surge", std::nullopt});
            game.add_temporary_effect(target, std::move(eff));
            return ActionResult{true, target->entity_id + " granted surge — can attack and use abilities this turn", {}};
        } else {
            game.refresh_ability_uses_for_entity(target);
            return ActionResult{true, target->entity_id + " abilities fully refreshed", {}};
        }
    }));

    // grant_reactive_armor (Reactive Armor spell): grant +1 Armor immediately; each damage instance
    // adds +1 stacking Armor until owner_turn_start (see apply_damage_packet).
    static_cast<void>(stack_manager.register_effect("grant_reactive_armor", [](GameState& game, const StackItem& item) {
        const auto resolved = resolve_stack_board_target(game, item, "grant_reactive_armor");
        if (!resolved.status.ok || resolved.status.message.find("(fizzled)") != std::string::npos) {
            return resolved.status;
        }
        const auto& target = resolved.target;
        if (!target || (target->entity_type != "unit" && !entity_is_structure(*target))) {
            return ActionResult{false, "grant_reactive_armor: requires a unit or structure target", {}};
        }
        auto it = std::find_if(target->temporary_effects.begin(), target->temporary_effects.end(),
            [](const TemporaryEntityEffect& e) { return e.effect_id == "reactive_armor_grant"; });
        if (it != target->temporary_effects.end()) {
            it->expire_on = "owner_turn_start";
            it->remaining_turns = 1;
            it->rules_text = "Gained 1 Armor from Reactive Armor. Whenever this unit takes damage it gains 1 more "
                              "Armor. The armor and ability is lost at the start of your next turn.";
            bool has_armor = false;
            for (PassiveAttributeGrant& grant : it->granted_attributes) {
                if (grant.key == "armor") {
                    grant.amount = 1;
                    has_armor = true;
                }
            }
            if (!has_armor) {
                it->granted_attributes.push_back({"armor", 1});
            }
            return ActionResult{true, target->entity_id + " gains Reactive Armor (+1 Armor, stacks on damage)", {}};
        }
        TemporaryEntityEffect eff;
        eff.effect_id = "reactive_armor_grant";
        eff.source_id = item.source_entity_id;
        eff.name = "Reactive Armor";
        eff.rules_text = "Gained 1 Armor from Reactive Armor. Whenever this unit takes damage it gains 1 more "
                         "Armor. The armor and ability is lost at the start of your next turn.";
        eff.expire_on = "owner_turn_start";
        eff.remaining_turns = 1;
        eff.granted_attributes.push_back({"armor", 1});
        game.add_temporary_effect(target, std::move(eff));
        return ActionResult{true, target->entity_id + " gains Reactive Armor (+1 Armor, stacks on damage)", {}};
    }));

    static_cast<void>(stack_manager.register_effect("repair_structure_adjacent", [](GameState& game, const StackItem& item) {
        const auto resolved = resolve_stack_board_target(game, item, "repair_structure_adjacent");
        if (!resolved.status.ok || resolved.status.message.find("(fizzled)") != std::string::npos) {
            return resolved.status;
        }
        if (!resolved.target || !entity_is_quick_repairs_target(*resolved.target)) {
            return ActionResult{false, "repair_structure_adjacent requires an adjacent allied structure or base", {}};
        }
        const bool target_is_base = entity_is_base(*resolved.target);
        int amount = 0;
        if (target_is_base) {
            if (const auto it = item.payload.find(effect_keys::kPayloadBaseAmount); it != item.payload.end()) {
                amount = it->second;
            }
        } else if (const auto it = item.payload.find(effect_keys::kPayloadAmount); it != item.payload.end()) {
            amount = it->second;
        }
        HealPacket packet;
        packet.target = resolved.target;
        packet.amount = std::max(0, amount);
        packet.source_label = item.source_name.empty() ? item.effect_key : item.source_name;
        return apply_heal_packet(game, packet);
    }));

    // Board-targeted variant: grants next_damage_bonus to any ally unit or structure on the board.
    // Optional payload `cast_damage` deals that much damage to unit targets on resolution (Bootleg Go-Go Powder).
    static_cast<void>(stack_manager.register_effect("grant_next_damage_bonus", [](GameState& game, const StackItem& item) {
        const auto resolved = resolve_stack_board_target(game, item, "grant_next_damage_bonus");
        if (!resolved.status.ok || resolved.status.message.find("(fizzled)") != std::string::npos) {
            return resolved.status;
        }
        if (!resolved.target || (resolved.target->entity_type != "unit" && !entity_is_structure(*resolved.target))) {
            return ActionResult{false, "grant_next_damage_bonus: requires a unit or structure target", {}};
        }
        std::string message;
        const int cast_damage = std::max(0, payload_int(item, "cast_damage", 0));
        if (cast_damage > 0 && resolved.target->entity_type == "unit") {
            DamagePacket packet;
            packet.target = resolved.target;
            packet.amount = cast_damage;
            packet.damage_type = damage_type_from_stack_payload(item.payload);
            packet.source_label = item.source_name.empty() ? item.effect_key : item.source_name;
            packet.defer_reactive = true;
            const auto hit = apply_damage_packet(game, packet);
            if (!hit.ok) {
                return hit;
            }
            message = "Dealt " + std::to_string(cast_damage) + " damage to " + resolved.target->entity_id;
            if (resolved.target->current_health <= 0) {
                return ActionResult{true, message, {}};
            }
        }
        int amount = 1;
        if (const auto it = item.payload.find(effect_keys::kPayloadAmount); it != item.payload.end()) {
            amount = it->second;
        }
        amount = std::max(0, amount);
        if (amount <= 0) {
            return ActionResult{true, message.empty() ? "No damage bonus applied" : message, {}};
        }
        add_entity_effect(*resolved.target, "next_damage_bonus", amount, item.item_id);
        propagate_buff_to_surrounding_mirror_passives(game, *resolved.target, "next_damage_bonus", amount);
        if (!message.empty()) {
            message += "; ";
        }
        message += "Granted +" + std::to_string(amount) + " damage to the next attack or ability from "
            + resolved.target->entity_id;
        return ActionResult{true, message, {}};
    }));

    static_cast<void>(stack_manager.register_effect("grant_next_damage_bonus_adjacent",
        [](GameState& game, const StackItem& item) {
            const auto resolved = resolve_stack_board_target(game, item, "grant_next_damage_bonus_adjacent");
            if (!resolved.status.ok || resolved.status.message.find("(fizzled)") != std::string::npos) {
                return resolved.status;
            }
            if (!resolved.target || (resolved.target->entity_type != "unit" && !entity_is_structure(*resolved.target))) {
                return ActionResult{false, "grant_next_damage_bonus_adjacent requires an allied unit or structure", {}};
            }
            int amount = 0;
            if (const auto it = item.payload.find(effect_keys::kPayloadAmount); it != item.payload.end()) {
                amount = it->second;
            }
            amount = std::max(0, amount);
            if (amount <= 0) {
                return ActionResult{true, "No Tune-Up bonus applied", {}};
            }
            add_entity_effect(*resolved.target, "next_damage_bonus", amount, item.item_id);
            propagate_buff_to_surrounding_mirror_passives(game, *resolved.target, "next_damage_bonus", amount);
            return ActionResult{
                true, "Granted +" + std::to_string(amount) + " damage to the next damaging attack or ability from " + resolved.target->entity_id, {}};
        }));

    static_cast<void>(stack_manager.register_effect("grant_attack_damage_turn_self", [](GameState& game, const StackItem& item) {
        if (item.source_entity_id.empty()) {
            return ActionResult{false, "grant_attack_damage_turn_self requires a source entity", {}};
        }
        const auto src_it = game.board.all_entities_map.find(item.source_entity_id);
        if (src_it == game.board.all_entities_map.end() || !src_it->second) {
            return ActionResult{false, "Source entity is no longer on the board", {}};
        }
        int amount = 2;
        if (const auto it = item.payload.find(effect_keys::kPayloadAmount); it != item.payload.end()) {
            amount = it->second;
        }
        amount = std::max(0, amount);
        if (amount <= 0) {
            return ActionResult{true, "No attack damage bonus applied", {}};
        }
        TemporaryEntityEffect eff;
        eff.effect_id = "attack_damage_turn_buff";
        eff.name = "+" + std::to_string(amount) + " attack damage this turn";
        eff.expire_on = "owner_turn_end";
        eff.stat_grants.bonus_attack = amount;
        src_it->second->temporary_effects.push_back(std::move(eff));
        return ActionResult{
            true,
            src_it->second->entity_id + " gained +" + std::to_string(amount) + " attack damage this turn",
            {}};
    }));

    static_cast<void>(stack_manager.register_effect("grant_ranged_range_turn_self", [](GameState& game, const StackItem& item) {
        if (item.source_entity_id.empty()) {
            return ActionResult{false, "grant_ranged_range_turn_self requires a source entity", {}};
        }
        const auto src_it = game.board.all_entities_map.find(item.source_entity_id);
        if (src_it == game.board.all_entities_map.end() || !src_it->second) {
            return ActionResult{false, "Source entity is no longer on the board", {}};
        }
        int amount = 1;
        if (const auto it = item.payload.find(effect_keys::kPayloadAmount); it != item.payload.end()) {
            amount = it->second;
        }
        amount = std::max(0, amount);
        if (amount <= 0) {
            return ActionResult{true, "No range bonus applied", {}};
        }
        TemporaryEntityEffect eff;
        eff.effect_id = "ranged_range_turn_buff";
        eff.name = "+" + std::to_string(amount) + " ranged range this turn";
        eff.expire_on = "owner_turn_end";
        eff.stat_grants.bonus_ranged_range = amount;
        src_it->second->temporary_effects.push_back(std::move(eff));
        return ActionResult{
            true,
            src_it->second->entity_id + " gained +" + std::to_string(amount) + " ranged range this turn",
            {}};
    }));

    static_cast<void>(stack_manager.register_effect("grant_next_damage_bonus_self", [](GameState& game, const StackItem& item) {
        if (item.source_entity_id.empty()) {
            return ActionResult{false, "grant_next_damage_bonus_self requires a source unit", {}};
        }
        const auto src_it = game.board.all_entities_map.find(item.source_entity_id);
        if (src_it == game.board.all_entities_map.end() || !src_it->second) {
            return ActionResult{false, "Source unit is no longer on the board", {}};
        }
        int amount = 1;
        if (const auto it = item.payload.find(effect_keys::kPayloadAmount); it != item.payload.end()) {
            amount = it->second;
        }
        amount = std::max(0, amount);
        if (amount <= 0) {
            return ActionResult{true, "No damage bonus applied", {}};
        }
        add_entity_effect(*src_it->second, "next_damage_bonus", amount, item.item_id);
        propagate_buff_to_surrounding_mirror_passives(game, *src_it->second, "next_damage_bonus", amount);
        return ActionResult{
            true,
            "Granted +" + std::to_string(amount) + " damage to the next damaging attack or ability from " + src_it->second->entity_id,
            {}};
    }));

    static_cast<void>(stack_manager.register_effect("grant_magus_charge_self", [](GameState& game, const StackItem& item) {
        if (item.source_entity_id.empty()) {
            return ActionResult{false, "grant_magus_charge_self requires a source unit", {}};
        }
        const auto src_it = game.board.all_entities_map.find(item.source_entity_id);
        if (src_it == game.board.all_entities_map.end() || !src_it->second) {
            return ActionResult{false, "Source unit is no longer on the board", {}};
        }
        int amount = 1;
        if (const auto it = item.payload.find(effect_keys::kPayloadAmount); it != item.payload.end()) {
            amount = it->second;
        }
        amount = std::max(0, amount);
        if (amount <= 0) {
            return ActionResult{true, "No Arcane Charge applied", {}};
        }
        add_entity_effect(*src_it->second, "magus_charge", amount, item.item_id);
        return ActionResult{
            true,
            "Granted " + std::to_string(amount) + " Arcane Charge to " + src_it->second->entity_id,
            {}};
    }));

    static_cast<void>(stack_manager.register_effect("grant_mana_pylon_charge_self", [](GameState& game, const StackItem& item) {
        if (item.source_entity_id.empty()) {
            return ActionResult{false, "grant_mana_pylon_charge_self requires a source entity", {}};
        }
        const auto src_it = game.board.all_entities_map.find(item.source_entity_id);
        if (src_it == game.board.all_entities_map.end() || !src_it->second) {
            return ActionResult{false, "Source entity is no longer on the board", {}};
        }
        int amount = 1;
        if (const auto it = item.payload.find(effect_keys::kPayloadAmount); it != item.payload.end()) {
            amount = it->second;
        }
        amount = std::max(0, amount);
        if (amount <= 0) {
            return ActionResult{true, "No Mana Pylon charge applied", {}};
        }
        if (!add_entity_effect(*src_it->second, effect_keys::kManaPylonChargeEffectId, amount, item.item_id)) {
            return ActionResult{false, "grant_mana_pylon_charge_self: could not apply charge effect", {}};
        }
        return ActionResult{
            true,
            "Granted " + std::to_string(amount) + " Mana Pylon charge to " + src_it->second->entity_id,
            {}};
    }));

    static_cast<void>(stack_manager.register_effect("grant_seraphina_resonance_self", [](GameState& game, const StackItem& item) {
        if (item.source_entity_id.empty()) {
            return ActionResult{false, "grant_seraphina_resonance_self requires a source unit", {}};
        }
        const auto src_it = game.board.all_entities_map.find(item.source_entity_id);
        if (src_it == game.board.all_entities_map.end() || !src_it->second) {
            return ActionResult{false, "Source unit is no longer on the board", {}};
        }
        Entity& unit = *src_it->second;
        if (!unit.owner) {
            return ActionResult{false, "grant_seraphina_resonance_self: source has no owner", {}};
        }
        int amount = 1;
        if (const auto it = item.payload.find(effect_keys::kPayloadAmount); it != item.payload.end()) {
            amount = std::max(0, it->second);
        }
        int threshold = 3;
        if (const auto thr_it = item.payload.find("threshold"); thr_it != item.payload.end()) {
            threshold = std::max(1, thr_it->second);
        }
        int hand_expires = 2;
        if (const auto exp_it = item.payload.find("hand_expires_after_owner_turn_ends"); exp_it != item.payload.end()) {
            hand_expires = std::max(0, exp_it->second);
        }
        std::string grant_card_key = "symphony_scorching_sphere";
        if (const auto key_it = item.string_payload.find("grant_card_key");
            key_it != item.string_payload.end() && !key_it->second.empty()) {
            grant_card_key = key_it->second;
        }
        if (amount > 0) {
            add_entity_effect(unit, "seraphina_resonance", amount, item.item_id);
        }
        int granted = 0;
        while (entity_effect_amount(unit, "seraphina_resonance") >= threshold) {
            reduce_entity_effect(unit, "seraphina_resonance", threshold);
            if (game.grant_temporary_hand_card(*unit.owner, grant_card_key, hand_expires)) {
                granted += 1;
            }
        }
        return ActionResult{
            true,
            "Seraphina Resonance updated" + (granted > 0 ? ("; granted " + std::to_string(granted) + " hand spell(s)") : ""),
            {}};
    }));

    static_cast<void>(stack_manager.register_effect("grant_movement_speed_self", [](GameState& game, const StackItem& item) {
        if (item.source_entity_id.empty()) {
            return ActionResult{false, "grant_movement_speed_self requires a source unit", {}};
        }
        const auto src_it = game.board.all_entities_map.find(item.source_entity_id);
        if (src_it == game.board.all_entities_map.end() || !src_it->second) {
            return ActionResult{false, "Source unit is no longer on the board", {}};
        }
        int amount = 1;
        if (const auto it = item.payload.find(effect_keys::kPayloadAmount); it != item.payload.end()) {
            amount = it->second;
        }
        amount = std::max(0, amount);
        if (amount > 0) {
            apply_grant_movement_speed_self(game, src_it->second, amount, item.item_id);
        }
        return ActionResult{true, "Granted +" + std::to_string(amount) + " movement speed to self", {}};
    }));

    static_cast<void>(stack_manager.register_effect("release_stored_energy_spell_turquoise", [](GameState& game, const StackItem& item) {
        if (item.source_entity_id.empty()) {
            return ActionResult{false, "release_stored_energy_spell_turquoise requires a source unit", {}};
        }
        const auto src_it = game.board.all_entities_map.find(item.source_entity_id);
        if (src_it == game.board.all_entities_map.end() || !src_it->second) {
            return ActionResult{false, "Source unit is no longer on the board", {}};
        }
        Entity& unit = *src_it->second;
        if (!unit.owner) {
            return ActionResult{false, "release_stored_energy_spell_turquoise: source has no owner", {}};
        }
        const int player_id = *unit.owner;
        const int stored = entity_effect_amount(unit, "ancient_frog_stored_energy");
        if (stored > 0) {
            credit_tagged_float(game.turn_manager, "spell_ability", player_id, EnergyType::Turquoise, stored);
        }
        return ActionResult{true, "Released " + std::to_string(stored) + " stored turquoise flux energy to owner", {}};
    }));

    // Delayed next_damage_bonus: stores the bonus as a TemporaryEntityEffect that expires at
    // owner_turn_start, granting the next_damage_bonus stacks then. This makes the buff available
    // at the START of the owner's next turn rather than immediately.
    static_cast<void>(stack_manager.register_effect("grant_delayed_next_damage_bonus", [](GameState& game, const StackItem& item) {
        const auto resolved = resolve_stack_board_target(game, item, "grant_delayed_next_damage_bonus");
        if (!resolved.status.ok || resolved.status.message.find("(fizzled)") != std::string::npos) {
            return resolved.status;
        }
        auto unit = std::dynamic_pointer_cast<Unit>(resolved.target);
        if (!unit) {
            return ActionResult{false, "grant_delayed_next_damage_bonus: target is not a unit", {}};
        }
        const int amount = payload_int(item, effect_keys::kPayloadAmount, 8);
        TemporaryEntityEffect pending;
        pending.effect_id       = "delayed_next_damage_bonus";
        pending.source_id       = item.source_entity_id;
        pending.name            = item.source_name.empty() ? "Delayed Damage Bonus" : item.source_name;
        pending.rules_text      = "At the start of this unit's owner's next turn, this unit's next attack or ability deals +"
                                  + std::to_string(amount) + " damage.";
        pending.expire_on       = "owner_turn_start";
        pending.remaining_turns = 1;
        pending.stat_grants.on_expire_next_damage_bonus = amount;
        game.add_temporary_effect(resolved.target, std::move(pending));
        return ActionResult{true,
            unit->entity_id + " will gain +" + std::to_string(amount) + " damage bonus at the start of their owner's next turn",
            {}};
    }));

    // Focus spell: deal damage to target equal to half the caster's attack roll (floor), then
    // spawn a Replicator Bot adjacent to the target with the caster's keywords. Bot HP and nominal
    // melee damage equal the damage dealt; sync_unit_damage_ranges_from_nominal gives the natural
    // 3-point spread (d-1)–(d+1). Bot spawns stunned. Caster is identified via source_entity_id.
