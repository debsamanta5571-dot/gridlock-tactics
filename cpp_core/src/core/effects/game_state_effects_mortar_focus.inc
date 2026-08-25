// game_state_effects_mortar_focus.inc — shard of register_default_effects()
    static_cast<void>(stack_manager.register_effect("stun_and_damage", [](GameState& game, const StackItem& item) {
        const auto resolved = resolve_stack_board_target(game, item, "stun_and_damage");
        if (!resolved.status.ok || resolved.status.message.find("(fizzled)") != std::string::npos) {
            return resolved.status;
        }
        int amount = payload_int(item, effect_keys::kPayloadAmount, 3);
        int unit_type_bonus = 0;
        if (resolved.target) {
            unit_type_bonus =
                unit_type_bonus_damage_for_target(*resolved.target, item.bonus_damage_unit_types, item.bonus_damage_amount);
        }
        amount += unit_type_bonus;
        amount += item.ability_damage_bonus;
        std::shared_ptr<Entity> source;
        if (!item.source_entity_id.empty()) {
            const auto src_it = game.board.all_entities_map.find(item.source_entity_id);
            if (src_it != game.board.all_entities_map.end()) {
                source = src_it->second;
            }
        }
        DamagePacket packet;
        packet.source = source;
        packet.target = resolved.target;
        packet.amount = amount;
        packet.damage_type = damage_type_from_stack_payload(item.payload);
        packet.pierces_damage_prevention = item.pierces_damage_prevention;
        packet.from_melee_attack = false;
        packet.from_basic_attack = false;
        packet.ability_grants_lifesteal = item.heals_on_damage_dealt;
        packet.ability_grants_soul_steal = item.heals_allied_base_on_damage_dealt;
        if (!item.soul_steal_heal_base_entity_id.empty()) {
            const auto base_it = game.board.all_entities_map.find(item.soul_steal_heal_base_entity_id);
            if (base_it != game.board.all_entities_map.end()) {
                packet.soul_steal_heal_base = base_it->second;
            }
        }
        packet.source_label = item.source_name.empty() ? "stun_and_damage" : item.source_name;
        apply_damage_packet(game, packet);
        if (!resolved.target || resolved.target->current_health <= 0) {
            return ActionResult{true, packet.source_label + ": dealt " + std::to_string(amount) + " damage", {}};
        }
        const int stun_stacks = std::max(1, payload_int(item, "stun", 1));
        if (!add_entity_effect(*resolved.target, "stunned", stun_stacks)) {
            return ActionResult{true, packet.source_label + ": dealt " + std::to_string(amount) + " damage; target immune to stun", {}};
        }
        return ActionResult{true, packet.source_label + ": dealt " + std::to_string(amount) + " damage and applied " + std::to_string(stun_stacks) + " stun", {}};
    }));

    static_cast<void>(stack_manager.register_effect("magus_charge_strike", [](GameState& game, const StackItem& item) {
        const auto src_it = game.board.all_entities_map.find(item.source_entity_id);
        if (src_it == game.board.all_entities_map.end() || !src_it->second) {
            return ActionResult{false, "magus_charge_strike: source not found", {}};
        }
        auto& source = *src_it->second;
        const int charges = consume_all_entity_effect(source, "magus_charge");
        if (charges <= 0) {
            return ActionResult{true, "magus_charge_strike: no Arcane Charge (fizzled)", {}};
        }
        const auto resolved = resolve_stack_board_target(game, item, "magus_charge_strike");
        if (!resolved.status.ok || resolved.status.message.find("(fizzled)") != std::string::npos) {
            return resolved.status;
        }
        if (!resolved.target) {
            return ActionResult{true, "magus_charge_strike: no target (fizzled)", {}};
        }
        DamagePacket packet;
        packet.source = src_it->second;
        packet.target = resolved.target;
        packet.amount = charges;
        packet.damage_type = damage_type_from_stack_payload(item.payload);
        packet.pierces_damage_prevention = item.pierces_damage_prevention;
        packet.ability_grants_lifesteal = item.heals_on_damage_dealt;
        packet.ability_grants_soul_steal = item.heals_allied_base_on_damage_dealt;
        packet.from_melee_attack = false;
        packet.defer_reactive = true;
        packet.source_label = item.source_name.empty() ? "magus_charge_strike" : item.source_name;
        return apply_damage_packet(game, packet);
    }));

    static_cast<void>(stack_manager.register_effect("magus_charge_surrounding_burst", [](GameState& game, const StackItem& item) {
        const auto src_it = game.board.all_entities_map.find(item.source_entity_id);
        if (src_it == game.board.all_entities_map.end() || !src_it->second || !src_it->second->position) {
            return ActionResult{true, "magus_charge_surrounding_burst: source gone (fizzled)", {}};
        }
        auto& source = *src_it->second;
        const int charges = consume_all_entity_effect(source, "magus_charge");
        if (charges <= 0) {
            return ActionResult{true, "magus_charge_surrounding_burst: no Arcane Charge (fizzled)", {}};
        }

        std::vector<std::shared_ptr<Entity>> victims;
        std::set<std::string> seen;
        auto consider = [&](const std::shared_ptr<Entity>& ent) {
            if (!ent || !seen.insert(ent->entity_id).second) {
                return;
            }
            if (ent->entity_type == "unit" || entity_is_structure(*ent) || entity_is_base(*ent)) {
                victims.push_back(ent);
            }
        };
        consider(src_it->second);
        for (const auto& [cx, cy] : entity_surrounding_cells(source)) {
            consider(game.board.entity_at(cx, cy));
        }
        if (victims.empty()) {
            return ActionResult{true, "magus_charge_surrounding_burst: no targets (fizzled)", {}};
        }

        std::string msg;
        std::vector<std::shared_ptr<Entity>> to_destroy;
        for (const auto& victim : victims) {
            if (!victim || !game.board.all_entities_map.contains(victim->entity_id)) {
                continue;
            }
            DamagePacket packet;
            packet.source = src_it->second;
            packet.target = victim;
            packet.amount = charges;
            packet.damage_type = damage_type_from_stack_payload(item.payload);
            packet.pierces_damage_prevention = item.pierces_damage_prevention;
            packet.ability_grants_lifesteal = item.heals_on_damage_dealt;
            packet.ability_grants_soul_steal = item.heals_allied_base_on_damage_dealt;
            packet.from_melee_attack = false;
            packet.defer_reactive = true;
            packet.source_label = item.source_name.empty() ? "magus_charge_surrounding_burst" : item.source_name;
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
        for (const auto& victim : to_destroy) {
            if (game.board.all_entities_map.contains(victim->entity_id)) {
                game.destroy_board_entity(victim);
            }
        }
        game.refresh_passive_auras();
        return ActionResult{true, msg.empty() ? "magus_charge_surrounding_burst resolved" : msg, {}};
    }));

    // focus_relentless_attack: focus spell attack with temporary Relentless X (this attack only).
    // Payload: amount (default 3). Does not spend the caster's normal attack budget for the turn.
    static_cast<void>(stack_manager.register_effect("focus_relentless_attack", [](GameState& game, const StackItem& item) {
        if (item.source_entity_id.empty()) {
            return ActionResult{false, "focus_relentless_attack: requires a focus caster", {}};
        }
        const auto src_it = game.board.all_entities_map.find(item.source_entity_id);
        if (src_it == game.board.all_entities_map.end() || !src_it->second
                || src_it->second->current_health <= 0) {
            return ActionResult{true, "focus_relentless_attack: caster gone (fizzled)", {}};
        }
        auto caster = std::dynamic_pointer_cast<Unit>(src_it->second);
        if (!caster) {
            return ActionResult{false, "focus_relentless_attack: caster is not a unit", {}};
        }
        const auto xit = item.targets.find(effect_keys::kCellX);
        const auto yit = item.targets.find(effect_keys::kCellY);
        if (xit == item.targets.end() || yit == item.targets.end()) {
            return ActionResult{false, "focus_relentless_attack: missing target cell", {}};
        }
        const std::pair<int, int> target_cell{xit->second, yit->second};
        const int rel_amount = std::max(1, payload_int(item, effect_keys::kPayloadAmount, 3));
        const int saved_attacks = caster->attacks_remaining_this_turn;
        if (saved_attacks <= 0) {
            caster->attacks_remaining_this_turn = 1;
        }
        const bool prefer_ranged = [&]() {
            const int dist = min_chebyshev_entity_to_cell(*caster, target_cell.first, target_cell.second);
            const AttackProfile ranged = attack_profile_for_unit(*caster, true);
            return ranged.use_ranged && dist >= ranged.range_min && dist <= ranged.range_max;
        }();
        const std::string rel_effect_id = "focus_relentless_" + (item.item_id.empty() ? "spell" : item.item_id);
        TemporaryEntityEffect rel_eff;
        rel_eff.effect_id = rel_effect_id;
        rel_eff.source_id = item.source_entity_id;
        rel_eff.name = "Relentless";
        rel_eff.rules_text = "Relentless " + std::to_string(rel_amount) + " for this attack.";
        rel_eff.expire_on = "never";
        rel_eff.granted_attributes.push_back(PassiveAttributeGrant{"relentless", rel_amount});
        caster->temporary_effects.push_back(std::move(rel_eff));
        auto result = resolve_attack(game, caster, item.controller_id, target_cell, prefer_ranged, true);
        caster->temporary_effects.erase(
            std::remove_if(caster->temporary_effects.begin(), caster->temporary_effects.end(),
                [&](const TemporaryEntityEffect& e) { return e.effect_id == rel_effect_id; }),
            caster->temporary_effects.end());
        caster->attacks_remaining_this_turn = saved_attacks;
        return result;
    }));

    static_cast<void>(stack_manager.register_effect("basic_attack", [](GameState& game, const StackItem& item) {
        if (item.source_entity_id.empty()) {
            return ActionResult{false, "Attack source is missing", {}};
        }
        const auto source_it = game.board.all_entities_map.find(item.source_entity_id);
        if (source_it == game.board.all_entities_map.end() || !source_it->second) {
            return ActionResult{false, "Attacker is no longer on the board", {}};
        }
        auto attacker = std::dynamic_pointer_cast<Unit>(source_it->second);
        if (!attacker) {
            return ActionResult{false, "Attack source is not a unit", {}};
        }
        auto xit = item.targets.find(effect_keys::kCellX);
        auto yit = item.targets.find(effect_keys::kCellY);
        if (xit == item.targets.end() || yit == item.targets.end()) {
            return ActionResult{false, "Attack target position is missing", {}};
        }
        const auto target = game.board.entity_at(xit->second, yit->second);
        if (!target) {
            return ActionResult{false, "Attack target is no longer at that position", {}};
        }
        if (!item.target_entity_id.empty() && target->entity_id != item.target_entity_id) {
            return ActionResult{false, "Original attack target is no longer at that position", {}};
        }
        const bool prefer_ranged = item.payload.contains(effect_keys::kAttackPreferRanged) && item.payload.at(effect_keys::kAttackPreferRanged) != 0;
        std::optional<std::pair<int, int>> soul_heal_cell;
        if (const auto hx = item.targets.find(effect_keys::kHealBaseX); hx != item.targets.end()) {
            if (const auto hy = item.targets.find(effect_keys::kHealBaseY); hy != item.targets.end()) {
                soul_heal_cell = std::pair<int, int>{hx->second, hy->second};
            }
        }
        return resolve_attack(game, attacker, item.controller_id, {xit->second, yit->second}, prefer_ranged, true, soul_heal_cell);
    }));

    // ── multi_hit_damage (Fury Strikes) ──────────────────────────────────────────────────────────
    // Hits the same target N times; each hit goes through the full damage pipeline so on-hit
    // effects (overload conduits, lifesteal, passive reactives, etc.) fire independently per hit.
    static_cast<void>(stack_manager.register_effect("multi_hit_damage", [](GameState& game, const StackItem& item) {
        const auto resolved = resolve_stack_board_target(game, item, "multi_hit_damage");
        if (!resolved.status.ok || resolved.status.message.find("(fizzled)") != std::string::npos) {
            return resolved.status;
        }
        int amount = 0;
        if (const auto it = item.payload.find(effect_keys::kPayloadAmount); it != item.payload.end()) {
            amount = it->second;
        }
        int hits = 1;
        if (const auto it = item.payload.find("hits"); it != item.payload.end()) {
            hits = std::max(1, it->second);
        }
        std::shared_ptr<Entity> source;
        if (!item.source_entity_id.empty()) {
            const auto src_it = game.board.all_entities_map.find(item.source_entity_id);
            if (src_it != game.board.all_entities_map.end()) {
                source = src_it->second;
            }
        }
        // Pre-consume the next_damage_bonus ONCE before the loop so the full bonus applies to
        // every hit (e.g. a Tune-Up should make each Fury Strike do +2, not just the first).
        // apply_damage_packet calls consume_next_damage_bonus internally; since we already
        // drained it here, those calls return 0 and do not double-count.
        const int per_hit_bonus = source ? consume_first_entity_effect(*source, "next_damage_bonus") : 0;
        const int per_hit_amount = amount + per_hit_bonus;

        int hits_landed = 0;
        ActionResult last{true, "multi_hit_damage: 0 hits", {}};
        for (int h = 0; h < hits; ++h) {
            // Re-check each iteration — a previous hit may have killed the target.
            if (!resolved.target || resolved.target->current_health <= 0
                || !game.board.all_entities_map.contains(resolved.target->entity_id)) {
                break;
            }
            DamagePacket packet;
            packet.source                         = source;
            packet.target                         = resolved.target;
            packet.amount                         = per_hit_amount;
            packet.damage_type                    = damage_type_from_stack_payload(item.payload);
            packet.pierces_damage_prevention      = item.pierces_damage_prevention;
            packet.ability_grants_lifesteal       = item.heals_on_damage_dealt;
            packet.ability_grants_soul_steal      = item.heals_allied_base_on_damage_dealt;
            packet.defer_reactive                 = true;
            packet.source_label = item.source_name.empty() ? "Fury Strikes" : item.source_name;
            last = apply_damage_packet(game, packet);
            ++hits_landed;
        }
        last.message = "multi_hit_damage: " + std::to_string(hits_landed) + "/" + std::to_string(hits) + " hits";
        return last;
    }));

    // ── missile_storm ─────────────────────────────────────────────────────────────────────────────
    // No player-selected target. The handler auto-selects up to max_targets nearest enemies within
    // max_range (Chebyshev) from the source and fires a separate damage event at each.
    // heal_surrounding_allies: heal all allied units in the 8-way surrounding cells of the source
    // unit for `amount` HP each. Used by Field Nurse's "Patch Up" ability.
    static_cast<void>(stack_manager.register_effect("heal_surrounding_allies", [](GameState& game, const StackItem& item) {
        if (item.source_entity_id.empty()) {
            return ActionResult{false, "heal_surrounding_allies requires a source unit", {}};
        }
        const auto src_it = game.board.all_entities_map.find(item.source_entity_id);
        if (src_it == game.board.all_entities_map.end() || !src_it->second
                || src_it->second->current_health <= 0) {
            return ActionResult{true, "heal_surrounding_allies: source gone (fizzled)", {}};
        }
        const Entity& source = *src_it->second;
        const int amount = std::max(0, payload_int(item, effect_keys::kPayloadAmount, 2));
        if (amount <= 0) {
            return ActionResult{true, "heal_surrounding_allies: zero heal amount", {}};
        }
        const std::string label = item.source_name.empty() ? "Field Dressing" : item.source_name;

        std::set<std::string> healed_ids;
        int count = 0;
        for (const auto& [cx, cy] : entity_surrounding_cells(source)) {
            auto neighbor = game.board.entity_at(cx, cy);
            if (!neighbor || neighbor->entity_type != "unit" || neighbor->current_health <= 0) continue;
            if (!neighbor->owner || teams_hostile(game, *neighbor->owner, item.controller_id)) continue;
            if (!healed_ids.insert(neighbor->entity_id).second) continue;
            HealPacket hp;
            hp.target       = neighbor;
            hp.amount       = amount;
            hp.source_label = label;
            apply_heal_packet(game, hp);
            ++count;
        }
        if (count == 0) {
            return ActionResult{true, "heal_surrounding_allies: no allied units in range (fizzled)", {}};
        }
        return ActionResult{true,
            label + " healed " + std::to_string(count) + " surrounding allied unit(s) for " + std::to_string(amount) + " HP", {}};
    }));

    // cleanse: remove all negative status stacks from the target allied unit.
    // Negative statuses: poison, fire, bleed, silenced, jammed, rooted, stunned, overload.
    static_cast<void>(stack_manager.register_effect("cleanse", [](GameState& game, const StackItem& item) {
        const auto resolved = resolve_stack_board_target(game, item, "cleanse");
        if (!resolved.status.ok || resolved.status.message.find("(fizzled)") != std::string::npos) {
            return resolved.status;
        }
        if (!resolved.target || resolved.target->entity_type != "unit") {
            return ActionResult{false, "cleanse: target must be a unit", {}};
        }
        // Collect all negative-status keys currently on the target (dedup by key string).
        // The catalog determines what counts as negative — add is_negative=true there to
        // make a new debuff cleanable without touching this handler.
        std::set<std::string> negative_keys_present;
        for (const EntityEffectInstance& eff : resolved.target->entity_effects) {
            if (eff.amount > 0 && is_negative_status_effect(eff.key)) {
                negative_keys_present.insert(eff.key);
            }
        }
        int removed = 0;
        for (const std::string& key : negative_keys_present) {
            const int amt = entity_effect_amount(*resolved.target, key);
            if (amt > 0) {
                reduce_entity_effect(*resolved.target, key, amt);
                removed += amt;
            }
        }
        game.refresh_passive_auras();
        if (removed == 0) {
            return ActionResult{true, "cleanse: " + resolved.target->entity_id + " had no debuffs (fizzled)", {}};
        }
        return ActionResult{true,
            "Cleansed " + resolved.target->entity_id + " (" + std::to_string(removed) + " debuff stack(s) removed)", {}};
    }));


    // triage: remove one randomly chosen negative status from the target allied unit
    // (all stacks of that debuff type). Field Nurse Triage; cleanse spell still removes all.
    static_cast<void>(stack_manager.register_effect("triage", [](GameState& game, const StackItem& item) {
        const auto resolved = resolve_stack_board_target(game, item, "triage");
        if (!resolved.status.ok || resolved.status.message.find("(fizzled)") != std::string::npos) {
            return resolved.status;
        }
        if (!resolved.target || resolved.target->entity_type != "unit") {
            return ActionResult{false, "triage: target must be a unit", {}};
        }
        const auto removed = remove_one_random_negative_status(*resolved.target, game.rng());
        if (!removed) {
            return ActionResult{true, "triage: " + resolved.target->entity_id + " had no debuffs (fizzled)", {}};
        }
        game.refresh_passive_auras();
        return ActionResult{true,
            "Triage removed " + *removed + " from " + resolved.target->entity_id, {}};
    }));


    // heal_and_triage: heal an ally, then remove one random negative status (all stacks of that type).
    static_cast<void>(stack_manager.register_effect("heal_and_triage", [](GameState& game, const StackItem& item) {
        const auto resolved = resolve_stack_board_target(game, item, "heal_and_triage");
        if (!resolved.status.ok || resolved.status.message.find("(fizzled)") != std::string::npos) {
            return resolved.status;
        }
        if (!resolved.target) {
            return ActionResult{false, "heal_and_triage: no valid target", {}};
        }
        const int amount = payload_int(item, effect_keys::kPayloadAmount, 3);
        HealPacket packet;
        packet.target = resolved.target;
        packet.amount = std::max(0, amount);
        packet.source_label = item.source_name.empty() ? item.effect_key : item.source_name;
        auto heal_res = apply_heal_packet(game, packet);
        if (!heal_res.ok) {
            return heal_res;
        }
        std::string msg = heal_res.message;
        if (resolved.target->entity_type == "unit") {
            if (const auto removed = remove_one_random_negative_status(*resolved.target, game.rng())) {
                game.refresh_passive_auras();
                msg += "; removed " + *removed;
            }
        }
        return ActionResult{true, msg, {}};
    }));

    // grant_bonus_attack: grant the source unit one additional attack this turn.
    // No board target — uses source_entity_id. Used by Diesel Rider's Blitz ability (barrage).
    static_cast<void>(stack_manager.register_effect("grant_bonus_attack", [](GameState& game, const StackItem& item) {
        if (item.source_entity_id.empty()) {
            return ActionResult{false, "grant_bonus_attack requires a source unit", {}};
        }
        const auto src_it = game.board.all_entities_map.find(item.source_entity_id);
        if (src_it == game.board.all_entities_map.end() || !src_it->second
                || src_it->second->current_health <= 0) {
            return ActionResult{true, "grant_bonus_attack: source gone (fizzled)", {}};
        }
        auto unit = std::dynamic_pointer_cast<Unit>(src_it->second);
        if (!unit) {
            return ActionResult{false, "grant_bonus_attack: source is not a unit", {}};
        }
        unit->bonus_attacks_remaining_this_turn += 1;
        return ActionResult{true,
            unit->entity_id + " gained a bonus attack (" + std::to_string(unit->bonus_attacks_remaining_this_turn) + " bonus remaining)", {}};
    }));

    // heal_self: heal the source entity for `amount` HP.
    // No board target — heals source_entity_id directly.
    // Used as pickup_effect_key for supply-drop pickups (the collecting unit is the source).
    // Payload: "amount" (default 4).
    static_cast<void>(stack_manager.register_effect("heal_self", [](GameState& game, const StackItem& item) {
        if (item.source_entity_id.empty()) {
            return ActionResult{false, "heal_self: no source entity", {}};
        }
        const auto src_it = game.board.all_entities_map.find(item.source_entity_id);
        if (src_it == game.board.all_entities_map.end() || !src_it->second
                || src_it->second->current_health <= 0) {
            return ActionResult{true, "heal_self: source gone (fizzled)", {}};
        }
        const int amount = payload_int(item, effect_keys::kPayloadAmount, 4);
        HealPacket hp;
        hp.target = src_it->second;
        hp.amount = amount;
        hp.source_label = src_it->second->entity_id + " (supply drop)";
        apply_heal_packet(game, hp);
        return ActionResult{true,
            src_it->second->entity_id + " healed for " + std::to_string(amount), {}};
    }));

    // place_trench: turn an unmodified tile on or adjacent to an allied unit into a trench.
    static_cast<void>(stack_manager.register_effect("place_trench", [](GameState& game, const StackItem& item) {
        const auto xit = item.targets.find(effect_keys::kCellX);
        const auto yit = item.targets.find(effect_keys::kCellY);
        if (xit == item.targets.end() || yit == item.targets.end()) {
            return ActionResult{false, "place_trench: requires a target cell", {}};
        }
        const int tx = xit->second;
        const int ty = yit->second;
        const auto vr = validate_place_trench_target(game, item.controller_id, tx, ty);
        if (!vr.ok) {
            return vr;
        }
        const auto sq = game.board.get_square(tx, ty);
        if (!sq) {
            return ActionResult{false, "place_trench: target cell is not on the board", {}};
        }
        if (!place_trench_on_square(sq)) {
            return ActionResult{false, "place_trench: failed to place trench at (" + std::to_string(tx) + "," + std::to_string(ty) + ")", {}};
        }
        return ActionResult{true, "Trench placed at (" + std::to_string(tx) + "," + std::to_string(ty) + ")", {}};
    }));

    // gas_strike: place a gas cloud overlay on a 5-tile cross (center + 4 cardinal neighbors).
    // Units on the affected tiles take 1 Poison immediately (friendly and enemy alike).
    // Each subsequent turn-end, every unit owned by the active player that stands on any gas
    // cloud tile receives 1 Poison — regardless of who placed the cloud (see
    // process_tile_overlay_effects, called from TurnManager::end_turn before DOT processing).
    // Duration decrements at the cloud owner's turn-end; the cloud is removed when it expires.
    static_cast<void>(stack_manager.register_effect("gas_strike", [](GameState& game, const StackItem& item) {
        const auto xit = item.targets.find(effect_keys::kCellX);
        const auto yit = item.targets.find(effect_keys::kCellY);
        if (xit == item.targets.end() || yit == item.targets.end()) {
            return ActionResult{false, "gas_strike: requires a target cell", {}};
        }
        const int cx = xit->second;
        const int cy = yit->second;

        // Five-cell cross: center + cardinal neighbors
        const std::pair<int,int> cross_offsets[5] = {{0,0},{1,0},{-1,0},{0,1},{0,-1}};

        std::vector<std::pair<int, int>> cells;
        cells.reserve(5);
        for (const auto& [dx, dy] : cross_offsets) {
            cells.emplace_back(cx + dx, cy + dy);
        }
        const int cells_affected = place_gas_cloud_overlays(game, item, cells, 3);
        poison_units_on_cells(game, cells, "gas_strike");
        return ActionResult{true,
            "Gas cloud placed on " + std::to_string(cells_affected) + " tile(s)", {}};
    }));

    // scorching_sphere: 13-tile blast (3×3 + cardinal edge extensions). Deal damage, apply Fire to
    // all units in the area, and place fire_tile overlays on unoccupied cells.
    static_cast<void>(stack_manager.register_effect("scorching_sphere", [](GameState& game, const StackItem& item) {
        const auto xit = item.targets.find(effect_keys::kCellX);
        const auto yit = item.targets.find(effect_keys::kCellY);
        if (xit == item.targets.end() || yit == item.targets.end()) {
            return ActionResult{false, "scorching_sphere: requires a target cell", {}};
        }
        const int cx = xit->second;
        const int cy = yit->second;
        const int amount = payload_int(item, effect_keys::kPayloadAmount, 5);
        const int fire_stacks = payload_int(item, "fire", 1);
        const int overlay_duration = overlay_duration_from_payload(item.payload, 2);
        const auto cells = scorching_sphere_cells(cx, cy);

        std::shared_ptr<Entity> caster_ent;
        if (!item.source_entity_id.empty()) {
            const auto it = game.board.all_entities_map.find(item.source_entity_id);
            if (it != game.board.all_entities_map.end()) {
                caster_ent = it->second;
            }
        }

        int hit_count = 0;
        for (const auto& ent : entities_in_cells(game, cells)) {
            if (!ent || ent->current_health <= 0) {
                continue;
            }
            DamagePacket packet;
            packet.source = caster_ent;
            packet.target = ent;
            packet.amount = amount + item.ability_damage_bonus;
            packet.damage_type = DamageType::Physical;
            packet.pierces_damage_prevention = item.pierces_damage_prevention;
            packet.ability_grants_lifesteal = item.heals_on_damage_dealt;
            packet.source_label = item.source_name.empty() ? "Scorching Sphere" : item.source_name;
            packet.defer_reactive = true;
            apply_damage_packet(game, packet);
            ++hit_count;
        }
        if (fire_stacks > 0) {
            fire_units_on_cells(game, cells, "scorching_sphere");
        }
        const int tiles_burning = place_fire_tile_overlays_on_unoccupied(game, item, cells, overlay_duration);
        return ActionResult{true,
            "Scorching sphere hit " + std::to_string(hit_count) + " entity/entities and ignited "
                + std::to_string(tiles_burning) + " unoccupied tile(s)",
            {}};
    }));

    // gas_grenade: cleave-pattern gas cloud (3 tiles) with configurable duration (default 2).
    static_cast<void>(stack_manager.register_effect("gas_grenade", [](GameState& game, const StackItem& item) {
        if (item.source_entity_id.empty()) {
            return ActionResult{false, "gas_grenade: requires a source unit", {}};
        }
        const auto src_it = game.board.all_entities_map.find(item.source_entity_id);
        if (src_it == game.board.all_entities_map.end() || !src_it->second || src_it->second->current_health <= 0) {
            return ActionResult{true, "gas_grenade: source missing or dead (fizzled)", {}};
        }
        const auto xit = item.targets.find(effect_keys::kCellX);
        const auto yit = item.targets.find(effect_keys::kCellY);
        if (xit == item.targets.end() || yit == item.targets.end()) {
            return ActionResult{false, "gas_grenade: requires a target cell", {}};
        }
        const int duration = overlay_duration_from_payload(item.payload, 2);
        const auto cells = cleave_pattern_cells(*src_it->second, xit->second, yit->second);
        const int cells_affected = place_gas_cloud_overlays(game, item, cells, duration);
        poison_units_on_cells(game, cells, "gas_grenade");
        return ActionResult{true,
            "Gas grenade placed on " + std::to_string(cells_affected) + " tile(s)", {}};
    }));

    // gas_chain_detonate: remove gas on a chosen tile, deal damage to occupants, chain through surrounding gas tiles.
