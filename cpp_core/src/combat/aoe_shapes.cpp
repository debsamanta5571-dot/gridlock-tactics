#include "tactics/combat/aoe_shapes.hpp"

#include <algorithm>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace tactics {

// ---------------------------------------------------------------------------
// Registry
// ---------------------------------------------------------------------------

namespace {

struct Registry {
    std::map<std::string, AoEShapeFn> shapes;
    std::mutex mtx;
};

Registry& registry()
{
    static Registry r;
    return r;
}

// Helper: perpendicular to a cardinal/diagonal direction
std::pair<int, int> perpendicular(int dx, int dy)
{
    // For cardinal directions the perpendicular is the other axis.
    // For diagonal directions we rotate 90° CW: (dx,dy) → (dy,-dx)
    if (dx == 0) {
        return {1, 0};  // up/down → perp is horizontal
    }
    if (dy == 0) {
        return {0, 1};  // left/right → perp is vertical
    }
    // diagonal: rotate 90° CW
    return {dy, -dx};
}

// ---------------------------------------------------------------------------
// Shape: rectangle
//   Fills a `width × depth` rectangle ahead of anchor along (dx,dy).
//   For diagonal directions falls back to the classic stacked-L (6 cells).
// ---------------------------------------------------------------------------
std::vector<Cell> shape_rectangle(int ax, int ay, int dx, int dy, const AoEShapeParams& p)
{
    const int width = std::max(1, p.width);
    const int depth = std::max(1, p.depth);

    if (dx != 0 && dy != 0) {
        // Diagonal stacked-L - unchanged from original Energy Wave behaviour.
        return {
            {ax + dx,     ay},
            {ax + dx,     ay + dy},
            {ax,          ay + dy},
            {ax + 2 * dx, ay + dy},
            {ax + 2 * dx, ay + 2 * dy},
            {ax + dx,     ay + 2 * dy},
        };
    }

    const auto [px, py] = perpendicular(dx, dy);
    const int half = width / 2;
    std::vector<Cell> out;
    out.reserve(static_cast<std::size_t>(width * depth));
    std::set<Cell> seen;
    for (int d = 1; d <= depth; ++d) {
        for (int w = 0; w < width; ++w) {
            const int lat = w - half;
            const Cell c{ax + d * dx + lat * px, ay + d * dy + lat * py};
            if (seen.insert(c).second) {
                out.push_back(c);
            }
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// Shape: helix (Beam Splitter Mk II)
//   Matches the authored pattern:
//     depth 1: two side cells (lat -1 and +1)
//     depth 2: center cell (lat 0)
//     depth 3: two side cells (lat -1 and +1)
//     depth 4: center cell (lat 0)
//   Total = 6 cells.
// ---------------------------------------------------------------------------
std::vector<Cell> shape_helix(int ax, int ay, int dx, int dy, const AoEShapeParams& /*p*/)
{
    const auto [px, py] = perpendicular(dx, dy);

    std::vector<Cell> out;
    out.reserve(6);
    std::set<Cell> seen;
    static constexpr std::pair<int, int> kSteps[] = {
        {1, -1}, {1, 1},
        {2, 0},
        {3, -1}, {3, 1},
        {4, 0},
    };
    for (const auto& [fwd, lat] : kSteps) {
        const Cell c{ax + fwd * dx + lat * px, ay + fwd * dy + lat * py};
        if (seen.insert(c).second) {
            out.push_back(c);
        }
    }
    return out;
}


// ---------------------------------------------------------------------------
// Shape: square_at_range (Outdated Grenade, Pocket Nuke)
//   Square centered on the cell max_range steps along (dx,dy). Radius from payload (default 1 = 3x3).
// ---------------------------------------------------------------------------
std::vector<Cell> shape_square_at_range(int ax, int ay, int dx, int dy, const AoEShapeParams& p)
{
    if (dx == 0 && dy == 0) {
        return {};
    }
    const int range = std::max(1, p.max_range);
    int radius = 1;
    if (p.payload) {
        if (const auto it = p.payload->find("radius"); it != p.payload->end()) {
            radius = std::max(0, it->second);
        }
    }
    const int cx = ax + range * dx;
    const int cy = ay + range * dy;
    std::vector<Cell> out;
    out.reserve(static_cast<std::size_t>((2 * radius + 1) * (2 * radius + 1)));
    for (int ox = -radius; ox <= radius; ++ox) {
        for (int oy = -radius; oy <= radius; ++oy) {
            out.push_back({cx + ox, cy + oy});
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// Shape: line (Piercing Shot)
//   A single-cell-wide ray projected forward from the anchor up to max_range.
//   Matches the piercing_shot handler's ray (preview only - the handler runs its
//   own base-skipping projection at resolve time).
// ---------------------------------------------------------------------------
std::vector<Cell> shape_line(int ax, int ay, int dx, int dy, const AoEShapeParams& p)
{
    if (dx == 0 && dy == 0) {
        return {};
    }
    const int range = std::max(1, p.max_range);
    std::vector<Cell> out;
    out.reserve(static_cast<std::size_t>(range));
    for (int d = 1; d <= range; ++d) {
        out.push_back({ax + d * dx, ay + d * dy});
    }
    return out;
}

}  // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void register_aoe_shape(const std::string& shape_key, AoEShapeFn fn)
{
    auto& r = registry();
    std::lock_guard<std::mutex> lock(r.mtx);
    r.shapes[shape_key] = std::move(fn);
}

std::vector<Cell> compute_aoe_cells(const std::string& shape_key,
    int ax, int ay, int dx, int dy,
    const AoEShapeParams& params)
{
    ensure_builtin_aoe_shapes_registered();
    auto& r = registry();
    std::lock_guard<std::mutex> lock(r.mtx);
    const auto it = r.shapes.find(shape_key);
    if (it == r.shapes.end()) {
        return {};
    }
    return it->second(ax, ay, dx, dy, params);
}

std::vector<std::string> list_aoe_shape_keys()
{
    ensure_builtin_aoe_shapes_registered();
    auto& r = registry();
    std::lock_guard<std::mutex> lock(r.mtx);
    std::vector<std::string> keys;
    keys.reserve(r.shapes.size());
    for (const auto& [k, _] : r.shapes) {
        keys.push_back(k);
    }
    return keys;
}

bool aoe_shape_known(const std::string& shape_key)
{
    ensure_builtin_aoe_shapes_registered();
    auto& r = registry();
    std::lock_guard<std::mutex> lock(r.mtx);
    return r.shapes.count(shape_key) != 0;
}

void ensure_builtin_aoe_shapes_registered()
{
    static bool registered = false;
    if (registered) return;
    // Not fully thread-safe for the first call race, but static local init is
    // thread-safe in C++11 so we use a double-checked pattern here instead.
    auto& r = registry();
    {
        std::lock_guard<std::mutex> lock(r.mtx);
        if (registered) return;
        r.shapes["rectangle"] = shape_rectangle;
        r.shapes["helix"]     = shape_helix;
        r.shapes["line"]            = shape_line;
        r.shapes["square_at_range"] = shape_square_at_range;
        registered = true;
    }
}

AoEShapeParams aoe_shape_params_from_payload(const std::map<std::string, int>& payload)
{
    AoEShapeParams p;
    p.payload = &payload;
    if (const auto it = payload.find("width");     it != payload.end()) p.width     = it->second;
    if (const auto it = payload.find("depth");     it != payload.end()) p.depth     = it->second;
    if (const auto it = payload.find("max_range"); it != payload.end()) p.max_range = it->second;
    return p;
}

std::vector<Cell> scorching_sphere_cells(const int center_x, const int center_y)
{
    std::vector<Cell> out;
    out.reserve(13);
    std::set<Cell> seen;
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dy = -1; dy <= 1; ++dy) {
            const Cell c{center_x + dx, center_y + dy};
            if (seen.insert(c).second) {
                out.push_back(c);
            }
        }
    }
    static constexpr std::pair<int, int> kEdgeExtensions[] = {{0, -2}, {0, 2}, {2, 0}, {-2, 0}};
    for (const auto& [dx, dy] : kEdgeExtensions) {
        const Cell c{center_x + dx, center_y + dy};
        if (seen.insert(c).second) {
            out.push_back(c);
        }
    }
    return out;
}

std::vector<Cell> gas_strike_cross_cells(const int center_x, const int center_y)
{
    return {
        {center_x, center_y},
        {center_x + 1, center_y},
        {center_x - 1, center_y},
        {center_x, center_y + 1},
        {center_x, center_y - 1},
    };
}

std::vector<Cell> shocking_stimulus_aoe_cells(const int center_x, const int center_y)
{
    std::vector<Cell> out;
    out.reserve(16);
    for (int dx = -1; dx <= 2; ++dx) {
        for (int dy = -1; dy <= 2; ++dy) {
            out.emplace_back(center_x + dx, center_y + dy);
        }
    }
    return out;
}

}  // namespace tactics
