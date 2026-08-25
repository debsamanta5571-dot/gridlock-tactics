#include "tactics/apps/master_cli_dispatch.hpp"
#include "tactics/apps/sandbox_match.hpp"
#include "tactics/bot/bot_evaluator.hpp"
#include "tactics/bot/bot_match_driver.hpp"
#include "tactics/combat/combat_resolver.hpp"
#include "tactics/core/board_target_policy.hpp"
#include "tactics/entities/entity.hpp"
#include "tactics/cards/ability_catalog.hpp"
#include "tactics/cards/passive_catalog.hpp"
#include "tactics/common/match_defaults.hpp"
#include "tactics/content/project_content.hpp"
#include "tactics/core.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct CliArgs {
    uint64_t seed{42};
    int max_actions{20000};
    std::string policy{"random"};
    // Defaults mirror MctsConfig (multi-turn rollouts on) so the CLI does not silently
    // disable planning. -1 => "use the MctsConfig default".
    int mcts_simulations{-1};
    int mcts_playout_steps{-1};
    int mcts_playout_turns{-1};
    int mcts_max_branching{-1};
    std::string faction{"asterian_civilian_militia"};
    std::string content_dir;
    std::string feature_log_path;
    std::string weights_path;
    /** Team id per seat, e.g. "1,1,2,2" = 4 players, seats 1+2 vs 3+4. Empty = 1v1. */
    std::string teams;
    int base_health{-1};
    bool sandbox{false};
    bool verbose{false};
};

/** Parse "--teams 1,1,2,2" into per-seat team ids (seat i+1 → out[i]). */
std::vector<int> parse_team_ids(const std::string& csv)
{
    std::vector<int> ids;
    std::string tok;
    std::istringstream ss(csv);
    while (std::getline(ss, tok, ',')) {
        try {
            ids.push_back(std::stoi(tok));
        } catch (...) {
            return {};
        }
    }
    if (ids.size() < 2 || ids.size() > 8) {
        return {};
    }
    // The standard map defines base zones for seats 1 and 2 only, and the match ends when
    // a team has no living base - so seats 1 and 2 must be on OPPOSITE teams (a team with
    // no base-owning seat has lost before the first action). 2v2 is e.g. "1,2,1,2".
    if (ids.size() >= 2 && ids[0] == ids[1]) {
        return {};
    }
    return ids;
}

/** Add players (and team assignments) for the match: seat count comes from --teams when
 *  given, otherwise the standard 2-seat demo. */
void add_match_players(tactics::GameState& game, const std::vector<int>& team_ids)
{
    const int seats = team_ids.empty() ? tactics::kDefaultDemoSeatCount : static_cast<int>(team_ids.size());
    for (int seat = 1; seat <= seats; ++seat) {
        game.add_player(seat, "Bot " + std::to_string(seat));
    }
    for (std::size_t i = 0; i < team_ids.size(); ++i) {
        game.set_seat_team(static_cast<int>(i) + 1, team_ids[i]);
    }
}

bool load_project_content_from_dir(const std::string& content_dir, std::string& err_out)
{
    tactics::ensure_builtin_ability_catalog_loaded();
    tactics::ensure_builtin_passive_catalog_loaded();

    std::string prefix = content_dir;
    if (!prefix.empty() && prefix.back() != '/' && prefix.back() != '\\') {
        prefix += "/";
    }
    const auto read_file = [&](const std::string& rel, std::string& out, std::string& err) -> bool {
        (void)err;
        std::ifstream in(prefix + rel, std::ios::binary);
        if (!in) {
            return false;
        }
        std::ostringstream oss;
        oss << in.rdbuf();
        out = oss.str();
        return true;
    };
    return tactics::load_all_project_content(read_file, err_out);
}

std::string resolve_default_content_dir()
{
    const std::vector<std::string> candidates = {
        "TacticsGameUnreal 5.8/Content",
        "../TacticsGameUnreal 5.8/Content",
        "../../TacticsGameUnreal 5.8/Content",
    };
    for (const std::string& dir : candidates) {
        std::ifstream probe(dir + "/TacticsData/ability_catalog.json", std::ios::binary);
        if (probe) {
            return dir;
        }
    }
    return {};
}

CliArgs parse_args(int argc, char** argv)
{
    CliArgs args;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--seed" && i + 1 < argc) {
            args.seed = static_cast<uint64_t>(std::stoull(argv[++i]));
        } else if (arg == "--max-actions" && i + 1 < argc) {
            args.max_actions = std::stoi(argv[++i]);
        } else if (arg == "--policy" && i + 1 < argc) {
            args.policy = argv[++i];
        } else if (arg == "--faction" && i + 1 < argc) {
            args.faction = argv[++i];
        } else if (arg == "--content-dir" && i + 1 < argc) {
            args.content_dir = argv[++i];
        } else if (arg == "--sandbox") {
            args.sandbox = true;
        } else if (arg == "--verbose") {
            args.verbose = true;
        } else if (arg == "--mcts-sims" && i + 1 < argc) {
            args.mcts_simulations = std::stoi(argv[++i]);
        } else if (arg == "--mcts-playout-steps" && i + 1 < argc) {
            args.mcts_playout_steps = std::stoi(argv[++i]);
        } else if (arg == "--mcts-playout-turns" && i + 1 < argc) {
            args.mcts_playout_turns = std::stoi(argv[++i]);
        } else if (arg == "--mcts-branching" && i + 1 < argc) {
            args.mcts_max_branching = std::stoi(argv[++i]);
        } else if (arg == "--log-features" && i + 1 < argc) {
            args.feature_log_path = argv[++i];
        } else if (arg == "--weights" && i + 1 < argc) {
            args.weights_path = argv[++i];
        } else if (arg == "--teams" && i + 1 < argc) {
            args.teams = argv[++i];
        } else if (arg == "--base-health" && i + 1 < argc) {
            args.base_health = std::stoi(argv[++i]);
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "bot_match [--seed N] [--max-actions N] [--policy random|mcts]\n"
                         "          [--mcts-sims N] [--mcts-playout-steps N] [--mcts-playout-turns N] [--mcts-branching N]\n"
                         "          [--log-features FILE] [--weights FILE] [--teams 1,1,2,2]\n"
                         "          [--sandbox] [--faction KEY] [--base-health N] [--content-dir PATH] [--verbose]\n";
            std::exit(0);
        }
    }
    return args;
}

void apply_policy_cli_config(const CliArgs& args, tactics::bot::BotMatchDriverConfig& config)
{
    // Only override a MctsConfig default when the flag was explicitly passed (-1 => keep
    // the default, so the CLI never silently disables multi-turn rollouts).
    auto& mcts = config.policy_options.mcts;
    if (args.mcts_simulations >= 0) {
        mcts.max_simulations = args.mcts_simulations;
    }
    if (args.mcts_playout_steps >= 0) {
        mcts.max_playout_steps = args.mcts_playout_steps;
    }
    if (args.mcts_playout_turns >= 0) {
        mcts.max_playout_turns = args.mcts_playout_turns;
    }
    if (args.mcts_max_branching >= 0) {
        mcts.max_branching = static_cast<std::size_t>(args.mcts_max_branching);
    }
    config.feature_log_path = args.feature_log_path;
    if (!args.weights_path.empty()) {
        if (!tactics::bot::load_bot_value_weights(args.weights_path)) {
            std::cerr << "warning: could not load weights from " << args.weights_path
                      << " (kept defaults)\n";
        }
    }
}

void set_all_player_base_health(tactics::GameState& game, const int hp)
{
    const int clamped = std::max(1, hp);
    for (const int seat : game.turn_manager.players) {
        const std::string base_id = "base_p" + std::to_string(seat);
        const auto it = game.board.all_entities_map.find(base_id);
        if (it != game.board.all_entities_map.end() && it->second) {
            it->second->base_health = clamped;
            it->second->current_health = clamped;
        }
    }
}

void print_match_board_summary(const tactics::GameState& game, std::ostream& out)
{
    int attackable_cells = 0;
    int board_units = 0;
    int owned_entities = 0;
    for (const int seat : game.turn_manager.players) {
        const std::string base_id = "base_p" + std::to_string(seat);
        const auto base_it = game.board.all_entities_map.find(base_id);
        if (base_it != game.board.all_entities_map.end() && base_it->second) {
            out << " base_p" << seat << "_hp=" << base_it->second->current_health;
        }
        for (const auto& [_, ent] : game.board.all_entities_map) {
            if (ent && ent->owner && *ent->owner == seat && ent->current_health > 0) {
                ++owned_entities;
            }
            auto unit = std::dynamic_pointer_cast<tactics::Unit>(ent);
            if (!unit || !unit->owner || *unit->owner != seat || !tactics::entity_is_board_unit(*unit)) {
                continue;
            }
            ++board_units;
            attackable_cells += static_cast<int>(tactics::gather_attackable_goal_cells(
                const_cast<tactics::GameState&>(game), unit, seat).size());
        }
    }
    out << " board_units=" << board_units << " owned_entities=" << owned_entities
        << " attackable_cells=" << attackable_cells
        << " aether_units_killed=" << tactics::aether_units_killed_total(game)
        << " phase=" << tactics::turn_phase_to_string(game.turn_manager.current_phase);
}

}  // namespace

int main(int argc, char** argv)
{
    const CliArgs args = parse_args(argc, argv);

    std::string content_dir = args.content_dir;
    if (content_dir.empty()) {
        content_dir = resolve_default_content_dir();
    }
    if (!content_dir.empty()) {
        std::string load_err;
        if (!load_project_content_from_dir(content_dir, load_err)) {
            std::cerr << "Project content load warning: " << load_err << "\n";
        }
    } else {
        std::cerr << "WARN: no Content dir found; sandbox decks may be empty. Use --content-dir.\n";
    }

    const std::optional<uint64_t> match_seed = args.seed;

    const std::vector<int> team_ids = args.teams.empty() ? std::vector<int>{} : parse_team_ids(args.teams);
    if (!args.teams.empty() && team_ids.empty()) {
        std::cerr << "Invalid --teams (expected 2-8 comma-separated team ids, e.g. 1,1,2,2)\n";
        return 1;
    }

    if (args.sandbox) {
        tactics::GameState game("bot_match_sandbox", match_seed);
        game.set_game_mode(tactics::GameMode::Sandbox);
        add_match_players(game, team_ids);
        game.start_game();
        if (args.base_health >= 0) {
            set_all_player_base_health(game, args.base_health);
        }
        tactics::master_cli_seed_sandbox_state(game);

        std::string deck_err;
        if (!tactics::apply_sandbox_faction_deck_to_all_players(game, args.faction, &deck_err)) {
            std::cerr << "Failed to apply faction deck '" << args.faction << "': " << deck_err << "\n";
            return 1;
        }

        tactics::bot::BotMatchDriverConfig config;
        config.rng_seed = args.seed;
        config.max_actions = args.max_actions;
        config.policy_name = args.policy;
        config.verbose = args.verbose;
        config.legal_limits.max_spell_actions = 96;
        config.legal_limits.max_ability_actions = 64;
        config.legal_limits.max_move_actions = 48;
        apply_policy_cli_config(args, config);

        const tactics::bot::BotMatchResult result = tactics::bot::run_bot_match(game, config);

        std::cout << "actions=" << result.actions_taken;
        if (result.completed) {
            std::cout << " completed=1";
            if (result.winner_seat.has_value()) {
                std::cout << " winner=P" << *result.winner_seat;
            } else {
                std::cout << " winner=none";
            }
        } else {
            std::cout << " completed=0";
        }
        std::cout << " reason=" << result.end_reason;
        print_match_board_summary(game, std::cout);
        std::cout << "\n";
        return result.completed ? 0 : 2;
    }

    tactics::GameState game("bot_match_demo", match_seed);
    add_match_players(game, team_ids);
    game.start_game();
    if (args.base_health >= 0) {
        set_all_player_base_health(game, args.base_health);
    }
    tactics::master_cli_seed_demo_state(game);

    tactics::bot::BotMatchDriverConfig config;
    config.rng_seed = args.seed;
    config.max_actions = args.max_actions;
    config.policy_name = args.policy;
    config.verbose = args.verbose;
    apply_policy_cli_config(args, config);

    const tactics::bot::BotMatchResult result = tactics::bot::run_bot_match(game, config);

    std::cout << "mode=demo ";
    std::cout << "actions=" << result.actions_taken;
    if (result.completed) {
        std::cout << " completed=1";
        if (result.winner_seat.has_value()) {
            std::cout << " winner=P" << *result.winner_seat;
        } else {
            std::cout << " winner=none";
        }
    } else {
        std::cout << " completed=0";
    }
    std::cout << " reason=" << result.end_reason;
    print_match_board_summary(game, std::cout);
    std::cout << "\n";

    return result.completed ? 0 : 2;
}