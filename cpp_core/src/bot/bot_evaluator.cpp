#include "tactics/bot/bot_evaluator.hpp"

#include "tactics/attributes/attributes.hpp"
#include "tactics/board/aether.hpp"
#include "tactics/board/omni_energy_tile.hpp"
#include "tactics/board/scanner.hpp"
#include "tactics/bot/bot_match_outcome.hpp"
#include "tactics/combat/combat_resolver.hpp"
#include "tactics/cards/card_catalog.hpp"
#include "tactics/cards/cards.hpp"
#include "tactics/cards/passive_catalog.hpp"
#include "tactics/core/board_target_policy.hpp"
#include "tactics/core/turn_manager.hpp"
#include "tactics/entities/entity.hpp"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

// Score blends two axes. Axis 1 (the card/economy game, MTG-style) - base HP,
// card advantage, energy, flux engines. Axis 2 (the positional/tactical game,
// chess-style) - weighted piece values, board advancement toward the enemy base,
// and king-safety pressure around our own base. Everything is expressed as an
// (own - enemy) differential and squashed into [-1, 1].

namespace tactics::bot {
namespace {

/** Remaining HP of this seat's base, or 0 if the base is gone. */
int base_hp_for_seat(const GameState& game, const int seat)
{
    const std::string base_id = "base_p" + std::to_string(seat);
    const auto it = game.board.all_entities_map.find(base_id);
    if (it == game.board.all_entities_map.end() || !it->second) {
        return 0;
    }
    return std::max(0, it->second->current_health);
}

/** Footprint cells of a seat's base, for distance math. Empty if the base is gone. */
std::vector<std::pair<int, int>> base_cells_for_seat(const GameState& game, const int seat)
{
    std::vector<std::pair<int, int>> cells;
    const std::string base_id = "base_p" + std::to_string(seat);
    const auto it = game.board.all_entities_map.find(base_id);
    if (it == game.board.all_entities_map.end() || !it->second) {
        return cells;
    }
    const Entity& base = *it->second;
    if (base.position) {
        const auto [bx, by] = *base.position;
        for (const auto& [dx, dy] : entity_shape_offsets(base)) {
            cells.emplace_back(bx + dx, by + dy);
        }
    } else {
        cells = base.occupied_positions;
    }
    return cells;
}

/** Chebyshev distance from a unit's footprint to the nearest cell of a base. */
int distance_to_base(const Entity& unit, const std::vector<std::pair<int, int>>& base_cells)
{
    int best = INT_MAX;
    for (const auto& [cx, cy] : base_cells) {
        best = std::min(best, min_chebyshev_entity_to_cell(unit, cx, cy));
    }
    return best == INT_MAX ? 999 : best;
}

/** True when a passive is a repeatable value engine - an automated effect, a
 *  reactive trigger (e.g. Tithe: gain energy on damage), or a live allied aura.
 *  These are the highest-value assets on the board (MTG threat-assessment rule #1),
 *  so a unit/structure carrying one is worth a premium beyond its stats. */
bool passive_is_engine(const PassiveAbilitySpec& p)
{
    return !p.automated_effect_key.empty() || !p.reactive_trigger.empty()
        || p.applies_to == "allied_units" || !p.passive_mechanic.empty();
}

/** Summary of a player's still-to-come cards (draw pile + reserves). The bot does not
 *  know the *order*, but it knows the *composition* - how many cards are left and how
 *  much total impact (energy cost as a value proxy) they represent. This is the
 *  "what's coming next" model: a deep, expensive remaining deck is latent power. */
struct DeckOutlook {
    int count{0};
    double value{0.0};
};

/** A seat's army cohesion - how massed its units are. For each unit, count nearby
 *  friendly units (Chebyshev ≤ 2, capped at 3) and sum. A clustered army scores higher
 *  than the same units scattered, so the evaluator prefers formations over piecemeal
 *  spread. O(units^2) but units are few. */
double army_cohesion(const GameState& game, const int seat)
{
    std::vector<std::pair<int, int>> cells;
    for (const auto& [_, ent] : game.board.all_entities_map) {
        if (!ent || !ent->owner || *ent->owner != seat || ent->current_health <= 0
            || !entity_is_board_unit(*ent)) {
            continue;
        }
        if (ent->position) {
            cells.push_back(*ent->position);
        } else if (!ent->occupied_positions.empty()) {
            cells.push_back(ent->occupied_positions.front());
        }
    }
    double cohesion = 0.0;
    for (std::size_t i = 0; i < cells.size(); ++i) {
        int neighbors = 0;
        for (std::size_t j = 0; j < cells.size(); ++j) {
            if (i == j) {
                continue;
            }
            const int d = std::max(std::abs(cells[i].first - cells[j].first),
                std::abs(cells[i].second - cells[j].second));
            if (d <= 2) {
                ++neighbors;
            }
        }
        cohesion += std::min(neighbors, 3);
    }
    return cohesion;
}

DeckOutlook summarize_remaining_deck(const GameState& game, const int seat)
{
    DeckOutlook out;
    const auto it = game.players_decks.find(seat);
    if (it == game.players_decks.end()) {
        return out;
    }
    const Deck& deck = it->second;
    const auto tally = [&](const std::vector<CardInstanceId>& zone) {
        for (const CardInstanceId id : zone) {
            ++out.count;
            if (const CardInstance* inst = deck.pool.try_get(id)) {
                if (const CardDefinition* def = try_get_card_definition_ptr(inst->definition_id)) {
                    out.value += static_cast<double>(std::max(1, definition_total_energy_cost(*def)));
                    continue;
                }
            }
            out.value += 1.0;  // unknown card still counts as one future play
        }
    };
    tally(deck.deck);
    tally(deck.reserves);
    return out;
}

}  // namespace

int unit_nominal_attack(const Unit& u)
{
    const int melee = std::max(0, u.melee_damage);
    const int ranged = u.is_ranged ? std::max(0, u.ranged_damage) : 0;
    return std::max(melee, ranged);
}

double card_engine_future_value(const CardDefinition& def)
{
    if (!def.unit) {
        return 0.0;  // only deployed units/structures carry board passives
    }
    double value = 0.0;
    const auto tally = [&](const PassiveAbilitySpec& p) {
        if (!p.is_negative && passive_is_engine(p)) {
            value += 1.0;  // one "engine passive" worth of future advantage
        }
    };
    for (const PassiveAbilitySpec& p : def.unit->passive_abilities) {
        tally(p);
    }
    for (const std::string& id : def.unit->passive_ability_ids) {
        PassiveAbilitySpec p;
        if (try_get_passive_from_catalog(id, p)) {
            tally(p);
        }
    }
    return value;
}

double piece_value(const Entity& ent)
{
    // Survivability: current HP is the material that must be chewed through.
    double value = static_cast<double>(std::max(0, ent.current_health));

    if (const auto* u = dynamic_cast<const Unit*>(&ent)) {
        value += static_cast<double>(unit_nominal_attack(*u));
        // Mobility: a faster unit threatens more of the board and reaches objectives
        // sooner - the chess "activity" premium.
        value += static_cast<double>(std::max(0, u->movement + u->aura_bonus_movement)) * 0.5;
        // Reach: ranged units project force without exposing themselves to counters.
        if (u->is_ranged) {
            value += static_cast<double>(std::max(0, u->ranged_range + u->bonus_ranged_length)) * 0.6;
        }
    }

    // Pending "next attack/ability" damage boosts are stored value about to be spent.
    value += static_cast<double>(std::max(0, entity_effect_amount(ent, "next_damage_bonus")));

    // Evasion / reach / initiative keywords make a unit disproportionately valuable
    // because it dodges trades or strikes first. Flat premia keep this cheap.
    static const char* const kPremiumKeywords[] = {
        "flying", "trueshot", "first_strike", "evasive", "lifesteal", "base_breaker",
        "taunt",  // control: forces enemies to engage it, shielding the softer units behind
    };
    for (const char* kw : kPremiumKeywords) {
        if (entity_has_attribute(ent, kw)) {
            value += 2.0;
        }
    }
    value += static_cast<double>(std::max(0, entity_effect_amount(ent, "armor")));

    // Activated abilities are optionality - each usable ability is a lever the unit
    // brings that a vanilla body does not.
    value += static_cast<double>(ent.activated_abilities.size()) * 1.5;

    // Passives: engines (Tithe/spawners/auras/mortar) are worth far more than their
    // stat line. A turret that spawns a unit every turn must not be traded for a token.
    for (const PassiveAbilitySpec& passive : ent.passive_abilities) {
        if (entity_passive_is_suppressed(ent, passive.key) || passive.is_negative) {
            continue;
        }
        value += passive_is_engine(passive) ? 4.0 : 1.0;
    }

    // ── Status effects. These apply to whoever owns the entity, so the own-minus-enemy
    // material differential correctly makes a debuffed enemy good for us and a debuffed
    // ally bad. (Boost = next_damage_bonus, and armor, are already added above.)
    // Debuffs - reduce effective worth:
    const int overload = entity_effect_amount(ent, "overload");
    if (overload > 0) {
        value -= static_cast<double>(overload) * 1.5;  // stacks are pending self-damage
        if (overload >= kOverloadExplosionThreshold - 1) {
            value -= 4.0;  // one more stack detonates for 5 to self + adjacent
        }
    }
    // Damage-over-time ≈ HP already spoken for.
    value -= static_cast<double>(std::max(0, entity_effect_amount(ent, "bleed")));
    value -= static_cast<double>(std::max(0, entity_effect_amount(ent, "fire")));
    value -= static_cast<double>(std::max(0, entity_effect_amount(ent, "poison")));
    if (entity_is_stunned(ent)) {
        value -= 4.0;   // cannot act at all this turn - a tempo-dead body
    }
    if (entity_is_silenced(ent)) {
        value -= 2.0;   // keywords and activated abilities switched off
    }
    if (entity_is_jammed(ent)) {
        value -= 1.5;   // activated abilities blocked
    }
    // Buffs beyond boost/armor:
    if (entity_effect_amount(ent, "evasive") > 0) {
        value += 3.0;   // 50% dodge makes it hard to remove
    }

    return std::max(0.0, value);
}

int distance_cell_to_own_base(const GameState& game, const int player_id, const int x, const int y)
{
    int best = 999;
    for (const auto& [_, ent] : game.board.all_entities_map) {
        if (!ent || !ent->owner || !entity_is_base(*ent) || teams_hostile(game, player_id, *ent->owner)) {
            continue;
        }
        best = std::min(best, min_chebyshev_entity_to_cell(*ent, x, y));
    }
    return best;
}

namespace {

int unit_reach_for_base_threat(const Unit& u)
{
    int reach = std::max(1, u.melee_range);
    if (u.attack_type == AttackType::Ranged || u.attack_type == AttackType::Hybrid) {
        reach = std::max(reach, u.ranged_range + u.bonus_ranged_length);
    }
    return reach;
}

int base_effective_ranged_reach(const Unit& base)
{
    const PassiveStatGrant temp = temporary_stat_grants_for_entity(base);
    return base.ranged_range + base.bonus_ranged_length + temp.bonus_ranged_range;
}

}  // namespace

bool enemy_threatens_own_base(const GameState& game, const int player_id, const Entity& enemy)
{
    std::pair<int, int> from;
    if (enemy.position) {
        from = *enemy.position;
    } else if (!enemy.occupied_positions.empty()) {
        from = enemy.occupied_positions.front();
    } else {
        return false;
    }
    const auto* u = dynamic_cast<const Unit*>(&enemy);
    const int reach = u ? unit_reach_for_base_threat(*u) : 1;
    return distance_cell_to_own_base(game, player_id, from.first, from.second) <= reach + 1;
}

int own_base_defense_need(const GameState& game, const int player_id)
{
    int threat = 0;
    int defenders = 0;
    int base_hp = 0;
    for (const auto& [_, ent] : game.board.all_entities_map) {
        if (!ent || !ent->owner || ent->current_health <= 0) {
            continue;
        }
        const bool hostile = teams_hostile(game, player_id, *ent->owner);
        if (entity_is_base(*ent)) {
            if (!hostile) {
                base_hp += ent->current_health;
            }
            continue;
        }
        if (!entity_is_board_unit(*ent)) {
            continue;
        }
        if (hostile) {
            if (enemy_threatens_own_base(game, player_id, *ent)) {
                const auto* u = dynamic_cast<const Unit*>(ent.get());
                threat += u ? std::max(1, unit_nominal_attack(*u)) : 1;
            }
        } else {
            std::pair<int, int> at;
            if (ent->position) {
                at = *ent->position;
            } else if (!ent->occupied_positions.empty()) {
                at = ent->occupied_positions.front();
            } else {
                continue;
            }
            if (distance_cell_to_own_base(game, player_id, at.first, at.second) <= 2) {
                ++defenders;
            }
        }
    }
    if (threat == 0) {
        return 0;
    }
    if (base_hp > threat * 3) {
        return 0;
    }
    if (defenders >= 1) {
        threat /= 3;
    }
    return threat;
}

double base_turret_buff_kill_value(const GameState& game, const int player_id, const int bonus_damage,
    const int bonus_range)
{
    constexpr double kHighValueThreshold = 18.0;
    const std::string base_id = "base_p" + std::to_string(player_id);
    const auto base_it = game.board.all_entities_map.find(base_id);
    if (base_it == game.board.all_entities_map.end() || !base_it->second) {
        return 0.0;
    }
    const auto base_unit = std::dynamic_pointer_cast<Unit>(base_it->second);
    if (!base_unit) {
        return 0.0;
    }
    const DamageRange dmg = unit_effective_ranged_damage_range(*base_unit);
    const int atk_max = dmg.max + std::max(0, bonus_damage);
    const int reach_without = base_effective_ranged_reach(*base_unit);
    const int reach_with = reach_without + std::max(0, bonus_range);
    double best = 0.0;
    for (const auto& [_, ent] : game.board.all_entities_map) {
        if (!ent || !ent->owner || ent->current_health <= 0 || !entity_is_board_unit(*ent)) {
            continue;
        }
        if (!teams_hostile(game, player_id, *ent->owner)) {
            continue;
        }
        int dist = 999;
        if (!ent->occupied_positions.empty()) {
            for (const auto& [wx, wy] : ent->occupied_positions) {
                dist = std::min(dist, min_chebyshev_entity_to_cell(*base_it->second, wx, wy));
            }
        } else if (ent->position) {
            dist = min_chebyshev_entity_to_cell(*base_it->second, ent->position->first, ent->position->second);
        } else {
            continue;
        }
        if (bonus_range > 0) {
            if (dist <= reach_without || dist > reach_with) {
                continue;
            }
        } else if (dist > reach_without) {
            continue;
        }
        const int effective_hp = ent->current_health + std::max(0, armor_value(*ent));
        if (atk_max < effective_hp) {
            continue;
        }
        const double tv = piece_value(*ent);
        if (tv >= kHighValueThreshold) {
            best = std::max(best, tv);
        }
    }
    return best;
}

namespace {

// Per-turn "hold value" of controlling each permanent objective tile, in the same abstract
// points as piece_value (a good unit ≈ 15-40). Objectives are key squares that generate
// value each turn you hold them uncontested (all come online at kObjectiveActivationRound):
//  - scanner: a scan - peek the top of your deck and optionally discard from it, keeping the
//    rest. It does NOT draw an extra card; it is pure selection / draw-quality, so it is the
//    weakest objective.
//  - omni_energy: +1 floating omni per turn - a compounding resource ramp (a real pip), worth
//    clearly more than the scanner's soft selection.
//  - aether (center): escalating direct damage to the nearest enemy base - the win
//    condition, the strongest objective, but the holder takes the same escalating damage.
// To teach the bot a new objective type, add one entry to enumerate_bot_objectives.
constexpr double kScannerHoldValue = 4.0;
constexpr double kOmniHoldValue = 10.0;
constexpr double kAetherHoldValue = 20.0;

struct BotObjective {
    const std::vector<std::pair<int, int>>* cells;
    int home_seat;      // scanner/omni: the tile's owner (fires for that seat's ENEMY); 0 = neutral (aether)
    double hold_value;
    bool self_damages;  // aether: the controller is damaged too
    bool active;        // currently generates value (all objectives gated to round >= activation)
};

std::vector<BotObjective> enumerate_bot_objectives(const GameState& game)
{
    // Every objective tile comes online together at the activation round.
    const bool active = game.turn_manager.round_number >= kObjectiveActivationRound;
    std::vector<BotObjective> out;
    out.reserve(game.scanner_clusters_.size() + game.omni_energy_clusters_.size() + game.aether_clusters_.size());
    for (const ScannerClusterState& c : game.scanner_clusters_) {
        out.push_back({&c.cells, c.home_seat, kScannerHoldValue, false, active});
    }
    for (const OmniEnergyClusterState& c : game.omni_energy_clusters_) {
        out.push_back({&c.cells, c.home_seat, kOmniHoldValue, false, active});
    }
    for (const AetherClusterState& c : game.aether_clusters_) {
        out.push_back({&c.cells, 0, kAetherHoldValue, true, active});
    }
    return out;
}

bool cell_in_cells(const std::vector<std::pair<int, int>>& cells, const int x, const int y)
{
    for (const auto& [cx, cy] : cells) {
        if (cx == x && cy == y) {
            return true;
        }
    }
    return false;
}

// Seats of control-eligible units currently standing on a cluster's cells.
std::vector<int> control_owner_seats_on(const GameState& game, const std::vector<std::pair<int, int>>& cells)
{
    std::vector<int> owners;
    for (const auto& [_, ent] : game.board.all_entities_map) {
        if (!ent || ent->current_health <= 0 || !ent->owner) {
            continue;
        }
        if (!entity_counts_for_aether_control(*ent) || !entity_on_cluster_cells(*ent, cells)) {
            continue;
        }
        owners.push_back(*ent->owner);
    }
    return owners;
}

// The team in sole (uncontested) control of a cluster right now, or nullopt if the cluster is
// empty or contested by mutually-hostile owners. Mirrors the engine's control resolution.
std::optional<int> sole_controlling_team(const GameState& game, const std::vector<std::pair<int, int>>& cells)
{
    const std::vector<int> owners = control_owner_seats_on(game, cells);
    if (owners.empty()) {
        return std::nullopt;
    }
    for (std::size_t i = 0; i < owners.size(); ++i) {
        for (std::size_t j = i + 1; j < owners.size(); ++j) {
            if (teams_hostile(game, owners[i], owners[j])) {
                return std::nullopt;  // contested - no one fires
            }
        }
    }
    return game.team_of_seat(owners.front());
}

int hostiles_near_objective_cluster(const GameState& game, const int seat,
    const std::vector<std::pair<int, int>>& cells, const int radius)
{
    std::unordered_set<std::string> seen;
    for (const auto& [cx, cy] : cells) {
        for (const auto& [_, ent] : game.board.all_entities_map) {
            if (!ent || !ent->owner || ent->current_health <= 0 || !entity_is_board_unit(*ent)) {
                continue;
            }
            if (!teams_hostile(game, seat, *ent->owner)) {
                continue;
            }
            if (min_chebyshev_entity_to_cell(*ent, cx, cy) <= radius) {
                seen.insert(ent->entity_id);
            }
        }
    }
    return static_cast<int>(seen.size());
}

}  // namespace

double objective_control_value(const GameState& game, const int root_player)
{
    const int root_team = game.team_of_seat(root_player);
    double own = 0.0;
    double enemy = 0.0;
    for (const BotObjective& obj : enumerate_bot_objectives(game)) {
        if (!obj.active) {
            continue;
        }
        const std::optional<int> team = sole_controlling_team(game, *obj.cells);
        if (!team) {
            continue;
        }
        // Scanner/omni only pay a team HOSTILE to the tile's home seat - you fire off the
        // ENEMY's tile; sitting on your own tile grants nothing.
        if (obj.home_seat > 0 && *team == game.team_of_seat(obj.home_seat)) {
            continue;
        }
        if (*team == root_team) {
            own += obj.hold_value;
        } else {
            enemy += obj.hold_value;  // any other (hostile) team holding it counts against us
        }
    }
    return own - enemy;
}

double objective_cell_capture_value(const GameState& game, const int seat, const Entity& mover, const int x, const int y)
{
    if (!entity_counts_for_aether_control(mover)) {
        return 0.0;  // large units / bases can't establish control
    }
    const int seat_team = game.team_of_seat(seat);
    for (const BotObjective& obj : enumerate_bot_objectives(game)) {
        if (!cell_in_cells(*obj.cells, x, y)) {
            continue;
        }

        // Scanner/omni: standing on our OWN tile pays nothing - the only value is denying an
        // enemy who is currently firing off it by contesting the cell.
        if (obj.home_seat > 0 && seat_team == game.team_of_seat(obj.home_seat)) {
            for (const int owner : control_owner_seats_on(game, *obj.cells)) {
                if (teams_hostile(game, seat, owner)) {
                    return obj.hold_value * 0.7;  // contest to deny
                }
            }
            return 0.0;
        }

        // Current control units on the cluster by side (the mover is not on the cell yet).
        int allies_on = 0;
        int hostiles_on = 0;
        for (const int owner : control_owner_seats_on(game, *obj.cells)) {
            if (game.team_of_seat(owner) == seat_team) {
                ++allies_on;
            } else if (teams_hostile(game, seat, owner)) {
                ++hostiles_on;
            }
        }

        double v = obj.hold_value;
        if (hostiles_on > 0) {
            // A hostile holds/contests the cluster. Committing one body to contest denies their
            // control; piling further bodies into an already-contested cluster gains nothing
            // (this is what stops a multi-cell objective from becoming a stalemate scrum).
            if (allies_on > 0) {
                return 0.0;
            }
            v *= 0.6;  // deny the enemy's control
        } else if (allies_on > 0) {
            if (obj.self_damages) {
                // Safe hold: don't add bodies that will eat the burst. Reinforce only when
                // hostiles are on the tiles or massing nearby to retake.
                const int threat = hostiles_on + hostiles_near_objective_cluster(game, seat, *obj.cells, 3);
                if (threat == 0) {
                    return 0.0;
                }
                v *= (hostiles_on > 0) ? 0.45 : 0.3;  // contest pile-on vs preemptive block
            } else {
                v *= 0.25;  // we already hold it uncontested - reinforcement only
            }
        }

        // Before payout round: stage into position (full contest from round 3), discounted so
        // inert tiles never outrank real development in rounds 1–2.
        if (!obj.active) {
            v *= (game.turn_manager.round_number >= kObjectiveActivationRound - 1) ? 0.6 : 0.35;
        }

        // Aether damages every entity on the tiles each burst. Never park a unit that
        // would die to the next fire; only bodies that survive hold the center.
        if (obj.self_damages) {
            if (aether_would_kill_entity(game, seat, mover, x, y)) {
                return 0.0;
            }

            const double pv = piece_value(mover);
            const int burst = std::max(1, aether_predicted_burst_damage(game, seat, x, y));

            const int soak_buffer = burst + std::max(2, burst);
            if (mover.current_health < soak_buffer) {
                if (pv >= 8.0) {
                    return hostiles_on > 0 ? std::min(v * 0.15, 2.0) : 0.0;
                }
                v *= 0.35;
            }

            const double hp_cost = static_cast<double>(burst) * (1.2 + pv / 12.0);
            v = std::max(0.0, v - hp_cost);

            if (pv >= 14.0) {
                v *= 0.2;
            }
            if (pv >= 18.0) {
                return hostiles_on > 0 ? std::min(v, 1.0) : 0.0;
            }
        }
        return v;
    }
    return 0.0;
}

std::optional<double> terminal_value_for_player(const GameState& game, const int root_player)
{
    if (!is_match_over(game)) {
        return std::nullopt;
    }
    const std::optional<int> winner = winner_seat(game);
    if (!winner.has_value()) {
        return 0.0;
    }
    if (game.team_of_seat(*winner) == game.team_of_seat(root_player)) {
        return 1.0;
    }
    return -1.0;
}

namespace {

// Feature order is the contract between the C++ model, the self-play logger, and the
// offline trainer. Append new features at the END and add a matching default weight.
const std::vector<std::string>& value_feature_names()
{
    static const std::vector<std::string> names = {
        "base_diff", "material_diff", "advancement", "king_safety", "base_threat_frac",
        "hand_diff", "float_diff", "flux_diff", "cohesion_diff", "deck_count_diff",
        "deck_value", "press_advancement", "press_enemy_base", "race_pressure",
        "objective_control",
    };
    return names;
}

// Defaults reproduce the previous hand-tuned blend, plus a modest weight for the new
// lethal-race term and the objective-control term (feature[i] * weight[i]).
std::vector<double> default_value_weights()
{
    return {
        1.0 / 30.0, 0.004, 0.010, -0.008, -0.15,
        0.03, 0.02, 0.006, 0.004, 0.006,
        0.0006, 0.015, -0.004, 0.03,
        0.006,
    };
}

bool read_weight_file(const std::string& path, const std::size_t expected, std::vector<double>& out)
{
    std::ifstream in(path);
    if (!in) {
        return false;
    }
    std::vector<double> values;
    std::string line;
    while (std::getline(in, line)) {
        if (const auto hash = line.find('#'); hash != std::string::npos) {
            line = line.substr(0, hash);
        }
        std::istringstream ss(line);
        std::string tok;
        std::string last;
        while (ss >> tok) {
            last = tok;  // allow "name value" or a bare number - take the last token
        }
        if (last.empty()) {
            continue;
        }
        try {
            values.push_back(std::stod(last));
        } catch (...) {
            return false;
        }
    }
    if (values.size() != expected) {
        return false;
    }
    out = std::move(values);
    return true;
}

std::vector<double>& mutable_value_weights()
{
    static std::vector<double> weights = []() {
        std::vector<double> w = default_value_weights();
        if (const char* env = std::getenv("TACTICS_BOT_WEIGHTS")) {
            std::vector<double> loaded;
            if (read_weight_file(env, w.size(), loaded)) {
                w = std::move(loaded);
            }
        }
        return w;
    }();
    return weights;
}

/** Compute the engineered feature vector (see value_feature_names) for a position. */
std::vector<double> compute_value_features(const GameState& game, const int root_player)
{
    // ── Axis 1: card / economy resources (own - enemy) ──────────────────────────
    const int own_base = base_hp_for_seat(game, root_player);
    int enemy_base = 0;
    int own_hand = 0;
    int enemy_hand = 0;

    if (const auto* hand = game.players_hands.at(root_player)) {
        own_hand = static_cast<int>(hand->size());
    }
    for (const int seat : game.turn_manager.players) {
        if (seat == root_player || !teams_hostile(game, root_player, seat)) {
            continue;
        }
        enemy_base += base_hp_for_seat(game, seat);
        if (const auto* hand = game.players_hands.at(seat)) {
            enemy_hand += static_cast<int>(hand->size());
        }
    }

    int own_float = 0;
    int enemy_float = 0;
    for (const int seat : game.turn_manager.players) {
        const bool is_own = (seat == root_player);
        if (!is_own && !teams_hostile(game, root_player, seat)) {
            continue;
        }
        const auto it = game.turn_manager.player_energy.find(seat);
        if (it == game.turn_manager.player_energy.end()) {
            continue;
        }
        int sum = 0;
        for (const auto& [_, amount] : it->second) {
            sum += std::max(0, amount);
        }
        (is_own ? own_float : enemy_float) += sum;
    }

    const int own_flux = player_flux_energy_generated_total(game.turn_manager, root_player);
    int enemy_flux = 0;
    for (const int seat : game.turn_manager.players) {
        if (seat != root_player && teams_hostile(game, root_player, seat)) {
            enemy_flux += player_flux_energy_generated_total(game.turn_manager, seat);
        }
    }

    // Army cohesion: a massed force is worth more than the same units scattered.
    const double own_cohesion = army_cohesion(game, root_player);
    double enemy_cohesion = 0.0;
    for (const int seat : game.turn_manager.players) {
        if (seat != root_player && teams_hostile(game, root_player, seat)) {
            enemy_cohesion += army_cohesion(game, seat);
        }
    }

    // Deck outlook: cards still to come are latent card advantage. We know our own
    // deck's composition (count + total impact); for the opponent we can only see how
    // many cards they have left, so the differential uses counts and our own deck's
    // quality is an extra self bonus.
    const DeckOutlook own_deck = summarize_remaining_deck(game, root_player);
    int enemy_deck_count = 0;
    for (const int seat : game.turn_manager.players) {
        if (seat != root_player && teams_hostile(game, root_player, seat)) {
            enemy_deck_count += summarize_remaining_deck(game, seat).count;
        }
    }

    // ── Axis 2: positional / tactical (piece values + advancement + king safety) ─
    // Distances are normalized by the board span so the terms stay bounded.
    const BoardCellBounds bounds = game.board.cell_bounds();
    const double span = std::max(1, std::max(bounds.span_x(), bounds.span_y()));

    // Precompute each hostile base's cells once (usually just one enemy base).
    const std::vector<std::pair<int, int>> own_base_cells = base_cells_for_seat(game, root_player);
    std::vector<std::vector<std::pair<int, int>>> enemy_base_cell_sets;
    for (const int seat : game.turn_manager.players) {
        if (seat != root_player && teams_hostile(game, root_player, seat)) {
            enemy_base_cell_sets.push_back(base_cells_for_seat(game, seat));
        }
    }

    double own_material = 0.0;
    double enemy_material = 0.0;
    double own_advancement = 0.0;   // own units pressing toward an enemy base
    double king_safety_pressure = 0.0;  // enemy units massing near our base
    int base_threat = 0;            // total enemy attack that can already reach our base
    int own_base_threat = 0;        // total own attack that can already reach an enemy base

    for (const auto& [_, ent] : game.board.all_entities_map) {
        if (!ent || !ent->owner || ent->current_health <= 0) {
            continue;
        }
        // Count both units and owned structures (turrets, Shock Wire, Forward Supply):
        // a spawner or energy engine is a board asset and often a bigger threat than a
        // unit. Bases are handled by the separate base-HP term; terrain is unowned.
        if (!entity_is_board_unit(*ent) && !entity_is_building(*ent)) {
            continue;
        }
        const bool is_own = (*ent->owner == root_player);
        const bool is_enemy = !is_own && teams_hostile(game, root_player, *ent->owner);
        if (!is_own && !is_enemy) {
            continue;  // neutral / ally-of-neither - ignore
        }

        const double value = piece_value(*ent);
        (is_own ? own_material : enemy_material) += value;

        if (is_own) {
            // Advancement: reward our units for closing on the nearest enemy base.
            int nearest = 999;
            for (const auto& cells : enemy_base_cell_sets) {
                if (!cells.empty()) {
                    nearest = std::min(nearest, distance_to_base(*ent, cells));
                }
            }
            if (nearest < 999) {
                own_advancement += std::max(0.0, (span - static_cast<double>(nearest)) / span);
            }
            // Our firepower already in range of an enemy base - the offence half of the
            // lethal-race clock.
            if (const auto* u = dynamic_cast<const Unit*>(ent.get())) {
                int reach = std::max(1, u->melee_range);
                if (u->attack_type == AttackType::Ranged || u->attack_type == AttackType::Hybrid) {
                    reach = std::max(reach, u->ranged_range + u->bonus_ranged_length);
                }
                if (nearest <= reach) {
                    own_base_threat += unit_nominal_attack(*u);
                }
            }
        } else {
            // King safety: an enemy unit near our base is a threat weighted by its punch.
            if (!own_base_cells.empty()) {
                const int d = distance_to_base(*ent, own_base_cells);
                own_advancement -= std::max(0.0, (span - static_cast<double>(d)) / span);
                if (d <= 2) {
                    const auto* u = dynamic_cast<const Unit*>(ent.get());
                    const int atk = u ? unit_nominal_attack(*u) : 1;
                    king_safety_pressure += static_cast<double>(std::max(1, atk));
                }
                // Lethal detection: can this enemy already strike our base (within its
                // attack reach)? Sum that firepower - if it approaches our base HP, we are
                // in danger of losing and must defend or race.
                if (const auto* u = dynamic_cast<const Unit*>(ent.get())) {
                    int reach = std::max(1, u->melee_range);
                    if (u->attack_type == AttackType::Ranged || u->attack_type == AttackType::Hybrid) {
                        reach = std::max(reach, u->ranged_range + u->bonus_ranged_length);
                    }
                    if (d <= reach) {
                        base_threat += unit_nominal_attack(*u);
                    }
                }
            }
        }
    }

    // ── Assemble the feature vector (order must match value_feature_names()). The two
    // press-the-advantage terms carry their nonlinear gate (active only with a material
    // lead) *inside* the feature value, so the model stays linear in the weights and the
    // default weights below reproduce the old blend exactly.
    const double material_lead = own_material - enemy_material;
    const double lead_norm = material_lead > 0.0 ? std::clamp(material_lead / 80.0, 0.0, 1.0) : 0.0;
    const double base_threat_frac = (base_threat > 0 && own_base > 0)
        ? std::min(1.0, static_cast<double>(base_threat) / static_cast<double>(own_base))
        : 0.0;

    // Lethal-race clock: turns-to-kill ≈ base HP / firepower-in-range. race_pressure > 0
    // means we destroy their base first (press / race); < 0 means we are being out-raced
    // and should stabilise. Bounded so a lopsided clock does not swamp the material terms.
    const double my_turns_to_kill = own_base_threat > 0
        ? static_cast<double>(enemy_base) / static_cast<double>(own_base_threat)
        : 99.0;
    const double their_turns_to_kill = base_threat > 0
        ? static_cast<double>(own_base) / static_cast<double>(base_threat)
        : 99.0;
    double race_pressure = std::clamp(their_turns_to_kill - my_turns_to_kill, -6.0, 6.0);
    if (trailing_on_sudden_death_base_hp(game, root_player)) {
        race_pressure = std::clamp(
            race_pressure + static_cast<double>(sudden_death_base_hp_urgency(game, root_player)) / 50.0,
            -6.0, 6.0);
    }

    return {
        static_cast<double>(own_base - enemy_base),            // base_diff        (× 1/30)
        own_material - enemy_material,                          // material_diff    (× 0.004)
        own_advancement,                                        // advancement      (× 0.010)
        king_safety_pressure,                                   // king_safety      (× -0.008)
        base_threat_frac,                                       // base_threat_frac (× -0.15)
        static_cast<double>(own_hand - enemy_hand),            // hand_diff         (× 0.03)
        static_cast<double>(own_float - enemy_float),          // float_diff        (× 0.02)
        static_cast<double>(own_flux - enemy_flux),            // flux_diff         (× 0.006)
        own_cohesion - enemy_cohesion,                          // cohesion_diff    (× 0.004)
        static_cast<double>(own_deck.count - enemy_deck_count),// deck_count_diff  (× 0.006)
        own_deck.value,                                         // deck_value       (× 0.0006)
        lead_norm * own_advancement,                            // press_advancement(× 0.015)
        lead_norm * static_cast<double>(enemy_base),            // press_enemy_base (× -0.004)
        race_pressure,                                          // race_pressure    (× 0.03)
        objective_control_value(game, root_player),             // objective_control(× 0.006)
    };
}

}  // namespace

const std::vector<std::string>& bot_value_feature_names()
{
    return value_feature_names();
}

std::vector<double> bot_value_features(const GameState& game, const int root_player)
{
    return compute_value_features(game, root_player);
}

const std::vector<double>& bot_value_weights()
{
    return mutable_value_weights();
}

void set_bot_value_weights(std::vector<double> weights)
{
    if (weights.size() == value_feature_names().size()) {
        mutable_value_weights() = std::move(weights);
    }
}

bool load_bot_value_weights(const std::string& path)
{
    std::vector<double> loaded;
    if (!read_weight_file(path, value_feature_names().size(), loaded)) {
        return false;
    }
    mutable_value_weights() = std::move(loaded);
    return true;
}

double HeuristicEvaluator::evaluate(const GameState& game, const int root_player) const
{
    if (const auto terminal = terminal_value_for_player(game, root_player)) {
        return *terminal;
    }
    // Linear value model: value = clamp( features · weights ). Default weights reproduce
    // the hand-tuned heuristic; a trained weight file (self-play) can replace them.
    const std::vector<double>& weights = mutable_value_weights();
    const std::vector<double> features = compute_value_features(game, root_player);
    double score = 0.0;
    for (std::size_t i = 0; i < features.size() && i < weights.size(); ++i) {
        score += features[i] * weights[i];
    }
    return std::clamp(score, -1.0, 1.0);
}

}  // namespace tactics::bot
