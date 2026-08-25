#pragma once

#include "tactics/common/types.hpp"
#include "tactics/core/game_state.hpp"

#include <cstddef>
#include <optional>

namespace tactics::bot {

struct BotObservation {
    TurnPhase phase{TurnPhase::Energy};
    std::optional<int> active_player;
    std::optional<int> reaction_priority_player;
    int round_number{1};
    bool match_over{false};
    std::optional<int> winner_seat;
    int acting_player{0};
    std::size_t legal_action_count{0};
    /** v2: richer policy context */
    int hand_size{0};
    int reserves_size{0};
    int own_base_hp{0};
    int enemy_base_hp{0};
    int pending_attack_count{0};
    bool in_reaction_window{false};
    bool can_pass_reaction_window{false};
    bool must_respond_reaction_window{false};
    bool combat_viz_paused{false};
    /** v3: energy economy context (see bot_economy.hpp) */
    int float_energy_total{0};
    int untapped_zone_pips{0};
    int total_spendable{0};
    int reflex_options{0};
    int channeled_spell_options{0};
    bool on_active_turn{false};
    bool should_reserve_reflex{false};
    /** v4: opponent threat assessment - how much interaction the enemy can represent.
     *  Cards in hand + available energy (float + untapped zones) they could spend on a
     *  reflex spell/ability during our turn. `opponent_can_react` is true when they have
     *  both, i.e. they may be holding a trick and we should not over-extend blindly. */
    int enemy_hand_size{0};
    int enemy_available_energy{0};
    bool opponent_can_react{false};
    /** v5: board material - summed piece value of living units per side. The policy
     *  presses when `own_board_value` clearly exceeds `enemy_board_value` (more material
     *  means you can afford trades and should force the game toward the enemy base). */
    int own_board_value{0};
    int enemy_board_value{0};
};

BotObservation build_bot_observation(const GameState& game, int acting_player, std::size_t legal_action_count);

}  // namespace tactics::bot