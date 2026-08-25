#include "tactics/board/omni_energy_tile.hpp"

#include "tactics/board/aether.hpp"
#include "tactics/board/board_layout.hpp"
#include "tactics/board/tile_modifiers.hpp"
#include "tactics/core/game_state.hpp"
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

void grant_floating_omni_energy(GameState& game, const int player_id, const int amount)
{
    if (amount <= 0) {
        return;
    }
    auto itp = game.turn_manager.player_energy.find(player_id);
    if (itp == game.turn_manager.player_energy.end()) {
        return;
    }
    itp->second[EnergyType::Omni] += amount;
}

void process_omni_energy_cluster_at_turn_start(GameState& game, const int active_player_id,
    OmniEnergyClusterState& cluster)
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

    grant_floating_omni_energy(game, active_player_id, 1);
    cluster.teams_fired_this_round.insert(active_team);
}

}  // namespace

bool square_has_omni_energy(const GridSquare& sq)
{
    const SquareModifier* terrain = square_terrain_modifier(sq);
    return terrain && terrain->name == kOmniEnergyModifierName;
}

bool is_omni_energy_world_cell(const GameState& game, const int wx, const int wy)
{
    return find_omni_energy_cluster_at_cell(game, wx, wy) != nullptr;
}

bool place_omni_energy_on_square(std::shared_ptr<GridSquare> sq)
{
    if (!sq) {
        return false;
    }
    set_terrain_modifier(*sq, SquareModifier{
        .name = kOmniEnergyModifierName,
        .layer = TileLayer::Terrain,
        .movement_cost = 1.0f,
    });
    return true;
}

bool seed_omni_energy_cluster(GameState& game, const OmniEnergyClusterSpec& spec)
{
    if (spec.cluster_id.empty() || spec.cells.empty() || spec.home_seat <= 0) {
        return false;
    }
    for (const auto& [wx, wy] : spec.cells) {
        place_omni_energy_on_square(game.board.get_square(wx, wy));
    }
    for (OmniEnergyClusterState& cluster : game.omni_energy_clusters_) {
        if (cluster.cluster_id == spec.cluster_id) {
            cluster.home_seat = spec.home_seat;
            cluster.cells = spec.cells;
            return true;
        }
    }
    OmniEnergyClusterState cluster;
    cluster.cluster_id = spec.cluster_id;
    cluster.home_seat = spec.home_seat;
    cluster.cells = spec.cells;
    game.omni_energy_clusters_.push_back(std::move(cluster));
    return true;
}

void seed_standard_duel_omni_energy_tiles(GameState& game)
{
    if (game.board_width() != kStandardBoardWidth || game.board_height() != kStandardBoardHeight) {
        return;
    }
    if (game.board_layout().layout_id != kDefaultBoardLayoutId) {
        return;
    }
    seed_omni_energy_cluster(game, make_standard_duel_p1_omni_energy_cluster());
    seed_omni_energy_cluster(game, make_standard_duel_p2_omni_energy_cluster());
}

const OmniEnergyClusterState* find_omni_energy_cluster_at_cell(const GameState& game, const int wx, const int wy)
{
    for (const OmniEnergyClusterState& cluster : game.omni_energy_clusters_) {
        if (cell_in_cluster(cluster.cells, wx, wy)) {
            return &cluster;
        }
    }
    return nullptr;
}

const OmniEnergyClusterState* find_omni_energy_cluster_by_id(const GameState& game, const std::string& cluster_id)
{
    for (const OmniEnergyClusterState& cluster : game.omni_energy_clusters_) {
        if (cluster.cluster_id == cluster_id) {
            return &cluster;
        }
    }
    return nullptr;
}

bool entity_on_omni_energy_tile(const GameState& game, const Entity& e)
{
    for (const OmniEnergyClusterState& cluster : game.omni_energy_clusters_) {
        if (entity_on_cluster_cells(e, cluster.cells)) {
            return true;
        }
    }
    return false;
}

void clear_omni_energy_teams_fired_for_new_round(GameState& game)
{
    for (OmniEnergyClusterState& cluster : game.omni_energy_clusters_) {
        cluster.teams_fired_this_round.clear();
    }
}

void process_omni_energy_tiles_at_turn_start(GameState& game, const int active_player_id)
{
    // All permanent objective tiles come online together at the activation round.
    if (game.turn_manager.round_number < kObjectiveActivationRound) {
        return;
    }
    for (OmniEnergyClusterState& cluster : game.omni_energy_clusters_) {
        process_omni_energy_cluster_at_turn_start(game, active_player_id, cluster);
    }
}

}  // namespace tactics