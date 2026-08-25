#pragma once

#include "tactics/entities/entity.hpp"

#include <cmath>
#include <functional>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace tactics {

class GameState;

/** Packed (x,y) key for sparse board maps (must stay aligned with `GameBoard` global index). */
inline long long board_cell_key(int x, int y) {
    return (static_cast<long long>(x) << 32) | static_cast<unsigned int>(y);
}

/** Enter-destination cost for one orthogonal/diagonal step (pathfinder + move validation). */
inline float step_enter_cell_move_cost(float dest_tile_movement_cost, int from_x, int from_y, int to_x, int to_y) {
    const int dx = to_x - from_x, dy = to_y - from_y;
    return dest_tile_movement_cost + ((std::abs(dx) + std::abs(dy) == 2) ? 0.5f : 0.0f);
}

/** Movement budget for pathfinding when the mover is not a `Unit` (matches `Unit::movement` default scale). */
inline constexpr int kDefaultPathfindMoveBudget = 5;
/** Sentinel cost for off-board / blocked movement queries (pathfinder treats as unreachable). */
inline constexpr float kUnreachableTileMovementCost = 999.0f;
/** Float slack when comparing total path cost to movement budget (`resolve_move_goal`). */
inline constexpr float kMovePathTotalEpsilon = 0.01f;
/**
 * Minimum pathfinding budget granted to any unit that can move, regardless of debuffs.
 * Guarantees every unit can always take at least one step (even onto rough terrain when
 * no normal-cost tile is accessible).  Non-unit entities (buildings, bases) are not
 * affected - they receive budget 0 and cannot move.
 */
inline constexpr int kMinimumMoveSteps = 1;

/** Movement budget is explicit unit data; large units should set a higher base movement if needed. */
inline int pathfind_movement_budget_for_entity(const std::shared_ptr<Entity>& entity) {
    if (!entity || !entity_can_move(*entity)) {
        return 0;
    }
    if (auto u = std::dynamic_pointer_cast<Unit>(entity)) {
        const int effective = unit_effective_movement(*u);
        if (effective > 0) {
            return effective;
        }
        // Units authored with 0 base movement stay immobile until they gain movement speed.
        if (u->movement <= 0) {
            return 0;
        }
        // Floor at kMinimumMoveSteps: a debuffed unit that normally moves can still take 1 step.
        return kMinimumMoveSteps;
    }
    return kDefaultPathfindMoveBudget;
}

/**
 * Layer of a tile modifier - controls exclusivity.
 *   Terrain  - base tile type (trench, road, rough, damaging…). At most one per cell.
 *   Overlay  - dynamic effect placed on top of terrain (gas, oil spill, …). At most one per cell.
 * A cell may have one Terrain modifier AND one Overlay modifier simultaneously.
 * Setting a new modifier in a layer silently replaces the previous one in that layer.
 */
enum class TileLayer {
    Terrain,
    Overlay,
};

struct SquareModifier {
    std::string name;
    TileLayer layer{TileLayer::Terrain};
    // Movement cost contribution: 0.0f = no effect (tile uses base movement cost).
    // Values >1.0f slow movement (e.g. 1.5f for trench); values <1.0f but >0.0f speed it up.
    // Overlays like gas or smoke should leave this at 0.0f - only physical terrain modifiers
    // like vines or rough ground should set a non-zero movement cost.
    float movement_cost{0.0f};
    bool blocks_line_of_sight{false};
    int damage_on_enter{0};
    bool is_void{false};
    // Optional duration tracking for timed modifiers.
    // owner_seat: which player's turn-end ticks the countdown (nullopt = no owner).
    // duration:   remaining owner-turn-ends before auto-removal (nullopt = infinite, never expires).
    // On the turn the duration reaches 0 the modifier still applies its effects, then is removed.
    // Ownerless modifiers (owner_seat = nullopt) decay on every player's turn-end.
    std::optional<int> owner_seat{};
    std::optional<int> duration{};
};

struct GridSquare {
    int x{0};
    int y{0};
    std::shared_ptr<Entity> entity{};
    bool occupied{false};
    std::vector<SquareModifier> modifiers{};
};

struct LOSResult {
    /** False when start/end are off-grid or span invalid coordinates for this board. */
    bool query_valid{false};
    bool clear{false};
    std::shared_ptr<Entity> blocker{};
    std::vector<std::pair<int, int>> path{};
};

class GridManager {
public:
    GridManager(int offset_x = 0, int offset_y = 0);
    void initialize_grid(int width, int height);
    /** Add individual world cells (for jigsaw modules). Skips cells that already exist in this manager. */
    void add_world_cells(const std::vector<std::pair<int, int>>& world_cells);
    std::shared_ptr<GridSquare> get_square(int x, int y) const;
    bool can_place_entity(const std::shared_ptr<Entity>& entity, int base_x, int base_y) const;
    bool place_entity(const std::shared_ptr<Entity>& entity, int base_x, int base_y);
    bool remove_entity(const std::shared_ptr<Entity>& entity);
    float movement_cost(int x, int y) const;
    std::optional<std::vector<std::pair<int, int>>> find_path(const std::shared_ptr<Entity>& entity, int target_x, int target_y,
        const GameState* taunt_rules = nullptr) const;
    /** Anchor cells reachable within the entity's movement budget (includes current anchor). */
    std::vector<std::pair<int, int>> find_reachable_anchors(const std::shared_ptr<Entity>& entity, const GameState* taunt_rules = nullptr) const;
    /**
     * Anchors reachable in exactly one step, ignoring terrain cost.
     * Used as a fallback when the unit has no normal destinations (budget too small for
     * available terrain).  Taunt, landing, and diagonal-corner rules still apply.
     */
    std::vector<std::pair<int, int>> find_emergency_step_anchors(const std::shared_ptr<Entity>& entity, const GameState* taunt_rules = nullptr) const;
    LOSResult line_of_sight(std::pair<int, int> start, std::pair<int, int> end) const;
    const std::unordered_map<long long, std::shared_ptr<GridSquare>>& squares() const { return squares_; }
private:
    bool anchor_fits_entity(const std::shared_ptr<Entity>& entity, int anchor_x, int anchor_y) const;
    static long long key(int x, int y);
    int offset_x_{0};
    int offset_y_{0};
    int width_{0};
    int height_{0};
    std::unordered_map<long long, std::shared_ptr<GridSquare>> squares_;
};

using GetGridSquareFn = std::function<std::shared_ptr<GridSquare>(int x, int y)>;

/** Terrain damage for one movement step (matches pathfinder enter-damage). */
int footprint_step_terrain_damage(const GetGridSquareFn& get_square, const Entity& entity, int from_x, int from_y, int to_x,
                                  int to_y);
/** Sum of per-step terrain damage along a path (excludes duplicate end-footprint pass). */
int path_footprint_terrain_damage(const GetGridSquareFn& get_square, const Entity& entity,
                                  const std::vector<std::pair<int, int>>& path);

}  // namespace tactics
