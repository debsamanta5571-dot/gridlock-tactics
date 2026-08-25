#include "tactics/bot/bot_match_driver.hpp"

#include "tactics/bot/bot_action.hpp"
#include "tactics/bot/bot_evaluator.hpp"
#include "tactics/bot/bot_match_outcome.hpp"
#include "tactics/bot/bot_observation.hpp"
#include "tactics/bot/bot_session.hpp"
#include "tactics/bot/bot_step.hpp"
#include "tactics/bot/legal_action_generator.hpp"
#include "tactics/common/types.hpp"

#include <fstream>
#include <vector>

#include <iostream>
#include <sstream>
#include <unordered_map>

namespace tactics::bot {

BotMatchResult run_bot_match(GameState& game, const BotMatchDriverConfig& config)
{
    BotMatchResult result;

    // Self-play training data: buffer the value features (from P1's perspective) at each
    // turn boundary, then on any exit write them all labelled by the final result. `finalize`
    // wraps every return so the label is known before the rows are flushed.
    const int log_seat = game.turn_manager.players.empty() ? 0 : game.turn_manager.players.front();
    std::vector<std::vector<double>> logged_features;
    auto finalize = [&](BotMatchResult r) -> BotMatchResult {
        if (!config.feature_log_path.empty() && !logged_features.empty()) {
            double label = 0.5;  // draw / no winner
            if (r.winner_seat.has_value()) {
                label = (game.team_of_seat(*r.winner_seat) == game.team_of_seat(log_seat)) ? 1.0 : 0.0;
            }
            if (std::ofstream out(config.feature_log_path, std::ios::app); out) {
                for (const std::vector<double>& row : logged_features) {
                    out << label;
                    for (const double v : row) {
                        out << ',' << v;
                    }
                    out << '\n';
                }
            }
        }
        return r;
    };

    BotPolicyOptions policy_options = config.policy_options;
    policy_options.mcts.legal_limits = config.legal_limits;
    auto policy = make_bot_policy(config.policy_name, policy_options);
    std::mt19937 rng(static_cast<std::mt19937::result_type>(config.rng_seed));

    game.set_combat_visualization_enabled(false);

    BotSession session;
    if (const auto cp = game.turn_manager.current_player()) {
        session.controlled_player = *cp;
    }

    std::unordered_map<int, int> seat_turn_number;
    for (const int seat : game.turn_manager.players) {
        seat_turn_number[seat] = 0;
    }
    int last_active_seat = -1;
    if (const auto cp = game.turn_manager.current_player()) {
        seat_turn_number[*cp] = 1;
        last_active_seat = *cp;
    }

    // Stall detection: a "material progress" signature = total base HP + living board
    // units. In real play it changes constantly (units deploy/die, bases take damage). If
    // it is frozen for a long stretch, both sides are locked in a no-progress loop (e.g.
    // both boards wiped and neither can rebuild) - end the match as a draw rather than
    // grinding to the action cap.
    long last_progress_sig = -1;
    int steps_since_progress = 0;
    const int kStallLimit = 600;

    for (int step = 0; step < config.max_actions; ++step) {
        if (is_match_over(game)) {
            result.completed = true;
            result.winner_seat = winner_seat(game);
            result.actions_taken = step;
            if (sudden_death_timeout(game)) {
                result.end_reason = result.winner_seat.has_value()
                    ? "sudden_death_base_hp"
                    : "sudden_death_draw";
            } else {
                result.end_reason = result.winner_seat.has_value()
                    ? "base_destroyed"
                    : "match_over_no_winner";
            }
            return finalize(result);
        }

        long progress_sig = 0;
        for (const auto& [_, ent] : game.board.all_entities_map) {
            if (!ent || ent->current_health <= 0) {
                continue;
            }
            if (ent->entity_type == "base") {
                progress_sig += ent->current_health;
            } else if (ent->entity_type == "unit") {
                progress_sig += 1000;  // any unit on board is "material"; weight distinct from HP
            }
        }
        if (progress_sig != last_progress_sig) {
            last_progress_sig = progress_sig;
            steps_since_progress = 0;
        } else if (++steps_since_progress >= kStallLimit) {
            result.end_reason = "stalled_no_progress_draw";
            result.actions_taken = step;
            return finalize(result);
        }

        const std::optional<int> acting = bot_acting_seat(game);
        const TurnPhase log_phase = game.turn_manager.current_phase;
        const std::optional<int> log_active_turn = game.turn_manager.current_player();
        if (log_active_turn.has_value() && *log_active_turn != last_active_seat) {
            seat_turn_number[*log_active_turn]++;
            last_active_seat = *log_active_turn;
            if (!config.feature_log_path.empty()) {
                logged_features.push_back(bot_value_features(game, log_seat));
            }
            if (config.verbose) {
                std::unordered_map<int, int> u_count;
                std::unordered_map<int, int> u_hp;
                for (const auto& [_, ent] : game.board.all_entities_map) {
                    if (!ent || !ent->owner || ent->current_health <= 0 || ent->entity_type != "unit") {
                        continue;
                    }
                    u_count[*ent->owner]++;
                    u_hp[*ent->owner] += ent->current_health;
                }
                std::cout << "[state] turn=P" << *log_active_turn << "#" << seat_turn_number[*log_active_turn];
                for (const int seat : game.turn_manager.players) {
                    std::cout << " P" << seat << "=" << u_count[seat] << "u/" << u_hp[seat] << "hp";
                }
                std::cout << "\n";
            }
        }
        const int log_turn =
            log_active_turn.has_value() ? seat_turn_number[*log_active_turn] : 0;

        const BotStepResult step_result = step_bot_once(game, *policy, session, rng, config.legal_limits);
        if (!step_result.attempted) {
            continue;
        }
        if (!step_result.ok) {
            result.end_reason = step_result.message;
            result.actions_taken = step + 1;
            return finalize(result);
        }

        const BotAction& executed = step_result.executed;
        const BotAction& chosen = step_result.chosen;
        const std::string chosen_detail =
            config.verbose ? format_bot_action_detail(game, chosen) : std::string{};
        std::string executed_detail = chosen_detail;
        if (!bot_actions_equivalent(executed, chosen)) {
            executed_detail = format_bot_action_detail(game, executed);
        }

        if (config.verbose) {
            std::cout << "[bot] step=" << step;
            if (log_active_turn.has_value()) {
                std::cout << " turn=P" << *log_active_turn << "#" << log_turn;
            }
            std::cout << " phase=" << turn_phase_to_string(log_phase);
            if (acting.has_value()) {
                std::cout << " actor=P" << *acting;
            }
            std::cout << " " << bot_action_kind_name(executed.kind);
            if (!executed_detail.empty()) {
                std::cout << " | " << executed_detail;
            }
            if (!bot_actions_equivalent(executed, chosen)) {
                std::cout << " (fallback from " << bot_action_kind_name(chosen.kind);
                if (!chosen_detail.empty()) {
                    std::cout << ": " << chosen_detail;
                }
                std::cout << ")";
            }
            std::cout << "\n";
        }
    }

    result.end_reason = "max_actions_reached";
    result.actions_taken = config.max_actions;
    if (is_match_over(game)) {
        result.completed = true;
        result.winner_seat = winner_seat(game);
    }
    return finalize(result);
}

}  // namespace tactics::bot