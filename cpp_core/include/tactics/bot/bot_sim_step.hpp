#pragma once

#include "tactics/bot/bot_evaluator.hpp"
#include "tactics/bot/bot_policy.hpp"
#include "tactics/bot/bot_session.hpp"
#include "tactics/bot/legal_action_generator.hpp"
#include "tactics/core/game_state.hpp"

#include <random>

namespace tactics::bot {

struct BotSimStepConfig {
    LegalActionGenLimits legal_limits{};
    int max_playout_steps{120};
    /** Stop the rollout after this many turn-owner changes (0 = only the step cap). This is
     *  the multi-turn planning horizon: how many turns ahead a simulation looks before the
     *  static evaluator scores the resulting position. */
    int max_playout_turns{0};
};

/** Apply one bot action and auto-resume combat visualization when paused. */
bool apply_bot_action_checked(GameState& game, BotSession& session, const BotAction& action);

/**
 * Random (or custom) rollout from `game` until terminal, stuck, or step cap.
 * Returns value in [-1, 1] for `root_player`.
 */
double run_playout(GameState& game, int root_player, const IBotPolicy& rollout_policy, const IBotEvaluator& evaluator,
    std::mt19937& rng, const BotSimStepConfig& config);

}  // namespace tactics::bot