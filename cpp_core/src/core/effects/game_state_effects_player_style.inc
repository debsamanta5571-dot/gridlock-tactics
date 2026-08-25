// game_state_effects_player_style.inc — shard of register_default_effects()
    static_cast<void>(stack_manager.register_effect("hookshot_pull", [](GameState& game, const StackItem& item) {
        const auto resolved = resolve_stack_board_target(game, item, "hookshot_pull");
        if (!resolved.status.ok || resolved.status.message.find("(fizzled)") != std::string::npos) {
            return resolved.status;
        }
        if (!resolved.target || !resolved.target->position) {
            return ActionResult{false, "hookshot_pull: requires a board entity target", {}};
        }
        if (resolved.target->entity_type != "unit") {
            return ActionResult{false, "hookshot_pull: target must be a unit", {}};
        }
        if (const auto immune = stack_fizzle_if_immune(*resolved.target, item.effect_key)) {
            return *immune;
        }

        // Check source entity
        if (item.source_entity_id.empty()) {
            return ActionResult{false, "hookshot_pull: requires a source entity", {}};
        }
        const auto src_it = game.board.all_entities_map.find(item.source_entity_id);
        if (src_it == game.board.all_entities_map.end() || !src_it->second || !src_it->second->position) {
            return ActionResult{true, "hookshot_pull: source missing (fizzled)", {}};
        }
        auto source = src_it->second;

        // Validate orthogonal/diagonal line (8-directional)
        const auto [sx, sy] = *source->position;
        const auto [tx, ty] = *resolved.target->position;
        const int dx = tx - sx;
        const int dy = ty - sy;
        
        // Check if the line is orthogonal (one component zero) or diagonal (absolute values equal)
        const bool is_orthogonal = (dx == 0 || dy == 0);
        const bool is_diagonal = (std::abs(dx) == std::abs(dy));
        
        if (!is_orthogonal && !is_diagonal) {
            return ActionResult{false, "hookshot_pull: target must be orthogonal or diagonal from source", {}};
        }

        // Deal damage first
        const int damage_amount = payload_int(item, effect_keys::kPayloadAmount, 3);
        DamagePacket packet;
        packet.source = source;
        packet.target = resolved.target;
        packet.amount = damage_amount;
        packet.damage_type = damage_type_from_stack_payload(item.payload);
        packet.pierces_damage_prevention = item.pierces_damage_prevention;
        packet.ability_grants_lifesteal = item.heals_on_damage_dealt;
        packet.source_label = item.source_name.empty() ? "Hookshot" : item.source_name;
        apply_damage_packet(game, packet);

        // Pull the target toward the source (opposite of push)
        // Calculate direction from target to source
        int dir_x = 0;
        int dir_y = 0;
        if (dx > 0) dir_x = -1;
        else if (dx < 0) dir_x = 1;
        if (dy > 0) dir_y = -1;
        else if (dy < 0) dir_y = 1;

        // Pull until adjacent to source or blocked
        int tiles_pulled = 0;
        const int max_pull_distance = std::abs(dx) + std::abs(dy);

        for (int i = 0; i < max_pull_distance; ++i) {
            if (!game.board.all_entities_map.contains(resolved.target->entity_id)) {
                break; // Target destroyed
            }
            if (!resolved.target->position) {
                break; // Target no longer has position
            }

            const auto [cur_x, cur_y] = *resolved.target->position;
            
            // Check if adjacent to source (Chebyshev distance 1)
            const int dist_to_source = std::max(std::abs(cur_x - sx), std::abs(cur_y - sy));
            if (dist_to_source <= 1) {
                break; // Already adjacent
            }

            // Calculate next position
            const int next_x = cur_x + dir_x;
            const int next_y = cur_y + dir_y;

            // Check if next position is valid
            const auto next_sq = game.board.get_square(next_x, next_y);
            if (!next_sq) {
                break; // Off board
            }

            // Check if blocked by another entity
            if (next_sq->occupied && next_sq->entity && next_sq->entity->entity_id != resolved.target->entity_id) {
                break; // Blocked
            }

            // Move the target
            game.board.remove_entity(resolved.target);
            resolved.target->position = {next_x, next_y};
            if (!game.board.place_entity(resolved.target, next_x, next_y)) {
                resolved.target->position = {cur_x, cur_y};
                game.board.place_entity(resolved.target, cur_x, cur_y);
                break;
            }
            ++tiles_pulled;
        }

        if (tiles_pulled > 0 && game.board.all_entities_map.contains(resolved.target->entity_id)) {
            game.cancel_stale_queued_batch_items_after_entity_move(resolved.target->entity_id);
            game.process_bleed_after_movement(resolved.target);
            game.refresh_passive_auras();
        }

        return ActionResult{true, "Hookshot: dealt " + std::to_string(damage_amount) + " damage and pulled " + std::to_string(tiles_pulled) + " tile(s)", {}};
    }));

    // swap_weapons: toggles an alternate attack mode on and off.
    // First cast applies "pistol_mode" TemporaryEntityEffect (expire_on "never"); casting again
    // removes it (swap back).  Payload int: ranged_min, ranged_max, range_bonus.
    // Payload string: suppress (comma-sep keywords), grant (comma-sep "key:amount").
    static_cast<void>(stack_manager.register_effect("swap_weapons", [parse_csv, parse_keyword_grants](GameState& game, const StackItem& item) {
        const auto src_it = game.board.all_entities_map.find(item.source_entity_id);
        if (src_it == game.board.all_entities_map.end() || !src_it->second
                || src_it->second->current_health <= 0) {
            return ActionResult{true, "swap_weapons: source gone (fizzled)", {}};
        }
        auto& effs = src_it->second->temporary_effects;
        // Toggle off: remove existing pistol_mode.
        auto mode_it = std::find_if(effs.begin(), effs.end(),
            [](const TemporaryEntityEffect& e) { return e.effect_id == "pistol_mode"; });
        if (mode_it != effs.end()) {
            effs.erase(mode_it);
            game.refresh_passive_auras();
            return ActionResult{true, "Weapon swapped back to default", {}};
        }
        // Toggle on: build pistol_mode effect.
        TemporaryEntityEffect mode;
        mode.effect_id       = "pistol_mode";
        mode.source_id       = item.source_entity_id;
        mode.name            = "Pistol Mode";
        mode.expire_on       = "never";
        mode.remaining_turns = 0;
        const int rmin   = payload_int(item, "ranged_min",   0);
        const int rmax   = payload_int(item, "ranged_max",   0);
        const int rbonus = payload_int(item, "range_bonus",  0);
        if (rmin   > 0) mode.stat_grants.override_ranged_damage_min = rmin;
        if (rmax   > 0) mode.stat_grants.override_ranged_damage_max = rmax;
        if (rbonus != 0) mode.stat_grants.bonus_ranged_range = rbonus;
        const auto sup_it = item.string_payload.find("suppress");
        if (sup_it != item.string_payload.end())
            mode.suppress_attributes = parse_csv(sup_it->second);
        const auto grant_it = item.string_payload.find("grant");
        if (grant_it != item.string_payload.end())
            mode.granted_attributes = parse_keyword_grants(grant_it->second);
        effs.push_back(std::move(mode));
        game.refresh_passive_auras();
        return ActionResult{true, "Weapon swapped to alternate mode", {}};
    }));

    // apply_dual_wield: removes any pistol_mode and applies a permanent dual_wield_mode.
    // Disable payload string key "disable" lists ability keys to lock out while active.
    static_cast<void>(stack_manager.register_effect("apply_dual_wield", [parse_csv, parse_keyword_grants](GameState& game, const StackItem& item) {
        const auto src_it = game.board.all_entities_map.find(item.source_entity_id);
        if (src_it == game.board.all_entities_map.end() || !src_it->second
                || src_it->second->current_health <= 0) {
            return ActionResult{true, "apply_dual_wield: source gone (fizzled)", {}};
        }
        auto& effs = src_it->second->temporary_effects;
        // Remove superseded pistol mode.
        effs.erase(std::remove_if(effs.begin(), effs.end(),
            [](const TemporaryEntityEffect& e) { return e.effect_id == "pistol_mode"; }), effs.end());
        // Idempotent: already in dual wield.
        if (std::any_of(effs.begin(), effs.end(),
                [](const TemporaryEntityEffect& e) { return e.effect_id == "dual_wield_mode"; })) {
            return ActionResult{true, "apply_dual_wield: already active (fizzled)", {}};
        }
        TemporaryEntityEffect mode;
        mode.effect_id       = "dual_wield_mode";
        mode.source_id       = item.source_entity_id;
        mode.name            = "Dual Wield Mode";
        mode.expire_on       = "never";
        mode.remaining_turns = 0;
        const int rmin   = payload_int(item, "ranged_min",   0);
        const int rmax   = payload_int(item, "ranged_max",   0);
        const int rbonus = payload_int(item, "range_bonus",  0);
        if (rmin   > 0) mode.stat_grants.override_ranged_damage_min = rmin;
        if (rmax   > 0) mode.stat_grants.override_ranged_damage_max = rmax;
        if (rbonus != 0) mode.stat_grants.bonus_ranged_range = rbonus;
        const auto sup_it = item.string_payload.find("suppress");
        if (sup_it != item.string_payload.end())
            mode.suppress_attributes = parse_csv(sup_it->second);
        const auto grant_it = item.string_payload.find("grant");
        if (grant_it != item.string_payload.end())
            mode.granted_attributes = parse_keyword_grants(grant_it->second);
        const auto dis_it = item.string_payload.find("disable");
        if (dis_it != item.string_payload.end())
            mode.disable_ability_keys = parse_csv(dis_it->second);
        effs.push_back(std::move(mode));
        game.refresh_passive_auras();
        return ActionResult{true, "Dual Wield mode applied", {}};
    }));


    static_cast<void>(stack_manager.register_effect("true_transformation", [parse_csv, parse_keyword_grants](GameState& game, const StackItem& item) {
        const auto src_it = game.board.all_entities_map.find(item.source_entity_id);
        if (src_it == game.board.all_entities_map.end() || !src_it->second
                || src_it->second->current_health <= 0) {
            return ActionResult{true, "true_transformation: source gone (fizzled)", {}};
        }
        auto& effs = src_it->second->temporary_effects;
        if (std::any_of(effs.begin(), effs.end(),
                [](const TemporaryEntityEffect& e) { return e.effect_id == "true_transformation_mode"; })) {
            return ActionResult{true, "true_transformation: already active (fizzled)", {}};
        }
        TemporaryEntityEffect mode;
        mode.effect_id       = "true_transformation_mode";
        mode.source_id       = item.source_entity_id;
        mode.name            = "True Transformation";
        mode.rules_text      = "Multistrike 1 and Lifesteal. Bloodlust passive disabled.";
        mode.expire_on       = "never";
        mode.remaining_turns = 0;
        const auto grant_it = item.string_payload.find("grant");
        if (grant_it != item.string_payload.end()) {
            mode.granted_attributes = parse_keyword_grants(grant_it->second);
        } else {
            mode.granted_attributes = parse_keyword_grants("multistrike:1,lifesteal");
        }
        const auto sup_it = item.string_payload.find("suppress_passive");
        if (sup_it != item.string_payload.end()) {
            mode.suppress_passive_keys = parse_csv(sup_it->second);
        }
        effs.push_back(std::move(mode));
        game.refresh_passive_auras();
        return ActionResult{true, "True Transformation: Multistrike 1 and Lifesteal; Bloodlust disabled", {}};
    }));

    // ── cross_shot ──────────────────────────────────────────────────────────────────────────────
    // No board target. Fire one shot at each cell exactly `range` tiles away in the 4 cardinal
    // directions (+/-x, +/-y). Each shot is LOS-checked; only hostile entities are hit.
    // On resolve (regardless of hits), the caster gains 1 Style stack.
    // Uses the caster's ranged damage profile; ability_damage_bonus applies to each hit.
    // Payload: range (default 2).
    static_cast<void>(stack_manager.register_effect("cross_shot", [](GameState& game, const StackItem& item) {
        if (item.source_entity_id.empty()) {
            return ActionResult{false, "cross_shot: no source entity", {}};
        }
        const auto src_it = game.board.all_entities_map.find(item.source_entity_id);
        if (src_it == game.board.all_entities_map.end() || !src_it->second
                || src_it->second->current_health <= 0 || !src_it->second->position) {
            return ActionResult{true, "cross_shot: source missing or dead (fizzled)", {}};
        }
        auto* unit = dynamic_cast<Unit*>(src_it->second.get());
        if (!unit) {
            return ActionResult{false, "cross_shot: source is not a unit", {}};
        }
        const int range     = payload_int(item, "range", 2);
        const int dmg_bonus = item.ability_damage_bonus;
        const int ax        = unit->position->first;
        const int ay        = unit->position->second;
        const auto ranged_profile = attack_profile_for_unit(*unit, /*prefer_ranged=*/true);
        const std::string label   = item.source_name.empty() ? "Cross Shot" : item.source_name;

        const std::array<std::pair<int,int>, 4> offsets = {{{0, -range}, {0, range}, {-range, 0}, {range, 0}}};
        int hit_count = 0;
        for (auto [dx, dy] : offsets) {
            const int tx = ax + dx;
            const int ty = ay + dy;
            if (!entity_has_line_of_sight_to_cell(game, *unit, {tx, ty})) continue;
            const auto tgt = game.board.entity_at(tx, ty);
            if (!tgt || tgt->current_health <= 0) continue;
            if (entities_allied(*unit, *tgt)) continue;
            const auto roll = roll_attack_damage(game.rng_, ranged_profile.damage_range, kDefaultCritChancePercent);
            DamagePacket pkt;
            pkt.source       = src_it->second;
            pkt.target       = tgt;
            pkt.amount       = roll.final_damage + dmg_bonus;
            pkt.rolled_amount = roll.final_damage;
            pkt.is_crit      = roll.is_crit;
            pkt.from_basic_attack = false;
            pkt.source_label = label;
            apply_damage_packet(game, pkt);
            ++hit_count;
        }
        add_entity_effect(*unit, "style", 1, item.source_entity_id);
        return ActionResult{true,
            label + ": " + std::to_string(hit_count) + " hit(s), Style stacks now "
                + std::to_string(entity_effect_amount(*unit, "style")), {}};
    }));

    // ── x_shot ─────────────────────────────────────────────────────────────────────────────────
    // Like cross_shot but fires at the 4 diagonal cells exactly `range` tiles away.
    // Payload: range (default 2).
    static_cast<void>(stack_manager.register_effect("x_shot", [](GameState& game, const StackItem& item) {
        if (item.source_entity_id.empty()) {
            return ActionResult{false, "x_shot: no source entity", {}};
        }
        const auto src_it = game.board.all_entities_map.find(item.source_entity_id);
        if (src_it == game.board.all_entities_map.end() || !src_it->second
                || src_it->second->current_health <= 0 || !src_it->second->position) {
            return ActionResult{true, "x_shot: source missing or dead (fizzled)", {}};
        }
        auto* unit = dynamic_cast<Unit*>(src_it->second.get());
        if (!unit) {
            return ActionResult{false, "x_shot: source is not a unit", {}};
        }
        const int range     = payload_int(item, "range", 2);
        const int dmg_bonus = item.ability_damage_bonus;
        const int ax        = unit->position->first;
        const int ay        = unit->position->second;
        const auto ranged_profile = attack_profile_for_unit(*unit, /*prefer_ranged=*/true);
        const std::string label   = item.source_name.empty() ? "X Shot" : item.source_name;

        const std::array<std::pair<int,int>, 4> offsets = {{{-range, -range}, {-range, range}, {range, -range}, {range, range}}};
        int hit_count = 0;
        for (auto [dx, dy] : offsets) {
            const int tx = ax + dx;
            const int ty = ay + dy;
            if (!entity_has_line_of_sight_to_cell(game, *unit, {tx, ty})) continue;
            const auto tgt = game.board.entity_at(tx, ty);
            if (!tgt || tgt->current_health <= 0) continue;
            if (entities_allied(*unit, *tgt)) continue;
            const auto roll = roll_attack_damage(game.rng_, ranged_profile.damage_range, kDefaultCritChancePercent);
            DamagePacket pkt;
            pkt.source        = src_it->second;
            pkt.target        = tgt;
            pkt.amount        = roll.final_damage + dmg_bonus;
            pkt.rolled_amount = roll.final_damage;
            pkt.is_crit       = roll.is_crit;
            pkt.from_basic_attack = false;
            pkt.source_label  = label;
            apply_damage_packet(game, pkt);
            ++hit_count;
        }
        add_entity_effect(*unit, "style", 1, item.source_entity_id);
        return ActionResult{true,
            label + ": " + std::to_string(hit_count) + " hit(s), Style stacks now "
                + std::to_string(entity_effect_amount(*unit, "style")), {}};
    }));

    // ── alternating_cross_x_shot ────────────────────────────────────────────────────────────────
    // Debonair: alternates Cross Shot (cardinal) and X Shot (diagonal) each activation.
    // State persists on the unit via entity effect `debonair_next_x_shot` (0 = cross next, 1 = x next).
    static_cast<void>(stack_manager.register_effect("alternating_cross_x_shot", [](GameState& game, const StackItem& item) {
        if (item.source_entity_id.empty()) {
            return ActionResult{false, "alternating_cross_x_shot: no source entity", {}};
        }
        const auto src_it = game.board.all_entities_map.find(item.source_entity_id);
        if (src_it == game.board.all_entities_map.end() || !src_it->second
                || src_it->second->current_health <= 0 || !src_it->second->position) {
            return ActionResult{true, "alternating_cross_x_shot: source missing or dead (fizzled)", {}};
        }
        auto* unit = dynamic_cast<Unit*>(src_it->second.get());
        if (!unit) {
            return ActionResult{false, "alternating_cross_x_shot: source is not a unit", {}};
        }
        const bool use_x_shot = entity_effect_amount(*unit, "debonair_next_x_shot") > 0;
        const int range     = payload_int(item, "range", 2);
        const int dmg_bonus = item.ability_damage_bonus;
        const int ax        = unit->position->first;
        const int ay        = unit->position->second;
        const auto ranged_profile = attack_profile_for_unit(*unit, /*prefer_ranged=*/true);
        const std::string label   = use_x_shot ? "X Shot" : "Cross Shot";

        const std::array<std::pair<int,int>, 4> offsets = use_x_shot
            ? std::array<std::pair<int,int>, 4>{{{-range, -range}, {-range, range}, {range, -range}, {range, range}}}
            : std::array<std::pair<int,int>, 4>{{{0, -range}, {0, range}, {-range, 0}, {range, 0}}};

        int hit_count = 0;
        for (auto [dx, dy] : offsets) {
            const int tx = ax + dx;
            const int ty = ay + dy;
            if (!entity_has_line_of_sight_to_cell(game, *unit, {tx, ty})) continue;
            const auto tgt = game.board.entity_at(tx, ty);
            if (!tgt || tgt->current_health <= 0) continue;
            if (entities_allied(*unit, *tgt)) continue;
            const auto roll = roll_attack_damage(game.rng_, ranged_profile.damage_range, kDefaultCritChancePercent);
            DamagePacket pkt;
            pkt.source        = src_it->second;
            pkt.target        = tgt;
            pkt.amount        = roll.final_damage + dmg_bonus;
            pkt.rolled_amount = roll.final_damage;
            pkt.is_crit       = roll.is_crit;
            pkt.from_basic_attack = false;
            pkt.source_label  = label;
            apply_damage_packet(game, pkt);
            ++hit_count;
        }

        if (use_x_shot) {
            reduce_entity_effect(*unit, "debonair_next_x_shot", entity_effect_amount(*unit, "debonair_next_x_shot"));
        } else {
            add_entity_effect(*unit, "debonair_next_x_shot", 1, item.source_entity_id);
        }
        add_entity_effect(*unit, "style", 1, item.source_entity_id);
        return ActionResult{true,
            label + ": " + std::to_string(hit_count) + " hit(s), Style stacks now "
                + std::to_string(entity_effect_amount(*unit, "style")), {}};
    }));

    // ── unleash_style ──────────────────────────────────────────────────────────────────────────
    // Board target: adjacent enemy (range 1). Consume all Style stacks, deal stacks * 2 damage
    // (+ ability_damage_bonus) to the target. Fizzles cleanly if the caster has no style stacks.
    static_cast<void>(stack_manager.register_effect("unleash_style", [](GameState& game, const StackItem& item) {
        if (item.source_entity_id.empty()) {
            return ActionResult{false, "unleash_style: no source entity", {}};
        }
        const auto src_it = game.board.all_entities_map.find(item.source_entity_id);
        if (src_it == game.board.all_entities_map.end() || !src_it->second
                || src_it->second->current_health <= 0) {
            return ActionResult{true, "unleash_style: source missing (fizzled)", {}};
        }
        const int stacks = entity_effect_amount(*src_it->second, "style");
        if (stacks <= 0) {
            return ActionResult{true, "unleash_style: no style stacks (fizzled)", {}};
        }
        const auto tgt_result = resolve_stack_board_target(game, item, "Unleash");
        if (!tgt_result.status.ok) {
            return tgt_result.status;
        }
        const auto& target = tgt_result.target;
        remove_entity_effect(*src_it->second, "style");
        const int damage = stacks * 2 + item.ability_damage_bonus;
        DamagePacket pkt;
        pkt.source        = src_it->second;
        pkt.target        = target;
        pkt.amount        = damage;
        pkt.rolled_amount = damage;
        pkt.is_crit       = false;
        pkt.from_basic_attack = false;
        pkt.source_label  = item.source_name.empty() ? "Unleash" : item.source_name;
        apply_damage_packet(game, pkt);
        return ActionResult{true,
            "Unleash: consumed " + std::to_string(stacks) + " style stacks, dealt "
                + std::to_string(damage) + " damage", {}};
    }));

    // ── NEW ACM CARDS ─────────────────────────────────────────────────────────────────────────────

    // Defective Augment: apply N jammed stacks to target unit and permanently grant +attack/+health.
    static_cast<void>(stack_manager.register_effect("apply_jammed_grant_stats",
        [](GameState& game, const StackItem& item) {
            const auto resolved = resolve_stack_board_target(game, item, "apply_jammed_grant_stats");
            if (!resolved.status.ok || resolved.status.message.find("(fizzled)") != std::string::npos) {
                return resolved.status;
            }
            if (!resolved.target) {
                return ActionResult{false, "apply_jammed_grant_stats: no target", {}};
            }
            int jammed_amount = 2;
            int attack_gain   = 3;
            int health_gain   = 3;
            if (const auto it = item.payload.find("jammed"); it != item.payload.end()) jammed_amount = it->second;
            if (const auto it = item.payload.find("attack"); it != item.payload.end()) attack_gain = it->second;
            if (const auto it = item.payload.find("health"); it != item.payload.end()) health_gain = it->second;
            for (int i = 0; i < jammed_amount; ++i) {
                add_entity_effect(*resolved.target, "jammed", 1);
            }
            auto unit = std::dynamic_pointer_cast<Unit>(resolved.target);
            if (unit) {
                if (attack_gain > 0) {
                    unit->melee_damage += attack_gain;
                    unit->melee_damage_min = 0;
                    unit->melee_damage_max = 0;
                    sync_unit_damage_ranges_from_nominal(*unit);
                }
                if (health_gain > 0) {
                    unit->base_health += health_gain;
                    unit->current_health += health_gain;
                }
            }
            return ActionResult{true,
                resolved.target->entity_id + " gains " + std::to_string(jammed_amount) + " jammed, +"
                    + std::to_string(attack_gain) + " attack, +" + std::to_string(health_gain) + " HP", {}};
        }));

    // Status Relay: swap all TemporaryEntityEffects between focus caster and target unit.
    static_cast<void>(stack_manager.register_effect("swap_temporary_effects",
        [](GameState& game, const StackItem& item) {
            const auto resolved = resolve_stack_board_target(game, item, "swap_temporary_effects");
            if (!resolved.status.ok || resolved.status.message.find("(fizzled)") != std::string::npos) {
                return resolved.status;
            }
            if (!resolved.target) {
                return ActionResult{false, "swap_temporary_effects: no target", {}};
            }
            // Focus caster is the source entity.
            if (item.source_entity_id.empty()) {
                return ActionResult{false, "swap_temporary_effects: requires a focus caster (source entity)", {}};
            }
            const auto src_it = game.board.all_entities_map.find(item.source_entity_id);
            if (src_it == game.board.all_entities_map.end() || !src_it->second) {
                return ActionResult{false, "swap_temporary_effects: caster not on board", {}};
            }
            auto& src_effs = src_it->second->temporary_effects;
            auto& tgt_effs = resolved.target->temporary_effects;
            std::swap(src_effs, tgt_effs);
            return ActionResult{true,
                "Swapped temporary effects between " + item.source_entity_id
                    + " and " + resolved.target->entity_id, {}};
        }));

    // Sylvia's Special Blend: grant target unit slow_becomes_fast until owner turn end.
    static_cast<void>(stack_manager.register_effect("grant_slow_becomes_fast",
        [](GameState& game, const StackItem& item) {
            const auto resolved = resolve_stack_board_target(game, item, "grant_slow_becomes_fast");
            if (!resolved.status.ok || resolved.status.message.find("(fizzled)") != std::string::npos) {
                return resolved.status;
            }
            if (!resolved.target) {
                return ActionResult{false, "grant_slow_becomes_fast: no target", {}};
            }
            TemporaryEntityEffect eff;
            eff.effect_id = "slow_becomes_fast_marker";
            eff.source_id = item.source_entity_id;
            eff.name = "Sylvia's Special Blend";
            eff.rules_text = "This unit's slow spells and abilities are treated as fast until end of turn.";
            eff.expire_on = "owner_turn_end";
            eff.remaining_turns = 1;
            game.add_temporary_effect(resolved.target, std::move(eff));
            return ActionResult{true, resolved.target->entity_id + " gains Sylvia's Special Blend until end of turn", {}};
        }));

    // Strip for Parts: destroy target ally; gain floating orange energy = 2 × its total cost (tagged pool).
    static_cast<void>(stack_manager.register_effect("sacrifice_ally_gain_mana",
        [](GameState& game, const StackItem& item) {
            const auto resolved = resolve_stack_board_target(game, item, "sacrifice_ally_gain_mana");
            if (!resolved.status.ok || resolved.status.message.find("(fizzled)") != std::string::npos) {
                return resolved.status;
            }
            if (!resolved.target || resolved.target->entity_type != "unit") {
                return ActionResult{false, "sacrifice_ally_gain_mana: requires an allied unit target", {}};
            }
            // Look up the card definition via source_card_id to get energy cost.
            int total_cost = 0;
            if (!resolved.target->source_card_id.empty()) {
                const int owner_id = resolved.target->owner ? *resolved.target->owner : item.controller_id;
                const auto deck_it = game.players_decks.find(owner_id);
                if (deck_it != game.players_decks.end()) {
                    const auto cid = deck_it->second.find_card_by_public_id(resolved.target->source_card_id);
                    if (cid.is_valid()) {
                        const auto& inst = deck_it->second.pool.at(cid);
                        CardDefinition def;
                        if (try_get_card_definition(inst.definition_id, def)) {
                            for (const auto& [type, amt] : def.energy_cost) total_cost += amt;
                        }
                    }
                }
            }
            const int mana_gain = total_cost * 2;
            game.board.remove_entity(resolved.target);
            if (mana_gain > 0) {
                credit_tagged_float(game.turn_manager, "spell_ability", item.controller_id,
                    EnergyType::Orange, mana_gain);
            }
            return ActionResult{true,
                resolved.target->entity_id + " sacrificed; controller gains " + std::to_string(mana_gain)
                    + " flux energy (spell_ability pool)", {}};
        }));

    // The Starforged Aberration: randomly redistribute base stats among allied units in W×H area.
    static_cast<void>(stack_manager.register_effect("randomize_stats_area",
        [](GameState& game, const StackItem& item) {
            int cx = -1, cy = -1;
            if (const auto it = item.targets.find(effect_keys::kCellX); it != item.targets.end()) cx = it->second;
            if (const auto it = item.targets.find(effect_keys::kCellY); it != item.targets.end()) cy = it->second;
            if (cx < 0 || cy < 0) return ActionResult{false, "randomize_stats_area: no board cell target", {}};
            int W = 4, H = 2;
            if (const auto it = item.payload.find("width");  it != item.payload.end()) W = it->second;
            if (const auto it = item.payload.find("height"); it != item.payload.end()) H = it->second;
            std::vector<std::shared_ptr<Unit>> units;
            for (int dy = 0; dy < H; ++dy) {
                for (int dx = 0; dx < W; ++dx) {
                    const auto sq = game.board.get_square(cx + dx - W/2, cy + dy - H/2);
                    if (!sq || !sq->occupied || !sq->entity) continue;
                    auto u = std::dynamic_pointer_cast<Unit>(sq->entity);
                    if (!u || !u->owner) continue;
                    if (teams_hostile(game, item.controller_id, *u->owner)) continue;
                    units.push_back(u);
                }
            }
            if (units.empty()) return ActionResult{true, "randomize_stats_area: no allied units in area", {}};
            int total_hp = 0, total_atk = 0;
            for (const auto& u : units) {
                total_hp  += u->base_health;
                total_atk += u->melee_damage;
            }
            const int n = static_cast<int>(units.size());
            if (total_hp < n || total_atk < n) {
                return ActionResult{false,
                    "randomize_stats_area: pooled stats too low to give each unit at least 1 attack and 1 HP",
                    {}};
            }
            auto random_partition_at_least_one = [&](int total) {
                std::vector<int> parts(static_cast<size_t>(n), 1);
                int remaining = total - n;
                std::uniform_int_distribution<int> dist(0, n - 1);
                for (int i = 0; i < remaining; ++i) {
                    parts[static_cast<size_t>(dist(game.rng()))]++;
                }
                return parts;
            };
            const auto hp_parts = random_partition_at_least_one(total_hp);
            const auto atk_parts = random_partition_at_least_one(total_atk);
            for (int i = 0; i < n; ++i) {
                auto& u = units[static_cast<size_t>(i)];
                const int new_hp = hp_parts[static_cast<size_t>(i)];
                const int new_atk = atk_parts[static_cast<size_t>(i)];
                const int hp_delta = new_hp - u->base_health;
                u->base_health = new_hp;
                u->current_health = std::max(1, u->current_health + hp_delta);
                u->melee_damage = new_atk;
                u->melee_damage_min = 0;
                u->melee_damage_max = 0;
                sync_unit_damage_ranges_from_nominal(*u);
            }
            return ActionResult{true,
                "Randomly redistributed stats among " + std::to_string(n) + " allied units ("
                    + std::to_string(total_atk) + " attack / " + std::to_string(total_hp) + " HP total)",
                {}};
        }));

    // Overly Affectionate Cyberware: grant Hyperactive Scanning passive ability.
    static_cast<void>(stack_manager.register_effect("grant_keyword_mirror_passive",
        [](GameState& game, const StackItem& item) {
            const auto resolved = resolve_stack_board_target(game, item, "grant_keyword_mirror_passive");
            if (!resolved.status.ok || resolved.status.message.find("(fizzled)") != std::string::npos) {
                return resolved.status;
            }
            if (!resolved.target) {
                return ActionResult{false, "grant_keyword_mirror_passive: no target", {}};
            }
            int max_deploy_cost = INT_MAX;
            if (const auto mit = item.payload.find("max_deploy_cost"); mit != item.payload.end()) {
                max_deploy_cost = mit->second;
            }
            if (!entity_satisfies_max_deploy_cost(*resolved.target, max_deploy_cost)) {
                return ActionResult{false,
                    "grant_keyword_mirror_passive: target must be a unit with total energy cost "
                        + std::to_string(max_deploy_cost) + " or less",
                    {}};
            }
            // Avoid duplicate grant.
            for (const auto& p : resolved.target->passive_abilities) {
                if (p.key == "hyperactive_scanning") {
                    return ActionResult{true, resolved.target->entity_id + " already has Hyperactive Scanning", {}};
                }
            }
            PassiveAbilitySpec passive;
            if (!try_get_passive_from_catalog("hyperactive_scanning", passive)) {
                return ActionResult{false, "grant_keyword_mirror_passive: passive not in catalog", {}};
            }
            resolved.target->passive_abilities.push_back(std::move(passive));
            game.mark_passive_auras_dirty();
            game.refresh_passive_auras();
            return ActionResult{true,
                resolved.target->entity_id + " gains Hyperactive Scanning", {}};
        }));

    // Defective Graft: stun target ally and grant a one-shot on-damage detonation marker.
