#pragma once

#include "tactics/bot/bot_action.hpp"
#include "tactics/bot/bot_observation.hpp"
#include "tactics/bot/mcts_config.hpp"
#include "tactics/core/game_state.hpp"

#include <memory>
#include <random>
#include <string>
#include <vector>

namespace tactics::bot {

/** Chooses one legal action for the bot. */
class IBotPolicy {
public:
    virtual ~IBotPolicy() = default;
    virtual BotAction choose(const GameState& game, const BotObservation& obs, const std::vector<BotAction>& legal,
        std::mt19937& rng) const = 0;
};

/** Picks uniformly among legal actions (debug / baseline). */
class RandomLegalPolicy final : public IBotPolicy {
public:
    BotAction choose(const GameState& game, const BotObservation& obs, const std::vector<BotAction>& legal,
        std::mt19937& rng) const override;
};

/** The shared action-scoring "brain": base-priority + combat (lethality, target value,
 *  counterattack risk, press-the-advantage) + formation/movement + energy economy.
 *  Higher is better. There is no longer a standalone greedy policy - MCTS is the only
 *  bot, and it uses this scorer as its search prior, its non-tactical decision-maker, and
 *  its rollout policy, so every heuristic improvement flows into MCTS. */
int score_bot_action(const GameState& game, const BotObservation& obs, const std::vector<BotAction>& legal,
    const BotAction& action);

/** Batch form: scores every legal action for `obs` while building the (shared) economy
 *  snapshot only once. Returned scores are index-aligned with `legal`. */
std::vector<int> score_bot_actions(const GameState& game, const BotObservation& obs,
    const std::vector<BotAction>& legal);

struct BotPolicyOptions {
    MctsConfig mcts{};
};

std::unique_ptr<IBotPolicy> make_bot_policy(const std::string& name, const BotPolicyOptions& options = {});

}  // namespace tactics::bot