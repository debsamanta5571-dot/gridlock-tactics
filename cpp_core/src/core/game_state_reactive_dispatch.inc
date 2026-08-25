// game_state_reactive_dispatch.inc — reactive passive effect_key dispatch table.
// Included inside the anonymous namespace of game_state.cpp (after helper definitions).

void reactive_apply_permanent_stat_growth_from_payload(Unit& unit, const PassiveAbilitySpec& passive)
{
    int health_gain = 1;
    int attack_gain = 1;
    if (const auto it = passive.reactive_effect_payload.find("health"); it != passive.reactive_effect_payload.end()) {
        health_gain = it->second;
    }
    if (const auto it = passive.reactive_effect_payload.find("attack"); it != passive.reactive_effect_payload.end()) {
        attack_gain = it->second;
    }
    if (const auto it = passive.reactive_effect_payload.find(effect_keys::kPayloadAmount);
        it != passive.reactive_effect_payload.end()) {
        if (!passive.reactive_effect_payload.contains("health")
            && !passive.reactive_effect_payload.contains("attack")) {
            health_gain = attack_gain = it->second;
        }
    }
    apply_permanent_stat_growth_to_unit(unit, std::max(0, health_gain), std::max(0, attack_gain));
}

bool reactive_draw_cards_owner_with_cap(ReactiveDispatchCtx& ctx, const PassiveAbilitySpec& passive)
{
    if (!ctx.game || !ctx.mutable_actor) {
        return false;
    }
    int amount = 1;
    if (const auto it = passive.reactive_effect_payload.find(effect_keys::kPayloadAmount);
        it != passive.reactive_effect_payload.end()) {
        amount = std::max(0, it->second);
    }
    if (amount <= 0) {
        return false;
    }
    const int owner = ctx.killer_owner ? *ctx.killer_owner : (ctx.player_id ? *ctx.player_id : 0);
    const auto cap_it = passive.reactive_effect_payload.find("turn_cap");
    if (cap_it != passive.reactive_effect_payload.end() && cap_it->second > 0) {
        const int cap = cap_it->second;
        const std::string tally_key = "reactive_tally_" + passive.key;
        const int tally = entity_effect_amount(*ctx.mutable_actor, tally_key);
        if (tally >= cap) {
            return false;
        }
        const int granted = std::min(amount, cap - tally);
        if (granted > 0 && owner > 0) {
            (void)ctx.game->draw_cards(owner, granted);
            add_entity_effect(*ctx.mutable_actor, tally_key, granted);
            return true;
        }
        return false;
    }
    if (owner > 0) {
        (void)ctx.game->draw_cards(owner, amount);
        return true;
    }
    return false;
}

bool reactive_apply_energy_with_turn_cap(ReactiveDispatchCtx& ctx, const PassiveAbilitySpec& passive)
{
    if (!ctx.game || !ctx.mutable_actor || !ctx.player_id) {
        return false;
    }
    const auto cap_it = passive.reactive_effect_payload.find("turn_cap");
    if (cap_it != passive.reactive_effect_payload.end() && cap_it->second > 0) {
        const int cap = cap_it->second;
        const std::string tally_key = "reactive_tally_" + passive.key;
        const int tally = entity_effect_amount(*ctx.mutable_actor, tally_key);
        if (tally >= cap) {
            return false;
        }
        const auto amt_it = passive.reactive_effect_payload.find(effect_keys::kPayloadAmount);
        const int full_amount = (amt_it != passive.reactive_effect_payload.end()) ? std::max(0, amt_it->second) : 1;
        const int granted = std::min(full_amount, cap - tally);
        if (granted > 0) {
            apply_reactive_energy_gain(ctx.game->turn_manager, *ctx.player_id, passive, granted);
            add_entity_effect(*ctx.mutable_actor, tally_key, granted);
            return true;
        }
        return false;
    }
    apply_reactive_energy_gain(ctx.game->turn_manager, *ctx.player_id, passive);
    return true;
}

bool dispatch_reactive_effect_key(ReactiveDispatchCtx& ctx, const PassiveAbilitySpec& passive)
{
    if (!ctx.game || passive.reactive_effect_key.empty()) {
        return false;
    }
    const std::string& fx = passive.reactive_effect_key;

    if (fx == "grant_permanent_stat_growth_self") {
        if (!ctx.actor_unit) {
            return false;
        }
        reactive_apply_permanent_stat_growth_from_payload(*ctx.actor_unit, passive);
        return true;
    }
    if (fx == "grant_permanent_conduit_self") {
        if (!ctx.actor_unit) {
            return false;
        }
        int amount = 1;
        if (const auto it = passive.reactive_effect_payload.find("amount"); it != passive.reactive_effect_payload.end()) {
            amount = it->second;
        }
        if (amount > 0) {
            const int current = entity_attribute_amount(*ctx.actor_unit, "conduit", 0);
            set_entity_attribute_amount(*ctx.actor_unit, "conduit", current + amount);
            return true;
        }
        return false;
    }
    if (fx == "draw_cards_owner") {
        return reactive_draw_cards_owner_with_cap(ctx, passive);
    }
    if (fx == "release_stored_energy_spell_turquoise") {
        if (!ctx.dying_entity || !ctx.dying_entity->owner) {
            return false;
        }
        const int stored = entity_effect_amount(*ctx.dying_entity, "ancient_frog_stored_energy");
        if (stored > 0) {
            credit_tagged_float(ctx.game->turn_manager, "spell_ability", *ctx.dying_entity->owner,
                EnergyType::Turquoise, stored);
        }
        return stored > 0;
    }
    if (fx == "deal_damage_to_all_enemies_nearby") {
        if (!ctx.dying_entity || !ctx.dying_entity->owner || !ctx.dying_entity->position) {
            return false;
        }
        int amount = 1;
        if (const auto it = passive.reactive_effect_payload.find(effect_keys::kPayloadAmount);
            it != passive.reactive_effect_payload.end()) {
            amount = std::max(0, it->second);
        }
        if (amount <= 0) {
            return false;
        }
        const int player_id = *ctx.dying_entity->owner;
        const int dx[] = {-1, 0, 1, -1, 1, -1, 0, 1};
        const int dy[] = {-1, -1, -1, 0, 0, 1, 1, 1};
        std::vector<std::string> hit_ids;
        for (int d = 0; d < 8; ++d) {
            const int nx = ctx.dying_entity->position->first + dx[d];
            const int ny = ctx.dying_entity->position->second + dy[d];
            const auto sq = ctx.game->board.get_square(nx, ny);
            if (!sq || !sq->occupied || !sq->entity || !sq->entity->owner) {
                continue;
            }
            if (!teams_hostile(*ctx.game, player_id, *sq->entity->owner)) {
                continue;
            }
            const std::string& hit_id = sq->entity->entity_id;
            if (std::find(hit_ids.begin(), hit_ids.end(), hit_id) != hit_ids.end()) {
                continue;
            }
            hit_ids.push_back(hit_id);
            DamagePacket dp;
            dp.target = sq->entity;
            dp.amount = amount;
            (void)apply_damage_packet(*ctx.game, dp);
        }
        return true;
    }
    if (fx == "fire_tile_footprint_on_death") {
        if (!ctx.dying_entity || !ctx.dying_entity->position || !ctx.dying_entity->owner) {
            return false;
        }
        const int duration = [&]() {
            const auto it = passive.reactive_effect_payload.find("duration");
            return it != passive.reactive_effect_payload.end() ? it->second : 2;
        }();
        const int radius = [&]() {
            const auto it = passive.reactive_effect_payload.find("radius");
            return it != passive.reactive_effect_payload.end() ? it->second : 1;
        }();
        std::vector<std::pair<int, int>> cells;
        const auto [ox, oy] = *ctx.dying_entity->position;
        for (int dx = -radius; dx <= radius; ++dx) {
            for (int dy = -radius; dy <= radius; ++dy) {
                cells.push_back({ox + dx, oy + dy});
            }
        }
        fire_units_on_cells(*ctx.game, cells, "last_gasp_fire");
        StackItem fire_item;
        fire_item.controller_id = *ctx.dying_entity->owner;
        place_fire_tile_overlays_on_unoccupied(*ctx.game, fire_item, cells, duration);
        return true;
    }
    if (fx == "spawn_melee_robot_on_hit") {
        if (!ctx.mutable_actor || !ctx.victim_entity) {
            return false;
        }
        spawn_replication_robot(*ctx.game, *ctx.mutable_actor, *ctx.victim_entity, ctx.damage_dealt);
        return true;
    }
    if (fx == "spawn_half_stat_melee_token_stunned") {
        if (!ctx.dying_entity || !ctx.dying_entity->position || !ctx.dying_entity->owner
                || ctx.dying_entity->entity_type != "unit") {
            return false;
        }
        const auto* unit = dynamic_cast<const Unit*>(ctx.dying_entity);
        if (!unit) {
            return false;
        }
        const int hp = unit->base_health / 2;
        const int attack = unit->melee_damage / 2;
        if (hp <= 0 && attack <= 0) {
            return false;
        }
        ctx.game->queue_pending_death_spawn(ctx.dying_entity->entity_id, *ctx.dying_entity->owner,
            ctx.dying_entity->position->first, ctx.dying_entity->position->second, std::max(1, hp),
            std::max(1, attack));
        return true;
    }
    if (fx == "refresh_movement_self") {
        if (!ctx.mutable_actor) {
            return false;
        }
        if (auto* unit = dynamic_cast<Unit*>(ctx.mutable_actor)) {
            unit->moves_remaining_this_turn = std::max(unit->moves_remaining_this_turn, 1);
            unit->standard_moves_remaining_this_turn = std::max(unit->standard_moves_remaining_this_turn, 1);
            return true;
        }
        return false;
    }
    if (fx == "apply_overload_to_attacker") {
        if (!ctx.attacker_entity || !ctx.game) {
            return false;
        }
        const auto amt_it = passive.reactive_effect_payload.find(effect_keys::kPayloadAmount);
        const int amount = (amt_it != passive.reactive_effect_payload.end()) ? std::max(1, amt_it->second) : 1;
        if (ctx.game->board.all_entities_map.contains(ctx.attacker_entity->entity_id)) {
            ctx.game->apply_overload_stacks(ctx.game->board.all_entities_map.at(ctx.attacker_entity->entity_id), amount);
            return true;
        }
        return false;
    }
    if (fx == "grant_movement_speed_self") {
        if (!ctx.mutable_actor) {
            return false;
        }
        int amount = 1;
        if (const auto amt_it = passive.reactive_effect_payload.find(effect_keys::kPayloadAmount);
            amt_it != passive.reactive_effect_payload.end()) {
            amount = std::max(0, amt_it->second);
        }
        if (amount > 0) {
            const auto unit_sp = ctx.game->board.all_entities_map[ctx.mutable_actor->entity_id];
            apply_grant_movement_speed_self(*ctx.game, unit_sp, amount, "spellbound_" + passive.key);
            return true;
        }
        return false;
    }
    if (fx == "grant_next_damage_bonus_self") {
        if (!ctx.mutable_actor) {
            return false;
        }
        int amount = 1;
        if (const auto amt_it = passive.reactive_effect_payload.find(effect_keys::kPayloadAmount);
            amt_it != passive.reactive_effect_payload.end()) {
            amount = std::max(0, amt_it->second);
        }
        if (amount > 0) {
            add_entity_effect(*ctx.mutable_actor, "next_damage_bonus", amount, "spellbound_" + passive.key);
            propagate_buff_to_surrounding_mirror_passives(*ctx.game, *ctx.mutable_actor, "next_damage_bonus", amount);
            return true;
        }
        return false;
    }
    if (fx == "grant_magus_charge_self") {
        if (!ctx.mutable_actor) {
            return false;
        }
        int amount = 1;
        if (const auto amt_it = passive.reactive_effect_payload.find(effect_keys::kPayloadAmount);
            amt_it != passive.reactive_effect_payload.end()) {
            amount = std::max(0, amt_it->second);
        }
        if (amount > 0) {
            add_entity_effect(*ctx.mutable_actor, "magus_charge", amount, "spellbound_" + passive.key);
            return true;
        }
        return false;
    }
    if (fx == "grant_seraphina_resonance_self") {
        if (!ctx.mutable_actor || !ctx.mutable_actor->owner || !ctx.player_id) {
            return false;
        }
        int amount = 1;
        if (const auto it = passive.reactive_effect_payload.find(effect_keys::kPayloadAmount); it != passive.reactive_effect_payload.end()) {
            amount = std::max(0, it->second);
        }
        int threshold = 3;
        if (const auto thr_it = passive.reactive_effect_payload.find("threshold"); thr_it != passive.reactive_effect_payload.end()) {
            threshold = std::max(1, thr_it->second);
        }
        int hand_expires = 2;
        if (const auto exp_it = passive.reactive_effect_payload.find("hand_expires_after_owner_turn_ends");
            exp_it != passive.reactive_effect_payload.end()) {
            hand_expires = std::max(0, exp_it->second);
        }
        std::string grant_card_key = "symphony_scorching_sphere";
        if (const auto key_it = passive.reactive_string_payload.find("grant_card_key");
            key_it != passive.reactive_string_payload.end() && !key_it->second.empty()) {
            grant_card_key = key_it->second;
        }
        if (amount > 0) {
            add_entity_effect(*ctx.mutable_actor, "seraphina_resonance", amount, "spellbound_" + passive.key);
        }
        while (entity_effect_amount(*ctx.mutable_actor, "seraphina_resonance") >= threshold) {
            reduce_entity_effect(*ctx.mutable_actor, "seraphina_resonance", threshold);
            (void)ctx.game->grant_temporary_hand_card(*ctx.player_id, grant_card_key, hand_expires);
        }
        return amount > 0;
    }

    if (fx.rfind("gain_", 0) == 0) {
        if (!ctx.player_id) {
            return false;
        }
        return reactive_apply_energy_with_turn_cap(ctx, passive);
    }
    return false;
}
