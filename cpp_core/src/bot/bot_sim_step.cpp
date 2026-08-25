#include "tactics/bot/bot_sim_step.hpp"

#include "tactics/bot/action_executor.hpp"
#include "tactics/bot/bot_match_outcome.hpp"
#include "tactics/bot/bot_observation.hpp"
#include "tactics/bot/legal_action_generator.hpp"

namespace tactics::bot {

bool apply_bot_action_checked(GameState& game, BotSession& session, const BotAction& action)
{
    const BotActionResult result = execute_bot_action(game, session, action);
    if (!result.ok) {
        return false;
    }
    for (int guard = 0; guard < 8 && game.is_combat_visualization_paused(); ++guard) {
        BotAction resume;
        resume.kind = BotActionKind::ResumeCombatViz;
        resume.player_id = session.controlled_player;
        if (!execute_bot_action(game, session, resume).ok) {
            return false;
        }
    }
    return true;
}

double run_playout(GameState& game, const int root_player, const IBotPolicy& rollout_policy,
    const IBotEvaluator& evaluator, std::mt19937& rng, const BotSimStepConfig& config)
{
    (void)rollout_policy;
    BotSession session;
    if (const auto cp = game.turn_manager.current_player()) {
        session.controlled_player = *cp;
    }

    // Multi-turn horizon: count how many times the turn owner changes and stop after
    // `max_playout_turns` of them, so a simulation looks a bounded number of turns ahead
    // (both our future turns and the opponent's replies) before the evaluator scores it.
    std::optional<int> last_turn_owner = game.turn_manager.current_player();
    int turn_changes = 0;

    for (int step = 0; step < config.max_playout_steps; ++step) {
        if (is_match_over(game)) {
            break;
        }
        if (config.max_playout_turns > 0) {
            const std::optional<int> owner = game.turn_manager.current_player();
            if (owner != last_turn_owner) {
                last_turn_owner = owner;
                if (++turn_changes >= config.max_playout_turns) {
                    break;
                }
            }
        }

        const std::optional<int> acting = bot_acting_seat(game);
        if (!acting.has_value()) {
            BotAction resume;
            resume.kind = BotActionKind::ResumeCombatViz;
            resume.player_id = session.controlled_player;
            if (!apply_bot_action_checked(game, session, resume)) {
                break;
            }
            continue;
        }

        session.controlled_player = *acting;
        const std::vector<BotAction> legal = generate_legal_actions(game, *acting, config.legal_limits);
        if (legal.empty()) {
            break;
        }

        // MCTS rollouts use uniform random play - scoring every legal action during rollout
        // can overflow the stack when the hand is large (sandbox / full-faction testing).
        // Tree expansion and the final pick still use the heuristic scorer.
        std::uniform_int_distribution<std::size_t> dist(0, legal.size() - 1);
        const BotAction chosen = legal[dist(rng)];
        if (!apply_bot_action_checked(game, session, chosen)) {
            break;
        }
    }

    if (const auto terminal = terminal_value_for_player(game, root_player)) {
        return *terminal;
    }
    return evaluator.evaluate(game, root_player);
}

}  // namespace tactics::bot