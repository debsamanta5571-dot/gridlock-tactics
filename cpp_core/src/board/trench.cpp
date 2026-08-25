#include "tactics/board/aether.hpp"
#include "tactics/board/scanner.hpp"
#include "tactics/board/omni_energy_tile.hpp"
#include "tactics/board/trench.hpp"

#include "tactics/attributes/attributes.hpp"
#include "tactics/board/tile_modifiers.hpp"
#include "tactics/core/game_state.hpp"
#include "tactics/entities/entity.hpp"

#include <algorithm>
#include <cmath>

namespace tactics {
namespace {

void append_footprint_cells(const Entity& ent, std::vector<std::pair<int, int>>& cells)
{
    cells = ent.occupied_positions;
    if (cells.empty() && ent.position) {
        const auto [ax, ay] = *ent.position;
        for (const auto& [dx, dy] : entity_shape_offsets(ent)) {
            cells.push_back({ax + dx, ay + dy});
        }
    }
}

bool chebyshev_adjacent(int ax, int ay, int bx, int by)
{
    return std::max(std::abs(ax - bx), std::abs(ay - by)) == 1;
}

}  // namespace

bool square_is_unmodified(const GridSquare& sq)
{
    return sq.modifiers.empty();  // true only when both layers are absent
}

bool square_has_trench(const GridSquare& sq)
{
    return std::any_of(sq.modifiers.begin(), sq.modifiers.end(),
        [](const SquareModifier& mod) { return mod.name == kTrenchModifierName; });
}

bool cell_is_adjacent_to_or_under_allied_unit(const GameState& game, int controller, int tx, int ty)
{
    bool found = false;
    game.board.for_each_entity([&](const std::shared_ptr<Entity>& ent) {
        if (found || !ent || !ent->owner) {
            return;
        }
        if (!entity_can_move(*ent)) {
            return;
        }
        if (teams_hostile(game, controller, *ent->owner)) {
            return;
        }
        std::vector<std::pair<int, int>> cells;
        append_footprint_cells(*ent, cells);
        for (const auto& [cx, cy] : cells) {
            if (cx == tx && cy == ty) {
                found = true;
                return;
            }
            if (chebyshev_adjacent(cx, cy, tx, ty)) {
                found = true;
                return;
            }
        }
    });
    return found;
}

ActionResult validate_place_trench_target(const GameState& game, int controller, int tx, int ty)
{
    const auto sq = game.board.get_square(tx, ty);
    if (!sq) {
        return {false, "place_trench: target cell is not on the board", {}};
    }
    // Block if the cell already has a terrain-layer modifier (overlays are fine - a gas cloud
    // or other overlay does not prevent digging a trench on the same tile).
    if (square_has_aether(*sq)) {
        return {false, "place_trench: aether tiles cannot be replaced", {}};
    }
    if (square_has_scanner(*sq)) {
        return {false, "place_trench: scanner tiles cannot be replaced", {}};
    }
    if (square_has_omni_energy(*sq)) {
        return {false, "place_trench: omni energy tiles cannot be replaced", {}};
    }
    if (square_has_terrain_modifier(*sq)) {
        return {false, "place_trench: target tile already has a terrain modifier", {}};
    }
    if (!cell_is_adjacent_to_or_under_allied_unit(game, controller, tx, ty)) {
        return {false, "place_trench: target must be on or adjacent to an allied unit", {}};
    }
    return {true, "Trench target allowed", {}};
}

int trench_armor_bonus_for_entity(const GetGridSquareFn& get_square, const Entity& e)
{
    if (entity_is_silenced(e) || has_flying(e)) {
        return 0;
    }
    if (entity_shape_offsets(e).size() != 1 || !e.position) {
        return 0;
    }
    const auto [ax, ay] = *e.position;
    const auto sq = get_square(ax, ay);
    if (!sq || !square_has_trench(*sq)) {
        return 0;
    }
    return 1;
}

bool place_trench_on_square(std::shared_ptr<GridSquare> sq)
{
    if (!sq || square_has_terrain_modifier(*sq)) {
        return false;
    }
    set_terrain_modifier(*sq, SquareModifier{.name=kTrenchModifierName, .layer=TileLayer::Terrain, .movement_cost=kTrenchMovementCost});
    return true;
}

}  // namespace tactics