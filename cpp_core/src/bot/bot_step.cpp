#include "tactics/bot/bot_step.hpp"

#include "tactics/bot/action_executor.hpp"

#include <string>
#include <vector>

namespace tactics::bot {
namespace {

int fallback_priority(const BotActionKind kind)
{
    switch (kind) {
    case BotActionKind::EndMainPhase:
    case BotActionKind::CommitAttackDeclaration:
    case BotActionKind::PassPriority:
    case BotActionKind::MoveConfirm:
        return 100;
    case BotActionKind::ChooseEnergyZone:
        return 90;
    case BotActionKind::ResolveTerritoryTarget:
    case BotActionKind::UseLand:
        return 88;
    case BotActionKind::SkipTerritoryTarget:
    case BotActionKind::SkipTerritoryLoot:
    case BotActionKind::TerritoryLootDiscard:
        return 72;
    case BotActionKind::ResumeCombatViz:
        return 80;
    case BotActionKind::DiscardHandCard:
    case BotActionKind::ScanDiscard:
    case BotActionKind::ScanFinish:
        return 70;
    default:
        return 0;
    }
}

bool try_execute_fallback(GameState& game, BotSession& session, const BotAction& chosen,
    const std::vector<BotAction>& legal, BotAction& executed, BotActionResult& exec)
{
    const BotAction* best = nullptr;
    int best_priority = -1;
    const bool chosen_was_move = chosen.kind == BotActionKind::MovePreview
        || chosen.kind == BotActionKind::MoveConfirm
        || chosen.kind == BotActionKind::MoveCancel
        || chosen.kind == BotActionKind::MoveRotate;
    for (const BotAction& fallback : legal) {
        if (bot_actions_equivalent(fallback, chosen)) {
            continue;
        }
        if (chosen_was_move) {
            switch (fallback.kind) {
            case BotActionKind::MovePreview:
            case BotActionKind::MoveConfirm:
            case BotActionKind::MoveCancel:
            case BotActionKind::MoveRotate:
                continue;
            default:
                break;
            }
        }
        const int priority = fallback_priority(fallback.kind);
        if (priority > best_priority) {
            best_priority = priority;
            best = &fallback;
        }
    }
    if (best && best_priority > 0) {
        exec = execute_bot_action(game, session, *best);
        if (exec.ok) {
            executed = *best;
            return true;
        }
    }

    for (const BotAction& fallback : legal) {
        if (bot_actions_equivalent(fallback, chosen)) {
            continue;
        }
        if (chosen_was_move) {
            switch (fallback.kind) {
            case BotActionKind::MovePreview:
            case BotActionKind::MoveConfirm:
            case BotActionKind::MoveCancel:
            case BotActionKind::MoveRotate:
                continue;
            default:
                break;
            }
        }
        exec = execute_bot_action(game, session, fallback);
        if (exec.ok) {
            executed = fallback;
            return true;
        }
    }
    return false;
}

}  // namespace

BotStepResult step_bot_once(GameState& game, IBotPolicy& policy, BotSession& session, std::mt19937& rng,
    const LegalActionGenLimits& limits)
{
    BotStepResult result;

    const std::optional<int> acting = bot_acting_seat(game);
    if (!acting.has_value()) {
        BotAction resume;
        resume.kind = BotActionKind::ResumeCombatViz;
        resume.player_id = session.controlled_player;
        result.attempted = true;
        result.chosen = resume;
        result.executed = resume;
        const BotActionResult exec = execute_bot_action(game, session, resume);
        result.ok = exec.ok;
        result.message = exec.message;
        return result;
    }

    session.controlled_player = *acting;
    auto legal = generate_legal_actions(game, *acting, limits);
    if (legal.empty()) {
        result.attempted = true;
        result.ok = false;
        result.message = std::string("no legal actions for seat in phase ")
            + turn_phase_to_string(game.turn_manager.current_phase);
        return result;
    }

    const BotObservation obs = build_bot_observation(game, *acting, legal.size());
    const BotAction chosen = policy.choose(game, obs, legal, rng);
    result.attempted = true;
    result.chosen = chosen;

    BotAction executed = chosen;
    BotActionResult exec = execute_bot_action(game, session, chosen);
    if (!exec.ok) {
        bool recovered = try_execute_fallback(game, session, chosen, legal, executed, exec);
        if (!recovered) {
            legal = generate_legal_actions(game, *acting, limits);
            recovered = try_execute_fallback(game, session, chosen, legal, executed, exec);
        }
        if (!recovered) {
            result.ok = false;
            result.message = std::string("action_failed: ") + bot_action_kind_name(chosen.kind) + ": " + exec.message;
            return result;
        }
    }

    result.ok = true;
    result.executed = executed;
    result.message = exec.message;
    return result;
}

}  // namespace tactics::bot