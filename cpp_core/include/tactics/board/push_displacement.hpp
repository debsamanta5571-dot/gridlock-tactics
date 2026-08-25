#pragma once

#include "tactics/common/types.hpp"
#include "tactics/entities/entity.hpp"

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace tactics {

class GameState;

struct PushDisplacementOptions {
    /** When true, Immovable does not block the push (Crushing Advance crush-push only). */
    bool bypass_immovable{false};
    std::string collision_source_label{"push collision"};
};

struct PushDisplacementResult {
    bool ok{true};
    std::string message;
    int tiles_moved{0};
    bool fell_into_void{false};
    bool collision{false};
    int collision_damage{0};
    std::shared_ptr<Entity> collision_blocker;
};

/**
 * Forcibly displace `entity` up to `distance` tiles along (dir_x, dir_y).
 * Applies terrain enter damage and tile overlay debuffs on traversed cells.
 * Void traversal/landing removes the unit. Blocked steps deal 1 + remaining push tiles
 * damage to the mover and to a blocking entity (walls damage only the mover).
 */
PushDisplacementResult resolve_push_displacement(
    GameState& game,
    const std::shared_ptr<Entity>& entity,
    int dir_x,
    int dir_y,
    int distance,
    PushDisplacementOptions options = {});

/** Octilinear push direction away from `source` toward `target` anchor. */
std::pair<int, int> push_direction_away_from_source(const Entity& source, const Entity& target);

/**
 * Octilinear push direction for controller spells: away from the controller's base pad
 * toward `target` (continues past the target along that ray).
 */
std::pair<int, int> push_direction_away_from_controller_base(
    const GameState& game, int controller_id, const Entity& target);

/** Adjacent direction-picker cells around `target` (cardinal or 8-way per payload `cardinal_only`). */
std::vector<std::pair<int, int>> push_direction_aim_indicator_cells(
    const GameState& game, const Entity& target, const std::map<std::string, int>& payload);

/** Octilinear direction from `target` anchor toward (`aim_x`, `aim_y`). */
std::pair<int, int> push_direction_from_aim_cell(const Entity& target, int aim_x, int aim_y);

/** Cells the target would traverse when pushed `distance` tiles along the chosen direction. */
std::vector<std::pair<int, int>> preview_push_displacement_path_cells(
    const Entity& target, int dir_x, int dir_y, int distance);

}  // namespace tactics