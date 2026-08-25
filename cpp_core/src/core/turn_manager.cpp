#include "tactics/board/aether.hpp"
#include "tactics/board/scanner.hpp"
#include "tactics/board/omni_energy_tile.hpp"
#include "tactics/core/turn_manager.hpp"

#include "tactics/core/game_state.hpp"
#include "tactics/core/passive_action_order.hpp"

#include <algorithm>
#include <optional>
#include <set>

namespace {

using tactics::EnergyType;
using tactics::EnergyZone;
using tactics::GameState;

struct ZoneTapDecision {
    int zone_index{-1};
    EnergyType produced_type{EnergyType::Neutral};
};

struct PaymentPlan {
    bool ok{false};
    std::map<EnergyType, int> float_spend{};
    std::vector<ZoneTapDecision> zone_taps{};
    std::string reason;
};

std::map<EnergyType, int> zone_options(const EnergyZone& zone) {
    std::map<EnergyType, int> out;
    if (!zone.is_tapped) {
        for (const auto& [etype, amount] : zone.energy_produced) {
            if (amount > 0) out[etype] = amount;
        }
    }
    // Conquering Territories: an energy-only "use land" ability is an auto-tappable source too.
    const int ai = zone.auto_tap_ability_index();
    if (ai >= 0) {
        for (const auto& [etype, amount] : zone.land_abilities[static_cast<std::size_t>(ai)].energy_produced) {
            if (amount > 0) out[etype] += amount;
        }
    }
    return out;
}

int zone_flex_score(const EnergyZone& zone) {
    auto opts = zone_options(zone);
    int score = static_cast<int>(opts.size());
    if (opts.contains(EnergyType::Omni)) score += 10;
    // Tap plain energy lands before lands that also carry a special (effect) ability, so those are
    // preserved for their special use. Lower score is chosen first, so penalize special lands.
    if (zone.has_special_land_ability()) score += 100;
    return score;
}

std::optional<std::pair<int, EnergyType>> choose_zone_for_color(
    const std::vector<EnergyZone>& zones,
    const std::vector<int>& available_indices,
    EnergyType color) {
    std::vector<int> exact;
    std::vector<int> omni;
    for (int idx : available_indices) {
        auto opts = zone_options(zones[idx]);
        if (opts.contains(color)) exact.push_back(idx);
        if (opts.contains(EnergyType::Omni)) omni.push_back(idx);
    }
    const auto& candidates = !exact.empty() ? exact : omni;
    if (candidates.empty()) return std::nullopt;
    int chosen = candidates[0];
    for (int idx : candidates) {
        if (zone_flex_score(zones[idx]) < zone_flex_score(zones[chosen])) chosen = idx;
    }
    auto opts = zone_options(zones[chosen]);
    EnergyType produced = opts.contains(color) ? color : EnergyType::Omni;
    return std::make_pair(chosen, produced);
}

/** Pay neutral from float without spending chroma that is also explicitly required (e.g. 1N+1O
 *  with one tithe-orange + one untapped zone: reserve orange for the colored step). */
std::optional<EnergyType> pick_neutral_pool_type(const std::map<EnergyType, int>& pool,
                                                 const std::map<EnergyType, int>& raw_cost) {
    for (EnergyType et : tactics::kEnergyNeutralSpendPoolPriority) {
        const auto it = pool.find(et);
        if (it == pool.end() || it->second <= 0) {
            continue;
        }
        if (et != EnergyType::Neutral) {
            int reserved = 0;
            if (const auto cost_it = raw_cost.find(et); cost_it != raw_cost.end()) {
                reserved = std::max(0, cost_it->second);
            }
            if (it->second <= reserved) {
                continue;
            }
        }
        return et;
    }
    return std::nullopt;
}

std::optional<EnergyType> choose_neutral_zone_output(const EnergyZone& zone) {
    auto opts = zone_options(zone);
    if (opts.empty()) return std::nullopt;
    if (opts.contains(EnergyType::Neutral)) return EnergyType::Neutral;
    for (EnergyType et : tactics::kEnergyChromaTypes) {
        if (opts.contains(et)) return et;
    }
    if (opts.contains(EnergyType::Omni)) return EnergyType::Omni;
    return std::nullopt;
}

/** For paying generic (neutral) cost: prefer neutral-producing zones, then least-flexible among those, then other zones. */
std::optional<int> choose_zone_index_for_neutral_payment(const std::vector<EnergyZone>& zones, const std::vector<int>& available_indices) {
    if (available_indices.empty()) return std::nullopt;
    std::vector<int> neutral_producers;
    for (int idx : available_indices) {
        if (zone_options(zones[idx]).contains(EnergyType::Neutral)) neutral_producers.push_back(idx);
    }
    const auto& pool = !neutral_producers.empty() ? neutral_producers : available_indices;
    int chosen = pool[0];
    for (int idx : pool) {
        if (zone_flex_score(zones[idx]) < zone_flex_score(zones[chosen])) chosen = idx;
    }
    return chosen;
}

PaymentPlan plan_payment(const GameState& game, const std::map<EnergyType, int>& raw_cost, int player_id, const std::map<EnergyType, int>& player_float) {
    PaymentPlan plan;
    bool has_cost = false;
    for (const auto& [_, amt] : raw_cost) {
        if (amt > 0) {
            has_cost = true;
            break;
        }
    }
    if (!has_cost) {
        plan.ok = true;
        return plan;
    }

    auto zones_it = game.players_energy_zones.find(player_id);
    const std::vector<EnergyZone>* zones_ptr = (zones_it == game.players_energy_zones.end()) ? nullptr : &zones_it->second;
    std::vector<int> available_zone_indices;
    if (zones_ptr) {
        for (int i = 0; i < static_cast<int>(zones_ptr->size()); ++i) {
            // Only zones that can actually produce energy right now - a passive untapped zone or a land
            // with an available auto-tap ability. (Empty options would trip the neutral fallback.)
            if (!zone_options((*zones_ptr)[i]).empty()) available_zone_indices.push_back(i);
        }
    }

    std::map<EnergyType, int> float_pool = player_float;
    for (EnergyType et : tactics::kEnergyBillingAllTypes) {
        if (!float_pool.contains(et)) float_pool[et] = 0;
        plan.float_spend[et] = 0;
    }

    const auto remove_available = [&](std::vector<int>& arr, int value) {
        arr.erase(std::remove(arr.begin(), arr.end(), value), arr.end());
    };

    // 0) Neutral requirement FIRST (neutral float, then any float per pick_neutral_pool_type, then zones - neutral zones before flexible colored ones)
    int neutral_need = 0;
    if (auto it = raw_cost.find(EnergyType::Neutral); it != raw_cost.end()) neutral_need = std::max(0, it->second);
    while (neutral_need > 0) {
        auto pool_type = pick_neutral_pool_type(float_pool, raw_cost);
        if (pool_type) {
            float_pool[*pool_type] -= 1;
            plan.float_spend[*pool_type] += 1;
            neutral_need -= 1;
            continue;
        }

        if (!zones_ptr || available_zone_indices.empty()) {
            plan.reason = "Not enough energy for neutral cost";
            return plan;
        }

        const int chosen = choose_zone_index_for_neutral_payment(*zones_ptr, available_zone_indices).value_or(available_zone_indices[0]);
        auto produced_type = choose_neutral_zone_output((*zones_ptr)[chosen]);
        if (!produced_type) {
            plan.reason = "Zone cannot produce usable energy";
            return plan;
        }
        int produced_amount = zone_options((*zones_ptr)[chosen])[*produced_type];
        int spend_now = std::min(neutral_need, produced_amount);
        neutral_need -= spend_now;
        int leftover = produced_amount - spend_now;
        if (leftover > 0) float_pool[*produced_type] += leftover;
        plan.zone_taps.push_back({chosen, *produced_type});
        remove_available(available_zone_indices, chosen);
    }

    // 1) Strict colored requirements (after neutral is paid)
    for (EnergyType color : tactics::kEnergyChromaTypes) {
        int need = 0;
        if (auto it = raw_cost.find(color); it != raw_cost.end()) need = std::max(0, it->second);
        if (need <= 0) continue;

        int use_exact = std::min(need, float_pool[color]);
        float_pool[color] -= use_exact;
        plan.float_spend[color] += use_exact;
        need -= use_exact;

        int use_omni = std::min(need, float_pool[EnergyType::Omni]);
        float_pool[EnergyType::Omni] -= use_omni;
        plan.float_spend[EnergyType::Omni] += use_omni;
        need -= use_omni;

        while (need > 0) {
            if (!zones_ptr) {
                plan.reason = "Not enough energy for colored cost";
                return plan;
            }
            auto selection = choose_zone_for_color(*zones_ptr, available_zone_indices, color);
            if (!selection) {
                plan.reason = "Not enough energy for colored cost";
                return plan;
            }
            const int zone_idx = selection->first;
            const EnergyType produced_type = selection->second;
            auto opts = zone_options((*zones_ptr)[zone_idx]);
            const int produced_amount = opts[produced_type];
            if (produced_amount <= 0) {
                plan.reason = "Invalid zone output";
                return plan;
            }
            int spend_now = std::min(need, produced_amount);
            need -= spend_now;
            int leftover = produced_amount - spend_now;
            if (leftover > 0) float_pool[produced_type] += leftover;
            plan.zone_taps.push_back({zone_idx, produced_type});
            remove_available(available_zone_indices, zone_idx);
        }
    }

    // 1b) Omni-only requirements
    int omni_need = 0;
    if (auto it = raw_cost.find(EnergyType::Omni); it != raw_cost.end()) omni_need = std::max(0, it->second);
    if (omni_need > 0) {
        int use_omni = std::min(omni_need, float_pool[EnergyType::Omni]);
        float_pool[EnergyType::Omni] -= use_omni;
        plan.float_spend[EnergyType::Omni] += use_omni;
        omni_need -= use_omni;

        while (omni_need > 0) {
            if (!zones_ptr) {
                plan.reason = "Not enough omni energy";
                return plan;
            }
            int chosen = -1;
            for (int idx : available_zone_indices) {
                auto opts = zone_options((*zones_ptr)[idx]);
                if (!opts.contains(EnergyType::Omni)) continue;
                if (chosen < 0 || zone_flex_score((*zones_ptr)[idx]) < zone_flex_score((*zones_ptr)[chosen])) chosen = idx;
            }
            if (chosen < 0) {
                plan.reason = "Not enough omni energy";
                return plan;
            }
            int produced_amount = zone_options((*zones_ptr)[chosen])[EnergyType::Omni];
            int spend_now = std::min(omni_need, produced_amount);
            omni_need -= spend_now;
            int leftover = produced_amount - spend_now;
            if (leftover > 0) float_pool[EnergyType::Omni] += leftover;
            plan.zone_taps.push_back({chosen, EnergyType::Omni});
            remove_available(available_zone_indices, chosen);
        }
    }

    plan.ok = true;
    return plan;
}

}  // namespace

namespace tactics {

// ---------------------------------------------------------------------------
// Tagged-pool restriction registry.
// Add a new entry here to define additional pool types.
//   key          - pool name (matches reactive_string_payload["pool"])
//   value        - set of ActionTypes that may draw from this pool
// ---------------------------------------------------------------------------
static const std::unordered_map<std::string, std::set<ActionType>> kTaggedPoolRestrictions = {
    {"spell_ability", {ActionType::Spell, ActionType::Ability}},
    {"spell_only",    {ActionType::Spell}},
    {"unit_deploy",   {ActionType::Deploy}},
};

static bool tagged_pool_allows(const std::string& tag, ActionType at) {
    const auto it = kTaggedPoolRestrictions.find(tag);
    if (it == kTaggedPoolRestrictions.end()) return true; // unknown tag: unrestricted
    return it->second.count(at) > 0;
}

TurnManager::TurnManager(GameBoard& board) : board_(board) {}

void TurnManager::add_player(int player_id) {
    if (std::find(players.begin(), players.end(), player_id) == players.end()) {
        players.push_back(player_id);
        std::map<EnergyType, int> energy;
        for (EnergyType e : kEnergyBillingAllTypes) {
            energy[e] = 0;
        }
        player_energy[player_id] = energy;
        player_flux_energy_generated_total[player_id] = 0;
        player_overload_applied_total[player_id] = 0;
        player_ability_damage_dealt_total[player_id] = 0;
    }
}

void credit_tagged_float(TurnManager& tm, const std::string& tag, int player_id, EnergyType etype, int amount)
{
    if (amount <= 0) {
        return;
    }
    tm.player_tagged_float[tag][player_id][etype] += amount;
    if (tag == "spell_ability") {
        tm.player_flux_energy_generated_total[player_id] += amount;
    }
}

int player_flux_energy_generated_total(const TurnManager& tm, int player_id)
{
    const auto it = tm.player_flux_energy_generated_total.find(player_id);
    return it != tm.player_flux_energy_generated_total.end() ? it->second : 0;
}

int player_overload_applied_total(const TurnManager& tm, int player_id)
{
    const auto it = tm.player_overload_applied_total.find(player_id);
    return it != tm.player_overload_applied_total.end() ? it->second : 0;
}

int player_ability_damage_dealt_total(const TurnManager& tm, int player_id)
{
    const auto it = tm.player_ability_damage_dealt_total.find(player_id);
    return it != tm.player_ability_damage_dealt_total.end() ? it->second : 0;
}

std::optional<int> TurnManager::current_player() const {
    if (players.empty()) return std::nullopt;
    return players[current_player_index];
}

bool TurnManager::can_afford(const GameState& game, int player_id,
                             const std::map<EnergyType, int>& cost,
                             ActionType action_type) const {
    auto it = player_energy.find(player_id);
    if (it == player_energy.end()) return false;

    // Start with unrestricted pool.
    std::map<EnergyType, int> combined_float = it->second;

    // Merge in any tagged pools that allow this action type.
    for (const auto& [tag, per_player] : player_tagged_float) {
        if (!tagged_pool_allows(tag, action_type)) continue;
        const auto pit = per_player.find(player_id);
        if (pit == per_player.end()) continue;
        for (const auto& [etype, amount] : pit->second) {
            combined_float[etype] += amount;
        }
    }

    return plan_payment(game, cost, player_id, combined_float).ok;
}

void TurnManager::spend_energy(GameState& game, int player_id,
                               const std::map<EnergyType, int>& cost,
                               ActionType action_type) {
    auto it = player_energy.find(player_id);
    if (it == player_energy.end()) return;

    // Build combined float for planning (unrestricted + allowed tagged pools).
    std::map<EnergyType, int> combined_float = it->second;
    for (const auto& [tag, per_player] : player_tagged_float) {
        if (!tagged_pool_allows(tag, action_type)) continue;
        const auto pit = per_player.find(player_id);
        if (pit == per_player.end()) continue;
        for (const auto& [etype, amount] : pit->second) {
            combined_float[etype] += amount;
        }
    }

    auto plan = plan_payment(game, cost, player_id, combined_float);
    if (!plan.ok) return;

    // Drain tagged pools first (they are single-purpose; spend before unrestricted).
    // Iterate in deterministic tag order so spend order is stable.
    std::vector<std::string> allowed_tags;
    for (const auto& [tag, _] : player_tagged_float) {
        if (tagged_pool_allows(tag, action_type)) allowed_tags.push_back(tag);
    }
    std::sort(allowed_tags.begin(), allowed_tags.end());

    for (const auto& tag : allowed_tags) {
        auto& per_player = player_tagged_float[tag];
        auto pit = per_player.find(player_id);
        if (pit == per_player.end()) continue;
        auto& tpool = pit->second;
        for (auto& [etype, spend] : plan.float_spend) {
            if (spend <= 0) continue;
            const int avail = tpool.count(etype) ? tpool.at(etype) : 0;
            const int from_t = std::min(spend, avail);
            tpool[etype] = std::max(0, tpool[etype] - from_t);
            spend -= from_t;
        }
    }

    // Drain remaining from unrestricted pool.
    auto& pool = player_energy[player_id];
    for (const auto& [etype, amount] : plan.float_spend) {
        pool[etype] = std::max(0, pool[etype] - amount);
    }

    auto zones_it = game.players_energy_zones.find(player_id);
    if (zones_it == game.players_energy_zones.end()) return;
    auto& zones = zones_it->second;
    for (const auto& tap : plan.zone_taps) {
        if (tap.zone_index < 0 || tap.zone_index >= static_cast<int>(zones.size())) continue;
        zones[tap.zone_index].tap(tap.produced_type);
    }
}

std::optional<EnergySpendRecord> TurnManager::spend_energy_recorded(GameState& game, int player_id,
    const std::map<EnergyType, int>& cost, ActionType action_type)
{
    auto it = player_energy.find(player_id);
    if (it == player_energy.end()) {
        return std::nullopt;
    }

    std::map<EnergyType, int> combined_float = it->second;
    for (const auto& [tag, per_player] : player_tagged_float) {
        if (!tagged_pool_allows(tag, action_type)) {
            continue;
        }
        const auto pit = per_player.find(player_id);
        if (pit == per_player.end()) {
            continue;
        }
        for (const auto& [etype, amount] : pit->second) {
            combined_float[etype] += amount;
        }
    }

    auto plan = plan_payment(game, cost, player_id, combined_float);
    if (!plan.ok) {
        return std::nullopt;
    }

    EnergySpendRecord record;
    record.unrestricted_spend = plan.float_spend;
    for (const auto& tap : plan.zone_taps) {
        record.zone_taps.push_back({tap.zone_index, tap.produced_type});
    }

    std::vector<std::string> allowed_tags;
    for (const auto& [tag, _] : player_tagged_float) {
        if (tagged_pool_allows(tag, action_type)) {
            allowed_tags.push_back(tag);
        }
    }
    std::sort(allowed_tags.begin(), allowed_tags.end());

    for (const auto& tag : allowed_tags) {
        auto& per_player = player_tagged_float[tag];
        auto pit = per_player.find(player_id);
        if (pit == per_player.end()) {
            continue;
        }
        auto& tpool = pit->second;
        auto& tag_record = record.tagged_spend[tag];
        for (auto& [etype, spend] : record.unrestricted_spend) {
            if (spend <= 0) {
                continue;
            }
            const int avail = tpool.count(etype) ? tpool.at(etype) : 0;
            const int from_t = std::min(spend, avail);
            if (from_t > 0) {
                tpool[etype] = std::max(0, tpool[etype] - from_t);
                tag_record[etype] += from_t;
                spend -= from_t;
            }
        }
    }

    auto& pool = player_energy[player_id];
    for (const auto& [etype, amount] : record.unrestricted_spend) {
        pool[etype] = std::max(0, pool[etype] - amount);
    }

    auto zones_it = game.players_energy_zones.find(player_id);
    if (zones_it != game.players_energy_zones.end()) {
        auto& zones = zones_it->second;
        for (const auto& tap : record.zone_taps) {
            if (tap.zone_index < 0 || tap.zone_index >= static_cast<int>(zones.size())) {
                continue;
            }
            zones[tap.zone_index].tap(tap.produced_type);
        }
    }

    return record;
}

void TurnManager::refund_energy_spend(GameState& game, int player_id, const EnergySpendRecord& record)
{
    for (const auto& [tag, per_type] : record.tagged_spend) {
        auto& per_player = player_tagged_float[tag];
        auto& tpool = per_player[player_id];
        for (const auto& [etype, amount] : per_type) {
            tpool[etype] += amount;
        }
    }
    auto& pool = player_energy[player_id];
    for (const auto& [etype, amount] : record.unrestricted_spend) {
        pool[etype] += amount;
    }
    auto zones_it = game.players_energy_zones.find(player_id);
    if (zones_it != game.players_energy_zones.end()) {
        auto& zones = zones_it->second;
        for (const auto& tap : record.zone_taps) {
            if (tap.zone_index < 0 || tap.zone_index >= static_cast<int>(zones.size())) {
                continue;
            }
            zones[tap.zone_index].untap();
        }
    }
}

void TurnManager::start_turn(GameState& game) {
    game.clear_phase_undo_stack();
    auto cp = current_player();
    if (!cp) return;
    // Clear per-turn deploy discount for the player whose turn is starting.
    deploy_discount_per_unit[*cp] = 0;
    game.expire_temporary_effects_for_turn(*cp, "owner_turn_start");
    game.apply_core_cracker_shutdown_at_turn_start(*cp);
    process_aether_tiles_at_turn_start(game, *cp);
    process_omni_energy_tiles_at_turn_start(game, *cp);
    process_scanner_tiles_at_turn_start(game, *cp);
    process_automated_passive_actions(game, *cp, kPassiveActionTimingOwnerTurnStart);
    game.process_start_of_turn_status_effects(*cp);
    for (auto& entity : game.board.all_entities()) {
        if (entity->owner && *entity->owner == *cp) {
            entity->has_moved_this_turn = false;
            entity->has_attacked_this_turn = false;
            entity->frenzy_triggered_this_turn = false;
            entity->death_shield_used_this_turn = false;
            // Include temp effects that survived expiry (e.g. multi-turn bonus moves).
            const PassiveStatGrant temp = temporary_stat_grants_for_entity(*entity);
            entity->moves_remaining_this_turn = entity_can_move(*entity) ? 1 + entity->bonus_moves + temp.bonus_moves : 0;
            refresh_standard_moves_remaining(*entity);
            entity->attacks_remaining_this_turn = 1;
            entity->bonus_attacks_remaining_this_turn = entity->bonus_attacks + temp.bonus_attacks;
            // Reactions (counterattacks) refill only for units owned by the player whose turn is starting.
            entity->reactions_remaining_this_turn = 3;
            // Coordinated fire shot counter expires at the start of the owner's next turn.
            entity->attacked_targets_this_turn.clear();
            if (auto* u = dynamic_cast<Unit*>(entity.get())) {
                u->coordinated_fire_shots_remaining = 0;
                u->coordinated_fire_damage_min = 0;
                u->coordinated_fire_damage_max = 0;
            }
            // Ability uses and barrage cast counts refresh at owner turn start.
            // Deployment fatigue blocks usage, not this refresh (fatigue expires above in the same start_turn).
            refresh_entity_ability_uses(*entity);
            entity->barrage_cast_counts_this_turn.clear();
        }
        // Per-turn reactive tally counters reset every turn (any player's), not just the owner's.
        entity->entity_effects.erase(
            std::remove_if(entity->entity_effects.begin(), entity->entity_effects.end(),
                [](const tactics::EntityEffectInstance& eff) {
                    return eff.key.rfind("reactive_tally_", 0) == 0;
                }),
            entity->entity_effects.end());
    }
    for (auto& [_, deck] : game.players_decks) {
        deck.refresh_turn_limited_card_attributes();
    }
    for (auto& z : game.players_energy_zones[*cp]) {
        z.untap();
        z.refresh_land_use();  // Conquering Territories: restore the shared 1/turn use, clear depletion.
    }
    if (skip_first_turn_draw_) {
        skip_first_turn_draw_ = false;
    } else if (!game.IsAwaitingScan()) {
        game.draw_cards(*cp, 1);
    } else {
        deferred_turn_draw_seat_ = *cp;
    }
    current_phase = TurnPhase::Energy;
    pending_energy_choices[*cp] = game.players_energy_zones_decks[*cp].draw(3);
    if (pending_energy_choices[*cp].empty()) current_phase = TurnPhase::Main;
}

ActionResult TurnManager::choose_energy_zone(GameState& game, int player_id, int choice_index) {
    auto cp = current_player();
    if (!cp || *cp != player_id) return {false, "Not your turn", {}};
    if (current_phase != TurnPhase::Energy) return {false, "Not in energy phase", {}};
    auto& choices = pending_energy_choices[player_id];
    if (choices.empty()) {
        current_phase = TurnPhase::Main;
        return {true, "No energy choices available. Entered Main Phase.", {}};
    }
    if (choice_index < 0 || choice_index >= static_cast<int>(choices.size())) return {false, "Invalid energy choice index", {}};
    auto chosen = choices[choice_index];
    chosen.untap();
    game.players_energy_zones[player_id].push_back(chosen);
    // Conquering Territories: resolve entrance (groundwork / depleted / enter effects).
    const bool kept = game.on_territory_conquered(player_id, game.players_energy_zones[player_id].back());
    std::vector<EnergyZone> remain;
    for (int i = 0; i < static_cast<int>(choices.size()); ++i) if (i != choice_index) remain.push_back(choices[i]);
    game.players_energy_zones_decks[player_id].shuffle_back(remain, game.rng());
    choices.clear();
    game.clear_phase_undo_stack();
    current_phase = TurnPhase::Main;
    if (!kept) {
        return {true, chosen.name + " was destroyed (groundwork not met).", {}};
    }
    return {true, "Placed " + chosen.name, {}};
}

ActionResult TurnManager::skip_energy_zone(GameState& game, int player_id) {
    auto cp = current_player();
    if (!cp || *cp != player_id) return {false, "Not your turn", {}};
    if (current_phase != TurnPhase::Energy) return {false, "Not in energy phase", {}};
    game.players_energy_zones_decks[player_id].shuffle_back(pending_energy_choices[player_id], game.rng());
    pending_energy_choices[player_id].clear();
    current_phase = TurnPhase::Main;
    return {true, "Skipped zone placement", {}};
}

bool TurnManager::end_turn(GameState& game) {
    auto cp = current_player();
    if (!cp) return false;
    game.process_end_of_turn_regen(*cp);                                           // Wave 1: regen
    process_automated_passive_actions(game, *cp, kPassiveActionTimingOwnerTurnEnd); // Wave 2: passives
    game.process_tile_overlay_effects(*cp);                                         // Wave 2.5: tile overlays (gas cloud → poison, etc.)
    game.process_end_of_turn_dot(*cp);                                              // Wave 3: DOT
    game.clear_core_cracker_deploy_exempt_at_turn_end(*cp);
    game.expire_vulnerable_turn_end_for_turn(*cp);
    game.expire_active_turn_buffs_for_turn(*cp);
    game.expire_temporary_effects_for_turn(*cp, "owner_turn_end");
    game.expire_temporary_hand_cards_for_player(*cp);
    pending_energy_choices[*cp].clear();
    // All floating energy (every seat) expires when the active turn ends - including gains on
    // non-active players during this turn (e.g. Mana Frog dying on the opponent's attack).
    for (const int player_id : players) {
        for (EnergyType e : kEnergyBillingAllTypes) {
            player_energy[player_id][e] = 0;
        }
        for (auto& [tag, per_player] : player_tagged_float) {
            per_player.erase(player_id);
        }
    }
    current_player_index = (current_player_index + 1) % players.size();
    if (current_player_index == 0) {
        ++round_number;
        clear_aether_teams_fired_for_new_round(game);
        clear_omni_energy_teams_fired_for_new_round(game);
        clear_scanner_teams_fired_for_new_round(game);
        for (int player_id : players) {
            game.expire_temporary_effects_for_turn(player_id, "round_start");
            process_automated_passive_actions(game, player_id, kPassiveActionTimingRoundStart);
        }
        // Sudden-death trigger: once every player's draw deck has run dry (discard is
        // permanent, so this is one-way), start the countdown from this round.
        if (all_decks_empty_since_round < 0 && !game.players_decks.empty()) {
            bool all_empty = true;
            for (const auto& [_, deck] : game.players_decks) {
                if (!deck.deck.empty()) {
                    all_empty = false;
                    break;
                }
            }
            if (all_empty) {
                all_decks_empty_since_round = round_number;
            }
        }
    }
    start_turn(game);
    return true;
}

}  // namespace tactics
