#pragma once

#include "tactics/bot/bot_action.hpp"
#include "tactics/bot/bot_session.hpp"
#include "tactics/core/game_state.hpp"

namespace tactics::bot {

struct BotActionResult {
    bool ok{false};
    std::string message;
};

BotActionResult execute_bot_action(GameState& game, BotSession& session, const BotAction& action);

}  // namespace tactics::bot