#include "tactics/board/aether.hpp"
#include "tactics/board/board_layout.hpp"
#include "tactics/bot/bot_evaluator.hpp"
#include "tactics/core/game_state.hpp"
#include "tactics/entities/entity.hpp"

#include <iostream>
#include <memory>
#include <string>

using tactics::GameState;
using tactics::Unit;
using tactics::bot::objective_cell_capture_value;
using tactics::normalize_entity_shape;

namespace {

int g_fails = 0;

#define REQUIRE(cond)                                                                                                  \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << " " #cond "\n";                                     \
            ++g_fails;                                                                                                 \
        }                                                                                                              \
    } while (0)

std::shared_ptr<Unit> place_unit(GameState& game, const std::string& id, int owner, int hp, int x, int y)
{
    auto u = std::make_shared<Unit>();
    u->entity_id = id;
    u->entity_type = "unit";
    u->unit_type = "Infantry";
    u->owner = owner;
    u->base_health = hp;
    u->current_health = hp;
    u->movement = 3;
    u->melee_damage = 3;
    u->shape = {{0, 0}};
    normalize_entity_shape(*u);
    if (!game.board.place_entity(u, x, y)) {
        std::cerr << "FAIL place_unit " << id << " at " << x << "," << y << "\n";
        ++g_fails;
        return nullptr;
    }
    game.note_entity_placed(u);
    return u;
}

GameState make_duel()
{
    GameState game("aether_ai_test", uint64_t{1});
    game.add_player(1, "P1");
    game.add_player(2, "P2");
    game.start_game();
    tactics::seed_standard_duel_aether_tiles(game);
    game.turn_manager.round_number = tactics::kObjectiveActivationRound;
    return game;
}

void test_aether_kills_and_tracks_units()
{
    GameState game = make_duel();
    REQUIRE(tactics::aether_units_killed_total(game) == 0);

    auto weak_a = place_unit(game, "weak_a", 1, 1, 3, 5);
    auto weak_b = place_unit(game, "weak_b", 1, 1, 4, 5);
    REQUIRE(weak_a && weak_a->current_health == 1);
    REQUIRE(weak_b && weak_b->current_health == 1);

    tactics::process_aether_tiles_at_turn_start(game, 1);

    REQUIRE(tactics::aether_units_killed_total(game) == 2);
    REQUIRE(game.board.all_entities_map.find("weak_a") == game.board.all_entities_map.end());
    REQUIRE(game.board.all_entities_map.find("weak_b") == game.board.all_entities_map.end());
}

void test_aether_does_not_kill_soaking_unit()
{
    GameState game = make_duel();
    auto tank = place_unit(game, "tank", 1, 8, 3, 5);
    REQUIRE(tank && tank->current_health == 8);

    tactics::process_aether_tiles_at_turn_start(game, 1);

    REQUIRE(tactics::aether_units_killed_total(game) == 0);
    const auto it = game.board.all_entities_map.find("tank");
    REQUIRE(it != game.board.all_entities_map.end() && it->second);
    REQUIRE(it->second->current_health == 7);
}

void test_ai_refuses_lethal_aether_hold()
{
    GameState game = make_duel();
    auto fodder = place_unit(game, "fodder", 1, 1, 2, 3);
    auto soak = place_unit(game, "soak", 1, 8, 5, 3);
    REQUIRE(fodder);
    REQUIRE(soak);

    const double fodder_center = objective_cell_capture_value(game, 1, *fodder, 3, 5);
    const double soak_center = objective_cell_capture_value(game, 1, *soak, 3, 5);
    REQUIRE(fodder_center == 0.0);
    REQUIRE(soak_center > 0.0);
    REQUIRE(tactics::aether_would_kill_entity(game, 1, *fodder, 3, 5));
    REQUIRE(!tactics::aether_would_kill_entity(game, 1, *soak, 3, 5));
}

void test_ai_refuses_lethal_aether_even_to_contest()
{
    GameState game = make_duel();
    auto enemy = place_unit(game, "enemy", 2, 6, 4, 6);
    auto fodder = place_unit(game, "fodder", 1, 1, 2, 3);
    REQUIRE(enemy);
    REQUIRE(fodder);

    const double contest = objective_cell_capture_value(game, 1, *fodder, 3, 5);
    REQUIRE(contest == 0.0);
}

}  // namespace

int main()
{
    test_aether_kills_and_tracks_units();
    test_aether_does_not_kill_soaking_unit();
    test_ai_refuses_lethal_aether_hold();
    test_ai_refuses_lethal_aether_even_to_contest();
    if (g_fails != 0) {
        std::cerr << "aether_bot_test: " << g_fails << " failed\n";
        return 1;
    }
    std::cout << "aether_bot_test: all passed\n";
    return 0;
}
