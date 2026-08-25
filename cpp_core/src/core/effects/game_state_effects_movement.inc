// game_state_effects_movement.inc — shard of register_default_effects()
    static_cast<void>(stack_manager.register_effect("terra_cone_strike", [](GameState& game, const StackItem& item) {
        if (item.source_entity_id.empty()) {
            return ActionResult{false, "terra_cone_strike: no source entity", {}};
        }
        const auto src_it = game.board.all_entities_map.find(item.source_entity_id);
        if (src_it == game.board.all_entities_map.end() || !src_it->second
                || !src_it->second->position || src_it->second->current_health <= 0) {
            return ActionResult{true, "terra_cone_strike: source missing or dead (fizzled)", {}};
        }
        auto source = src_it->second;
        const auto [sx, sy] = *source->position;

        const auto xit = item.targets.find(effect_keys::kCellX);
        const auto yit = item.targets.find(effect_keys::kCellY);
        if (xit == item.targets.end() || yit == item.targets.end()) {
            return ActionResult{false, "terra_cone_strike: requires a target cell", {}};
        }
        const int aim_x = xit->second;
        const int aim_y = yit->second;
        const int max_range = payload_int(item, "max_range", 1);
        const auto vr = validate_directional_area_damage_ability_target(
            game, item.controller_id, *source, item.targets, max_range, true);
        if (!vr.ok) {
            return vr;
        }
        const auto [dir_x, dir_y] = snap_octilinear_direction(sx, sy, aim_x, aim_y);
        const int primary_amount = payload_int(item, effect_keys::kPayloadAmount, 6);
        const int splash_amount = payload_int(item, "splash_amount", 3);

        int hits = 0;
        const auto damage_hostile_at = [&](const int cx, const int cy, const int amount) {
            const auto ent = game.board.entity_at(cx, cy);
            if (!ent || !ent->owner || ent->current_health <= 0) {
                return;
            }
            if (!teams_hostile(game, item.controller_id, *ent->owner)) {
                return;
            }
            DamagePacket pkt;
            pkt.source = source;
            pkt.target = ent;
            pkt.amount = amount;
            pkt.damage_type = DamageType::Physical;
            pkt.from_basic_attack = false;
            pkt.source_label = source->entity_id + " (terra cone)";
            apply_damage_packet(game, pkt);
            ++hits;
        };

        damage_hostile_at(aim_x, aim_y, primary_amount);
        for (const auto& [cx, cy] : terra_cone_splash_cells(sx, sy, dir_x, dir_y)) {
            damage_hostile_at(cx, cy, splash_amount);
        }
        return ActionResult{true, source->entity_id + " terra cone strike: hit " + std::to_string(hits)
            + " enemy(ies)", {}};
    }));

    // push_unit: forcibly displace a target entity away from the source.
    // Payload: amount (tiles, default 1). Optional dir_x/dir_y override direction for scripted effects.
    static_cast<void>(stack_manager.register_effect("push_unit", [](GameState& game, const StackItem& item) {
        const auto resolved = resolve_stack_board_target(game, item, "push_unit");
        if (!resolved.status.ok || resolved.status.message.find("(fizzled)") != std::string::npos) {
            return resolved.status;
        }
        if (!resolved.target || !resolved.target->position) {
            return ActionResult{false, "push_unit: requires a board entity target", {}};
        }
        if (const auto immune = stack_fizzle_if_immune(*resolved.target, item.effect_key)) {
            return *immune;
        }
        if (entity_is_immovable(*resolved.target)) {
            return ActionResult{true, resolved.target->entity_id + " is Immovable — push fizzled", {}};
        }

        int dir_x = 0;
        int dir_y = 0;
        const auto dir_x_it = item.payload.find("dir_x");
        const auto dir_y_it = item.payload.find("dir_y");
        if (dir_x_it != item.payload.end() && dir_y_it != item.payload.end()) {
            dir_x = dir_x_it->second;
            dir_y = dir_y_it->second;
        } else if (!item.source_entity_id.empty()) {
            const auto src_it = game.board.all_entities_map.find(item.source_entity_id);
            if (src_it == game.board.all_entities_map.end() || !src_it->second) {
                return ActionResult{true, "push_unit: source missing (fizzled)", {}};
            }
            const auto dir = push_direction_away_from_source(*src_it->second, *resolved.target);
            dir_x = dir.first;
            dir_y = dir.second;
        } else if (effect_key_uses_push_direction_aim(item.effect_key)) {
            const auto axit = item.targets.find(effect_keys::kAimX);
            const auto ayit = item.targets.find(effect_keys::kAimY);
            if (axit == item.targets.end() || ayit == item.targets.end()) {
                return ActionResult{false, "push_unit: requires push direction (fizzled)", {}};
            }
            const auto dir = push_direction_from_aim_cell(*resolved.target, axit->second, ayit->second);
            dir_x = dir.first;
            dir_y = dir.second;
        } else {
            const auto dir = push_direction_away_from_controller_base(game, item.controller_id, *resolved.target);
            dir_x = dir.first;
            dir_y = dir.second;
        }
        if (dir_x == 0 && dir_y == 0) {
            return ActionResult{true, "push_unit: zero push direction (fizzled)", {}};
        }

        const int distance = payload_int(item, effect_keys::kPayloadAmount, 1);
        PushDisplacementOptions opts;
        opts.collision_source_label = item.source_name.empty() ? "push_unit" : item.source_name;
        const auto push_result = resolve_push_displacement(game, resolved.target, dir_x, dir_y, distance, opts);
        if (push_result.tiles_moved > 0 && game.board.all_entities_map.contains(resolved.target->entity_id)) {
            game.cancel_stale_queued_batch_items_after_entity_move(resolved.target->entity_id);
            game.process_bleed_after_movement(resolved.target);
            game.refresh_passive_auras();
        }
        if (push_result.collision && push_result.collision_blocker
                && game.board.all_entities_map.contains(push_result.collision_blocker->entity_id)) {
            game.refresh_passive_auras();
        }
        return ActionResult{push_result.ok, push_result.message, {}};
    }));

    // terra_dash: move to an empty (or pickup) cell exactly `exact_range` tiles away on an
    // octilinear ray. Payload: exact_range (default 3).
    static_cast<void>(stack_manager.register_effect("terra_dash", [](GameState& game, const StackItem& item) {
        if (item.source_entity_id.empty()) {
            return ActionResult{false, "terra_dash: no source entity", {}};
        }
        const auto src_it = game.board.all_entities_map.find(item.source_entity_id);
        if (src_it == game.board.all_entities_map.end() || !src_it->second
                || !src_it->second->position || src_it->second->current_health <= 0) {
            return ActionResult{true, "terra_dash: source missing or dead (fizzled)", {}};
        }
        auto source = src_it->second;
        const auto [sx, sy] = *source->position;

        const auto xit = item.targets.find(effect_keys::kCellX);
        const auto yit = item.targets.find(effect_keys::kCellY);
        if (xit == item.targets.end() || yit == item.targets.end()) {
            return ActionResult{false, "terra_dash: requires a target cell", {}};
        }
        const int aim_x = xit->second;
        const int aim_y = yit->second;
        if (aim_x == sx && aim_y == sy) {
            return ActionResult{false, "terra_dash: cannot target own cell", {}};
        }

        const int exact_range = payload_int(item, "exact_range", 3);
        const auto vr = validate_directional_area_damage_ability_target(
            game, item.controller_id, *source, item.targets, exact_range, true);
        if (!vr.ok) {
            return vr;
        }
        const int dist = std::max(std::abs(aim_x - sx), std::abs(aim_y - sy));
        if (dist != exact_range) {
            return ActionResult{false, "terra_dash: destination must be exactly "
                + std::to_string(exact_range) + " tiles away", {}};
        }

        const auto landing_sq = game.board.get_square(aim_x, aim_y);
        if (!landing_sq) {
            return ActionResult{false, "terra_dash: landing cell is not a valid board cell", {}};
        }
        std::shared_ptr<Entity> landing_pickup;
        if (landing_sq->occupied) {
            if (landing_sq->entity && entity_is_pickup(*landing_sq->entity)) {
                landing_pickup = landing_sq->entity;
                game.board.remove_entity(landing_pickup);
            } else {
                return ActionResult{false, "terra_dash: landing cell is occupied — must be empty or a pickup", {}};
            }
        }

        game.board.remove_entity(source);
        if (!game.board.place_entity(source, aim_x, aim_y)) {
            game.board.place_entity(source, sx, sy);
            if (landing_pickup) {
                game.board.place_entity(landing_pickup, aim_x, aim_y);
            }
            return ActionResult{false, "terra_dash: failed to place source at landing cell", {}};
        }
        if (landing_pickup && !landing_pickup->pickup_effect_key.empty() && source->owner
                && game.stack_manager.has_effect_handler(landing_pickup->pickup_effect_key)) {
            StackItem pk_item;
            pk_item.source_type      = "pickup";
            pk_item.source_name      = landing_pickup->entity_id;
            pk_item.source_entity_id = source->entity_id;
            pk_item.controller_id    = *source->owner;
            pk_item.effect_key       = landing_pickup->pickup_effect_key;
            pk_item.speed            = EffectSpeed::Blazing;
            pk_item.payload          = landing_pickup->pickup_payload;
            game.stack_manager.add_item(game, std::move(pk_item));
        }
        game.mark_passive_auras_dirty();
        return ActionResult{true, source->entity_id + " terra dash: landed at ("
            + std::to_string(aim_x) + "," + std::to_string(aim_y) + ")", {}};
    }));

    // second_wave: allow all stockpile cards in the controller's deck to be played twice this turn.
    static_cast<void>(stack_manager.register_effect("second_wave", [](GameState& game, const StackItem& item) {
        auto it = game.players_decks.find(item.controller_id);
        if (it == game.players_decks.end()) {
            return ActionResult{false, "second_wave: unknown controller player", {}};
        }
        it->second.stockpile_double_play_active = true;
        return ActionResult{true,
            "Second Wave: player " + std::to_string(item.controller_id)
            + " may play each stockpile card twice this turn", {}};
    }));

    // mobilize: grant the controller a per-unit neutral deploy discount for the rest of this turn.
    // DeployAction::get_cost subtracts from neutral only; colored-only units are unaffected.
    // Payload: amount (default 1).
    static_cast<void>(stack_manager.register_effect("mobilize", [](GameState& game, const StackItem& item) {
        const int amount = std::max(0, payload_int(item, effect_keys::kPayloadAmount, 1));
        game.turn_manager.deploy_discount_per_unit[item.controller_id] += amount;
        return ActionResult{true,
            "Mobilize: player " + std::to_string(item.controller_id)
            + " deploys cost " + std::to_string(amount) + " less neutral this turn", {}};
    }));

    // summon_on_deployment_zone: spawn hybrid Conscript tokens on every unoccupied cell of the
    // controller's deploy zone, up to max_count. Each token has 2–4 melee/ranged damage, 4 HP,
    // movement 3, and receives deployment fatigue on spawn.
    // Payload: "max_count" (int, default 8).
    static_cast<void>(stack_manager.register_effect("summon_on_deployment_zone", [](GameState& game, const StackItem& item) {
        const std::optional<BoardRectZone> zone = deploy_zone_for_player(game.board_layout(), item.controller_id);
        if (!zone) {
            return ActionResult{true, "summon_on_deployment_zone: no deploy zone for this player (fizzled)", {}};
        }
        const int max_count = std::max(1, payload_int(item, "max_count", 8));
        int spawned = 0;
        for (int dy = 0; dy < zone->height && spawned < max_count; ++dy) {
            for (int dx = 0; dx < zone->width && spawned < max_count; ++dx) {
                const int cx = zone->anchor_x + dx;
                const int cy = zone->anchor_y + dy;
                const auto sq = game.board.get_square(cx, cy);
                if (!sq || sq->occupied) {
                    continue;
                }
                auto token = std::make_shared<Unit>();
                token->entity_type = "unit";
                token->unit_type   = "infantry";
                token->owner       = item.controller_id;
                token->attack_type = AttackType::Hybrid;
                token->base_health    = 4;
                token->current_health = 4;
                token->melee_damage   = 3;   // nominal; sync_unit_damage_ranges_from_nominal → 2–4
                token->ranged_damage  = 3;
                token->ranged_range   = 2;
                token->ranged_deadzone = 0;
                token->movement       = 3;
                token->shape          = {{0, 0}};
                normalize_entity_shape(*token);
                sync_unit_damage_ranges_from_nominal(*token);
                game.assign_monotonic_entity_id(token,
                    "conscript_" + std::to_string(item.controller_id) + "_" + std::to_string(cx) + "_"
                    + std::to_string(cy));
                if (!game.board.place_entity(token, cx, cy)) {
                    continue;
                }
                game.note_entity_placed(token);
                apply_deployment_fatigue(game, token);
                ++spawned;
            }
        }
        game.mark_passive_auras_dirty();
        if (spawned == 0) {
            return ActionResult{true, "summon_on_deployment_zone: no free cells in deploy zone (fizzled)", {}};
        }
        return ActionResult{true,
            "Summoned " + std::to_string(spawned) + " Conscript(s) in the deploy zone", {}};
    }));

    // Spawn one Requisition Trooper (4 HP, 2–4 melee, 3 move) in the first free deploy-zone
    // cell. Untargeted — used by the Field Requisition modal card's "spawn" mode.
    static_cast<void>(stack_manager.register_effect("spawn_card_unit_deploy_zone", [](GameState& game, const StackItem& item) {
        const auto key_it = item.string_payload.find("card_key");
        if (key_it == item.string_payload.end() || key_it->second.empty()) {
            return ActionResult{false, "spawn_card_unit_deploy_zone: requires card_key", {}};
        }
        CardDefinition def;
        if (!try_get_card_definition(key_it->second, def) || !definition_is_unit(def)) {
            return ActionResult{true, "spawn_card_unit_deploy_zone: unknown unit (fizzled)", {}};
        }
        const std::optional<BoardRectZone> zone = deploy_zone_for_player(game.board_layout(), item.controller_id);
        if (!zone) {
            return ActionResult{true, "spawn_card_unit_deploy_zone: no deploy zone (fizzled)", {}};
        }
        CardInstance scratch;
        scratch.definition_id = try_card_def_id_for_key(key_it->second);
        for (int dy = 0; dy < zone->height; ++dy) {
            for (int dx = 0; dx < zone->width; ++dx) {
                const int cx = zone->anchor_x + dx;
                const int cy = zone->anchor_y + dy;
                const auto sq = game.board.get_square(cx, cy);
                if (!sq || sq->occupied) {
                    continue;
                }
                auto unit = create_unit_from_definition(def, scratch, item.controller_id, "_pending");
                if (!unit) {
                    continue;
                }
                game.assign_monotonic_entity_id(unit, def.name);
                if (!game.board.place_entity(unit, cx, cy)) {
                    continue;
                }
                game.note_entity_placed(unit);
                apply_deployment_fatigue(game, unit);
                game.mark_passive_auras_dirty();
                return ActionResult{true, "Spawned " + def.name + " in the deploy zone", {}};
            }
        }
        return ActionResult{true, "spawn_card_unit_deploy_zone: no free deploy cell (fizzled)", {}};
    }));

    static_cast<void>(stack_manager.register_effect("spawn_requisition_trooper", [](GameState& game, const StackItem& item) {
        const std::optional<BoardRectZone> zone = deploy_zone_for_player(game.board_layout(), item.controller_id);
        if (!zone) {
            return ActionResult{true, "spawn_requisition_trooper: no deploy zone (fizzled)", {}};
        }
        for (int dy = 0; dy < zone->height; ++dy) {
            for (int dx = 0; dx < zone->width; ++dx) {
                const int cx = zone->anchor_x + dx;
                const int cy = zone->anchor_y + dy;
                const auto sq = game.board.get_square(cx, cy);
                if (!sq || sq->occupied) {
                    continue;
                }
                auto token = std::make_shared<Unit>();
                token->entity_type = "unit";
                token->unit_type   = "Requisition Trooper";
                token->owner       = item.controller_id;
                token->attack_type = AttackType::Melee;
                token->base_health    = 4;
                token->current_health = 4;
                token->melee_damage   = 3;  // nominal; sync_unit_damage_ranges_from_nominal → 2–4
                token->movement       = 3;
                token->shape          = {{0, 0}};
                normalize_entity_shape(*token);
                sync_unit_damage_ranges_from_nominal(*token);
                game.assign_monotonic_entity_id(token,
                    "requisition_trooper_" + std::to_string(item.controller_id) + "_" + std::to_string(cx) + "_"
                    + std::to_string(cy));
                if (!game.board.place_entity(token, cx, cy)) {
                    continue;
                }
                game.note_entity_placed(token);
                apply_deployment_fatigue(game, token);
                game.mark_passive_auras_dirty();
                return ActionResult{true, "Spawned a Requisition Trooper in the deploy zone", {}};
            }
        }
        return ActionResult{true, "spawn_requisition_trooper: no free deploy cell (fizzled)", {}};
    }));


    static_cast<void>(stack_manager.register_effect("spawn_deck_units_deploy_zone",
        [](GameState& game, const StackItem& item) {
            return execute_spawn_deck_units_deploy_zone(game, item);
        }));


    static_cast<void>(stack_manager.register_effect("missile_storm", [](GameState& game, const StackItem& item) {
        std::shared_ptr<Entity> source;
        if (!item.source_entity_id.empty()) {
            const auto src_it = game.board.all_entities_map.find(item.source_entity_id);
            if (src_it != game.board.all_entities_map.end()) {
                source = src_it->second;
            }
        }
        if (!source || !source->position) {
            return ActionResult{false, "missile_storm: source entity missing or unplaced", {}};
        }
        int amount = 0;
        if (const auto it = item.payload.find(effect_keys::kPayloadAmount); it != item.payload.end()) {
            amount = it->second;
        }
        int max_targets = 4;
        if (const auto it = item.payload.find("max_targets"); it != item.payload.end()) {
            max_targets = std::max(1, it->second);
        }
        int max_range = 4;
        if (const auto it = item.payload.find("max_range"); it != item.payload.end()) {
            max_range = std::max(1, it->second);
        }
        // Pre-consume next_damage_bonus ONCE before the candidates loop so the full bonus applies
        // to every missile target — not just the first one apply_damage_packet happens to hit.
        amount += consume_first_entity_effect(*source, "next_damage_bonus");

        // Collect living enemy units/structures within range, ordered by proximity.
        struct Candidate { int dist; std::shared_ptr<Entity> entity; };
        std::vector<Candidate> candidates;
        game.board.for_each_entity([&](const std::shared_ptr<Entity>& e) {
            if (!e || !e->owner || !source->owner) return;
            if (!teams_hostile(game, *e->owner, *source->owner)) return;
            if (e->current_health <= 0) return;
            if (!e->position) return;
            const int dist = min_chebyshev_entity_to_cell(*source, e->position->first, e->position->second);
            if (dist <= max_range) {
                candidates.push_back({dist, e});
            }
        });
        std::sort(candidates.begin(), candidates.end(),
            [](const Candidate& a, const Candidate& b) { return a.dist < b.dist; });

        if (candidates.empty()) {
            return ActionResult{true, "missile_storm: no targets in range", {}};
        }

        const int n = std::min(static_cast<int>(candidates.size()), max_targets);
        int hits = 0;
        for (int i = 0; i < n; ++i) {
            auto& tgt = candidates[i].entity;
            if (!tgt || tgt->current_health <= 0
                || !game.board.all_entities_map.contains(tgt->entity_id)) {
                continue;
            }
            DamagePacket packet;
            packet.source                    = source;
            packet.target                    = tgt;
            packet.amount                    = amount;
            packet.damage_type               = damage_type_from_stack_payload(item.payload);
            packet.pierces_damage_prevention = item.pierces_damage_prevention;
            packet.ability_grants_lifesteal  = item.heals_on_damage_dealt;
            packet.defer_reactive            = true;
            packet.source_label = item.source_name.empty() ? "Missile Storm" : item.source_name;
            apply_damage_packet(game, packet);
            ++hits;
        }
        return ActionResult{true, "missile_storm: hit " + std::to_string(hits) + " target(s)", {}};
    }));

    // apply_coordinated_fire: activate coordinated fire mode on the source unit.
    // Stores `amount` shots (default 5) in coordinated_fire_shots_remaining on the unit.
    // Each time any OTHER friendly unit attacks an enemy, this unit fires a free ranged shot
    // at the same target (inline in resolve_attack — no extra spell window, CAN counterattack).
    // Shots expire at the start of the owner's next turn.
    // Requires: consumes_attack_action = true on the ability.
    static_cast<void>(stack_manager.register_effect("apply_coordinated_fire", [](GameState& game, const StackItem& item) {
        if (item.source_entity_id.empty()) {
            return ActionResult{false, "apply_coordinated_fire: requires a source unit", {}};
        }
        const auto sit = game.board.all_entities_map.find(item.source_entity_id);
        if (sit == game.board.all_entities_map.end() || !sit->second
                || sit->second->current_health <= 0) {
            return ActionResult{true, "apply_coordinated_fire: source gone (fizzled)", {}};
        }
        auto unit = std::dynamic_pointer_cast<Unit>(sit->second);
        if (!unit) {
            return ActionResult{false, "apply_coordinated_fire: source is not a unit", {}};
        }
        const int max_shots = payload_int(item, "amount", 5);
        unit->coordinated_fire_shots_remaining = max_shots;
        // Optional per-shot damage override: "damage_min" / "damage_max" in the payload.
        // When set, coordinated fire shots use this range instead of the unit's normal ranged stats.
        const int cf_dmg_min = payload_int(item, "damage_min", 0);
        const int cf_dmg_max = payload_int(item, "damage_max", 0);
        if (cf_dmg_min > 0 && cf_dmg_max >= cf_dmg_min) {
            unit->coordinated_fire_damage_min = cf_dmg_min;
            unit->coordinated_fire_damage_max = cf_dmg_max;
        }
        return ActionResult{true,
            unit->entity_id + " enters coordinated fire stance (" + std::to_string(max_shots) + " shots ready)", {}};
    }));

    // grant_relentless_aura: grant surrounding allied units a temporary Relentless X keyword
    // until end of the current owner's turn (expire_on = "owner_turn_end").
    // No board target — iterates entity_surrounding_cells(source).
    // Payload: "amount" (relentless value, default 3).
    static_cast<void>(stack_manager.register_effect("grant_relentless_aura", [](GameState& game, const StackItem& item) {
        if (item.source_entity_id.empty()) {
            return ActionResult{false, "grant_relentless_aura: no source entity", {}};
        }
        const auto src_it = game.board.all_entities_map.find(item.source_entity_id);
        if (src_it == game.board.all_entities_map.end() || !src_it->second
                || src_it->second->current_health <= 0 || !src_it->second->owner) {
            return ActionResult{true, "grant_relentless_aura: source missing (fizzled)", {}};
        }
        auto& source = *src_it->second;
        const int rel_amount = payload_int(item, effect_keys::kPayloadAmount, 3);
        int count = 0;
        for (const auto& [cx, cy] : entity_surrounding_cells(source)) {
            auto ally = game.board.entity_at(cx, cy);
            if (!ally || ally->current_health <= 0) continue;
            if (!ally->owner || teams_hostile(game, *source.owner, *ally->owner)) continue;
            if (!std::dynamic_pointer_cast<Unit>(ally)) continue;
            TemporaryEntityEffect eff;
            eff.effect_id        = "relentless_aura_grant";
            eff.source_id        = item.source_entity_id;
            eff.name             = "Relentless";
            eff.rules_text       = "Relentless " + std::to_string(rel_amount) + " until end of turn.";
            eff.expire_on        = "owner_turn_end";
            eff.remaining_turns  = 1;
            eff.granted_attributes.push_back(PassiveAttributeGrant{"relentless", rel_amount});
            ally->temporary_effects.push_back(std::move(eff));
            ++count;
        }
        if (count == 0) {
            return ActionResult{true, "grant_relentless_aura: no surrounding allies (fizzled)", {}};
        }
        return ActionResult{true,
            "Granted Relentless " + std::to_string(rel_amount) + " to "
            + std::to_string(count) + " surrounding ally/allies", {}};
    }));

    // grant_movement_aura: grant +N bonus movement to all surrounding allied units until end of
    // the current owner's turn (expire_on = "owner_turn_end"). Used by War Time Belles march passive.
    // No board target — iterates entity_surrounding_cells(source). Payload: "amount" (default 1).
    static_cast<void>(stack_manager.register_effect("grant_movement_aura", [](GameState& game, const StackItem& item) {
        if (item.source_entity_id.empty()) {
            return ActionResult{false, "grant_movement_aura: no source entity", {}};
        }
        const auto src_it = game.board.all_entities_map.find(item.source_entity_id);
        if (src_it == game.board.all_entities_map.end() || !src_it->second
                || src_it->second->current_health <= 0 || !src_it->second->owner) {
            return ActionResult{true, "grant_movement_aura: source missing (fizzled)", {}};
        }
        auto& source = *src_it->second;
        const int amount = payload_int(item, effect_keys::kPayloadAmount, 1);
        int count = 0;
        for (const auto& [cx, cy] : entity_surrounding_cells(source)) {
            auto ally = game.board.entity_at(cx, cy);
            if (!ally || ally->current_health <= 0) continue;
            if (!ally->owner || teams_hostile(game, *source.owner, *ally->owner)) continue;
            if (!std::dynamic_pointer_cast<Unit>(ally)) continue;
            TemporaryEntityEffect eff;
            eff.effect_id       = "movement_aura_grant";
            eff.source_id       = item.source_entity_id;
            eff.name            = "March Aura";
            eff.rules_text      = "+" + std::to_string(amount) + " movement this turn (March Aura).";
            eff.expire_on       = "owner_turn_end";
            eff.remaining_turns = 1;
            eff.stat_grants.bonus_movement = amount;
            game.add_temporary_effect(ally, std::move(eff));
            ++count;
        }
        if (count == 0) {
            return ActionResult{true, "grant_movement_aura: no surrounding allies (fizzled)", {}};
        }
        return ActionResult{true,
            "March Aura: granted +" + std::to_string(amount) + " movement to "
            + std::to_string(count) + " surrounding ally/allies", {}};
    }));

    // grant_damage_aura: grant +N bonus melee and ranged damage to all surrounding allied units
    // until end of the current owner's turn (expire_on = "owner_turn_end"). No board target.
    // Payload: "amount" (default 1).
    static_cast<void>(stack_manager.register_effect("grant_damage_aura", [](GameState& game, const StackItem& item) {
        if (item.source_entity_id.empty()) {
            return ActionResult{false, "grant_damage_aura: no source entity", {}};
        }
        const auto src_it = game.board.all_entities_map.find(item.source_entity_id);
        if (src_it == game.board.all_entities_map.end() || !src_it->second
                || src_it->second->current_health <= 0 || !src_it->second->owner) {
            return ActionResult{true, "grant_damage_aura: source missing (fizzled)", {}};
        }
        auto& source = *src_it->second;
        const int amount = payload_int(item, effect_keys::kPayloadAmount, 1);
        int count = 0;
        for (const auto& [cx, cy] : entity_surrounding_cells(source)) {
            auto ally = game.board.entity_at(cx, cy);
            if (!ally || ally->current_health <= 0) continue;
            if (!ally->owner || teams_hostile(game, *source.owner, *ally->owner)) continue;
            if (!std::dynamic_pointer_cast<Unit>(ally)) continue;
            TemporaryEntityEffect eff;
            eff.effect_id             = "damage_aura_grant";
            eff.source_id             = item.source_entity_id;
            eff.name                  = "Battle Hymn";
            eff.rules_text            = "+" + std::to_string(amount) + " damage this turn (Battle Hymn).";
            eff.expire_on             = "owner_turn_end";
            eff.remaining_turns       = 1;
            eff.stat_grants.bonus_melee_damage  = amount;
            eff.stat_grants.bonus_ranged_damage = amount;
            game.add_temporary_effect(ally, std::move(eff));
            ++count;
        }
        if (count == 0) {
            return ActionResult{true, "grant_damage_aura: no surrounding allies (fizzled)", {}};
        }
        return ActionResult{true,
            "Battle Hymn: granted +" + std::to_string(amount) + " damage to "
            + std::to_string(count) + " surrounding ally/allies", {}};
    }));

    // grant_multistrike_ally: grant Multistrike X to a targeted allied unit until end of the
    // current owner's turn (expire_on = "owner_turn_end"). Board-targeted (allied unit).
    // Payload: "amount" (multistrike value, default 1).
    static_cast<void>(stack_manager.register_effect("grant_multistrike_ally", [](GameState& game, const StackItem& item) {
        const auto resolved = resolve_stack_board_target(game, item, "grant_multistrike_ally");
        if (!resolved.status.ok || resolved.status.message.find("(fizzled)") != std::string::npos) {
            return resolved.status;
        }
        if (!resolved.target || resolved.target->entity_type != "unit") {
            return ActionResult{false, "grant_multistrike_ally: target must be a unit", {}};
        }
        if (!resolved.target->owner || !item.controller_id) {
            return ActionResult{false, "grant_multistrike_ally: owner missing", {}};
        }
        if (teams_hostile(game, *resolved.target->owner, item.controller_id)) {
            return ActionResult{false, "grant_multistrike_ally: cannot target an enemy unit", {}};
        }
        const int amount = payload_int(item, effect_keys::kPayloadAmount, 1);
        TemporaryEntityEffect eff;
        eff.effect_id       = "multistrike_ally_grant";
        eff.source_id       = item.source_entity_id;
        eff.name            = "Multistrike";
        eff.rules_text      = "Multistrike " + std::to_string(amount) + " this turn (Esprit Volley).";
        eff.expire_on       = "owner_turn_end";
        eff.remaining_turns = 1;
        eff.granted_attributes.push_back(PassiveAttributeGrant{"multistrike", amount});
        game.add_temporary_effect(resolved.target, std::move(eff));
        return ActionResult{true,
            "Granted Multistrike " + std::to_string(amount) + " to " + resolved.target->entity_id, {}};
    }));

    // expand_death_shield_range_self: permanently expand the Sentinel Veil aura range by applying
    // a never-expiring TemporaryEntityEffect with bonus_aura_range = amount.
    // refresh_passive_auras already adds bonus_aura_range from temp effects to all aura passives.
    static_cast<void>(stack_manager.register_effect("expand_death_shield_range_self", [](GameState& game, const StackItem& item) {
        if (item.source_entity_id.empty()) {
            return ActionResult{false, "expand_death_shield_range_self: no source entity", {}};
        }
        const auto src_it = game.board.all_entities_map.find(item.source_entity_id);
        if (src_it == game.board.all_entities_map.end() || !src_it->second) {
            return ActionResult{true, "expand_death_shield_range_self: source missing (fizzled)", {}};
        }
        const int amount = [&]() -> int {
            const auto it = item.payload.find("amount");
            return (it != item.payload.end()) ? it->second : 1;
        }();
        TemporaryEntityEffect eff;
        eff.effect_id     = "sentinel_veil_range_" + item.source_entity_id;
        eff.source_id     = item.source_entity_id;
        eff.name          = "Sentinel Veil Range";
        eff.rules_text    = "Sentinel Veil protection range permanently expanded.";
        eff.expire_on     = "never";
        eff.remaining_turns = 1;
        eff.stat_grants.bonus_aura_range = amount;
        game.add_temporary_effect(src_it->second, std::move(eff));
        game.mark_passive_auras_dirty();
        return ActionResult{true,
            src_it->second->entity_id + " Sentinel Veil range expanded by " + std::to_string(amount), {}};
    }));

    // Helper: parse a comma-separated string payload field into a vector of tokens.
    auto parse_csv = [](const std::string& s) {
        std::vector<std::string> out;
        std::istringstream ss(s);
        std::string tok;
        while (std::getline(ss, tok, ',')) {
            if (!tok.empty()) out.push_back(std::move(tok));
        }
        return out;
    };
    // Helper: parse "key:amount" or "key" grant strings into PassiveAttributeGrant list.
    auto parse_keyword_grants = [](const std::string& s) {
        std::vector<PassiveAttributeGrant> out;
        std::istringstream ss(s);
        std::string tok;
        while (std::getline(ss, tok, ',')) {
            if (tok.empty()) continue;
            const auto colon = tok.find(':');
            PassiveAttributeGrant g;
            if (colon != std::string::npos) {
                g.key = tok.substr(0, colon);
                try { g.amount = std::stoi(tok.substr(colon + 1)); } catch (...) {}
            } else {
                g.key = tok;
            }
            if (!g.key.empty() && !attribute_is_non_copyable(g.key)) out.push_back(std::move(g));
        }
        return out;
    };

    // hookshot_pull: deal damage to a target unit and pull it toward the caster along a straight line
    // (orthogonal or diagonal only). The pull continues until the target is adjacent to the caster or blocked.
    // Payload: amount (damage, default 3), max_range (default 4).
