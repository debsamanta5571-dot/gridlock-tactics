#pragma once

#include "tactics/bot/bot_policy.hpp"
#include "tactics/bot/mcts_config.hpp"

namespace tactics::bot {

/** Monte Carlo tree search using score_bot_action as the prior and rollout policy. */
class MctsPolicy final : public IBotPolicy {
public:
    explicit MctsPolicy(MctsConfig config = {});

    /** Runs MCTS and returns the most-visited legal action. */
    BotAction choose(const GameState& game, const BotObservation& obs, const std::vector<BotAction>& legal,
        std::mt19937& rng) const override;

private:
    MctsConfig config_;
};

}  // namespace tactics::bot