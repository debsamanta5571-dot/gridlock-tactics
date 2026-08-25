#pragma once

#include "tactics/core/game_state.hpp"
#include "tactics/entities/entity.hpp"

#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace tactics {

inline bool has_taunt(const Entity& e) { return entity_has_attribute(e, "taunt"); }

/** Anchor used for taunt rules: pending move destination when previewing, else board anchor. */
std::optional<std::pair<int, int>> taunt_acting_anchor_for_unit(const GameState& game, const Entity& unit);

/** True when two world cells share an edge (orthogonal only, not diagonal corners). */
bool world_cells_share_orthogonal_edge(int ax, int ay, int bx, int by);

/**
 * True when any footprint cell of `a` at `a_anchor` shares an edge with any cell of `b` at `b_anchor`.
 * Diagonal corner contact does not count.
 */
bool entity_orthogonally_adjacent_at(const Entity& a, int a_anchor_x, int a_anchor_y, const Entity& b, int b_anchor_x, int b_anchor_y);

/** Enemy taunt units orthogonally adjacent to `mover` at `anchor_x`/`anchor_y`. */
std::vector<std::shared_ptr<Entity>> enemy_taunts_orthogonally_adjacent_to_anchor(const GameState& game, const Entity& mover, int anchor_x,
    int anchor_y);

inline std::vector<std::shared_ptr<Entity>> enemy_taunts_orthogonally_adjacent_to(const GameState& game, const Entity& mover)
{
    const auto anchor = taunt_acting_anchor_for_unit(game, mover);
    if (!anchor) {
        return {};
    }
    return enemy_taunts_orthogonally_adjacent_to_anchor(game, mover, anchor->first, anchor->second);
}

bool entity_orthogonally_adjacent_to_enemy_taunt_at(const GameState& game, const Entity& mover, int anchor_x, int anchor_y);

inline bool entity_orthogonally_adjacent_to_enemy_taunt(const GameState& game, const Entity& mover)
{
    const auto anchor = taunt_acting_anchor_for_unit(game, mover);
    if (!anchor) {
        return false;
    }
    return entity_orthogonally_adjacent_to_enemy_taunt_at(game, mover, anchor->first, anchor->second);
}

/**
 * True when a movement anchor is on a tile orthogonally adjacent to an enemy taunt.
 * Further movement in the same action cannot continue from this anchor (sticky stop).
 */
bool movement_anchor_stops_under_enemy_taunt(const GameState& game, const Entity& mover, int anchor_x, int anchor_y);

/**
 * Movement destination allowed under taunt.
 * Units already adjacent cannot move away; others may end on an adjacent tile but not pass through it.
 */
bool move_destination_allowed_under_taunt(const GameState& game, const Entity& mover, int from_anchor_x, int from_anchor_y, int dest_anchor_x,
    int dest_anchor_y);

/** No anchor before the path terminus may be orthogonally adjacent to enemy taunt. */
bool path_respects_taunt_stop(const GameState& game, const Entity& mover, const std::vector<std::pair<int, int>>& path);

/** When restricted, enemy board targets must be an orthogonally adjacent enemy taunt. */
bool taunt_allows_board_target(const GameState& game, const Entity* acting_unit, int controller_seat, const Entity& target,
    std::optional<std::pair<int, int>> acting_anchor = std::nullopt);

}  // namespace tactics
