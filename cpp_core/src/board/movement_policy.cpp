#include "tactics/board/movement_policy.hpp"

#include "tactics/attributes/attributes.hpp"
#include "tactics/entities/entity.hpp"

#include <algorithm>
#include <cmath>

namespace tactics {

const MovementPolicy& default_movement_policy()
{
    static const MovementPolicy policy{};
    return policy;
}

float terrain_movement_cost_for_square(const GridSquare& sq, const MovementPolicy& policy)
{
    float cost = policy.normal_tile_cost;
    for (const auto& mod : sq.modifiers) {
        if (mod.movement_cost > cost) {
            cost = mod.movement_cost;
        } else if (cost <= policy.normal_tile_cost && mod.movement_cost > 0.0f && mod.movement_cost < cost) {
            cost = mod.movement_cost;
        }
    }
    return cost;
}

int terrain_damage_on_enter_for_square(const GridSquare& sq)
{
    int damage = 0;
    for (const auto& mod : sq.modifiers) {
        damage += std::max(0, mod.damage_on_enter);
    }
    return damage;
}

bool terrain_square_is_void(const GridSquare& sq)
{
    return std::any_of(sq.modifiers.begin(), sq.modifiers.end(), [](const SquareModifier& mod) { return mod.is_void; });
}

float diagonal_movement_surcharge(int from_x, int from_y, int to_x, int to_y, const MovementPolicy& policy)
{
    return (std::abs(to_x - from_x) + std::abs(to_y - from_y) == 2) ? policy.diagonal_surcharge : 0.0f;
}

bool entity_moves_over_blockers_for_pathing(const Entity& entity)
{
    return moves_over_blockers(entity);
}

bool entity_ignores_terrain_for_pathing(const Entity& entity)
{
    return ignores_terrain_movement_cost(entity);
}

bool entity_ignores_damaging_terrain(const Entity& entity)
{
    return has_flying(entity);
}

bool entity_can_land_on_square(const Entity& entity, const GridSquare& sq)
{
    if (!terrain_square_is_void(sq)) return true;
    if (has_flying(entity)) return true;
    // Large units allow individual void cells; a separate footprint-level threshold check
    // (footprint_void_fraction_below_threshold) enforces the 50% rule at the anchor level.
    if (entity_uses_void_threshold_for_landing(entity)) return true;
    return false;
}

bool entity_should_fall_into_void(const Entity& entity, const std::vector<const GridSquare*>& occupied_squares, const MovementPolicy& policy)
{
    if (has_flying(entity) || occupied_squares.empty()) {
        return false;
    }
    int void_count = 0;
    int valid_count = 0;
    for (const GridSquare* sq : occupied_squares) {
        if (!sq) {
            continue;
        }
        ++valid_count;
        if (terrain_square_is_void(*sq)) {
            ++void_count;
        }
    }
    return valid_count > 0 && static_cast<float>(void_count) / static_cast<float>(valid_count) >= policy.void_fall_threshold;
}

bool footprint_void_fraction_below_threshold(
    const GetGridSquareFn& get_square,
    const std::vector<std::pair<int, int>>& offsets,
    int anchor_x, int anchor_y,
    const MovementPolicy& policy)
{
    int void_count = 0, cell_count = 0;
    for (const auto& [dx, dy] : offsets) {
        if (auto sq = get_square(anchor_x + dx, anchor_y + dy)) {
            ++cell_count;
            if (terrain_square_is_void(*sq)) ++void_count;
        }
    }
    return cell_count == 0
        || static_cast<float>(void_count) / static_cast<float>(cell_count) < policy.void_fall_threshold;
}

bool footprint_void_fraction_below_threshold(
    const GetGridSquareFn& get_square,
    const Entity& entity, int anchor_x, int anchor_y,
    const MovementPolicy& policy)
{
    return footprint_void_fraction_below_threshold(get_square, entity_shape_offsets(entity), anchor_x, anchor_y, policy);
}

}  // namespace tactics
