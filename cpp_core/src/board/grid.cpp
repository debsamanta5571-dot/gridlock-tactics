#include "tactics/board/grid.hpp"

#include "tactics/attributes/attributes.hpp"
#include "tactics/board/adjacency.hpp"
#include "tactics/board/movement_policy.hpp"
#include "tactics/combat/taunt.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <queue>
#include <tuple>
#include <vector>

namespace tactics {

int footprint_step_terrain_damage(const GetGridSquareFn& get_square, const Entity& entity, int from_x, int from_y, int to_x, int to_y);

namespace {

constexpr float kPathEps = 1e-4f;

// Use kSurroundingOffsets from adjacency.hpp - the canonical 8-direction array.

struct PathLabel {
    int damage{0};
    float move{0.0f};
    float taunt_pref{0.0f};
    int x{0};
    int y{0};
    int prev{-1};
    bool active{true};
};

bool dominates_path_label(const PathLabel& a, int damage, float move, float taunt_pref) {
    return a.active && a.damage <= damage && a.move <= move + kPathEps && a.taunt_pref <= taunt_pref + kPathEps;
}

bool label_is_stale(const std::vector<PathLabel>& labels, int label_id, int damage, float move, float taunt_pref) {
    if (label_id < 0 || label_id >= static_cast<int>(labels.size())) {
        return true;
    }
    const PathLabel& label = labels[static_cast<size_t>(label_id)];
    return !label.active || label.damage != damage || std::abs(label.move - move) > kPathEps ||
        std::abs(label.taunt_pref - taunt_pref) > kPathEps;
}

bool add_path_label(std::vector<PathLabel>& labels, std::map<std::pair<int, int>, std::vector<int>>& labels_by_cell, PathLabel label, int& out_id) {
    const auto cell = std::make_pair(label.x, label.y);
    auto& cell_labels = labels_by_cell[cell];
    for (int id : cell_labels) {
        if (dominates_path_label(labels[static_cast<size_t>(id)], label.damage, label.move, label.taunt_pref)) {
            return false;
        }
    }
    for (int id : cell_labels) {
        PathLabel& existing = labels[static_cast<size_t>(id)];
        if (existing.active && label.damage <= existing.damage && label.move <= existing.move + kPathEps &&
            label.taunt_pref <= existing.taunt_pref + kPathEps) {
            existing.active = false;
        }
    }
    out_id = static_cast<int>(labels.size());
    labels.push_back(label);
    cell_labels.push_back(out_id);
    return true;
}

bool square_blocks_los(const GridSquare& sq) {
    for (const auto& mod : sq.modifiers) {
        if (mod.blocks_line_of_sight) return true;
    }
    return false;
}

bool contains_cell(const std::vector<std::pair<int, int>>& cells, int x, int y) {
    return std::find(cells.begin(), cells.end(), std::make_pair(x, y)) != cells.end();
}

std::vector<std::pair<int, int>> footprint_cells_at(const Entity& entity, int anchor_x, int anchor_y) {
    std::vector<std::pair<int, int>> cells;
    const auto offs = entity_shape_offsets(entity);
    cells.reserve(offs.size());
    for (const auto& [dx, dy] : offs) {
        cells.push_back({anchor_x + dx, anchor_y + dy});
    }
    return cells;
}

bool anchor_in_bounds(const GridManager& grid, const Entity& entity, int anchor_x, int anchor_y) {
    for (const auto& [dx, dy] : entity_shape_offsets(entity)) {
        if (!grid.get_square(anchor_x + dx, anchor_y + dy)) {
            return false;
        }
    }
    return true;
}

bool anchor_landing_allowed(const GridManager& grid, const Entity& entity, int anchor_x, int anchor_y) {
    const bool uses_threshold = entity_uses_void_threshold_for_landing(entity);
    for (const auto& [dx, dy] : entity_shape_offsets(entity)) {
        auto sq = grid.get_square(anchor_x + dx, anchor_y + dy);
        if (!sq || !entity_can_land_on_square(entity, *sq)) {
            return false;
        }
    }
    if (uses_threshold) {
        return footprint_void_fraction_below_threshold(
            [&grid](int x, int y) { return grid.get_square(x, y); }, entity, anchor_x, anchor_y);
    }
    return true;
}

bool entity_can_path_through_occupant(const Entity& mover, const std::shared_ptr<Entity>& occupant) {
    if (!occupant || occupant.get() == &mover) {
        return true;
    }
    // Pickups do not block movement - units pass through them freely.
    // The actual pickup collection only fires when the unit ENDS its confirmed move on the cell.
    if (entity_is_pickup(*occupant)) {
        return true;
    }
    if (has_taunt(*occupant) && !entities_allied(mover, *occupant)) {
        return false;
    }
    if (does_not_block_movement(*occupant)) {
        return true;
    }
    return entities_allied(mover, *occupant) && entity_can_move(*occupant);
}

bool movement_step_enters_taunt_cell(const GridManager& grid, const Entity& mover, int from_anchor_x, int from_anchor_y, int to_anchor_x,
    int to_anchor_y)
{
    const auto from_cells = footprint_cells_at(mover, from_anchor_x, from_anchor_y);
    const auto to_cells = footprint_cells_at(mover, to_anchor_x, to_anchor_y);
    for (const auto& [tx, ty] : to_cells) {
        if (contains_cell(from_cells, tx, ty)) {
            continue;
        }
        const auto sq = grid.get_square(tx, ty);
        if (sq && sq->occupied && sq->entity && sq->entity.get() != &mover && has_taunt(*sq->entity) &&
            !entities_allied(mover, *sq->entity)) {
            return true;
        }
    }
    return false;
}

float taunt_path_step_penalty_for_grid(const GridManager& grid, const Entity& mover, int from_anchor_x, int from_anchor_y, int to_anchor_x,
    int to_anchor_y)
{
    constexpr float kAdjacentPenalty = 75.0f;
    constexpr float kThroughPenalty = 500.0f;
    const auto from_cells = footprint_cells_at(mover, from_anchor_x, from_anchor_y);
    const auto to_cells = footprint_cells_at(mover, to_anchor_x, to_anchor_y);
    float penalty = 0.0f;
    for (const auto& [_, sq] : grid.squares()) {
        if (!sq || !sq->occupied || !sq->entity || !has_taunt(*sq->entity) || !sq->entity->position ||
            entities_allied(mover, *sq->entity)) {
            continue;
        }
        const auto taunt_cells = footprint_cells_at(*sq->entity, sq->entity->position->first, sq->entity->position->second);
        for (const auto& [tx, ty] : to_cells) {
            if (contains_cell(from_cells, tx, ty)) {
                continue;
            }
            for (const auto& [qx, qy] : taunt_cells) {
                if (tx == qx && ty == qy) {
                    penalty = std::max(penalty, kThroughPenalty);
                } else if (world_cells_share_orthogonal_edge(tx, ty, qx, qy)) {
                    penalty = std::max(penalty, kAdjacentPenalty);
                }
            }
        }
    }
    return penalty;
}

bool anchor_pathable_for_entity(const GridManager& grid, const std::shared_ptr<Entity>& entity, int anchor_x, int anchor_y) {
    const bool uses_threshold = entity_uses_void_threshold_for_landing(*entity);
    const auto offs = entity_shape_offsets(*entity);
    for (const auto& [dx, dy] : offs) {
        const int wx = anchor_x + dx;
        const int wy = anchor_y + dy;
        auto sq = grid.get_square(wx, wy);
        if (!sq) {
            return false;
        }
        if (sq->occupied && sq->entity != entity && !entity_can_path_through_occupant(*entity, sq->entity)) {
            return false;
        }
        if (!entity_can_land_on_square(*entity, *sq)) {
            return false;
        }
    }
    if (uses_threshold) {
        return footprint_void_fraction_below_threshold(
            [&grid](int x, int y) { return grid.get_square(x, y); }, *entity, anchor_x, anchor_y);
    }
    return true;
}

float entered_footprint_step_cost(const GridManager& grid, const Entity& entity, int from_x, int from_y, int to_x, int to_y) {
    const auto from_cells = footprint_cells_at(entity, from_x, from_y);
    const auto to_cells = footprint_cells_at(entity, to_x, to_y);
    const bool ignores_terrain = entity_ignores_terrain_for_pathing(entity);
    float step = 0.0f;
    for (const auto& [txc, tyc] : to_cells) {
        if (!contains_cell(from_cells, txc, tyc)) {
            step += ignores_terrain ? default_movement_policy().normal_tile_cost : grid.movement_cost(txc, tyc);
        }
    }
    if (!entity_ignores_diagonal_movement_cost(entity)) {
        step += diagonal_movement_surcharge(from_x, from_y, to_x, to_y);
    }
    return step;
}

int entered_footprint_step_damage(const GridManager& grid, const Entity& entity, int from_x, int from_y, int to_x, int to_y) {
    return footprint_step_terrain_damage([&grid](int x, int y) { return grid.get_square(x, y); }, entity, from_x, from_y, to_x, to_y);
}

bool clip_line_to_cell_rect(double x0, double y0, double x1, double y1, int cx, int cy, double& out_enter_t) {
    const double xmin = static_cast<double>(cx) - 0.5;
    const double xmax = static_cast<double>(cx) + 0.5;
    const double ymin = static_cast<double>(cy) - 0.5;
    const double ymax = static_cast<double>(cy) + 0.5;
    const double dx = x1 - x0;
    const double dy = y1 - y0;
    double t0 = 0.0;
    double t1 = 1.0;

    const auto clip = [&](double p, double q) {
        if (std::abs(p) <= std::numeric_limits<double>::epsilon()) {
            return q >= 0.0;
        }
        const double r = q / p;
        if (p < 0.0) {
            if (r > t1) return false;
            if (r > t0) t0 = r;
        } else {
            if (r < t0) return false;
            if (r < t1) t1 = r;
        }
        return true;
    };

    if (!clip(-dx, x0 - xmin)) return false;
    if (!clip(dx, xmax - x0)) return false;
    if (!clip(-dy, y0 - ymin)) return false;
    if (!clip(dy, ymax - y0)) return false;
    out_enter_t = t0;
    return true;
}

std::vector<std::pair<int, int>> supercover_line_cells(std::pair<int, int> start, std::pair<int, int> end) {
    std::vector<std::tuple<double, int, int>> touched;
    const int min_x = std::min(start.first, end.first);
    const int max_x = std::max(start.first, end.first);
    const int min_y = std::min(start.second, end.second);
    const int max_y = std::max(start.second, end.second);
    const double x0 = static_cast<double>(start.first);
    const double y0 = static_cast<double>(start.second);
    const double x1 = static_cast<double>(end.first);
    const double y1 = static_cast<double>(end.second);

    for (int y = min_y; y <= max_y; ++y) {
        for (int x = min_x; x <= max_x; ++x) {
            double enter_t = 0.0;
            if (clip_line_to_cell_rect(x0, y0, x1, y1, x, y, enter_t)) {
                touched.push_back({enter_t, x, y});
            }
        }
    }

    std::sort(touched.begin(), touched.end(), [](const auto& a, const auto& b) {
        if (std::get<0>(a) != std::get<0>(b)) return std::get<0>(a) < std::get<0>(b);
        if (std::get<1>(a) != std::get<1>(b)) return std::get<1>(a) < std::get<1>(b);
        return std::get<2>(a) < std::get<2>(b);
    });

    std::vector<std::pair<int, int>> out;
    out.reserve(touched.size());
    for (const auto& [_, x, y] : touched) {
        const auto cell = std::make_pair(x, y);
        if (out.empty() || out.back() != cell) {
            out.push_back(cell);
        }
    }
    return out;
}

}  // namespace

namespace grid_path_detail {

bool contains_cell(const std::vector<std::pair<int, int>>& cells, int x, int y) {
    return std::find(cells.begin(), cells.end(), std::make_pair(x, y)) != cells.end();
}

std::vector<std::pair<int, int>> footprint_cells_at(const Entity& entity, int anchor_x, int anchor_y) {
    std::vector<std::pair<int, int>> cells;
    const auto offs = entity_shape_offsets(entity);
    cells.reserve(offs.size());
    for (const auto& [dx, dy] : offs) {
        cells.push_back({anchor_x + dx, anchor_y + dy});
    }
    return cells;
}

}  // namespace grid_path_detail

int footprint_step_terrain_damage(const GetGridSquareFn& get_square, const Entity& entity, int from_x, int from_y, int to_x,
                                  int to_y) {
    if (entity_ignores_damaging_terrain(entity)) {
        return 0;
    }
    const auto from_cells = grid_path_detail::footprint_cells_at(entity, from_x, from_y);
    const auto to_cells = grid_path_detail::footprint_cells_at(entity, to_x, to_y);
    int damage = 0;
    for (const auto& [txc, tyc] : to_cells) {
        if (!grid_path_detail::contains_cell(from_cells, txc, tyc)) {
            if (auto sq = get_square(txc, tyc)) {
                damage += terrain_damage_on_enter_for_square(*sq);
            }
        }
    }
    if (std::abs(to_x - from_x) + std::abs(to_y - from_y) == 2) {
        const auto side_a = grid_path_detail::footprint_cells_at(entity, to_x, from_y);
        const auto side_b = grid_path_detail::footprint_cells_at(entity, from_x, to_y);
        const auto side_damage = [&](const std::vector<std::pair<int, int>>& side_cells) {
            int out = 0;
            for (const auto& [sx, sy] : side_cells) {
                if (grid_path_detail::contains_cell(from_cells, sx, sy) || grid_path_detail::contains_cell(to_cells, sx, sy)) {
                    continue;
                }
                if (auto sq = get_square(sx, sy)) {
                    out += terrain_damage_on_enter_for_square(*sq);
                }
            }
            return out;
        };
        damage += std::max(side_damage(side_a), side_damage(side_b));
    }
    return damage;
}

int path_footprint_terrain_damage(const GetGridSquareFn& get_square, const Entity& entity,
                                  const std::vector<std::pair<int, int>>& path) {
    if (path.size() < 2) {
        return 0;
    }
    int total = 0;
    for (size_t i = 1; i < path.size(); ++i) {
        const auto [px, py] = path[i - 1];
        const auto [cx, cy] = path[i];
        total += footprint_step_terrain_damage(get_square, entity, px, py, cx, cy);
    }
    return total;
}

GridManager::GridManager(int offset_x, int offset_y) : offset_x_(offset_x), offset_y_(offset_y) {}

long long GridManager::key(int x, int y) { return board_cell_key(x, y); }

void GridManager::initialize_grid(int width, int height) {
    width_ = width;
    height_ = height;
    for (int x = 0; x < width; ++x) {
        for (int y = 0; y < height; ++y) {
            add_world_cells({{x + offset_x_, y + offset_y_}});
        }
    }
}

void GridManager::add_world_cells(const std::vector<std::pair<int, int>>& world_cells)
{
    for (const auto& [wx, wy] : world_cells) {
        const long long k = key(wx, wy);
        if (squares_.contains(k)) {
            continue;
        }
        auto sq = std::make_shared<GridSquare>();
        sq->x = wx;
        sq->y = wy;
        squares_[k] = sq;
        width_ = std::max(width_, wx - offset_x_ + 1);
        height_ = std::max(height_, wy - offset_y_ + 1);
    }
}

std::shared_ptr<GridSquare> GridManager::get_square(int x, int y) const {
    auto it = squares_.find(key(x, y));
    return it == squares_.end() ? nullptr : it->second;
}

bool GridManager::anchor_fits_entity(const std::shared_ptr<Entity>& entity, int anchor_x, int anchor_y) const {
    const bool uses_threshold = entity_uses_void_threshold_for_landing(*entity);
    const auto offs = entity_shape_offsets(*entity);
    for (const auto& [dx, dy] : offs) {
        const int wx = anchor_x + dx;
        const int wy = anchor_y + dy;
        auto sq = get_square(wx, wy);
        // A cell is unavailable if it's occupied by something other than this entity - except
        // pickups, which units can land on (collecting them is handled at move-confirm time).
        // Crushing Advance (crushes_on_move) also allows landing on occupied cells, UNLESS the
        // occupant is indestructible or shares the same unit_type as the crusher.
        if (!sq || (sq->occupied && sq->entity != entity && !entity_is_pickup(*sq->entity))) {
            if (!entity_has_attribute(*entity, "crushes_on_move")) return false;
            if (sq && sq->entity) {
                if (entity_has_attribute(*sq->entity, "indestructible")) return false;
                const auto occ_unit = std::dynamic_pointer_cast<Unit>(sq->entity);
                const auto mover_unit = std::dynamic_pointer_cast<Unit>(entity);
                if (occ_unit && mover_unit && occ_unit->unit_type == mover_unit->unit_type) return false;
            }
        }
        if (!entity_can_land_on_square(*entity, *sq)) {
            return false;
        }
    }
    if (uses_threshold) {
        return footprint_void_fraction_below_threshold(
            [this](int x, int y) { return get_square(x, y); }, *entity, anchor_x, anchor_y);
    }
    return true;
}

bool GridManager::can_place_entity(const std::shared_ptr<Entity>& entity, int base_x, int base_y) const {
    const bool uses_threshold = entity_uses_void_threshold_for_landing(*entity);
    const auto offs = entity_shape_offsets(*entity);
    for (const auto& [dx, dy] : offs) {
        auto sq = get_square(base_x + dx, base_y + dy);
        if (!sq || sq->occupied) return false;
        if (!entity_can_land_on_square(*entity, *sq)) return false;
    }
    if (uses_threshold) {
        return footprint_void_fraction_below_threshold(
            [this](int x, int y) { return get_square(x, y); }, *entity, base_x, base_y);
    }
    return true;
}

bool GridManager::place_entity(const std::shared_ptr<Entity>& entity, int base_x, int base_y) {
    if (!can_place_entity(entity, base_x, base_y)) return false;
    const auto offs = entity_shape_offsets(*entity);
    entity->shape = offs;
    entity->position = {base_x, base_y};
    entity->occupied_positions.clear();
    for (const auto& [dx, dy] : offs) {
        auto x = base_x + dx;
        auto y = base_y + dy;
        entity->occupied_positions.push_back({x, y});
        auto sq = get_square(x, y);
        sq->entity = entity;
        sq->occupied = true;
    }
    return true;
}

bool GridManager::remove_entity(const std::shared_ptr<Entity>& entity) {
    bool changed = false;
    for (auto it = entity->occupied_positions.begin(); it != entity->occupied_positions.end();) {
        const auto [x, y] = *it;
        if (auto sq = get_square(x, y); sq && sq->entity == entity) {
            sq->entity.reset();
            sq->occupied = false;
            it = entity->occupied_positions.erase(it);
            changed = true;
        } else {
            ++it;
        }
    }
    if (entity->occupied_positions.empty()) entity->position.reset();
    return changed;
}

float GridManager::movement_cost(int x, int y) const {
    auto sq = get_square(x, y);
    if (!sq) return kUnreachableTileMovementCost;
    return terrain_movement_cost_for_square(*sq);
}

std::optional<std::vector<std::pair<int, int>>> GridManager::find_path(const std::shared_ptr<Entity>& entity, int tx, int ty,
    const GameState* taunt_rules) const {
    if (!entity->position) return std::nullopt;
    auto [sx, sy] = *entity->position;

    if (taunt_rules && movement_anchor_stops_under_enemy_taunt(*taunt_rules, *entity, sx, sy) && (tx != sx || ty != sy)) {
        return std::nullopt;
    }

    const int max_move = pathfind_movement_budget_for_entity(entity);
    if (std::abs(tx - sx) == 1 && std::abs(ty - sy) == 1 && anchor_fits_entity(entity, tx, ty)) {
        if (!movement_step_enters_taunt_cell(*this, *entity, sx, sy, tx, ty) &&
            (entity_moves_over_blockers_for_pathing(*entity) ||
                (anchor_pathable_for_entity(*this, entity, tx, ty) && anchor_pathable_for_entity(*this, entity, sx, ty)))) {
            const float step = entered_footprint_step_cost(*this, *entity, sx, sy, tx, ty);
            if (step <= static_cast<float>(max_move) + kPathEps) {
                const std::vector<std::pair<int, int>> path{{sx, sy}, {tx, ty}};
                if (!taunt_rules || path_respects_taunt_stop(*taunt_rules, *entity, path)) {
                    return path;
                }
            }
        }
    }

    struct SearchState {
        int x{0};
        int y{0};
        int damage{0};
        float move{0.0f};
        float taunt_pref{0.0f};
        std::vector<std::pair<int, int>> path{};
    };

    auto path_beats = [](const SearchState& a, const SearchState& b) {
        if (a.damage != b.damage) {
            return a.damage < b.damage;
        }
        if (std::abs(a.move - b.move) > kPathEps) {
            return a.move < b.move - kPathEps;
        }
        return a.taunt_pref < b.taunt_pref - kPathEps;
    };

    std::vector<SearchState> open;
    open.push_back(SearchState{sx, sy, 0, 0.0f, 0.0f, {{sx, sy}}});
    std::optional<SearchState> best;

    while (!open.empty()) {
        SearchState cur = std::move(open.back());
        open.pop_back();

        if (cur.x == tx && cur.y == ty && anchor_fits_entity(entity, tx, ty)) {
            if (!best || path_beats(cur, *best)) {
                best = std::move(cur);
            }
            continue;
        }

        if (best && cur.damage > best->damage) {
            continue;
        }

        if (taunt_rules && movement_anchor_stops_under_enemy_taunt(*taunt_rules, *entity, cur.x, cur.y)) {
            continue;
        }

        for (auto [dx, dy] : kSurroundingOffsets) {
            int nx = cur.x + dx, ny = cur.y + dy;
            if (contains_cell(cur.path, nx, ny)) {
                continue;
            }
            if (entity_moves_over_blockers_for_pathing(*entity)) {
                if (!anchor_in_bounds(*this, *entity, nx, ny)) continue;
                if (!anchor_landing_allowed(*this, *entity, nx, ny)) continue;
            } else if (!anchor_pathable_for_entity(*this, entity, nx, ny)) continue;
            if (!entity_moves_over_blockers_for_pathing(*entity) && std::abs(dx) + std::abs(dy) == 2 &&
                (!anchor_pathable_for_entity(*this, entity, cur.x + dx, cur.y) || !anchor_pathable_for_entity(*this, entity, cur.x, cur.y + dy)))
                continue;
            if (movement_step_enters_taunt_cell(*this, *entity, cur.x, cur.y, nx, ny)) {
                continue;
            }
            const float step = entered_footprint_step_cost(*this, *entity, cur.x, cur.y, nx, ny);
            const float taunt_step = taunt_path_step_penalty_for_grid(*this, *entity, cur.x, cur.y, nx, ny);
            const int step_damage = entered_footprint_step_damage(*this, *entity, cur.x, cur.y, nx, ny);
            const float ng_move = cur.move + step;
            if (ng_move > static_cast<float>(max_move) + kPathEps) continue;
            const int ng_damage = cur.damage + step_damage;
            if (best && ng_damage > best->damage) {
                continue;
            }
            SearchState next{nx, ny, ng_damage, ng_move, cur.taunt_pref + taunt_step, cur.path};
            next.path.push_back({nx, ny});
            open.push_back(std::move(next));
        }
    }
    if (!best) {
        // Emergency min-move: if the destination is exactly one step away and physically
        // passable (valid landing, diagonal corners clear, taunt respected), allow the step
        // regardless of terrain cost.  This fires when the budget was too small to cover the
        // terrain (e.g. only rough tiles are adjacent, or the unit is movement-debuffed to
        // the floor budget of kMinimumMoveSteps).  Works for large units - all footprint
        // math goes through anchor_fits_entity / anchor_pathable_for_entity as normal.
        const int adx = tx - sx, ady = ty - sy;
        if (std::abs(adx) <= 1 && std::abs(ady) <= 1 && (adx != 0 || ady != 0) &&
                anchor_fits_entity(entity, tx, ty)) {
            bool ok = true;
            // Diagonal: both corridor corners must be passable.
            if (!entity_moves_over_blockers_for_pathing(*entity) && std::abs(adx) + std::abs(ady) == 2) {
                ok = anchor_pathable_for_entity(*this, entity, sx + adx, sy) &&
                     anchor_pathable_for_entity(*this, entity, sx, sy + ady);
            }
            if (ok && movement_step_enters_taunt_cell(*this, *entity, sx, sy, tx, ty)) ok = false;
            if (ok && taunt_rules) {
                const std::vector<std::pair<int, int>> ep{{sx, sy}, {tx, ty}};
                if (!path_respects_taunt_stop(*taunt_rules, *entity, ep)) ok = false;
            }
            if (ok) return std::vector<std::pair<int, int>>{{sx, sy}, {tx, ty}};
        }
        return std::nullopt;
    }
    return best->path;
}

std::vector<std::pair<int, int>> GridManager::find_reachable_anchors(const std::shared_ptr<Entity>& entity, const GameState* taunt_rules) const {
    std::vector<std::pair<int, int>> out;
    if (!entity || !entity->position) {
        return out;
    }
    const auto [sx, sy] = *entity->position;

    using Node = std::tuple<int, float, float, int>;
    std::priority_queue<Node, std::vector<Node>, std::greater<>> open;
    std::vector<PathLabel> labels;
    std::map<std::pair<int, int>, std::vector<int>> labels_by_cell;
    int start_id = -1;
    static_cast<void>(add_path_label(labels, labels_by_cell, PathLabel{0, 0.0f, 0.0f, sx, sy, -1, true}, start_id));
    open.push({0, 0.0f, 0.0f, start_id});

    const int max_move = pathfind_movement_budget_for_entity(entity);

    while (!open.empty()) {
        auto [damage_so_far, move_so_far, taunt_so_far, label_id] = open.top();
        open.pop();
        if (label_is_stale(labels, label_id, damage_so_far, move_so_far, taunt_so_far)) {
            continue;
        }
        // Copy by value: add_path_label() below push_backs into `labels`, which can
        // reallocate and invalidate any reference into it across the offset loop.
        const PathLabel cur_label = labels[static_cast<size_t>(label_id)];
        const int x = cur_label.x;
        const int y = cur_label.y;

        if (taunt_rules && movement_anchor_stops_under_enemy_taunt(*taunt_rules, *entity, x, y)) {
            continue;
        }

        for (auto [dx, dy] : kSurroundingOffsets) {
            const int nx = x + dx;
            const int ny = y + dy;
            if (entity_moves_over_blockers_for_pathing(*entity)) {
                if (!anchor_in_bounds(*this, *entity, nx, ny)) {
                    continue;
                }
                if (!anchor_landing_allowed(*this, *entity, nx, ny)) {
                    continue;
                }
            } else if (!anchor_pathable_for_entity(*this, entity, nx, ny)) {
                continue;
            }
            if (!entity_moves_over_blockers_for_pathing(*entity) && std::abs(dx) + std::abs(dy) == 2 &&
                (!anchor_pathable_for_entity(*this, entity, x + dx, y) || !anchor_pathable_for_entity(*this, entity, x, y + dy))) {
                continue;
            }
            if (movement_step_enters_taunt_cell(*this, *entity, x, y, nx, ny)) {
                continue;
            }
            const float step = entered_footprint_step_cost(*this, *entity, x, y, nx, ny);
            const float taunt_step = taunt_path_step_penalty_for_grid(*this, *entity, x, y, nx, ny);
            const int step_damage = entered_footprint_step_damage(*this, *entity, x, y, nx, ny);
            const float ng_move = cur_label.move + step;
            if (ng_move > static_cast<float>(max_move) + kPathEps) {
                continue;
            }
            const int ng_damage = cur_label.damage + step_damage;
            const float ng_taunt = cur_label.taunt_pref + taunt_step;
            int next_id = -1;
            if (add_path_label(labels, labels_by_cell, PathLabel{ng_damage, ng_move, ng_taunt, nx, ny, label_id, true}, next_id)) {
                open.push({ng_damage, ng_move, ng_taunt, next_id});
            }
        }
    }

    out.reserve(labels_by_cell.size());
    for (const auto& [cell, _] : labels_by_cell) {
        if (anchor_fits_entity(entity, cell.first, cell.second)) {
            out.push_back(cell);
        }
    }
    return out;
}

std::vector<std::pair<int, int>> GridManager::find_emergency_step_anchors(
        const std::shared_ptr<Entity>& entity, const GameState* taunt_rules) const {
    // Returns the set of anchors reachable in exactly one step, ignoring terrain cost.
    // Used as a fallback when the unit has no normal destinations at all (budget too small
    // for available terrain).  All other constraints - valid landing, diagonal-corner
    // passability, taunt - still apply.  Works for large units: all footprint math goes
    // through anchor_fits_entity / anchor_pathable_for_entity as normal.
    std::vector<std::pair<int, int>> out;
    if (!entity || !entity->position) return out;
    const auto [sx, sy] = *entity->position;

    // Under taunt the unit is locked in place - emergency is also blocked.
    if (taunt_rules && movement_anchor_stops_under_enemy_taunt(*taunt_rules, *entity, sx, sy)) {
        return out;
    }

    for (auto [dx, dy] : kSurroundingOffsets) {
        const int nx = sx + dx, ny = sy + dy;
        if (!anchor_fits_entity(entity, nx, ny)) continue;
        if (!entity_moves_over_blockers_for_pathing(*entity) && std::abs(dx) + std::abs(dy) == 2) {
            if (!anchor_pathable_for_entity(*this, entity, sx + dx, sy) ||
                !anchor_pathable_for_entity(*this, entity, sx, sy + dy)) continue;
        }
        if (movement_step_enters_taunt_cell(*this, *entity, sx, sy, nx, ny)) continue;
        if (taunt_rules && !move_destination_allowed_under_taunt(*taunt_rules, *entity, sx, sy, nx, ny)) continue;
        out.push_back({nx, ny});
    }
    return out;
}

LOSResult GridManager::line_of_sight(std::pair<int, int> start, std::pair<int, int> end) const {
    LOSResult out;
    auto start_sq = get_square(start.first, start.second);
    auto end_sq = get_square(end.first, end.second);
    if (!start_sq || !end_sq) {
        out.query_valid = false;
        out.clear = false;
        return out;
    }

    out.query_valid = true;
    out.clear = true;
    out.path = supercover_line_cells(start, end);

    for (const auto& [x, y] : out.path) {
        if ((x == start.first && y == start.second) || (x == end.first && y == end.second)) {
            continue;
        }
        auto sq = get_square(x, y);
        if (!sq) {
            out.query_valid = false;
            out.clear = false;
            out.blocker.reset();
            break;
        }
        if (square_blocks_los(*sq)) {
            out.clear = false;
            out.blocker.reset();
            break;
        }
        if (sq->entity && entity_blocks_line_of_sight(*sq->entity)) {
            out.clear = false;
            out.blocker = sq->entity;
            break;
        }
    }
    return out;
}

}  // namespace tactics
