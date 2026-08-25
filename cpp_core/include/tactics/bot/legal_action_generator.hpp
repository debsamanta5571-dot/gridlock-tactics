#pragma once

#include "tactics/bot/bot_action.hpp"
#include "tactics/core/game_state.hpp"

#include <vector>

namespace tactics::bot {

/** Optional caps for sandbox-scale catalogs (0 = unlimited). */
struct LegalActionGenLimits {
    std::size_t max_spell_actions{0};
    std::size_t max_ability_actions{0};
    std::size_t max_move_actions{0};
    /** When true, omit move_preview / confirm / cancel / rotate (Unreal pacing). */
    bool skip_move_actions{false};
};

/** Seat that should act next, or nullopt when only combat-viz resume is needed. */
std::optional<int> bot_acting_seat(const GameState& game);

/** Legal CLI-level actions the bot may take for player_id in the current phase. */
std::vector<BotAction> generate_legal_actions(GameState& game, int player_id,
    const LegalActionGenLimits& limits = {});

}  // namespace tactics::bot