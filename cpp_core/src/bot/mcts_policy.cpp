#include "tactics/bot/mcts_policy.hpp"

#include "tactics/apps/sandbox_match.hpp"
#include "tactics/bot/bot_action.hpp"
#include "tactics/bot/bot_evaluator.hpp"
#include "tactics/bot/bot_game_clone.hpp"
#include "tactics/bot/bot_match_outcome.hpp"
#include "tactics/bot/bot_observation.hpp"
#include "tactics/bot/bot_sim_step.hpp"
#include "tactics/bot/legal_action_generator.hpp"
#include "tactics/core/board_target_policy.hpp"
#include "tactics/cards/card_catalog.hpp"
#include "tactics/cards/card_runtime.hpp"

#include <nlohmann/json.hpp>
#include "tactics/energy/energy_zone.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>

namespace tactics::bot {
namespace {

/** One node in the search tree: action from parent, visit stats, and untried children. */
struct MctsNode {
    BotAction action_from_parent{};
    MctsNode* parent{nullptr};
    std::vector<std::unique_ptr<MctsNode>> children;
    std::vector<BotAction> untried_actions;
    int visits{0};
    double value_sum{0.0};
};

/** True when MCTS should search (combat/movement) instead of using the heuristic scorer alone. */
bool is_tactical_decision(const BotObservation& obs, const std::vector<BotAction>& legal)
{
    if (obs.phase == TurnPhase::Energy) {
        return false;
    }
    bool has_attack = false;
    bool has_commit = false;
    bool has_response = false;
    bool has_high_value_proactive_play = false;
    const bool proactive_phase = obs.phase == TurnPhase::Main || obs.phase == TurnPhase::SecondMain
        || obs.phase == TurnPhase::AttackDeclaration || obs.phase == TurnPhase::BonusAttackDeclaration;
    for (const BotAction& action : legal) {
        switch (action.kind) {
        case BotActionKind::DeclareAttack:
            has_attack = true;
            break;
        case BotActionKind::CommitAttackDeclaration:
            has_commit = true;
            break;
        case BotActionKind::Deploy:
        case BotActionKind::DeployReserve:
            // Deploying is a strategic commitment worth planning several turns out (curve,
            // where a body will matter, whether to develop or hold). Search it when we have
            // real energy to spend, so development is *planned* rather than picked one-ply.
            if (proactive_phase && obs.total_spendable >= 2) {
                has_high_value_proactive_play = true;
            }
            break;
        case BotActionKind::MovePreview:
        case BotActionKind::MoveConfirm:
            // Maneuvering is planned too: where a unit stands ripples across turns
            // (formation, screening, threats, reaching an objective). Search moves in the
            // proactive phases so positioning is looked ahead, not chosen one-ply.
            if (proactive_phase) {
                has_high_value_proactive_play = true;
            }
            break;
        case BotActionKind::CastSpell:
        case BotActionKind::CastSpellReserve:
        case BotActionKind::ActivateAbility:
            has_response = true;
            // A proactive spell/ability with energy behind it has multi-turn consequences
            // (tempo, board impact, ramp) - plan it too.
            if (proactive_phase && obs.total_spendable >= 2) {
                has_high_value_proactive_play = true;
            }
            break;
        case BotActionKind::Defend:
        case BotActionKind::Dash:
        case BotActionKind::Recover:
            has_response = true;
            break;
        default:
            break;
        }
    }
    if (obs.must_respond_reaction_window && has_response) {
        return true;
    }
    if (has_attack) {
        return true;
    }
    if (has_commit && obs.pending_attack_count > 0) {
        return true;
    }
    if (has_high_value_proactive_play) {
        return true;
    }
    return false;
}

double leaf_value(GameState& state, const int root_player, const MctsConfig& config, const IBotEvaluator& evaluator,
    const IBotPolicy& rollout_policy, std::mt19937& rng, const BotSimStepConfig& sim_config)
{
    if (const auto terminal = terminal_value_for_player(state, root_player)) {
        return *terminal;
    }
    if (config.max_playout_steps <= 0) {
        return evaluator.evaluate(state, root_player);
    }
    return run_playout(state, root_player, rollout_policy, evaluator, rng, sim_config);
}

bool actions_equivalent(const BotAction& a, const BotAction& b)
{
    return a.kind == b.kind && a.player_id == b.player_id && a.card_id == b.card_id && a.entity_id == b.entity_id
        && a.focus_caster_entity_id == b.focus_caster_entity_id && a.stack_target_id == b.stack_target_id
        && a.x == b.x && a.y == b.y && a.ranged == b.ranged && a.ability_key == b.ability_key
        && a.spell_x_amount == b.spell_x_amount && a.spell_mode == b.spell_mode
        && a.energy_zone_index_1based == b.energy_zone_index_1based
        && a.land_ability_index_1based == b.land_ability_index_1based
        && a.hand_index_1based == b.hand_index_1based && a.play_zone == b.play_zone;
}

/** Legal actions for a state, ordered best-first by the shared heuristic prior and capped
 *  to `max_branching`. Every tree node uses this - including the *opponent's* nodes - so
 *  the search assumes a competent opponent that plays its heuristic-best replies. That is
 *  prophylaxis inside the search itself: the bot stops walking into attacks and pushes a
 *  smart opponent would punish, because those lines now evaluate to the punished position. */
/** Caps for enumerating legal actions inside MCTS (tree expansion + rollouts). Full sandbox
 *  hands can enumerate hundreds of spell targets and blow the stack during search. */
LegalActionGenLimits search_legal_limits(const MctsConfig& config)
{
    LegalActionGenLimits limits = config.legal_limits;
    constexpr std::size_t kSearchSpellCap = 32;
    constexpr std::size_t kSearchAbilityCap = 24;
    constexpr std::size_t kSearchMoveCap = 24;
    if (limits.max_spell_actions == 0 || limits.max_spell_actions > kSearchSpellCap) {
        limits.max_spell_actions = kSearchSpellCap;
    }
    if (limits.max_ability_actions == 0 || limits.max_ability_actions > kSearchAbilityCap) {
        limits.max_ability_actions = kSearchAbilityCap;
    }
    if (limits.max_move_actions == 0 || limits.max_move_actions > kSearchMoveCap) {
        limits.max_move_actions = kSearchMoveCap;
    }
    return limits;
}

std::vector<BotAction> prior_ordered_actions(GameState& state, const int acting, const MctsConfig& config)
{
    const LegalActionGenLimits limits = search_legal_limits(config);
    std::vector<BotAction> legal = generate_legal_actions(state, acting, limits);
    if (legal.size() <= 1) {
        return legal;
    }
    const BotObservation obs = build_bot_observation(state, acting, legal.size());
    const std::vector<int> scores = score_bot_actions(state, obs, legal);
    std::vector<std::size_t> order(legal.size());
    for (std::size_t i = 0; i < order.size(); ++i) {
        order[i] = i;
    }
    std::sort(order.begin(), order.end(),
        [&](const std::size_t a, const std::size_t b) { return scores[a] > scores[b]; });
    const std::size_t cap =
        config.max_branching == 0 ? legal.size() : std::min(legal.size(), config.max_branching);
    std::vector<BotAction> out;
    out.reserve(cap);
    for (std::size_t k = 0; k < cap; ++k) {
        out.push_back(legal[order[k]]);
    }
    return out;
}

MctsNode* select_uct_child(MctsNode& node, const double exploration_constant)
{
    MctsNode* best = nullptr;
    double best_score = -std::numeric_limits<double>::infinity();
    const double log_parent = std::log(static_cast<double>(std::max(1, node.visits)));

    for (const std::unique_ptr<MctsNode>& child_ptr : node.children) {
        MctsNode& child = *child_ptr;
        if (child.visits == 0) {
            return &child;
        }
        const double q = child.value_sum / static_cast<double>(child.visits);
        const double u = exploration_constant * std::sqrt(log_parent / static_cast<double>(child.visits));
        const double score = q + u;
        if (score > best_score) {
            best_score = score;
            best = &child;
        }
    }
    return best;
}

void fill_child_untried(MctsNode& child, GameState& state, const MctsConfig& config)
{
    const std::optional<int> acting = bot_acting_seat(state);
    if (!acting.has_value() || is_match_over(state)) {
        return;
    }
    child.untried_actions = prior_ordered_actions(state, *acting, config);
}

/** Argmax over the shared action scorer, random tiebreak among equal-best. This is the
 *  non-tactical decision-maker and the rollout policy - the same heuristic brain the old
 *  greedy policy used, now living inside MCTS. */
BotAction pick_best_scored_action(const GameState& game, const BotObservation& obs,
    const std::vector<BotAction>& legal, std::mt19937& rng)
{
    const std::vector<int> scores = score_bot_actions(game, obs, legal);
    int best = std::numeric_limits<int>::min();
    std::vector<std::size_t> bucket;
    for (std::size_t i = 0; i < legal.size(); ++i) {
        if (scores[i] > best) {
            best = scores[i];
            bucket.clear();
            bucket.push_back(i);
        } else if (scores[i] == best) {
            bucket.push_back(i);
        }
    }
    if (bucket.empty()) {
        return legal[std::uniform_int_distribution<std::size_t>(0, legal.size() - 1)(rng)];
    }
    return legal[bucket[std::uniform_int_distribution<std::size_t>(0, bucket.size() - 1)(rng)]];
}

/** Rollout / playout policy: greedily follows the shared action scorer. */
class ScoredHeuristicPolicy final : public IBotPolicy {
public:
    BotAction choose(const GameState& game, const BotObservation& obs, const std::vector<BotAction>& legal,
        std::mt19937& rng) const override
    {
        if (legal.empty()) {
            throw std::runtime_error("ScoredHeuristicPolicy: no legal actions");
        }
        return pick_best_scored_action(game, obs, legal, rng);
    }
};

/** Energy a seat could spend right now (float + untapped zone production) - used to weight
 *  the determinization toward the opponent's *playable* cards. */
int seat_available_energy(const GameState& state, const int seat)
{
    int total = 0;
    if (const auto it = state.turn_manager.player_energy.find(seat); it != state.turn_manager.player_energy.end()) {
        for (const auto& [_, amount] : it->second) {
            total += std::max(0, amount);
        }
    }
    if (const auto zit = state.players_energy_zones.find(seat); zit != state.players_energy_zones.end()) {
        for (const EnergyZone& zone : zit->second) {
            for (const auto& [_, amount] : zone.available_auto_energy()) {
                total += std::max(0, amount);
            }
        }
    }
    return total;
}

/** Total energy cost of a card instance (0 if unknown). */
int card_energy_cost(const Deck& deck, const CardInstanceId id)
{
    if (const CardInstance* inst = deck.pool.try_get(id)) {
        if (const CardDefinition* def = try_get_card_definition_ptr(inst->definition_id)) {
            return definition_total_energy_cost(*def);
        }
    }
    return 0;
}

/** Determinization (Information-Set MCTS): from the root player's point of view the
 *  opponents' hand *contents* and every deck's *draw order* are hidden. Before each
 *  simulation we resample those into one plausible world so the rollout no longer "sees"
 *  the real enemy hand or future draws, and averaging over the simulations makes the bot
 *  plan against the *distribution* of what the opponent could hold. Two observed signals
 *  sharpen the resample beyond a blind shuffle:
 *    - Cards played are already excluded: the library we resample is only the seat's
 *      *remaining* cards (hand + draw pile); anything played, discarded, or in play lives in
 *      other zones, so we never deal back a card we have watched them spend.
 *    - Energy is accounted for: cards the seat can actually afford right now are the live
 *      threats, so we bias the sampled hand toward affordable cards (soft 2:1 weighting via
 *      Efraimidis–Spirakis) rather than treating an uncastable bomb as an equal threat.
 *  The bot's own hand is left intact (it legitimately knows it); only hidden info is moved. */
void determinize_hidden_information(GameState& state, const int root_player, std::mt19937& rng)
{
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    for (auto& [seat, deck] : state.players_decks) {
        if (seat == root_player) {
            std::shuffle(deck.deck.begin(), deck.deck.end(), rng);  // own hand known; hide draw order
            continue;
        }
        if (deck.deck.empty()) {
            continue;  // hand is the whole remaining library - nothing hidden to swap in
        }
        std::vector<CardInstanceId> library;
        library.reserve(deck.hand.size() + deck.deck.size());
        library.insert(library.end(), deck.hand.begin(), deck.hand.end());
        library.insert(library.end(), deck.deck.begin(), deck.deck.end());

        const int energy = seat_available_energy(state, seat);
        std::vector<std::pair<double, CardInstanceId>> keyed;
        keyed.reserve(library.size());
        for (const CardInstanceId id : library) {
            const double weight = (card_energy_cost(deck, id) <= energy) ? 2.0 : 1.0;
            const double u = std::max(1e-12, unit(rng));
            keyed.emplace_back(std::pow(u, 1.0 / weight), id);  // weighted sampling key
        }
        std::sort(keyed.begin(), keyed.end(),
            [](const std::pair<double, CardInstanceId>& a, const std::pair<double, CardInstanceId>& b) {
                return a.first > b.first;
            });

        // Top `hand_n` keys become the sampled hand; the rest form the draw pile. Mutate in
        // place so `players_hands` (an alias to `deck.hand`) still points at the new hand.
        const std::size_t hand_n = deck.hand.size();
        deck.hand.clear();
        deck.deck.clear();
        for (std::size_t i = 0; i < keyed.size(); ++i) {
            (i < hand_n ? deck.hand : deck.deck).push_back(keyed[i].second);
        }
        std::shuffle(deck.deck.begin(), deck.deck.end(), rng);
    }
}

void backpropagate_value(const std::vector<MctsNode*>& path, const double value)
{
    for (MctsNode* visited : path) {
        visited->visits += 1;
        visited->value_sum += value;
    }
}

}  // namespace

MctsPolicy::MctsPolicy(MctsConfig config) : config_(std::move(config)) {}

BotAction MctsPolicy::choose(const GameState& game, const BotObservation& obs, const std::vector<BotAction>& legal,
    std::mt19937& rng) const
{
    if (legal.empty()) {
        throw std::runtime_error("MctsPolicy: no legal actions");
    }
    if (legal.size() == 1) {
        return legal.front();
    }

    // Shallow MCTS Q undervalues defensive base turret fire: the leaf evaluator
    // underweights removing adjacent base threats in 0-step eval, so search picks
    // repositioning/buffs and commits empty even when the shared scorer ranks a base
    // DeclareAttack #1. Trust the scorer each batched step until the turret fires or
    // a higher-scored action appears.
    if (obs.phase == TurnPhase::AttackDeclaration || obs.phase == TurnPhase::BonusAttackDeclaration) {
        std::vector<BotAction> capped_legal;
        const std::vector<BotAction>* pool = &legal;
        if (legal.size() > config_.max_branching) {
            capped_legal = prior_ordered_actions(const_cast<GameState&>(game), obs.acting_player, config_);
            if (!capped_legal.empty()) {
                pool = &capped_legal;
            }
        }
        const BotAction heuristic_best = pick_best_scored_action(game, obs, *pool, rng);
        if (heuristic_best.kind == BotActionKind::DeclareAttack) {
            const auto ent_it = game.board.all_entities_map.find(heuristic_best.entity_id);
            if (ent_it != game.board.all_entities_map.end() && ent_it->second && entity_is_base(*ent_it->second)) {
                return heuristic_best;
            }
        }
    }

    if (config_.max_simulations <= 0 || !is_tactical_decision(obs, legal)) {
        if (legal.size() > config_.max_branching) {
            GameState& search_game = const_cast<GameState&>(game);
            const std::vector<BotAction> search_legal =
                prior_ordered_actions(search_game, obs.acting_player, config_);
            if (!search_legal.empty()) {
                return pick_best_scored_action(game, obs, search_legal, rng);
            }
        }
        return pick_best_scored_action(game, obs, legal, rng);
    }

    const int root_player = obs.acting_player;
    HeuristicEvaluator evaluator;
    ScoredHeuristicPolicy rollout_policy;

    // Full sandbox hands can enumerate 100+ spell targets. Score/search only the capped
    // search pool - never every action in the caller's `legal` vector.
    GameState& search_game = const_cast<GameState&>(game);
    std::vector<BotAction> search_legal = prior_ordered_actions(search_game, root_player, config_);
    if (search_legal.empty()) {
        search_legal = legal;
    }
    const std::vector<BotAction>& root_candidates = search_legal;

    // Heuristic prior over the root's legal actions - the combat/formation/economy brain.
    // It (a) orders and caps which actions the shallow sim budget explores (best first)
    // and (b) is blended into the final choice so search value and heuristic judgment
    // agree. This is what carries every scoring improvement into MCTS's tactical picks.
    const std::vector<int> root_scores = score_bot_actions(game, obs, root_candidates);
    std::vector<std::size_t> order(root_candidates.size());
    for (std::size_t i = 0; i < order.size(); ++i) {
        order[i] = i;
    }
    std::sort(order.begin(), order.end(),
        [&](const std::size_t a, const std::size_t b) { return root_scores[a] > root_scores[b]; });

    const int prior_min = *std::min_element(root_scores.begin(), root_scores.end());
    const int prior_max = *std::max_element(root_scores.begin(), root_scores.end());
    const auto norm_prior = [&](const BotAction& a) -> double {
        if (prior_max <= prior_min) {
            return 0.0;
        }
        for (std::size_t i = 0; i < root_candidates.size(); ++i) {
            if (actions_equivalent(root_candidates[i], a)) {
                return static_cast<double>(root_scores[i] - prior_min)
                    / static_cast<double>(prior_max - prior_min);
            }
        }
        return 0.0;
    };

    MctsNode root;
    // Prior-ordered, prior-capped root actions: keep the top-scoring `max_branching` and
    // expand them best-first so the limited simulations land on the best candidates.
    {
        const std::size_t cap = config_.max_branching == 0
            ? root_candidates.size()
            : std::min(root_candidates.size(), config_.max_branching);
        root.untried_actions.reserve(cap);
        for (std::size_t k = 0; k < cap; ++k) {
            root.untried_actions.push_back(root_candidates[order[k]]);
        }
    }

    // Budget split: a *strategic* decision (an attack/deploy/spell/ability is available) is
    // worth the full multi-turn rollout; a movement-only decision uses a cheap shallow
    // search (static-evaluator leaves, no rollout, fewer sims) because searching every move
    // with rollouts is far too expensive.
    const bool strategic = std::any_of(root_candidates.begin(), root_candidates.end(), [](const BotAction& a) {
        switch (a.kind) {
        case BotActionKind::DeclareAttack:
        case BotActionKind::CommitAttackDeclaration:
        case BotActionKind::Deploy:
        case BotActionKind::DeployReserve:
        case BotActionKind::CastSpell:
        case BotActionKind::CastSpellReserve:
        case BotActionKind::ActivateAbility:
            return true;
        default:
            return false;
        }
    });
    const int num_simulations =
        strategic ? config_.max_simulations : std::min(config_.max_simulations, config_.light_max_simulations);

    BotSimStepConfig sim_config;
    sim_config.legal_limits = search_legal_limits(config_);
    int playout_steps = strategic ? config_.max_playout_steps : 0;
    if (playout_steps > 0
        && (game.game_mode() == GameMode::Sandbox || game_id_is_sandbox(game.game_id()))) {
        // Full-faction sandbox hands make long rollouts stack-heavy; keep lookahead shallow.
        playout_steps = std::min(playout_steps, 8);
    }
    sim_config.max_playout_steps = playout_steps;
    sim_config.max_playout_turns = strategic ? config_.max_playout_turns : 0;

    // Parse the root snapshot once; each simulation clones from the DOM (re-lexing the
    // full snapshot JSON per sim dominated the search budget).
    nlohmann::json root_snapshot_dom;
    try {
        root_snapshot_dom = nlohmann::json::parse(game.build_match_snapshot_utf8());
    } catch (const std::exception&) {
        return pick_best_scored_action(game, obs, legal, rng);
    }
    const auto clone_from_root = [&]() { return clone_game_from_snapshot_json(root_snapshot_dom); };

    for (int sim = 0; sim < num_simulations; ++sim) {
        std::unique_ptr<GameState> state = clone_from_root();
        if (!state) {
            continue;
        }
        // Isolate randomness per simulation so rollouts do not advance the shared rng
        // used for real-game decisions, and each sim's replay path is self-consistent.
        std::mt19937 sim_rng(static_cast<std::mt19937::result_type>(rng()));
        // Resample the hidden information for this simulation so the lookahead does not peek
        // at the real enemy hand / draw order (Information-Set MCTS).
        determinize_hidden_information(*state, root_player, sim_rng);

        std::vector<MctsNode*> path;
        path.push_back(&root);
        MctsNode* node = &root;
        BotSession session;
        session.controlled_player = root_player;

        while (true) {
            if (is_match_over(*state)) {
                const double value = leaf_value(*state, root_player, config_, evaluator, rollout_policy, sim_rng, sim_config);
                backpropagate_value(path, value);
                break;
            }

            if (!node->untried_actions.empty()) {
                const BotAction action = node->untried_actions.front();
                node->untried_actions.erase(node->untried_actions.begin());

                auto child = std::make_unique<MctsNode>();
                child->action_from_parent = action;
                child->parent = node;

                session.controlled_player = action.player_id;
                if (!apply_bot_action_checked(*state, session, action)) {
                    backpropagate_value(path, leaf_value(*state, root_player, config_, evaluator, rollout_policy, sim_rng, sim_config));
                    break;
                }

                fill_child_untried(*child, *state, config_);

                MctsNode* child_raw = child.get();
                node->children.push_back(std::move(child));
                path.push_back(child_raw);
                node = child_raw;

                const double value =
                    leaf_value(*state, root_player, config_, evaluator, rollout_policy, sim_rng, sim_config);
                backpropagate_value(path, value);
                break;
            }

            if (node->children.empty()) {
                const double value =
                    leaf_value(*state, root_player, config_, evaluator, rollout_policy, sim_rng, sim_config);
                backpropagate_value(path, value);
                break;
            }

            MctsNode* next = select_uct_child(*node, config_.exploration_constant);
            if (!next) {
                break;
            }

            session.controlled_player = next->action_from_parent.player_id;
            if (!apply_bot_action_checked(*state, session, next->action_from_parent)) {
                break;
            }

            path.push_back(next);
            node = next;
        }
    }

    // Final choice: blend the search's value estimate (Q) with the heuristic prior so the
    // pick reflects both lookahead and combat judgment. With a shallow sim budget Q is
    // noisy, so the prior keeps choices sane; a decisively good/bad line (a large Q swing,
    // e.g. a winning or losing terminal) still overrides the prior.
    constexpr double kPriorWeight = 0.6;
    MctsNode* best_child = nullptr;
    double best_final = -std::numeric_limits<double>::infinity();
    for (const std::unique_ptr<MctsNode>& child_ptr : root.children) {
        MctsNode& child = *child_ptr;
        const double q = child.visits > 0 ? child.value_sum / static_cast<double>(child.visits) : -1.0;
        const double final_score = q + kPriorWeight * norm_prior(child.action_from_parent);
        if (final_score > best_final) {
            best_final = final_score;
            best_child = &child;
        }
    }
    if (best_child) {
        for (const BotAction& candidate : legal) {
            if (actions_equivalent(candidate, best_child->action_from_parent)) {
                return candidate;
            }
        }
        return best_child->action_from_parent;
    }

    return pick_best_scored_action(game, obs, legal, rng);
}

}  // namespace tactics::bot