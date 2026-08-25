#include "tactics/board/scanner.hpp"

#include "tactics/board/aether.hpp"
#include "tactics/board/board_layout.hpp"
#include "tactics/board/tile_modifiers.hpp"
#include "tactics/common/effect_keys.hpp"
#include "tactics/core/game_state.hpp"
#include "tactics/core/stack.hpp"
#include "tactics/entities/entity.hpp"

#include <set>
#include <string>
#include <vector>

namespace tactics {
namespace {

bool cell_in_cluster(const std::vector<std::pair<int, int>>& cells, const int x, const int y)
{
    for (const auto& [cx, cy] : cells) {
        if (cx == x && cy == y) {
            return true;
        }
    }
    return false;
}

std::set<int> teams_with_control_units_on_cluster(const GameState& game,
    const std::vector<std::pair<int, int>>& cluster_cells)
{
    std::set<int> teams;
    for (const auto& [_, ent] : game.board.all_entities_map) {
        if (!ent || ent->current_health <= 0 || !ent->owner) {
            continue;
        }
        if (!entity_counts_for_aether_control(*ent) || !entity_on_cluster_cells(*ent, cluster_cells)) {
            continue;
        }
        teams.insert(game.team_of_seat(*ent->owner));
    }
    return teams;
}

bool teams_are_mutually_hostile(const GameState& game, const std::set<int>& teams)
{
    for (const int team_a : teams) {
        for (const int team_b : teams) {
            if (team_a >= team_b) {
                continue;
            }
            for (const int seat_a : game.turn_manager.players) {
                if (game.team_of_seat(seat_a) != team_a) {
                    continue;
                }
                for (const int seat_b : game.turn_manager.players) {
                    if (game.team_of_seat(seat_b) != team_b) {
                        continue;
                    }
                    if (teams_hostile(game, seat_a, seat_b)) {
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

StackItem make_scanner_scan_item(const int player_id)
{
    StackItem item;
    item.controller_id = player_id;
    item.effect_key = "scan";
    item.payload[effect_keys::kPayloadAmount] = 1;
    return item;
}

void process_scanner_cluster_at_turn_start(GameState& game, const int active_player_id, ScannerClusterState& cluster)
{
    if (cluster.home_seat <= 0 || !teams_hostile(game, active_player_id, cluster.home_seat)) {
        return;
    }

    const int active_team = game.team_of_seat(active_player_id);
    const std::set<int> teams_on_cluster = teams_with_control_units_on_cluster(game, cluster.cells);

    if (teams_on_cluster.empty() || !teams_on_cluster.count(active_team)) {
        return;
    }
    if (teams_are_mutually_hostile(game, teams_on_cluster)) {
        return;
    }
    if (cluster.teams_fired_this_round.count(active_team) > 0) {
        return;
    }
    if (game.IsAwaitingScan()) {
        return;
    }

    const ActionResult result = game.stack_manager.resolve_item_direct(game, make_scanner_scan_item(active_player_id));
    if (!result.ok) {
        return;
    }

    cluster.teams_fired_this_round.insert(active_team);
}

}  // namespace

bool square_has_scanner(const GridSquare& sq)
{
    const SquareModifier* terrain = square_terrain_modifier(sq);
    return terrain && terrain->name == kScannerModifierName;
}

bool is_scanner_world_cell(const GameState& game, const int wx, const int wy)
{
    return find_scanner_cluster_at_cell(game, wx, wy) != nullptr;
}

bool place_scanner_on_square(std::shared_ptr<GridSquare> sq)
{
    if (!sq) {
        return false;
    }
    set_terrain_modifier(*sq, SquareModifier{
        .name = kScannerModifierName,
        .layer = TileLayer::Terrain,
        .movement_cost = 1.0f,
    });
    return true;
}

bool seed_scanner_cluster(GameState& game, const ScannerClusterSpec& spec)
{
    if (spec.cluster_id.empty() || spec.cells.empty() || spec.home_seat <= 0) {
        return false;
    }
    for (const auto& [wx, wy] : spec.cells) {
        place_scanner_on_square(game.board.get_square(wx, wy));
    }
    for (ScannerClusterState& cluster : game.scanner_clusters_) {
        if (cluster.cluster_id == spec.cluster_id) {
            cluster.home_seat = spec.home_seat;
            cluster.cells = spec.cells;
            return true;
        }
    }
    ScannerClusterState cluster;
    cluster.cluster_id = spec.cluster_id;
    cluster.home_seat = spec.home_seat;
    cluster.cells = spec.cells;
    game.scanner_clusters_.push_back(std::move(cluster));
    return true;
}

void seed_standard_duel_scanner_tiles(GameState& game)
{
    if (game.board_width() != kStandardBoardWidth || game.board_height() != kStandardBoardHeight) {
        return;
    }
    if (game.board_layout().layout_id != kDefaultBoardLayoutId) {
        return;
    }
    seed_scanner_cluster(game, make_standard_duel_p1_scanner_cluster());
    seed_scanner_cluster(game, make_standard_duel_p2_scanner_cluster());
}

const ScannerClusterState* find_scanner_cluster_at_cell(const GameState& game, const int wx, const int wy)
{
    for (const ScannerClusterState& cluster : game.scanner_clusters_) {
        if (cell_in_cluster(cluster.cells, wx, wy)) {
            return &cluster;
        }
    }
    return nullptr;
}

const ScannerClusterState* find_scanner_cluster_by_id(const GameState& game, const std::string& cluster_id)
{
    for (const ScannerClusterState& cluster : game.scanner_clusters_) {
        if (cluster.cluster_id == cluster_id) {
            return &cluster;
        }
    }
    return nullptr;
}

bool entity_on_scanner_tile(const GameState& game, const Entity& e)
{
    for (const ScannerClusterState& cluster : game.scanner_clusters_) {
        if (entity_on_cluster_cells(e, cluster.cells)) {
            return true;
        }
    }
    return false;
}

void clear_scanner_teams_fired_for_new_round(GameState& game)
{
    for (ScannerClusterState& cluster : game.scanner_clusters_) {
        cluster.teams_fired_this_round.clear();
    }
}

void process_scanner_tiles_at_turn_start(GameState& game, const int active_player_id)
{
    // All permanent objective tiles come online together at the activation round.
    if (game.turn_manager.round_number < kObjectiveActivationRound) {
        return;
    }
    for (ScannerClusterState& cluster : game.scanner_clusters_) {
        process_scanner_cluster_at_turn_start(game, active_player_id, cluster);
        if (game.IsAwaitingScan()) {
            break;
        }
    }
}

}  // namespace tactics