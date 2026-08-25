#pragma once

#include "tactics/board/grid.hpp"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace tactics {

struct Entity;

class GameState;

/** Axis-aligned bounds over all cells currently merged into `GameBoard::global_squares_` (world integer coordinates). */
struct BoardCellBounds {
    int min_x{0};
    int min_y{0};
    int max_x{-1};
    int max_y{-1};
    bool empty() const { return max_x < min_x || max_y < min_y; }
    int span_x() const { return empty() ? 0 : max_x - min_x + 1; }
    int span_y() const { return empty() ? 0 : max_y - min_y + 1; }
};

/** If `cells` has no tiles yet, use a zero-based main module rectangle of size `main_w` × `main_h`. */
inline BoardCellBounds cell_bounds_or_main_module(const BoardCellBounds& cells, int main_w, int main_h) {
    if (!cells.empty()) return cells;
    BoardCellBounds b;
    if (main_w < 1 || main_h < 1) return b;
    b.min_x = 0;
    b.min_y = 0;
    b.max_x = main_w - 1;
    b.max_y = main_h - 1;
    return b;
}

/** Merged world grid of all map modules currently in play. */
class GameBoard {
public:
    /**
     * Registers a rectangular module and merges cells into `global_squares_`.
     * @return false if any packed cell key already maps to a different `GridSquare` (would corrupt indexing).
     */
    bool add_modular_grid(const std::string& module_id, int width, int height, int offset_x = 0, int offset_y = 0);
    /** Merge explicit world cells (jigsaw piece). Returns false if any cell collides with a different square. */
    bool add_cell_module(const std::string& module_id, const std::vector<std::pair<int, int>>& world_cells);
    /** Union of all tiles in `global_squares_` (world integer coordinates; may include gaps between modules). */
    BoardCellBounds cell_bounds() const;
    int cell_count() const { return static_cast<int>(global_squares_.size()); }
    /** Remove all modules and tiles (entities must be removed separately). */
    void clear_geometry();
    std::shared_ptr<GridSquare> get_square(int x, int y) const;
    std::shared_ptr<Entity> entity_at(int x, int y) const;
    /** Owning grid module for a world cell (uses merged `global_squares_`). */
    std::shared_ptr<GridManager> grid_for_cell(int x, int y) const;
    bool can_place_entity_at(const std::shared_ptr<Entity>& entity, int x, int y) const;
    /** True if every footprint cell exists, is landable for `mover`, and is empty or occupied by `mover`. */
    bool can_place_shape_at(int anchor_x, int anchor_y, const std::vector<std::pair<int, int>>& offsets,
                            const std::shared_ptr<Entity>& mover) const;
    bool place_entity(const std::shared_ptr<Entity>& entity, int x, int y);
    bool remove_entity(const std::shared_ptr<Entity>& entity);
    float movement_cost(int x, int y) const;
    std::optional<std::vector<std::pair<int, int>>> find_path(const std::shared_ptr<Entity>& entity, int x, int y,
        const GameState* taunt_rules = nullptr) const;
    std::vector<std::pair<int, int>> find_reachable_anchors(const std::shared_ptr<Entity>& entity, const GameState* taunt_rules = nullptr) const;
    /** One-step anchors reachable ignoring terrain cost; see GridManager::find_emergency_step_anchors. */
    std::vector<std::pair<int, int>> find_emergency_step_anchors(const std::shared_ptr<Entity>& entity, const GameState* taunt_rules = nullptr) const;
    LOSResult line_of_sight(std::pair<int, int> start, std::pair<int, int> end) const;
    std::vector<std::shared_ptr<Entity>> all_entities() const;
    /** Iterate entities without copying the map into a vector. */
    template <typename Fn>
    void for_each_entity(Fn&& fn) const
    {
        for (const auto& [_, entity] : all_entities_map) {
            if (entity) {
                fn(entity);
            }
        }
    }
    std::unordered_map<std::string, std::shared_ptr<Entity>> all_entities_map;
private:
    void invalidate_bounds_cache() { bounds_dirty_ = true; }
    std::unordered_map<std::string, std::shared_ptr<GridManager>> grids_;
    std::unordered_map<long long, std::shared_ptr<GridSquare>> global_squares_;
    mutable BoardCellBounds bounds_cache_{};
    mutable bool bounds_dirty_{true};
};

}  // namespace tactics
