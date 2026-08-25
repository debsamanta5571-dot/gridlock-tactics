#include "tactics/bot/bot_observation.hpp"

#include "tactics/bot/bot_economy.hpp"
#include "tactics/bot/bot_evaluator.hpp"
#include "tactics/bot/bot_match_outcome.hpp"
#include "tactics/core/board_target_policy.hpp"
#include "tactics/energy/energy_zone.hpp"
#include "tactics/entities/entity.hpp"

#include <algorithm>

namespace tactics::bot {
namespace {

int base_hp_for_seat(const GameState& game, const int seat)
{
    const std::string base_id = "base_p" + std::to_string(seat);
    const auto it = game.board.all_entities_map.find(base_id);
    if (it == game.board.all_entities_map.end() || !it->second) {
        return 0;
    }
    return it->second->current_health;
}

int hostile_base_hp(const GameState& game, const int acting_player)
{
    int total = 0;
    for (const int seat : game.turn_manager.players) {
        if (teams_hostile(game, acting_player, seat)) {
            total += base_hp_for_seat(game, seat);
        }
    }
    return total;
}

/** Energy a seat could spend right now: float + all untapped zone production. */
int available_energy_for_seat(const GameState& game, const int seat)
{
    int total = 0;
    if (const auto it = game.turn_manager.player_energy.find(seat); it != game.turn_manager.player_energy.end()) {
        for (const auto& [_, amount] : it->second) {
            total += std::max(0, amount);
        }
    }
    if (const auto zit = game.players_energy_zones.find(seat); zit != game.players_energy_zones.end()) {
        for (const EnergyZone& zone : zit->second) {
            for (const auto& [_, amount] : zone.available_auto_energy()) {
                total += std::max(0, amount);
            }
        }
    }
    return total;
}

}  // namespace

BotObservation build_bot_observation(const GameState& game, const int acting_player, const std::size_t legal_action_count)
{
    BotObservation obs;
    obs.phase = game.turn_manager.current_phase;
    obs.active_player = game.turn_manager.current_player();
    obs.reaction_priority_player = game.reaction_window_priority_player();
    obs.round_number = game.turn_manager.round_number;
    obs.match_over = is_match_over(game);
    obs.winner_seat = winner_seat(game);
    obs.acting_player = acting_player;
    obs.legal_action_count = legal_action_count;
    obs.combat_viz_paused = game.is_combat_visualization_paused();

    if (const auto* hand = game.players_hands.at(acting_player)) {
        obs.hand_size = static_cast<int>(hand->size());
    }
    const auto deck_it = game.players_decks.find(acting_player);
    if (deck_it != game.players_decks.end()) {
        obs.reserves_size = static_cast<int>(deck_it->second.reserves.size());
    }
    obs.own_base_hp = base_hp_for_seat(game, acting_player);
    obs.enemy_base_hp = hostile_base_hp(game, acting_player);
    obs.pending_attack_count = static_cast<int>(game.pending_attack_declarations().size());

    const auto phase = obs.phase;
    obs.in_reaction_window = phase == TurnPhase::SpellWindow || phase == TurnPhase::SecondSpellWindow
        || phase == TurnPhase::Defense || phase == TurnPhase::BonusDefense;
    if (obs.in_reaction_window) {
        if (phase == TurnPhase::SpellWindow || phase == TurnPhase::SecondSpellWindow) {
            obs.can_pass_reaction_window = game.can_pass_spell_window(acting_player);
        } else {
            obs.can_pass_reaction_window = game.can_pass_defense_window(acting_player);
        }
        obs.must_respond_reaction_window = !obs.can_pass_reaction_window;
    }

    const BotEconomySnapshot economy = build_bot_economy_snapshot(game, obs);
    obs.float_energy_total = economy.float_energy_total;
    obs.untapped_zone_pips = economy.untapped_zone_pips;
    obs.total_spendable = economy.total_spendable;
    obs.reflex_options = economy.reflex_spell_options + economy.reflex_ability_options;
    obs.channeled_spell_options = economy.channeled_spell_options;
    obs.on_active_turn = economy.on_active_turn;
    obs.should_reserve_reflex = economy.should_reserve_reflex;

    // Opponent threat assessment: how much interaction the enemy can represent.
    int enemy_hand = 0;
    int enemy_energy = 0;
    for (const int seat : game.turn_manager.players) {
        if (!teams_hostile(game, acting_player, seat)) {
            continue;
        }
        if (const auto* hand = game.players_hands.at(seat)) {
            enemy_hand += static_cast<int>(hand->size());
        }
        enemy_energy += available_energy_for_seat(game, seat);
    }
    obs.enemy_hand_size = enemy_hand;
    obs.enemy_available_energy = enemy_energy;

    // Board material: summed piece value of living units on each side.
    for (const auto& [_, ent] : game.board.all_entities_map) {
        if (!ent || !ent->owner || ent->current_health <= 0 || !entity_is_board_unit(*ent)) {
            continue;
        }
        const int value = static_cast<int>(piece_value(*ent));
        if (*ent->owner == acting_player) {
            obs.own_board_value += value;
        } else if (teams_hostile(game, acting_player, *ent->owner)) {
            obs.enemy_board_value += value;
        }
    }
    // They can only threaten a reflex answer if they have both a card to cast and the
    // energy to pay for it. Tapped-out or empty-handed opponents cannot punish an
    // over-commit - it is safe to press.
    obs.opponent_can_react = enemy_hand > 0 && enemy_energy > 0;

    return obs;
}

}  // namespace tactics::bot