#pragma once

#include "tactics/board/grid.hpp"
#include "tactics/board/tile_modifiers.hpp"
#include "tactics/common/types.hpp"

#include <string>

namespace tactics {

class GameState;

inline constexpr const char* kTrenchModifierName = "trench";
inline constexpr float kTrenchMovementCost = 1.5f;

/** True when the cell has no modifiers in either layer (completely bare). */
bool square_is_unmodified(const GridSquare& sq);
bool square_has_trench(const GridSquare& sq);

/** True when (tx, ty) is on or Chebyshev-adjacent to a footprint cell of a friendly movable unit. */
bool cell_is_adjacent_to_or_under_allied_unit(const GameState& game, int controller, int tx, int ty);

ActionResult validate_place_trench_target(const GameState& game, int controller, int tx, int ty);

/** +1 armor for non-flying 1×1 units standing on a trench tile; 0 otherwise. */
int trench_armor_bonus_for_entity(const GetGridSquareFn& get_square, const Entity& e);

bool place_trench_on_square(std::shared_ptr<GridSquare> sq);

}  // namespace tactics