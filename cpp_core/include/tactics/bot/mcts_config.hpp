#pragma once

#include "tactics/bot/legal_action_generator.hpp"

namespace tactics::bot {

struct MctsConfig {
    // Strategic decisions (attacks, deploys, spells/abilities) get the multi-turn rollout
    // search - they are infrequent and worth the lookahead. The defaults below are tuned
    // for playable speed (~40 s/self-play-game headless, sub-second per real decision): the
    // heuristic rollout is heavy, so deeper horizons (turns 2–4) - while supported and
    // useful for offline analysis via the CLI - are too slow for the default.
    int max_simulations{8};
    /** Rollout depth after expansion (bot-action steps); hard safety cap on `max_playout_turns`. */
    int max_playout_steps{40};
    /** Multi-turn planning horizon: stop each rollout after this many turn-owner changes, so
     *  a simulation looks ~this many turns ahead (a full turn boundary crossed = real
     *  forward lookahead, not a static one-position score). Raise via --mcts-playout-turns. */
    int max_playout_turns{1};
    // Movement decisions are far more frequent, so they use a cheaper *shallow* search
    // (this many sims, static-evaluator leaves, no rollout). Positioning is still looked
    // ahead via the tree, but without the cost of a multi-turn rollout on every step  - 
    // otherwise searching every move with rollouts is computationally explosive.
    int light_max_simulations{8};
    double exploration_constant{1.4};
    /** Cap candidate actions per tree node (0 = unlimited). */
    std::size_t max_branching{20};
    LegalActionGenLimits legal_limits{};
};

}  // namespace tactics::bot