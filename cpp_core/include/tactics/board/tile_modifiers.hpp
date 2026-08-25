#pragma once

/**
 * tile_modifiers.hpp - Layer-aware tile modifier placement API.
 *
 * Each GridSquare supports two independent modifier slots:
 *   - Terrain (TileLayer::Terrain): the base tile type - trench, road, rough, etc.
 *     At most one per cell. Examples: trench, damaging lava, void edge.
 *   - Overlay (TileLayer::Overlay): a dynamic effect placed on top of terrain - gas cloud,
 *     oil spill, etc. At most one per cell. Overlays are mutually exclusive with each other
 *     (placing gas removes fire, placing fire removes gas), but NOT with terrain.
 *
 * A cell may have both a Terrain and an Overlay modifier at the same time.
 * The movement_cost and damage_on_enter from both layers are combined by
 * terrain_movement_cost_for_square / terrain_damage_on_enter_for_square in movement_policy.cpp.
 *
 * Usage:
 *   // Place a terrain modifier (replaces any existing terrain on this cell):
 *   set_terrain_modifier(*sq, SquareModifier{.name="trench", .layer=TileLayer::Terrain, .movement_cost=1.5f});
 *
 *   // Place an overlay (replaces any existing overlay on this cell):
 *   set_overlay_modifier(*sq, SquareModifier{.name="gas", .layer=TileLayer::Overlay, .damage_on_enter=1});
 *
 *   // Check what's on a cell:
 *   if (const auto* ov = square_overlay_modifier(*sq)) { ... ov->name ... }
 *
 *   // Remove an overlay (e.g. when gas disperses):
 *   clear_overlay_modifier(*sq);
 */

#include "tactics/board/grid.hpp"

#include <memory>

namespace tactics {

// ---------------------------------------------------------------------------
// Named overlay constants - add one per overlay type here
// ---------------------------------------------------------------------------

/** Gas cloud: applies 1 Poison to units standing on this tile at the end of their owner's turn. */
inline constexpr const char* kGasCloudOverlayName = "gas_cloud";

/** Burning tile: applies 1 Fire to units standing on this tile at the end of their owner's turn. */
inline constexpr const char* kFireTileOverlayName = "fire_tile";

// ---- Query helpers --------------------------------------------------------

/** Returns a pointer to the Terrain-layer modifier, or nullptr if the cell has none. */
const SquareModifier* square_terrain_modifier(const GridSquare& sq);

/** Returns a pointer to the Overlay-layer modifier, or nullptr if the cell has none. */
const SquareModifier* square_overlay_modifier(const GridSquare& sq);

inline bool square_has_terrain_modifier(const GridSquare& sq) { return square_terrain_modifier(sq) != nullptr; }
inline bool square_has_overlay_modifier(const GridSquare& sq) { return square_overlay_modifier(sq) != nullptr; }

/** True when the cell has no modifiers in either layer (bare empty tile). */
inline bool square_is_bare(const GridSquare& sq) { return sq.modifiers.empty(); }

// ---- Placement helpers ----------------------------------------------------

/**
 * Place or replace the Terrain-layer modifier on sq.
 * mod.layer is forced to TileLayer::Terrain regardless of what was passed.
 * Returns true if a previous terrain modifier was replaced; false if it was a fresh add.
 */
bool set_terrain_modifier(GridSquare& sq, SquareModifier mod);

/**
 * Place or replace the Overlay-layer modifier on sq.
 * mod.layer is forced to TileLayer::Overlay regardless of what was passed.
 * Returns true if a previous overlay modifier was replaced (i.e. one overlay swapped for another).
 */
bool set_overlay_modifier(GridSquare& sq, SquareModifier mod);

/**
 * Same-name overlays keep the longer duration (nullopt = infinite, always wins);
 * different overlay names replace each other entirely.
 */
bool merge_overlay_modifier(GridSquare& sq, SquareModifier mod);

/** Remove the Overlay modifier from sq. Returns true if one was present. */
bool clear_overlay_modifier(GridSquare& sq);

/** Remove the Terrain modifier from sq. Returns true if one was present. */
bool clear_terrain_modifier(GridSquare& sq);

}  // namespace tactics
