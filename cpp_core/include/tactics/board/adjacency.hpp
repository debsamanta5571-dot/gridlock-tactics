#pragma once

// ---------------------------------------------------------------------------
// Adjacency helpers
//
// Canonical vocabulary for "adjacent" vs "surrounding" used throughout rules:
//
//   Adjacent   – the 4 orthogonal (cardinal) neighbours of a cell/entity.
//   Surrounding – all 8 neighbours (orthogonal + diagonal) of a cell/entity.
//
// For multi-tile entities the result is the union of neighbours across every
// occupied cell, with the entity's own occupied cells excluded.
//
// Usage:
//   for (auto [cx, cy] : entity_adjacent_cells(e))   { ... }   // 4-way
//   for (auto [cx, cy] : entity_surrounding_cells(e)) { ... }  // 8-way
//
//   for (auto [dx, dy] : kAdjacentOffsets)   { ... }   // raw 4-direction loop
//   for (auto [dx, dy] : kSurroundingOffsets) { ... }  // raw 8-direction loop
// ---------------------------------------------------------------------------

#include "tactics/entities/entity.hpp"

#include <set>
#include <utility>
#include <vector>

namespace tactics {

/// Unit vectors for the 4 orthogonal (cardinal) directions.
/// "Adjacent" in all game rules means exactly these four neighbours.
constexpr std::pair<int, int> kAdjacentOffsets[4] = {
    {0, 1}, {1, 0}, {0, -1}, {-1, 0}
};

/// Unit vectors for all 8 octilinear directions (orthogonal + diagonal).
/// "Surrounding" in all game rules means exactly these eight neighbours.
constexpr std::pair<int, int> kSurroundingOffsets[8] = {
    {0, 1}, {1, 0}, {0, -1}, {-1, 0},
    {1, 1}, {1, -1}, {-1, 1}, {-1, -1}
};

/** Returns the unique board cells orthogonally adjacent (4-way) to any
 *  occupied cell of @p entity.  The entity's own occupied cells are excluded. */
inline std::vector<std::pair<int, int>> entity_adjacent_cells(const Entity& entity)
{
    if (!entity.position) { return {}; }
    const auto [ax, ay] = *entity.position;

    std::set<std::pair<int, int>> own;
    for (const auto& [dx, dy] : entity_shape_offsets(entity)) {
        own.emplace(ax + dx, ay + dy);
    }

    std::set<std::pair<int, int>> result;
    for (const auto& [ox, oy] : own) {
        for (const auto& [ndx, ndy] : kAdjacentOffsets) {
            const std::pair<int, int> n{ox + ndx, oy + ndy};
            if (!own.count(n)) { result.insert(n); }
        }
    }
    return {result.begin(), result.end()};
}

/** Returns the unique board cells surrounding (8-way) any occupied cell of
 *  @p entity.  The entity's own occupied cells are excluded. */
inline std::vector<std::pair<int, int>> entity_surrounding_cells(const Entity& entity)
{
    if (!entity.position) { return {}; }
    const auto [ax, ay] = *entity.position;

    std::set<std::pair<int, int>> own;
    for (const auto& [dx, dy] : entity_shape_offsets(entity)) {
        own.emplace(ax + dx, ay + dy);
    }

    std::set<std::pair<int, int>> result;
    for (const auto& [ox, oy] : own) {
        for (const auto& [ndx, ndy] : kSurroundingOffsets) {
            const std::pair<int, int> n{ox + ndx, oy + ndy};
            if (!own.count(n)) { result.insert(n); }
        }
    }
    return {result.begin(), result.end()};
}

}  // namespace tactics
