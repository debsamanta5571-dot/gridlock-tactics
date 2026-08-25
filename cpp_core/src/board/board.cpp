#include "tactics/board/board.hpp"

#include "tactics/attributes/attributes.hpp"
#include "tactics/board/movement_policy.hpp"

#include <algorithm>

namespace tactics {

bool GameBoard::add_modular_grid(const std::string& module_id, int width, int height, int ox, int oy) {
    auto gm = std::make_shared<GridManager>(ox, oy);
    gm->initialize_grid(width, height);
    for (const auto& [k, v] : gm->squares()) {
        auto it = global_squares_.find(k);
        if (it != global_squares_.end() && it->second != v) return false;
    }
    grids_[module_id] = gm;
    for (const auto& [k, v] : gm->squares()) global_squares_[k] = v;
    bounds_dirty_ = true;
    return true;
}

bool GameBoard::add_cell_module(const std::string& module_id, const std::vector<std::pair<int, int>>& world_cells)
{
    if (world_cells.empty()) {
        return false;
    }
    auto gm = std::make_shared<GridManager>(0, 0);
    gm->add_world_cells(world_cells);
    for (const auto& [k, v] : gm->squares()) {
        auto it = global_squares_.find(k);
        if (it != global_squares_.end() && it->second != v) {
            return false;
        }
    }
    grids_[module_id] = gm;
    for (const auto& [k, v] : gm->squares()) {
        global_squares_[k] = v;
    }
    bounds_dirty_ = true;
    return true;
}

void GameBoard::clear_geometry()
{
    for (auto& [_, sq] : global_squares_) {
        if (sq) {
            sq->entity.reset();
            sq->occupied = false;
            sq->modifiers.clear();
        }
    }
    grids_.clear();
    global_squares_.clear();
    all_entities_map.clear();
    bounds_dirty_ = true;
}

BoardCellBounds GameBoard::cell_bounds() const {
    if (!bounds_dirty_) return bounds_cache_;
    bounds_dirty_ = false;
    BoardCellBounds b;
    if (global_squares_.empty()) { bounds_cache_ = b; return b; }
    b.min_x = b.max_x = global_squares_.begin()->second->x;
    b.min_y = b.max_y = global_squares_.begin()->second->y;
    for (const auto& [_, sq] : global_squares_) {
        if (!sq) continue;
        b.min_x = std::min(b.min_x, sq->x);
        b.max_x = std::max(b.max_x, sq->x);
        b.min_y = std::min(b.min_y, sq->y);
        b.max_y = std::max(b.max_y, sq->y);
    }
    bounds_cache_ = b;
    return b;
}

std::shared_ptr<GridSquare> GameBoard::get_square(int x, int y) const {
    auto it = global_squares_.find(board_cell_key(x, y));
    return it == global_squares_.end() ? nullptr : it->second;
}

std::shared_ptr<Entity> GameBoard::entity_at(int x, int y) const {
    auto s = get_square(x, y);
    return s ? s->entity : nullptr;
}

std::shared_ptr<GridManager> GameBoard::grid_for_cell(int x, int y) const
{
    const auto sq = get_square(x, y);
    if (!sq) {
        return nullptr;
    }
    for (const auto& [_, gm] : grids_) {
        if (gm && gm->get_square(x, y) == sq) {
            return gm;
        }
    }
    return nullptr;
}

bool GameBoard::can_place_entity_at(const std::shared_ptr<Entity>& entity, int x, int y) const {
    if (const auto gm = grid_for_cell(x, y)) {
        return gm->can_place_entity(entity, x, y);
    }
    return false;
}

bool GameBoard::can_place_shape_at(int anchor_x, int anchor_y, const std::vector<std::pair<int, int>>& offsets,
                                   const std::shared_ptr<Entity>& mover) const {
    if (!mover) {
        return false;
    }
    for (const auto& [dx, dy] : offsets) {
        auto s = get_square(anchor_x + dx, anchor_y + dy);
        // Pickups are collected at move-confirm time; units can land on pickup-occupied cells.
        // Crushing Advance (crushes_on_move) additionally allows landing on occupied cells,
        // except cells with indestructible entities or units of the same type as the crusher.
        if (!s || (s->occupied && s->entity != mover && !(s->entity && entity_is_pickup(*s->entity)))) {
            if (!entity_has_attribute(*mover, "crushes_on_move")) return false;
            if (s && s->entity) {
                if (entity_has_attribute(*s->entity, "indestructible")) return false;
                const auto occ_unit = std::dynamic_pointer_cast<Unit>(s->entity);
                const auto mover_unit = std::dynamic_pointer_cast<Unit>(mover);
                if (occ_unit && mover_unit && occ_unit->unit_type == mover_unit->unit_type) return false;
            }
        }
        if (!entity_can_land_on_square(*mover, *s)) {
            return false;
        }
    }
    if (entity_uses_void_threshold_for_landing(*mover)) {
        return footprint_void_fraction_below_threshold(
            [this](int x, int y) { return get_square(x, y); }, offsets, anchor_x, anchor_y);
    }
    return true;
}

bool GameBoard::place_entity(const std::shared_ptr<Entity>& entity, int x, int y) {
    if (const auto gm = grid_for_cell(x, y)) {
        if (gm->place_entity(entity, x, y)) {
            all_entities_map[entity->entity_id] = entity;
            return true;
        }
    }
    return false;
}

bool GameBoard::remove_entity(const std::shared_ptr<Entity>& entity) {
    bool any = false;
    for (const auto& [_, gm] : grids_) {
        if (gm->remove_entity(entity)) any = true;
    }
    if (any) {
        all_entities_map.erase(entity->entity_id);
    }
    return any;
}

float GameBoard::movement_cost(int x, int y) const {
    if (const auto gm = grid_for_cell(x, y)) {
        return gm->movement_cost(x, y);
    }
    return kUnreachableTileMovementCost;
}

std::optional<std::vector<std::pair<int, int>>> GameBoard::find_path(const std::shared_ptr<Entity>& entity, int x, int y,
    const GameState* taunt_rules) const {
    if (!entity || !entity->position) return std::nullopt;
    if (const auto gm = grid_for_cell(entity->position->first, entity->position->second)) {
        return gm->get_square(x, y) ? gm->find_path(entity, x, y, taunt_rules) : std::nullopt;
    }
    return std::nullopt;
}

std::vector<std::pair<int, int>> GameBoard::find_reachable_anchors(const std::shared_ptr<Entity>& entity, const GameState* taunt_rules) const {
    if (!entity || !entity->position) {
        return {};
    }
    if (const auto gm = grid_for_cell(entity->position->first, entity->position->second)) {
        return gm->find_reachable_anchors(entity, taunt_rules);
    }
    return {};
}

std::vector<std::pair<int, int>> GameBoard::find_emergency_step_anchors(const std::shared_ptr<Entity>& entity, const GameState* taunt_rules) const {
    if (!entity || !entity->position) {
        return {};
    }
    if (const auto gm = grid_for_cell(entity->position->first, entity->position->second)) {
        return gm->find_emergency_step_anchors(entity, taunt_rules);
    }
    return {};
}

LOSResult GameBoard::line_of_sight(std::pair<int, int> start, std::pair<int, int> end) const {
    if (const auto gm = grid_for_cell(start.first, start.second)) {
        return gm->line_of_sight(start, end);
    }
    LOSResult out;
    out.query_valid = false;
    out.clear = false;
    out.path.push_back(start);
    return out;
}

std::vector<std::shared_ptr<Entity>> GameBoard::all_entities() const {
    std::vector<std::shared_ptr<Entity>> out;
    for (const auto& [_, e] : all_entities_map) out.push_back(e);
    return out;
}

}  // namespace tactics
