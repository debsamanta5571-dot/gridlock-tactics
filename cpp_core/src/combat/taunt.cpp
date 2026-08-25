#include "tactics/combat/taunt.hpp"

#include "tactics/attributes/attributes.hpp"

#include <algorithm>
#include <cmath>

namespace tactics {
namespace {

std::vector<std::pair<int, int>> footprint_cells_at_anchor(const Entity& entity, int anchor_x, int anchor_y)
{
    std::vector<std::pair<int, int>> cells;
    for (const auto& [dx, dy] : entity_shape_offsets(entity)) {
        cells.push_back({anchor_x + dx, anchor_y + dy});
    }
    return cells;
}

bool footprint_sets_share_orthogonal_edge(const std::vector<std::pair<int, int>>& a, const std::vector<std::pair<int, int>>& b)
{
    for (const auto& [ax, ay] : a) {
        for (const auto& [bx, by] : b) {
            if (world_cells_share_orthogonal_edge(ax, ay, bx, by)) {
                return true;
            }
        }
    }
    return false;
}

std::vector<std::shared_ptr<Entity>> friendly_units_orthogonally_adjacent_to_enemy_taunt(const GameState& game, int controller_seat)
{
    std::vector<std::shared_ptr<Entity>> out;
    for (const std::shared_ptr<Entity>& ent : game.board.all_entities()) {
        const auto unit = std::dynamic_pointer_cast<Unit>(ent);
        if (!unit || !unit->owner || *unit->owner != controller_seat) {
            continue;
        }
        if (entity_orthogonally_adjacent_to_enemy_taunt(game, *unit)) {
            out.push_back(ent);
        }
    }
    return out;
}

}  // namespace

std::optional<std::pair<int, int>> taunt_acting_anchor_for_unit(const GameState& game, const Entity& unit)
{
    if (unit.owner) {
        const std::optional<PendingMoveSelection> pending = game.get_pending_move_for(*unit.owner);
        if (pending && pending->unit_entity_id == unit.entity_id) {
            return std::make_pair(pending->resolved_ax, pending->resolved_ay);
        }
    }
    if (unit.position) {
        return *unit.position;
    }
    return std::nullopt;
}

bool world_cells_share_orthogonal_edge(int ax, int ay, int bx, int by)
{
    const int dx = std::abs(ax - bx);
    const int dy = std::abs(ay - by);
    return (dx == 1 && dy == 0) || (dx == 0 && dy == 1);
}

bool entity_orthogonally_adjacent_at(const Entity& a, int a_anchor_x, int a_anchor_y, const Entity& b, int b_anchor_x, int b_anchor_y)
{
    const auto a_cells = footprint_cells_at_anchor(a, a_anchor_x, a_anchor_y);
    const auto b_cells = footprint_cells_at_anchor(b, b_anchor_x, b_anchor_y);
    return footprint_sets_share_orthogonal_edge(a_cells, b_cells);
}

std::vector<std::shared_ptr<Entity>> enemy_taunts_orthogonally_adjacent_to_anchor(const GameState& game, const Entity& mover, int anchor_x,
    int anchor_y)
{
    std::vector<std::shared_ptr<Entity>> out;
    if (!mover.owner) {
        return out;
    }
    for (const std::shared_ptr<Entity>& ent : game.board.all_entities()) {
        if (!ent || !ent->owner) {
            continue;
        }
        if (ent->entity_id == mover.entity_id) {
            continue;
        }
        if (!has_taunt(*ent) || !teams_hostile(game, *mover.owner, *ent->owner)) {
            continue;
        }
        if (!ent->position) {
            continue;
        }
        if (entity_orthogonally_adjacent_at(mover, anchor_x, anchor_y, *ent, ent->position->first, ent->position->second)) {
            out.push_back(ent);
        }
    }
    return out;
}

bool entity_orthogonally_adjacent_to_enemy_taunt_at(const GameState& game, const Entity& mover, int anchor_x, int anchor_y)
{
    return !enemy_taunts_orthogonally_adjacent_to_anchor(game, mover, anchor_x, anchor_y).empty();
}

bool movement_anchor_stops_under_enemy_taunt(const GameState& game, const Entity& mover, int anchor_x, int anchor_y)
{
    return entity_orthogonally_adjacent_to_enemy_taunt_at(game, mover, anchor_x, anchor_y);
}

bool move_destination_allowed_under_taunt(const GameState& game, const Entity& mover, int from_anchor_x, int from_anchor_y, int dest_anchor_x,
    int dest_anchor_y)
{
    if (movement_anchor_stops_under_enemy_taunt(game, mover, from_anchor_x, from_anchor_y)) {
        return dest_anchor_x == from_anchor_x && dest_anchor_y == from_anchor_y;
    }
    return true;
}

bool path_respects_taunt_stop(const GameState& game, const Entity& mover, const std::vector<std::pair<int, int>>& path)
{
    if (path.size() < 2) {
        return true;
    }
    for (size_t i = 0; i + 1 < path.size(); ++i) {
        const auto& [ax, ay] = path[i];
        if (movement_anchor_stops_under_enemy_taunt(game, mover, ax, ay)) {
            return false;
        }
    }
    return true;
}

bool taunt_allows_board_target(const GameState& game, const Entity* acting_unit, int controller_seat, const Entity& target,
    std::optional<std::pair<int, int>> acting_anchor_override)
{
    if (!target.owner || !teams_hostile(game, controller_seat, *target.owner)) {
        return true;
    }

    if (acting_unit && acting_unit->owner) {
        std::optional<std::pair<int, int>> anchor = acting_anchor_override;
        if (!anchor) {
            anchor = taunt_acting_anchor_for_unit(game, *acting_unit);
        }
        if (!anchor) {
            return true;
        }
        if (!entity_orthogonally_adjacent_to_enemy_taunt_at(game, *acting_unit, anchor->first, anchor->second)) {
            return true;
        }
        if (!has_taunt(target) || !teams_hostile(game, *acting_unit->owner, *target.owner)) {
            return false;
        }
        if (!target.position) {
            return false;
        }
        return entity_orthogonally_adjacent_at(*acting_unit, anchor->first, anchor->second, target, target.position->first, target.position->second);
    }

    const auto restricted_actors = friendly_units_orthogonally_adjacent_to_enemy_taunt(game, controller_seat);
    if (restricted_actors.empty()) {
        return true;
    }
    if (!has_taunt(target)) {
        return false;
    }
    if (!target.position) {
        return false;
    }
    for (const std::shared_ptr<Entity>& actor : restricted_actors) {
        const auto anchor = taunt_acting_anchor_for_unit(game, *actor);
        if (!anchor) {
            continue;
        }
        if (entity_orthogonally_adjacent_at(*actor, anchor->first, anchor->second, target, target.position->first, target.position->second)) {
            return true;
        }
    }
    return false;
}

}  // namespace tactics
