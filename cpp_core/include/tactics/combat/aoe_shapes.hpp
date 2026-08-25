#pragma once

// ---------------------------------------------------------------------------
// AoE shape registry
//
// All directional-aim area effects share the effect key "directional_damage".
// The StackItem::string_payload["shape"] names the footprint algorithm.
//
// To add a new shape, call register_aoe_shape() once (e.g. at startup or in
// any translation unit's static initializer):
//
//   register_aoe_shape("my_shape",
//       [](int ax, int ay, int dx, int dy, const AoEShapeParams& p)
//           -> std::vector<std::pair<int,int>>
//       {
//           return { ... };
//       });
//
// Then set "shape": "my_shape" in the ability/spell JSON effect_payload.
// No other file needs to change.
// ---------------------------------------------------------------------------

#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace tactics {

using Cell = std::pair<int, int>;

/** Numeric parameters a shape function may read from the effect payload. */
struct AoEShapeParams {
    int width{3};      // rectangle width (tiles perpendicular to fire direction)
    int depth{2};      // rectangle depth (tiles forward)
    int max_range{4};  // aim-picker max range (cells on each of 8 rays shown to player)
    // raw int payload for shapes that need custom params
    const std::map<std::string, int>* payload{nullptr};
};

/** Function that returns world-space hit cells given the caster anchor, a unit direction, and params. */
using AoEShapeFn = std::function<std::vector<Cell>(int ax, int ay, int dx, int dy, const AoEShapeParams& p)>;

/** Register a named shape.  Safe to call before main(); re-registration replaces. */
void register_aoe_shape(const std::string& shape_key, AoEShapeFn fn);

/** Compute hit cells for the named shape.  Returns {} for unknown keys. */
std::vector<Cell> compute_aoe_cells(const std::string& shape_key,
    int ax, int ay, int dx, int dy,
    const AoEShapeParams& params);

/** All registered shape keys (sorted). */
std::vector<std::string> list_aoe_shape_keys();

/** True if shape_key is registered. */
bool aoe_shape_known(const std::string& shape_key);

/** Ensure built-in shapes (rectangle, helix) are registered.  Called automatically. */
void ensure_builtin_aoe_shapes_registered();

/** Build AoEShapeParams from a JSON-style int-payload map. */
AoEShapeParams aoe_shape_params_from_payload(const std::map<std::string, int>& payload);

/** 3×3 square (radius 1) plus one tile beyond the center of each edge (13 cells total). */
std::vector<Cell> scorching_sphere_cells(int center_x, int center_y);

/** Center plus four cardinal neighbors (5-tile cross). Used by Gas Strike. */
std::vector<Cell> gas_strike_cross_cells(int center_x, int center_y);

/** 4×4 area with dx/dy each spanning -1..+2 (16 cells). Used by Shocking Stimulus. */
std::vector<Cell> shocking_stimulus_aoe_cells(int center_x, int center_y);

}  // namespace tactics
