// game_state_effects_spawn_terrain.inc — shard of register_default_effects()
    static_cast<void>(stack_manager.register_effect("gas_chain_detonate", [](GameState& game, const StackItem& item) {
        if (item.source_entity_id.empty()) {
            return ActionResult{false, "gas_chain_detonate: requires a source unit", {}};
        }
        const auto src_it = game.board.all_entities_map.find(item.source_entity_id);
        if (src_it == game.board.all_entities_map.end() || !src_it->second || src_it->second->current_health <= 0) {
            return ActionResult{true, "gas_chain_detonate: source missing or dead (fizzled)", {}};
        }
        const auto xit = item.targets.find(effect_keys::kCellX);
        const auto yit = item.targets.find(effect_keys::kCellY);
        if (xit == item.targets.end() || yit == item.targets.end()) {
            return ActionResult{false, "gas_chain_detonate: requires a target cell", {}};
        }
        const int start_x = xit->second;
        const int start_y = yit->second;
        if (!cell_has_gas_overlay(game, start_x, start_y)) {
            return ActionResult{false, "gas_chain_detonate: target tile has no gas cloud", {}};
        }
        int damage = 3;
        if (const auto dit = item.payload.find(effect_keys::kPayloadAmount); dit != item.payload.end()) {
            damage = std::max(0, dit->second);
        }
        std::shared_ptr<Entity> source = src_it->second;
        std::queue<std::pair<int, int>> pending;
        std::set<std::pair<int, int>> visited;
        pending.emplace(start_x, start_y);
        visited.emplace(start_x, start_y);
        int tiles_detonated = 0;
        int units_damaged = 0;
        static constexpr std::pair<int, int> kGasBfsOffsets[8] = {
            {-1, -1}, {0, -1}, {1, -1},
            {-1,  0},          {1,  0},
            {-1,  1}, {0,  1}, {1,  1},
        };
        while (!pending.empty()) {
            const auto [cx, cy] = pending.front();
            pending.pop();
            if (!cell_has_gas_overlay(game, cx, cy)) {
                continue;
            }
            clear_gas_overlay_at(game, cx, cy);
            ++tiles_detonated;
            if (const auto ent = game.board.entity_at(cx, cy);
                ent && ent->current_health > 0 && game.board.all_entities_map.contains(ent->entity_id)) {
                DamagePacket packet;
                packet.source = source;
                packet.target = ent;
                packet.amount = damage;
                packet.source_label = item.source_name.empty() ? item.effect_key : item.source_name;
                if (apply_damage_packet(game, packet).ok) {
                    ++units_damaged;
                }
            }
            for (const auto& [dx, dy] : kGasBfsOffsets) {
                const int nx = cx + dx;
                const int ny = cy + dy;
                const auto neighbor = std::make_pair(nx, ny);
                if (visited.count(neighbor) || !cell_has_gas_overlay(game, nx, ny)) {
                    continue;
                }
                visited.insert(neighbor);
                pending.push(neighbor);
            }
        }
        return ActionResult{true,
            "Gas detonation cleared " + std::to_string(tiles_detonated) + " tile(s), damaged "
                + std::to_string(units_damaged) + " unit(s)",
            {}};
    }));

    // place_supply_drop: place a heal pickup on any empty board cell within Chebyshev range.
    // The pickup uses "heal_self" as its effect key — the first unit to step on it is healed.
    // Payload: "amount" (heal amount carried onto the pickup, default 4).
    static_cast<void>(stack_manager.register_effect("place_supply_drop", [](GameState& game, const StackItem& item) {
        const auto xit = item.targets.find(effect_keys::kCellX);
        const auto yit = item.targets.find(effect_keys::kCellY);
        if (xit == item.targets.end() || yit == item.targets.end()) {
            return ActionResult{false, "place_supply_drop: requires a target cell", {}};
        }
        const int tx = xit->second;
        const int ty = yit->second;
        const auto sq = game.board.get_square(tx, ty);
        if (!sq) {
            return ActionResult{false, "place_supply_drop: target cell is not on the board", {}};
        }
        if (sq->occupied) {
            return ActionResult{false, "place_supply_drop: target cell is occupied", {}};
        }
        const int amount = payload_int(item, effect_keys::kPayloadAmount, 4);
        const bool placed = place_map_pickup(game, tx, ty, "heal_self", {{effect_keys::kPayloadAmount, amount}});
        if (!placed) {
            return ActionResult{false, "place_supply_drop: failed to place pickup at (" + std::to_string(tx) + "," + std::to_string(ty) + ")", {}};
        }
        return ActionResult{true,
            "Supply drop placed at (" + std::to_string(tx) + "," + std::to_string(ty) + ") — heals " + std::to_string(amount) + " on collection", {}};
    }));


    // spawn_shock_wire_adjacent: place a Shock Wire building token on an empty cell adjacent to the source.
    // Payload: amount (HP, default 2). Token has no attacks.
    static_cast<void>(stack_manager.register_effect("spawn_shock_wire_adjacent", [](GameState& game, const StackItem& item) {
        if (item.source_entity_id.empty()) {
            return ActionResult{false, "spawn_shock_wire_adjacent: no source entity", {}};
        }
        const auto src_it = game.board.all_entities_map.find(item.source_entity_id);
        if (src_it == game.board.all_entities_map.end() || !src_it->second
                || !src_it->second->owner || !src_it->second->position) {
            return ActionResult{true, "spawn_shock_wire_adjacent: source missing or unplaced (fizzled)", {}};
        }
        const auto& source = *src_it->second;
        const auto xit = item.targets.find(effect_keys::kCellX);
        const auto yit = item.targets.find(effect_keys::kCellY);
        if (xit == item.targets.end() || yit == item.targets.end()) {
            return ActionResult{false, "spawn_shock_wire_adjacent: requires a target cell", {}};
        }
        const int tx = xit->second;
        const int ty = yit->second;
        const auto sq = game.board.get_square(tx, ty);
        if (!sq) {
            return ActionResult{false, "spawn_shock_wire_adjacent: target cell is not on the board", {}};
        }
        if (sq->occupied) {
            return ActionResult{false, "spawn_shock_wire_adjacent: target cell is occupied", {}};
        }
        if (min_chebyshev_entity_to_cell(source, tx, ty) != 1) {
            return ActionResult{false, "spawn_shock_wire_adjacent: target cell must be adjacent to the source", {}};
        }
        const int hp = std::max(1, payload_int(item, effect_keys::kPayloadAmount, 2));
        auto wire = std::make_shared<Unit>();
        wire->entity_type = "building";
        wire->unit_type = "Shock Wire";
        wire->owner = source.owner;
        wire->base_health = hp;
        wire->current_health = hp;
        wire->shape = {{0, 0}};
        normalize_entity_shape(*wire);
        PassiveAbilitySpec shock_retaliation;
        if (try_get_passive_from_catalog("shock_wire_shock_retaliation", shock_retaliation)) {
            wire->passive_abilities.push_back(std::move(shock_retaliation));
        }
        game.assign_monotonic_entity_id(wire,
            "shock_wire_" + source.entity_id + "_" + std::to_string(tx) + "_" + std::to_string(ty));
        if (!game.board.place_entity(wire, tx, ty)) {
            return ActionResult{false, "spawn_shock_wire_adjacent: placement failed", {}};
        }
        game.note_entity_placed(wire);
        game.mark_passive_auras_dirty();
        return ActionResult{true, "Placed Shock Wire at (" + std::to_string(tx) + "," + std::to_string(ty) + ")", {}};
    }));

    // spawn_conscripts_adjacent: spawn N Conscript tokens on random unoccupied surrounding cells
    // of the source unit. Tokens match the standard Conscript spec (Hybrid 2–4, 4 HP, movement 3)
    // and receive deployment fatigue on spawn.
    // Payload: "count" (int, default 2).
    static_cast<void>(stack_manager.register_effect("spawn_conscripts_adjacent", [](GameState& game, const StackItem& item) {
        if (item.source_entity_id.empty()) {
            return ActionResult{false, "spawn_conscripts_adjacent: no source entity", {}};
        }
        const auto src_it = game.board.all_entities_map.find(item.source_entity_id);
        if (src_it == game.board.all_entities_map.end() || !src_it->second
                || !src_it->second->owner || !src_it->second->position) {
            return ActionResult{true, "spawn_conscripts_adjacent: source entity missing or unplaced (fizzled)", {}};
        }
        const auto& source = *src_it->second;
        const int count = std::max(1, payload_int(item, "count", 2));

        std::vector<std::pair<int, int>> candidates;
        for (const auto& [cx, cy] : entity_surrounding_cells(source)) {
            const auto sq = game.board.get_square(cx, cy);
            if (sq && !sq->occupied) {
                candidates.emplace_back(cx, cy);
            }
        }

        int spawned = 0;
        for (int i = 0; i < count && !candidates.empty(); ++i) {
            std::uniform_int_distribution<int> pick(0, static_cast<int>(candidates.size()) - 1);
            const int idx = pick(game.rng());
            const auto [cx, cy] = candidates[idx];
            candidates.erase(candidates.begin() + idx);

            auto token = std::make_shared<Unit>();
            token->entity_type  = "unit";
            token->unit_type    = "infantry";
            token->owner        = source.owner;
            token->attack_type  = AttackType::Hybrid;
            token->base_health    = 4;
            token->current_health = 4;
            token->melee_damage   = 3;  // nominal → 2–4 via sync
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
        game.mark_passive_auras_dirty();
        if (spawned == 0) {
            return ActionResult{true, "spawn_conscripts_adjacent: no free surrounding cells (fizzled)", {}};
        }
        return ActionResult{true,
            "Spawned " + std::to_string(spawned) + " Conscript(s) adjacent to " + source.entity_id, {}};
    }));

    // spawn_flame_trooper_adjacent: spawn 1 Flame Trooper token on a random unoccupied surrounding
    // cell of the source unit. Token matches the Flame Trooper card spec (Hybrid 5–8 / 2–5 range 2,
    // 9 HP, movement 2) and receives deployment fatigue on spawn.
    static_cast<void>(stack_manager.register_effect("spawn_flame_trooper_adjacent", [](GameState& game, const StackItem& item) {
        if (item.source_entity_id.empty()) {
            return ActionResult{false, "spawn_flame_trooper_adjacent: no source entity", {}};
        }
        const auto src_it = game.board.all_entities_map.find(item.source_entity_id);
        if (src_it == game.board.all_entities_map.end() || !src_it->second
                || !src_it->second->owner || !src_it->second->position) {
            return ActionResult{true, "spawn_flame_trooper_adjacent: source entity missing or unplaced (fizzled)", {}};
        }
        const auto& source = *src_it->second;

        std::vector<std::pair<int, int>> candidates;
        for (const auto& [cx, cy] : entity_surrounding_cells(source)) {
            const auto sq = game.board.get_square(cx, cy);
            if (sq && !sq->occupied) {
                candidates.emplace_back(cx, cy);
            }
        }
        if (candidates.empty()) {
            return ActionResult{true, "spawn_flame_trooper_adjacent: no free surrounding cells (fizzled)", {}};
        }

        std::uniform_int_distribution<int> pick(0, static_cast<int>(candidates.size()) - 1);
        const auto [cx, cy] = candidates[pick(game.rng())];

        auto token = std::make_shared<Unit>();
        token->entity_type   = "unit";
        token->unit_type     = "infantry";
        token->owner         = source.owner;
        token->attack_type   = AttackType::Hybrid;
        token->base_health     = 9;
        token->current_health  = 9;
        token->melee_damage    = 6;
        token->melee_damage_min = 5;
        token->melee_damage_max = 8;
        token->ranged_damage   = 3;
        token->ranged_damage_min = 2;
        token->ranged_damage_max = 5;
        token->ranged_range    = 2;
        token->ranged_deadzone = 0;
        token->movement        = 2;
        token->shape           = {{0, 0}};
        normalize_entity_shape(*token);
        game.assign_monotonic_entity_id(token, "flame_trooper_" + source.entity_id);
        if (!game.board.place_entity(token, cx, cy)) {
            return ActionResult{true, "spawn_flame_trooper_adjacent: placement failed (fizzled)", {}};
        }
        game.note_entity_placed(token);
        apply_deployment_fatigue(game, token);
        game.mark_passive_auras_dirty();
        return ActionResult{true, "Spawned Flame Trooper adjacent to " + source.entity_id, {}};
    }));

    // core_cracker_prime: pay 3 gallantry (slow) to unlock this Core Cracker for the turn cycle.
    static_cast<void>(stack_manager.register_effect("core_cracker_prime", [](GameState& game, const StackItem& item) {
        if (item.source_entity_id.empty()) {
            return ActionResult{false, "core_cracker_prime: requires a source unit", {}};
        }
        const auto src_it = game.board.all_entities_map.find(item.source_entity_id);
        if (src_it == game.board.all_entities_map.end() || !src_it->second
                || src_it->second->current_health <= 0) {
            return ActionResult{true, "core_cracker_prime: source gone (fizzled)", {}};
        }
        auto& source = *src_it->second;
        if (!entity_is_core_cracker(source)) {
            return ActionResult{false, "core_cracker_prime: source is not a Core Cracker", {}};
        }
        source.core_cracker_shutdown = false;
        return ActionResult{true, source.entity_id + " primed — may act and react until your next turn start", {}};
    }));

    // core_cracker_breach: deal 15 damage to a targeted adjacent base, then destroy the
    // Core Cracker itself. The pierce flag is set so the base's normal immunity does not block it.
    // Payload: "amount" (default 15). Board target: enemy base (validated in ability JSON).
    static_cast<void>(stack_manager.register_effect("core_cracker_breach", [](GameState& game, const StackItem& item) {
        const auto resolved = resolve_stack_board_target(game, item, "core_cracker_breach");
        if (!resolved.status.ok || resolved.status.message.find("(fizzled)") != std::string::npos) {
            return resolved.status;
        }
        const int amount = payload_int(item, effect_keys::kPayloadAmount, 15);
        std::shared_ptr<Entity> source;
        if (!item.source_entity_id.empty()) {
            if (const auto src_it = game.board.all_entities_map.find(item.source_entity_id);
                    src_it != game.board.all_entities_map.end()) {
                source = src_it->second;
            }
        }
        DamagePacket packet;
        packet.source = source;
        packet.target = resolved.target;
        packet.amount = amount;
        packet.damage_type = DamageType::Physical;
        packet.from_basic_attack = false;
        packet.pierces_damage_prevention = true; // bypass base immunity
        apply_damage_packet(game, packet);
        const std::string result_msg = "core_cracker_breach: dealt " + std::to_string(packet.damage_dealt)
            + " damage to " + resolved.target->entity_id;
        // Destroy the source (Core Cracker sacrifices itself).
        if (source && game.board.all_entities_map.contains(source->entity_id)) {
            game.destroy_board_entity(source);
        }
        return ActionResult{true, result_msg, {}};
    }));

    // bunker_buster_strike: deal flat damage to an adjacent enemy base or structure.
    // The effect is explicitly exempt from the base's normal effect immunity (see stack_targets.cpp).
    // Payload: "amount" (default 5).
    static_cast<void>(stack_manager.register_effect("bunker_buster_strike", [](GameState& game, const StackItem& item) {
        const auto resolved = resolve_stack_board_target(game, item, "bunker_buster_strike");
        if (!resolved.status.ok || resolved.status.message.find("(fizzled)") != std::string::npos) {
            return resolved.status;
        }
        const int amount = payload_int(item, effect_keys::kPayloadAmount, 5);
        std::shared_ptr<Entity> source;
        if (!item.source_entity_id.empty()) {
            if (const auto src_it = game.board.all_entities_map.find(item.source_entity_id);
                    src_it != game.board.all_entities_map.end()) {
                source = src_it->second;
            }
        }
        DamagePacket packet;
        packet.source = source;
        packet.target = resolved.target;
        packet.amount = amount;
        packet.damage_type = DamageType::Physical;
        packet.from_basic_attack = false;
        apply_damage_packet(game, packet);
        return ActionResult{true, "bunker_buster_strike: dealt " + std::to_string(packet.damage_dealt)
            + " damage to " + resolved.target->entity_id, {}};
    }));


    // healing_flight: fly orthogonally [min_range, max_range], drop heal pickups on empty intermediate tiles.
    static_cast<void>(stack_manager.register_effect("healing_flight", [](GameState& game, const StackItem& item) {
        if (item.source_entity_id.empty()) {
            return ActionResult{false, "healing_flight: no source entity", {}};
        }
        const auto src_it = game.board.all_entities_map.find(item.source_entity_id);
        if (src_it == game.board.all_entities_map.end() || !src_it->second
                || !src_it->second->position || src_it->second->current_health <= 0) {
            return ActionResult{true, "healing_flight: source missing or dead (fizzled)", {}};
        }
        auto source = src_it->second;
        const auto [sx, sy] = *source->position;
        const auto xit = item.targets.find(effect_keys::kCellX);
        const auto yit = item.targets.find(effect_keys::kCellY);
        if (xit == item.targets.end() || yit == item.targets.end()) {
            return ActionResult{false, "healing_flight: requires a target cell", {}};
        }
        const int aim_x = xit->second;
        const int aim_y = yit->second;
        if (aim_x == sx && aim_y == sy) {
            return ActionResult{false, "healing_flight: cannot target own cell", {}};
        }
        const auto [dir_x, dir_y] = snap_octilinear_direction(sx, sy, aim_x, aim_y);
        if (dir_x != 0 && dir_y != 0) {
            return ActionResult{false, "healing_flight: must aim in a cardinal (orthogonal) direction", {}};
        }
        const int max_range = payload_int(item, "max_range", 5);
        const int kMinRange = payload_int(item, "min_range", 2);
        int actual_steps = 0;
        {
            bool found = false;
            for (int step = 1; step <= max_range; ++step) {
                if (sx + dir_x * step == aim_x && sy + dir_y * step == aim_y) {
                    found = true;
                    actual_steps = step;
                    break;
                }
            }
            if (!found) {
                return ActionResult{false, "healing_flight: aim cell is not on a valid cardinal ray within range "
                    + std::to_string(max_range), {}};
            }
        }
        if (actual_steps < kMinRange) {
            return ActionResult{false, "healing_flight: destination must be at least "
                + std::to_string(kMinRange) + " tiles away", {}};
        }
        const auto landing_sq = game.board.get_square(aim_x, aim_y);
        if (!landing_sq) {
            return ActionResult{false, "healing_flight: landing cell is not a valid board cell", {}};
        }
        std::shared_ptr<Entity> landing_pickup;
        if (landing_sq->occupied) {
            if (landing_sq->entity && entity_is_pickup(*landing_sq->entity)) {
                landing_pickup = landing_sq->entity;
                game.board.remove_entity(landing_pickup);
            } else {
                return ActionResult{false, "healing_flight: landing cell is occupied", {}};
            }
        }
        const int heal_amount = payload_int(item, effect_keys::kPayloadAmount, 4);
        int drops = 0;
        for (int step = 1; step < actual_steps; ++step) {
            const int cx = sx + dir_x * step;
            const int cy = sy + dir_y * step;
            const auto sq = game.board.get_square(cx, cy);
            if (!sq || sq->occupied) {
                continue;
            }
            if (place_map_pickup(game, cx, cy, "heal_self", {{effect_keys::kPayloadAmount, heal_amount}})) {
                ++drops;
            }
        }
        game.board.remove_entity(source);
        if (!game.board.place_entity(source, aim_x, aim_y)) {
            game.board.place_entity(source, sx, sy);
            if (landing_pickup) {
                game.board.place_entity(landing_pickup, aim_x, aim_y);
            }
            return ActionResult{false, "healing_flight: failed to place source at landing cell", {}};
        }
        if (landing_pickup && !landing_pickup->pickup_effect_key.empty() && source->owner
                && game.stack_manager.has_effect_handler(landing_pickup->pickup_effect_key)) {
            StackItem pk_item;
            pk_item.source_type = "pickup";
            pk_item.source_name = landing_pickup->entity_id;
            pk_item.source_entity_id = source->entity_id;
            pk_item.controller_id = *source->owner;
            pk_item.effect_key = landing_pickup->pickup_effect_key;
            pk_item.speed = EffectSpeed::Blazing;
            pk_item.payload = landing_pickup->pickup_payload;
            game.stack_manager.add_item(game, std::move(pk_item));
        }
        game.mark_passive_auras_dirty();
        return ActionResult{true, source->entity_id + " healing flight: dropped " + std::to_string(drops)
            + " pickup(s), landed at (" + std::to_string(aim_x) + "," + std::to_string(aim_y) + ")", {}};
    }));

    // bombing_run: Skyreaver dashes along an octilinear ray, damaging up to 2 enemies in
    // intermediate cells, then moves to the chosen landing cell (must be unoccupied).
    // Uses directional aim targeting — targets["cell_x"] / ["cell_y"] is the landing cell.
    // Payload: "amount" (damage per hit, default 3), "max_range" (default 4).
    static_cast<void>(stack_manager.register_effect("bombing_run", [](GameState& game, const StackItem& item) {
        // Resolve source.
        if (item.source_entity_id.empty()) {
            return ActionResult{false, "bombing_run: no source entity", {}};
        }
        const auto src_it = game.board.all_entities_map.find(item.source_entity_id);
        if (src_it == game.board.all_entities_map.end() || !src_it->second
                || !src_it->second->position || src_it->second->current_health <= 0) {
            return ActionResult{true, "bombing_run: source missing or dead (fizzled)", {}};
        }
        auto source = src_it->second;
        const auto [sx, sy] = *source->position;

        // Resolve aim (landing) cell.
        const auto xit = item.targets.find(effect_keys::kCellX);
        const auto yit = item.targets.find(effect_keys::kCellY);
        if (xit == item.targets.end() || yit == item.targets.end()) {
            return ActionResult{false, "bombing_run: requires a target cell", {}};
        }
        const int aim_x = xit->second;
        const int aim_y = yit->second;
        if (aim_x == sx && aim_y == sy) {
            return ActionResult{false, "bombing_run: cannot target own cell", {}};
        }

        // Snap aim to octilinear direction and validate the aim cell is on a cardinal ray.
        const auto [dir_x, dir_y] = snap_octilinear_direction(sx, sy, aim_x, aim_y);
        // Orthogonal (cardinal) only — diagonals are not allowed.
        if (dir_x != 0 && dir_y != 0) {
            return ActionResult{false, "bombing_run: must aim in a cardinal (orthogonal) direction", {}};
        }
        const int max_range = payload_int(item, "max_range", 3);
        const int kMinRange  = payload_int(item, "min_range", 2);
        int actual_steps = 0;
        {
            bool found = false;
            for (int step = 1; step <= max_range; ++step) {
                if (sx + dir_x * step == aim_x && sy + dir_y * step == aim_y) {
                    found = true;
                    actual_steps = step;
                    break;
                }
            }
            if (!found) {
                return ActionResult{false, "bombing_run: aim cell is not on a valid cardinal ray within range " + std::to_string(max_range), {}};
            }
        }
        if (actual_steps < kMinRange) {
            return ActionResult{false, "bombing_run: destination must be at least " + std::to_string(kMinRange) + " tiles away", {}};
        }

        // The landing cell must be empty. Pickups are collected on landing (removed before
        // placement, effect fires after); any other occupant blocks the movement.
        const auto landing_sq = game.board.get_square(aim_x, aim_y);
        if (!landing_sq) {
            return ActionResult{false, "bombing_run: landing cell is not a valid board cell", {}};
        }
        std::shared_ptr<Entity> landing_pickup;
        if (landing_sq->occupied) {
            if (landing_sq->entity && entity_is_pickup(*landing_sq->entity)) {
                landing_pickup = landing_sq->entity;
                game.board.remove_entity(landing_pickup);
            } else {
                return ActionResult{false, "bombing_run: landing cell is occupied — must be an empty tile", {}};
            }
        }

        // Collect intermediate cells (steps 1 .. N-1, where step N is the landing cell).
        const int damage = payload_int(item, effect_keys::kPayloadAmount, 3);
        int hits = 0;
        const int kMaxHits = 2;
        for (int step = 1; step < max_range + 1; ++step) {
            const int cx = sx + dir_x * step;
            const int cy = sy + dir_y * step;
            if (cx == aim_x && cy == aim_y) {
                break;  // reached landing cell — stop collecting intermediate entities
            }
            const auto ent = game.board.entity_at(cx, cy);
            if (!ent || !ent->owner || ent->current_health <= 0) {
                continue;
            }
            if (!teams_hostile(game, item.controller_id, *ent->owner)) {
                continue;  // only damage enemies
            }
            if (hits >= kMaxHits) {
                break;
            }
            DamagePacket pkt;
            pkt.source = source;
            pkt.target = ent;
            pkt.amount = damage;
            pkt.damage_type = DamageType::Physical;
            pkt.from_basic_attack = false;
            pkt.source_label = source->entity_id + " (bombing run)";
            apply_damage_packet(game, pkt);
            ++hits;
        }

        // Move source to landing cell.
        game.board.remove_entity(source);
        if (!game.board.place_entity(source, aim_x, aim_y)) {
            // Rollback: try to restore original position. Restore any pre-removed pickup too.
            game.board.place_entity(source, sx, sy);
            if (landing_pickup) {
                game.board.place_entity(landing_pickup, aim_x, aim_y);
            }
            return ActionResult{false, "bombing_run: failed to place source at landing cell", {}};
        }
        // Fire pickup effect for any pickup collected at the landing cell.
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
        return ActionResult{true, source->entity_id + " bombing run: hit " + std::to_string(hits)
            + " unit(s), landed at (" + std::to_string(aim_x) + "," + std::to_string(aim_y) + ")", {}};
    }));

    // terra_cone_strike: deal full damage to enemies on the chosen adjacent cell, then half
    // damage to enemies on the three-cell row two steps away along the same direction.
    // Payload: amount (default 6), splash_amount (default 3), max_range (default 1).
