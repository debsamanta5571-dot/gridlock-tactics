#pragma once

#include "tactics/board/grid.hpp"
#include "tactics/entities/entity.hpp"

namespace tactics {

struct MovementPolicy {
    float normal_tile_cost{1.0f};
    float diagonal_surcharge{0.5f};
    float unreachable_tile_cost{kUnreachableTileMovementCost};
    float damage_path_cost_weight{1000.0f};
    float void_fall_threshold{0.5f};
};

const MovementPolicy& default_movement_policy();
float terrain_movement_cost_for_square(const GridSquare& sq, const MovementPolicy& policy = default_movement_policy());
int terrain_damage_on_enter_for_square(const GridSquare& sq);
bool terrain_square_is_void(const GridSquare& sq);
float diagonal_movement_surcharge(int from_x, int from_y, int to_x, int to_y, const MovementPolicy& policy = default_movement_policy());
bool entity_moves_over_blockers_for_pathing(const Entity& entity);
bool entity_ignores_terrain_for_pathing(const Entity& entity);
bool entity_ignores_damaging_terrain(const Entity& entity);
bool entity_can_land_on_square(const Entity& entity, const GridSquare& sq);
bool entity_should_fall_into_void(const Entity& entity, const std::vector<const GridSquare*>& occupied_squares,
                                  const MovementPolicy& policy = default_movement_policy());
/** True when the void fraction across the footprint is below the threshold (entity may land here). */
bool footprint_void_fraction_below_threshold(const GetGridSquareFn& get_square,
    const std::vector<std::pair<int, int>>& offsets, int anchor_x, int anchor_y,
    const MovementPolicy& policy = default_movement_policy());
bool footprint_void_fraction_below_threshold(const GetGridSquareFn& get_square,
    const Entity& entity, int anchor_x, int anchor_y,
    const MovementPolicy& policy = default_movement_policy());

}  // namespace tactics
