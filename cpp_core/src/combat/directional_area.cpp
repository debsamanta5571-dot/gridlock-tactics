#include "tactics/combat/directional_area.hpp"

#include "tactics/board/adjacency.hpp"
#include "tactics/combat/aoe_shapes.hpp"
#include "tactics/common/effect_keys.hpp"
#include "tactics/effects/effect_traits.hpp"
#include "tactics/entities/entity.hpp"

#include <algorithm>
#include <cmath>
#include <set>

namespace tactics {
namespace {

int sign_nonzero(int v)
{
    if (v > 0) {
        return 1;
    }
    if (v < 0) {
        return -1;
    }
    return 0;
}

std::optional<std::pair<int, int>> actor_anchor(const Entity& actor)
{
    if (actor.position) {
        return *actor.position;
    }
    if (!actor.occupied_positions.empty()) {
        return actor.occupied_positions.front();
    }
    return std::nullopt;
}

bool is_on_octilinear_ray_from(const int ax, const int ay, const int tx, const int ty)
{
    const int vx = tx - ax;
    const int vy = ty - ay;
    if (vx == 0 && vy == 0) {
        return false;
    }
    const int adx = std::abs(vx);
    const int ady = std::abs(vy);
    return adx == 0 || ady == 0 || adx == ady;
}

}  // namespace

std::pair<int, int> snap_octilinear_direction(const int from_x, const int from_y, const int to_x, const int to_y)
{
    const int raw_dx = to_x - from_x;
    const int raw_dy = to_y - from_y;
    return {sign_nonzero(raw_dx), sign_nonzero(raw_dy)};
}

std::vector<std::pair<int, int>> directional_area_cells(const int anchor_x, const int anchor_y, const int dir_x, const int dir_y,
    const int width, const int depth)
{
    if (dir_x == 0 && dir_y == 0) {
        return {};
    }
    if (dir_x != 0 && dir_y != 0) {
        static_cast<void>(width);
        static_cast<void>(depth);
        return {
            {anchor_x + dir_x, anchor_y},
            {anchor_x + dir_x, anchor_y + dir_y},
            {anchor_x, anchor_y + dir_y},
            {anchor_x + 2 * dir_x, anchor_y + dir_y},
            {anchor_x + 2 * dir_x, anchor_y + 2 * dir_y},
            {anchor_x + dir_x, anchor_y + 2 * dir_y},
        };
    }
    int perp_x = 0;
    int perp_y = 0;
    if (dir_x == 0) {
        perp_x = 1;
        perp_y = 0;
    } else if (dir_y == 0) {
        perp_x = 0;
        perp_y = 1;
    } else {
        perp_x = -dir_y;
        perp_y = dir_x;
    }
    const int w = std::max(1, width);
    const int d = std::max(1, depth);
    const int half = w / 2;
    std::vector<std::pair<int, int>> out;
    out.reserve(static_cast<std::size_t>(w * d));
    std::set<std::pair<int, int>> seen;
    for (int depth_step = 1; depth_step <= d; ++depth_step) {
        for (int w_idx = 0; w_idx < w; ++w_idx) {
            const int lateral = w_idx - half;
            const int x = anchor_x + depth_step * dir_x + lateral * perp_x;
            const int y = anchor_y + depth_step * dir_y + lateral * perp_y;
            const auto cell = std::make_pair(x, y);
            if (seen.insert(cell).second) {
                out.push_back(cell);
            }
        }
    }
    return out;
}

std::vector<std::pair<int, int>> terra_cone_splash_cells(
    const int anchor_x, const int anchor_y, const int dir_x, const int dir_y)
{
    if (dir_x == 0 && dir_y == 0) {
        return {};
    }
    const int perp_x = -dir_y;
    const int perp_y = dir_x;
    const int base_x = anchor_x + 2 * dir_x;
    const int base_y = anchor_y + 2 * dir_y;
    return {
        {base_x - perp_x, base_y - perp_y},
        {base_x, base_y},
        {base_x + perp_x, base_y + perp_y},
    };
}

std::vector<std::pair<int, int>> directional_movement_landing_cells(
    const GameState& game, const Entity& actor, const int min_range, const int max_range, const bool allow_diagonals)
{
    const auto anchor = actor_anchor(actor);
    if (!anchor || max_range < 1) {
        return {};
    }
    const auto [ax, ay] = *anchor;
    std::vector<std::pair<int, int>> out;

    const auto scan_dir = [&](int ddx, int ddy) {
        for (int dist = 1; dist <= max_range; ++dist) {
            const int x = ax + dist * ddx;
            const int y = ay + dist * ddy;
            if (!game.board.get_square(x, y)) { break; }  // void / out-of-bounds: stop this ray
            if (dist < min_range) { continue; }
            // Valid landing: empty cell, or a cell whose only occupant is a pickup.
            const auto ent = game.board.entity_at(x, y);
            if (!ent || entity_is_pickup(*ent)) {
                out.push_back({x, y});
            }
            // A non-pickup occupant blocks landing here but NOT movement past it (flying over).
        }
    };

    if (allow_diagonals) {
        for (const auto [ddx, ddy] : kSurroundingOffsets) { scan_dir(ddx, ddy); }
    } else {
        for (const auto [ddx, ddy] : kAdjacentOffsets)    { scan_dir(ddx, ddy); }
    }
    return out;
}

std::vector<std::pair<int, int>> directional_area_aim_cells(
    const GameState& game, const Entity& actor, const int max_range, const bool allow_diagonals)
{
    const auto anchor = actor_anchor(actor);
    if (!anchor || max_range < 1) {
        return {};
    }
    const auto [ax, ay] = *anchor;
    std::vector<std::pair<int, int>> out;
    out.reserve(allow_diagonals ? 8 : 4);
    const auto scan_dir = [&](int ddx, int ddy) {
        std::optional<std::pair<int, int>> farthest;
        for (int dist = 1; dist <= max_range; ++dist) {
            const int x = ax + dist * ddx;
            const int y = ay + dist * ddy;
            if (!game.board.get_square(x, y)) { break; }
            farthest = std::make_pair(x, y);
        }
        if (farthest) { out.push_back(*farthest); }
    };
    if (allow_diagonals) {
        for (const auto [ddx, ddy] : kSurroundingOffsets) { scan_dir(ddx, ddy); }
    } else {
        for (const auto [ddx, ddy] : kAdjacentOffsets)    { scan_dir(ddx, ddy); }
    }
    return out;
}

std::vector<std::pair<int, int>> directional_aim_indicator_cells(
    const GameState& game, const Entity& actor, const std::map<std::string, int>& payload)
{
    const bool allow_diagonals = [&payload]() {
        const auto it = payload.find("cardinal_only");
        return !(it != payload.end() && it->second != 0);
    }();
    return directional_area_aim_cells(game, actor, kDirectionalAimIndicatorRange, allow_diagonals);
}

std::vector<std::pair<int, int>> cleave_pattern_cells(const Entity& caster, const int target_x, const int target_y)
{
    const auto anchor = actor_anchor(caster);
    if (!anchor) {
        return {};
    }
    const auto [ax0, ay0] = *anchor;
    int nearest_ax = ax0;
    int nearest_ay = ay0;
    int best_dist = std::abs(target_x - ax0) + std::abs(target_y - ay0);
    for (const auto& [dx, dy] : entity_shape_offsets(caster)) {
        const int cx2 = ax0 + dx;
        const int cy2 = ay0 + dy;
        const int d = std::abs(target_x - cx2) + std::abs(target_y - cy2);
        if (d < best_dist) {
            best_dist = d;
            nearest_ax = cx2;
            nearest_ay = cy2;
        }
    }

    const int raw_dx = target_x - nearest_ax;
    const int raw_dy = target_y - nearest_ay;
    int dir_x = 0;
    int dir_y = 0;
    if (raw_dx == 0) {
        dir_y = (raw_dy > 0) ? 1 : (raw_dy < 0 ? -1 : 0);
    } else if (raw_dy == 0) {
        dir_x = (raw_dx > 0) ? 1 : -1;
    } else if (std::abs(raw_dx) == std::abs(raw_dy)) {
        dir_x = (raw_dx > 0) ? 1 : -1;
        dir_y = (raw_dy > 0) ? 1 : -1;
    } else if (std::abs(raw_dx) > std::abs(raw_dy)) {
        dir_x = (raw_dx > 0) ? 1 : -1;
    } else {
        dir_y = (raw_dy > 0) ? 1 : -1;
    }

    std::pair<int, int> flank_cells[2];
    if (dir_x != 0 && dir_y != 0) {
        flank_cells[0] = {target_x - dir_x, target_y};
        flank_cells[1] = {target_x, target_y - dir_y};
    } else {
        flank_cells[0] = {target_x + (-dir_y), target_y + dir_x};
        flank_cells[1] = {target_x + dir_y, target_y - dir_x};
    }

    std::vector<std::pair<int, int>> out;
    out.reserve(3);
    std::set<std::pair<int, int>> seen;
    const auto add = [&](const int x, const int y) {
        const auto cell = std::make_pair(x, y);
        if (seen.insert(cell).second) {
            out.push_back(cell);
        }
    };
    add(target_x, target_y);
    add(flank_cells[0].first, flank_cells[0].second);
    add(flank_cells[1].first, flank_cells[1].second);
    return out;
}

std::vector<std::shared_ptr<Entity>> entities_in_cells(const GameState& game, const std::vector<std::pair<int, int>>& cells)
{
    std::vector<std::shared_ptr<Entity>> out;
    std::set<std::string> seen_ids;
    for (const auto& [x, y] : cells) {
        auto ent = game.board.entity_at(x, y);
        if (!ent || ent->current_health <= 0 || !seen_ids.insert(ent->entity_id).second) {
            continue;
        }
        if (ent->entity_type != "unit" && !entity_is_structure(*ent)
                && !entity_is_breakable_obstacle(*ent) && !entity_is_base(*ent)
                && !entity_is_pickup(*ent)) {
            continue;
        }
        out.push_back(std::move(ent));
    }
    return out;
}

ActionResult validate_directional_area_damage_ability_target(
    const GameState& game, const int controller_id, const Entity& actor, const std::map<std::string, int>& targets, const int max_range,
    const bool allow_diagonals)
{
    static_cast<void>(controller_id);
    const auto xit = targets.find(effect_keys::kCellX);
    const auto yit = targets.find(effect_keys::kCellY);
    if (xit == targets.end() || yit == targets.end()) {
        return {false, "This ability requires a direction cell", {}};
    }
    const int tx = xit->second;
    const int ty = yit->second;
    if (!game.board.get_square(tx, ty)) {
        return {false, "Direction cell is off the board", {}};
    }
    const auto anchor = actor_anchor(actor);
    if (!anchor) {
        return {false, "Caster has no board position", {}};
    }
    const auto [ax, ay] = *anchor;
    if (ax == tx && ay == ty) {
        return {false, "Choose a direction cell other than the caster anchor", {}};
    }
    if (allow_diagonals) {
        if (!is_on_octilinear_ray_from(ax, ay, tx, ty)) {
            return {false, "Direction cell must lie on an octilinear ray from the caster", {}};
        }
    } else {
        if (tx != ax && ty != ay) {
            return {false, "Direction cell must lie on a cardinal ray from the caster", {}};
        }
    }
    const int dist = std::max(std::abs(tx - ax), std::abs(ty - ay));
    if (dist > max_range) {
        return {false, "Direction cell is out of range", {}};
    }
    const auto [dx, dy] = snap_octilinear_direction(ax, ay, tx, ty);
    if (dx == 0 && dy == 0) {
        return {false, "Invalid direction", {}};
    }
    return {true, "Directional area target allowed", {}};
}

std::vector<std::pair<int, int>> preview_directional_area_damage_blast_cells(
    const GameState& game, const Entity& actor, const int aim_x, const int aim_y, const int width, const int depth, const int max_range)
{
    const std::map<std::string, int> targets = {{effect_keys::kCellX, aim_x}, {effect_keys::kCellY, aim_y}};
    const auto vr = validate_directional_area_damage_ability_target(game, 0, actor, targets, max_range, true);
    if (!vr.ok) {
        return {};
    }
    const auto anchor = actor_anchor(actor);
    if (!anchor) {
        return {};
    }
    const auto [ax, ay] = *anchor;
    const auto [dir_x, dir_y] = snap_octilinear_direction(ax, ay, aim_x, aim_y);
    return directional_area_cells(ax, ay, dir_x, dir_y, width, depth);
}

std::vector<std::pair<int, int>> helix_area_cells(const int anchor_x, const int anchor_y, const int dir_x, const int dir_y)
{
    if (dir_x == 0 && dir_y == 0) {
        return {};
    }
    int perp_x = 0;
    int perp_y = 0;
    if (dir_x == 0) {
        perp_x = 1;
        perp_y = 0;
    } else if (dir_y == 0) {
        perp_x = 0;
        perp_y = 1;
    } else {
        perp_x = -dir_y;
        perp_y = dir_x;
    }
    static constexpr struct {
        int dist;
        int lat;
    } kHelixSteps[] = {{1, 1}, {2, -1}, {3, 1}, {4, -1}, {4, 1}, {3, -1}};
    std::vector<std::pair<int, int>> out;
    out.reserve(6);
    std::set<std::pair<int, int>> seen;
    for (const auto& step : kHelixSteps) {
        const int x = anchor_x + step.dist * dir_x + step.lat * perp_x;
        const int y = anchor_y + step.dist * dir_y + step.lat * perp_y;
        const auto cell = std::make_pair(x, y);
        if (seen.insert(cell).second) {
            out.push_back(cell);
        }
    }
    return out;
}

std::vector<std::pair<int, int>> preview_helix_damage_blast_cells(
    const GameState& game, const Entity& actor, const int aim_x, const int aim_y, const int max_range)
{
    const std::map<std::string, int> targets = {{effect_keys::kCellX, aim_x}, {effect_keys::kCellY, aim_y}};
    const auto vr = validate_directional_area_damage_ability_target(game, 0, actor, targets, max_range, true);
    if (!vr.ok) {
        return {};
    }
    const auto anchor = actor_anchor(actor);
    if (!anchor) {
        return {};
    }
    const auto [ax, ay] = *anchor;
    const auto [dir_x, dir_y] = snap_octilinear_direction(ax, ay, aim_x, aim_y);
    return helix_area_cells(ax, ay, dir_x, dir_y);
}

std::vector<std::pair<int, int>> preview_directional_damage_blast_cells(
    const GameState& game, const Entity& actor, const int aim_x, const int aim_y,
    const std::string& shape_key,
    const std::map<std::string, int>& payload)
{
    const int max_range = [&]() {
        const auto it = payload.find("max_range");
        return it != payload.end() ? it->second : 4;
    }();
    const bool allow_diagonals = [&]() {
        const auto it = payload.find("cardinal_only");
        return !(it != payload.end() && it->second != 0);
    }();
    const std::map<std::string, int> targets = {{effect_keys::kCellX, aim_x}, {effect_keys::kCellY, aim_y}};
    const auto vr = validate_directional_area_damage_ability_target(game, 0, actor, targets, max_range, allow_diagonals);
    if (!vr.ok) {
        return {};
    }
    const auto anchor = actor_anchor(actor);
    if (!anchor) {
        return {};
    }
    const auto [ax, ay] = *anchor;
    const auto [dir_x, dir_y] = snap_octilinear_direction(ax, ay, aim_x, aim_y);
    const auto params = aoe_shape_params_from_payload(payload);
    return compute_aoe_cells(shape_key.empty() ? "rectangle" : shape_key, ax, ay, dir_x, dir_y, params);
}

std::vector<std::pair<int, int>> preview_directional_effect_blast_cells(
    const GameState& game, const Entity& actor, const int aim_x, const int aim_y, const std::string& effect_key,
    const std::map<std::string, int>& payload, const std::map<std::string, std::string>& string_payload)
{
    if (!effect_uses_directional_aim(effect_key)) {
        return {};
    }
    if (effect_key == "directional_damage") {
        const std::string shape_key = [&]() {
            const auto it = string_payload.find("shape");
            return it != string_payload.end() ? it->second : "rectangle";
        }();
        return preview_directional_damage_blast_cells(game, actor, aim_x, aim_y, shape_key, payload);
    }
    if (effect_key == "piercing_shot") {
        const int max_range = [&]() {
            const auto it = payload.find("max_range");
            return it != payload.end() ? it->second : 8;
        }();
        const std::map<std::string, int> targets = {{effect_keys::kCellX, aim_x}, {effect_keys::kCellY, aim_y}};
        const auto vr = validate_directional_area_damage_ability_target(game, 0, actor, targets, max_range, true);
        if (!vr.ok) {
            return {};
        }
        const auto anchor = actor_anchor(actor);
        if (!anchor) {
            return {};
        }
        const auto [ax, ay] = *anchor;
        const auto [dir_x, dir_y] = snap_octilinear_direction(ax, ay, aim_x, aim_y);
        std::vector<std::pair<int, int>> out;
        out.reserve(static_cast<std::size_t>(max_range));
        for (int dist = 1; dist <= max_range; ++dist) {
            const int x = ax + dist * dir_x;
            const int y = ay + dist * dir_y;
            if (!game.board.get_square(x, y)) {
                break;
            }
            out.push_back({x, y});
        }
        return out;
    }
    if (effect_key == "terra_cone_strike") {
        const int max_range = [&]() {
            const auto it = payload.find("max_range");
            return it != payload.end() ? it->second : 1;
        }();
        const std::map<std::string, int> targets = {{effect_keys::kCellX, aim_x}, {effect_keys::kCellY, aim_y}};
        const auto vr = validate_directional_area_damage_ability_target(game, 0, actor, targets, max_range, true);
        if (!vr.ok) {
            return {};
        }
        const auto anchor = actor_anchor(actor);
        if (!anchor) {
            return {};
        }
        const auto [ax, ay] = *anchor;
        const auto [dir_x, dir_y] = snap_octilinear_direction(ax, ay, aim_x, aim_y);
        std::set<std::pair<int, int>> seen{{aim_x, aim_y}};
        std::vector<std::pair<int, int>> out = {{aim_x, aim_y}};
        for (const auto& cell : terra_cone_splash_cells(ax, ay, dir_x, dir_y)) {
            if (seen.insert(cell).second) {
                out.push_back(cell);
            }
        }
        return out;
    }
    if (effect_key_is_movement_landing(effect_key)) {
        const auto anchor = actor_anchor(actor);
        if (!anchor) {
            return {};
        }
        const auto [ax, ay] = *anchor;
        if (ax == aim_x && ay == aim_y) {
            return {};
        }
        const auto [dir_x, dir_y] = snap_octilinear_direction(ax, ay, aim_x, aim_y);
        if (dir_x == 0 && dir_y == 0) {
            return {};
        }
        const int dist = std::max(std::abs(aim_x - ax), std::abs(aim_y - ay));
        std::vector<std::pair<int, int>> out;
        out.reserve(static_cast<std::size_t>(dist));
        for (int step = 1; step <= dist; ++step) {
            const int x = ax + step * dir_x;
            const int y = ay + step * dir_y;
            if (!game.board.get_square(x, y)) {
                break;
            }
            out.push_back({x, y});
        }
        return out;
    }
    return {};
}


bool effect_uses_directional_aim(const std::string& effect_key)
{
    return effect_key_uses_directional_aim(effect_key);
}

}  // namespace tactics
