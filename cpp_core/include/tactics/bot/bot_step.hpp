#pragma once

#include "tactics/bot/bot_action.hpp"
#include "tactics/bot/bot_policy.hpp"
#include "tactics/bot/bot_session.hpp"
#include "tactics/bot/legal_action_generator.hpp"
#include "tactics/core/game_state.hpp"

#include <random>
#include <string>

namespace tactics::bot {

struct BotStepResult {
    /** False when no seat should act (e.g. waiting on human combat-viz resume with no bot resume path). */
    bool attempted{false};
    bool ok{false};
    std::string message;
    BotAction chosen{};
    BotAction executed{};
};

/** Execute at most one bot action (policy choose + execute with driver fallbacks). */
BotStepResult step_bot_once(GameState& game, IBotPolicy& policy, BotSession& session, std::mt19937& rng,
    const LegalActionGenLimits& limits = {});

}  // namespace tactics::bot