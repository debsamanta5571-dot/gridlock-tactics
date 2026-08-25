#pragma once

#include "tactics/bot/bot_action.hpp"
#include "tactics/bot/bot_observation.hpp"
#include "tactics/core/game_state.hpp"

namespace tactics::bot {

/** Spendable energy and reflex/channeled option counts for TCG-style bot scoring. */
struct BotEconomySnapshot {
    int float_energy_total{0};
    int untapped_zone_pips{0};
    int total_spendable{0};
    int reflex_spell_options{0};
    int reflex_ability_options{0};
    int channeled_spell_options{0};
    int cheapest_reflex_cost{0};
    bool on_active_turn{false};
    /** Hold reflex mana on own turn when reflex plays exist and opponent may react. */
    bool should_reserve_reflex{false};
    int reflex_reserve_target{0};
    /** Bluff: hold a single pip of reaction-capable energy with cards in hand but no real
     *  reflex, so an opponent (who can only see our energy + hand count) plays around a
     *  trick we may not have. Bounded and gated to avoid tempo loss. */
    int bluff_reserve{0};
};

/** Spendable energy and reflex-reserve plan for the acting seat. */
BotEconomySnapshot build_bot_economy_snapshot(const GameState& game, const BotObservation& obs);

/** Hostile living entities hit by a directional spell/ability aim (0 when not directional). */
int directional_action_hostile_hits(const GameState& game, const BotAction& action, const std::string& effect_key,
    const std::map<std::string, int>& payload, const std::map<std::string, std::string>& string_payload);

/** Total pip cost for a bot action (0 when unknown or free). */
int bot_action_energy_cost(const GameState& game, const BotAction& action);

/**
 * TCG tempo / energy economy score layered on top of action-kind priority.
 * No blanket spell/ability penalties - opportunity cost, reflex reserve, curve, AoE patience.
 */
int score_bot_action_economy(const GameState& game, const BotObservation& obs, const BotEconomySnapshot& economy,
    const std::vector<BotAction>& legal, const BotAction& action);

}  // namespace tactics::bot