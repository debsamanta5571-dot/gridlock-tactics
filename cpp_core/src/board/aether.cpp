#include "tactics/board/aether.hpp"

#include "tactics/attributes/attributes.hpp"
#include "tactics/board/board_layout.hpp"
#include "tactics/board/tile_modifiers.hpp"
#include "tactics/combat/combat_resolver.hpp"
#include "tactics/core/game_state.hpp"
#include "tactics/entities/entity.hpp"

#include <algorithm>
#include <climits>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>

namespace tactics {
namespace {

/** True if (x, y) is one of this entity's occupied cells. */
bool cell_on_entity_footprint(const Entity& ent, const int x, const int y)
{
    if (!ent.occupied_positions.empty()) {
        for (const auto& [wx, wy] : ent.occupied_positions) {
            if (wx == x && wy == y) {
                return true;
            }
        }
        return false;
    }
    if (!ent.position) {
        return false;
    }
    const auto [ax, ay] = *ent.position;
    for (const auto& [dx, dy] : entity_shape_offsets(ent)) {
        if (ax + dx == x && ay + dy == y) {
            return true;
        }
    }
    return false;
}

/** True if (x, y) is listed in the cluster's cells. */
bool cell_in_cluster(const std::vector<std::pair<int, int>>& cells, const int x, const int y)
{
    for (const auto& [cx, cy] : cells) {
        if (cx == x && cy == y) {
            return true;
        }
    }
    return false;
}

/** Teams that currently have a control-counting unit on this cluster. */
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

/** True if at least two of these teams are enemies of each other. */
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

std::vector<std::pair<int, int>> base_cells_for_seat(const GameState& game, const int seat)
{
    std::vector<std::pair<int, int>> cells;
    const auto it = game.board.all_entities_map.find("base_p" + std::to_string(seat));
    if (it == game.board.all_entities_map.end() || !it->second) {
        return cells;
    }
    const Entity& base = *it->second;
    if (base.position) {
        const auto [bx, by] = *base.position;
        for (const auto& [dx, dy] : entity_shape_offsets(base)) {
            cells.emplace_back(bx + dx, by + dy);
        }
    } else {
        cells = base.occupied_positions;
    }
    return cells;
}

std::vector<std::pair<int, int>> controller_base_cells(const GameState& game, const int active_player_id)
{
    std::vector<std::pair<int, int>> cells = base_cells_for_seat(game, active_player_id);
    if (!cells.empty()) {
        return cells;
    }
    const int team = game.team_of_seat(active_player_id);
    for (const int seat : game.turn_manager.players) {
        if (game.team_of_seat(seat) != team) {
            continue;
        }
        cells = base_cells_for_seat(game, seat);
        if (!cells.empty()) {
            return cells;
        }
    }
    return cells;
}

int chebyshev_between_cell_sets(const std::vector<std::pair<int, int>>& from,
    const std::vector<std::pair<int, int>>& to)
{
    int best = INT_MAX;
    for (const auto& [fx, fy] : from) {
        for (const auto& [tx, ty] : to) {
            best = std::min(best, std::max(std::abs(fx - tx), std::abs(fy - ty)));
        }
    }
    return best == INT_MAX ? 999 : best;
}

std::shared_ptr<Entity> closest_hostile_base_to_controller(const GameState& game, const int active_player_id,
    const std::vector<std::pair<int, int>>& controller_cells)
{
    if (controller_cells.empty()) {
        return nullptr;
    }
    std::shared_ptr<Entity> closest;
    int best_distance = INT_MAX;
    for (const auto& [_, ent] : game.board.all_entities_map) {
        if (!ent || ent->current_health <= 0 || !entity_is_base(*ent) || !ent->owner) {
            continue;
        }
        if (!teams_hostile(game, active_player_id, *ent->owner)) {
            continue;
        }
        const std::vector<std::pair<int, int>> enemy_cells = base_cells_for_seat(game, *ent->owner);
        if (enemy_cells.empty()) {
            continue;
        }
        const int dist = chebyshev_between_cell_sets(controller_cells, enemy_cells);
        if (dist < best_distance) {
            best_distance = dist;
            closest = ent;
        }
    }
    return closest;
}

void deal_aether_damage(GameState& game, const std::shared_ptr<Entity>& target, const int amount,
    const std::string& label)
{
    if (!target || amount <= 0 || target->current_health <= 0) {
        return;
    }
    DamagePacket packet;
    packet.target = target;
    packet.amount = amount;
    packet.suppress_source_damage_bonuses = true;
    packet.source_label = label;
    (void)apply_damage_packet(game, packet);
}

void process_aether_cluster_at_turn_start(GameState& game, const int active_player_id, AetherClusterState& cluster)
{
    const int active_team = game.team_of_seat(active_player_id);
    const std::set<int> teams_on_cluster = teams_with_control_units_on_cluster(game, cluster.cells);

    if (teams_on_cluster.empty()) {
        cluster.damage_next = 1;
        cluster.last_sole_control_team.reset();
        return;
    }

    if (!teams_on_cluster.count(active_team)) {
        return;
    }

    if (teams_are_mutually_hostile(game, teams_on_cluster)) {
        return;
    }

    if (cluster.teams_fired_this_round.count(active_team) > 0) {
        return;
    }

    if (cluster.last_sole_control_team.has_value() && *cluster.last_sole_control_team != active_team) {
        cluster.damage_next = 1;
    }

    const int damage = std::max(1, cluster.damage_next);
    const std::string label = "Aether " + cluster.cluster_id + " (" + std::to_string(damage) + ")";

    // Collect targets first, then damage them: dealing aether damage can kill an entity and
    // remove it from all_entities_map (or spawn death-trigger entities), which would invalidate
    // this iterator mid-loop. Holding shared_ptrs keeps the victims alive through resolution.
    std::vector<std::shared_ptr<Entity>> targets;
    std::set<std::string> seen;
    for (const auto& [_, ent] : game.board.all_entities_map) {
        if (!ent || ent->current_health <= 0 || entity_is_pickup(*ent)
            || !entity_on_cluster_cells(*ent, cluster.cells)) {
            continue;
        }
        if (!seen.insert(ent->entity_id).second) {
            continue;
        }
        targets.push_back(ent);
    }
    for (const std::shared_ptr<Entity>& ent : targets) {
        const bool was_alive = ent && ent->current_health > 0;
        const bool is_unit = ent && !entity_is_base(*ent);
        deal_aether_damage(game, ent, damage, label);
        if (was_alive && is_unit && ent && ent->current_health <= 0) {
            ++cluster.units_killed;
        }
    }

    const std::vector<std::pair<int, int>> controller_cells = controller_base_cells(game, active_player_id);
    if (const std::shared_ptr<Entity> target_base =
            closest_hostile_base_to_controller(game, active_player_id, controller_cells)) {
        deal_aether_damage(game, target_base, damage, label);
    }

    cluster.last_sole_control_team = active_team;
    cluster.damage_next = damage + 1;
    cluster.teams_fired_this_round.insert(active_team);
}

}  // namespace

bool square_has_aether(const GridSquare& sq)
{
    const SquareModifier* terrain = square_terrain_modifier(sq);
    return terrain && terrain->name == kAetherModifierName;
}

bool is_aether_world_cell(const GameState& game, const int wx, const int wy)
{
    return find_aether_cluster_at_cell(game, wx, wy) != nullptr;
}

bool place_aether_on_square(std::shared_ptr<GridSquare> sq)
{
    if (!sq) {
        return false;
    }
    set_terrain_modifier(*sq, SquareModifier{
        .name = kAetherModifierName,
        .layer = TileLayer::Terrain,
        .movement_cost = 1.0f,
    });
    return true;
}

bool seed_aether_cluster(GameState& game, const AetherClusterSpec& spec)
{
    if (spec.cluster_id.empty() || spec.cells.empty()) {
        return false;
    }
    for (const auto& [wx, wy] : spec.cells) {
        place_aether_on_square(game.board.get_square(wx, wy));
    }
    for (AetherClusterState& cluster : game.aether_clusters_) {
        if (cluster.cluster_id == spec.cluster_id) {
            cluster.cells = spec.cells;
            return true;
        }
    }
    AetherClusterState cluster;
    cluster.cluster_id = spec.cluster_id;
    cluster.cells = spec.cells;
    game.aether_clusters_.push_back(std::move(cluster));
    return true;
}

void seed_standard_duel_aether_tiles(GameState& game)
{
    if (game.board_width() != kStandardBoardWidth || game.board_height() != kStandardBoardHeight) {
        return;
    }
    if (game.board_layout().layout_id != kDefaultBoardLayoutId) {
        return;
    }
    seed_aether_cluster(game, make_standard_duel_center_aether_cluster());
}

const AetherClusterState* find_aether_cluster_at_cell(const GameState& game, const int wx, const int wy)
{
    for (const AetherClusterState& cluster : game.aether_clusters_) {
        if (cell_in_cluster(cluster.cells, wx, wy)) {
            return &cluster;
        }
    }
    return nullptr;
}

const AetherClusterState* find_aether_cluster_by_id(const GameState& game, const std::string& cluster_id)
{
    for (const AetherClusterState& cluster : game.aether_clusters_) {
        if (cluster.cluster_id == cluster_id) {
            return &cluster;
        }
    }
    return nullptr;
}

bool entity_counts_for_aether_control(const Entity& e)
{
    if (e.current_health <= 0 || !e.owner || entity_is_base(e) || entity_is_pickup(e) || has_large_unit(e)) {
        return false;
    }
    if (e.entity_type == "unit" || e.entity_type == "low_cover") {
        return true;
    }
    if (entity_is_structure(e) && entity_shape_offsets(e).size() == 1) {
        return true;
    }
    return false;
}

bool entity_on_cluster_cells(const Entity& e, const std::vector<std::pair<int, int>>& cluster_cells)
{
    for (const auto& [wx, wy] : cluster_cells) {
        if (cell_on_entity_footprint(e, wx, wy)) {
            return true;
        }
    }
    return false;
}

bool entity_on_aether_tile(const GameState& game, const Entity& e)
{
    for (const AetherClusterState& cluster : game.aether_clusters_) {
        if (entity_on_cluster_cells(e, cluster.cells)) {
            return true;
        }
    }
    return false;
}

int aether_predicted_burst_damage(const GameState& game, const int seat, const int x, const int y)
{
    const AetherClusterState* cluster = find_aether_cluster_at_cell(game, x, y);
    if (!cluster) {
        return 0;
    }
    const int seat_team = game.team_of_seat(seat);
    if (!cluster->last_sole_control_team.has_value() || *cluster->last_sole_control_team != seat_team) {
        return 1;
    }
    return std::max(1, cluster->damage_next);
}

bool aether_would_kill_entity(const GameState& game, const int seat, const Entity& e, const int x, const int y)
{
    if (e.current_health <= 0 || entity_is_base(e) || entity_is_pickup(e)) {
        return false;
    }
    const int burst = aether_predicted_burst_damage(game, seat, x, y);
    return burst > 0 && e.current_health <= burst;
}

int aether_units_killed_total(const GameState& game)
{
    int total = 0;
    for (const AetherClusterState& cluster : game.aether_clusters_) {
        total += cluster.units_killed;
    }
    return total;
}

void clear_aether_teams_fired_for_new_round(GameState& game)
{
    for (AetherClusterState& cluster : game.aether_clusters_) {
        cluster.teams_fired_this_round.clear();
    }
}

void process_aether_tiles_at_turn_start(GameState& game, const int active_player_id)
{
    if (game.turn_manager.round_number < kObjectiveActivationRound) {
        return;
    }
    for (AetherClusterState& cluster : game.aether_clusters_) {
        process_aether_cluster_at_turn_start(game, active_player_id, cluster);
    }
}

}  // namespace tactics