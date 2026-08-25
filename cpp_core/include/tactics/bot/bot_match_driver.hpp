#pragma once

#include "tactics/bot/bot_policy.hpp"
#include "tactics/bot/legal_action_generator.hpp"

#include "tactics/core/game_state.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace tactics::bot {

struct BotMatchDriverConfig {
    uint64_t rng_seed{1};
    int max_actions{20000};
    std::string policy_name{"random"};
    bool verbose{false};
    LegalActionGenLimits legal_limits{};
    BotPolicyOptions policy_options{};
    /** If set, append one CSV row per turn (`label,feat0,…`) of self-play training data:
     *  the value features from seat P1's perspective, labelled by the final result
     *  (1 win / 0 loss / 0.5 draw) for an offline trainer. */
    std::string feature_log_path{};
};

struct BotMatchResult {
    bool completed{false};
    std::optional<int> winner_seat;
    int actions_taken{0};
    std::string end_reason;
};

BotMatchResult run_bot_match(GameState& game, const BotMatchDriverConfig& config);

}  // namespace tactics::bot