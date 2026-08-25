#include "tactics/bot/bot_match_outcome.hpp"

#include "tactics/entities/entity.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <vector>

namespace tactics::bot {

bool player_has_living_base(const GameState& game, const int seat)
{
    const std::string id = "base_p" + std::to_string(seat);
    const auto it = game.board.all_entities_map.find(id);
    if (it == game.board.all_entities_map.end() || !it->second) {
        return false;
    }
    return entity_is_base(*it->second) && it->second->current_health > 0;
}

namespace {

int base_hp_for_seat(const GameState& game, const int seat)
{
    const std::string id = "base_p" + std::to_string(seat);
    const auto it = game.board.all_entities_map.find(id);
    if (it == game.board.all_entities_map.end() || !it->second) {
        return 0;
    }
    return std::max(0, it->second->current_health);
}

int living_base_teams(const GameState& game)
{
    std::set<int> teams;
    for (const int seat : game.turn_manager.players) {
        if (player_has_living_base(game, seat)) {
            teams.insert(game.team_of_seat(seat));
        }
    }
    return static_cast<int>(teams.size());
}

}  // namespace

bool sudden_death_timeout(const GameState& game)
{
    const int since = game.turn_manager.all_decks_empty_since_round;
    return since >= 0
        && game.turn_manager.round_number >= since + TurnManager::kSuddenDeathRounds;
}

int team_base_hp(const GameState& game, const int team)
{
    int total = 0;
    for (const int seat : game.turn_manager.players) {
        if (game.team_of_seat(seat) == team) {
            total += base_hp_for_seat(game, seat);
        }
    }
    return total;
}

int rounds_until_sudden_death(const GameState& game)
{
    const int since = game.turn_manager.all_decks_empty_since_round;
    if (since < 0) {
        return -1;
    }
    return (since + TurnManager::kSuddenDeathRounds) - game.turn_manager.round_number;
}

bool trailing_on_sudden_death_base_hp(const GameState& game, const int seat)
{
    if (game.turn_manager.all_decks_empty_since_round < 0) {
        return false;
    }
    const int my_team = game.team_of_seat(seat);
    const int my_hp = team_base_hp(game, my_team);
    int best_other = -1;
    std::set<int> seen_teams;
    for (const int s : game.turn_manager.players) {
        const int team = game.team_of_seat(s);
        if (team == my_team || !seen_teams.insert(team).second) {
            continue;
        }
        best_other = std::max(best_other, team_base_hp(game, team));
    }
    return best_other >= 0 && my_hp < best_other;
}

int sudden_death_base_hp_urgency(const GameState& game, const int seat)
{
    if (!trailing_on_sudden_death_base_hp(game, seat)) {
        return 0;
    }
    const int my_team = game.team_of_seat(seat);
    const int my_hp = team_base_hp(game, my_team);
    int best_other = 0;
    std::set<int> seen_teams;
    for (const int s : game.turn_manager.players) {
        const int team = game.team_of_seat(s);
        if (team == my_team || !seen_teams.insert(team).second) {
            continue;
        }
        best_other = std::max(best_other, team_base_hp(game, team));
    }
    const int deficit = std::max(0, best_other - my_hp);
    const int rounds_left = std::max(0, rounds_until_sudden_death(game));
    const int clock_elapsed = TurnManager::kSuddenDeathRounds - rounds_left;
    return std::min(280, deficit * 12 + clock_elapsed * 30);
}

bool is_match_over(const GameState& game)
{
    // Base victory: at most one team still has a living base.
    if (living_base_teams(game) <= 1) {
        return true;
    }
    // Sudden death: everyone's deck ran out and the extra rounds have elapsed.
    return sudden_death_timeout(game);
}

std::optional<int> winner_seat(const GameState& game)
{
    if (!is_match_over(game)) {
        return std::nullopt;
    }

    // Base-destruction outcome: exactly one team's base(s) survive.
    if (living_base_teams(game) == 1) {
        for (const int seat : game.turn_manager.players) {
            if (player_has_living_base(game, seat)) {
                return seat;  // representative seat of the surviving team
            }
        }
        return std::nullopt;
    }

    // Otherwise this is a sudden-death timeout with multiple teams still standing:
    // the team with the most *combined* base HP wins; a tie is a draw.
    std::map<int, int> team_hp;
    for (const int seat : game.turn_manager.players) {
        team_hp[game.team_of_seat(seat)] += base_hp_for_seat(game, seat);
    }
    int best_team = -1;
    int best_hp = -1;
    bool tie = false;
    for (const auto& [team, hp] : team_hp) {
        if (hp > best_hp) {
            best_hp = hp;
            best_team = team;
            tie = false;
        } else if (hp == best_hp) {
            tie = true;
        }
    }
    if (tie || best_team < 0) {
        return std::nullopt;  // draw
    }
    for (const int seat : game.turn_manager.players) {
        if (game.team_of_seat(seat) == best_team) {
            return seat;  // representative seat of the higher-HP team
        }
    }
    return std::nullopt;
}

}  // namespace tactics::bot