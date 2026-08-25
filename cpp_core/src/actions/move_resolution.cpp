#include "tactics/actions/move_resolution.hpp"

#include "tactics/attributes/attributes.hpp"
#include "tactics/combat/taunt.hpp"
#include "tactics/board/grid.hpp"
#include "tactics/board/movement_policy.hpp"
#include "tactics/core/game_state.hpp"
#include "tactics/entities/entity.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace tactics {

namespace {

bool contains_cell(const std::vector<std::pair<int, int>>& cells, int x, int y) {
    return std::find(cells.begin(), cells.end(), std::make_pair(x, y)) != cells.end();
}

std::vector<std::pair<int, int>> footprint_cells_at(const Entity& entity, int anchor_x, int anchor_y) {
    std::vector<std::pair<int, int>> cells;
    const auto offs = entity_shape_offsets(entity);
    cells.reserve(offs.size());
    for (const auto& [dx, dy] : offs) {
        cells.push_back({anchor_x + dx, anchor_y + dy});
    }
    return cells;
}

float path_movement_total(const GameState& game, const std::shared_ptr<Entity>& actor, const std::vector<std::pair<int, int>>& path) {
    if (!actor) {
        return std::numeric_limits<float>::infinity();
    }
    float total = 0.0f;
    const bool ignores_terrain = entity_ignores_terrain_for_pathing(*actor);
    const bool ignores_diagonal = entity_ignores_diagonal_movement_cost(*actor);
    for (size_t i = 1; i < path.size(); ++i) {
        const auto [px, py] = path[i - 1];
        const auto [cx, cy] = path[i];
        const auto from_cells = footprint_cells_at(*actor, px, py);
        const auto to_cells = footprint_cells_at(*actor, cx, cy);
        for (const auto& [tx, ty] : to_cells) {
            if (!contains_cell(from_cells, tx, ty)) {
                total += ignores_terrain ? 1.0f : game.board.movement_cost(tx, ty);
            }
        }
        if (!ignores_diagonal && std::abs(cx - px) + std::abs(cy - py) == 2) {
            total += 0.5f;
        }
    }
    return total;
}

int path_terrain_damage_total(const GameState& game, const std::shared_ptr<Entity>& actor, const std::vector<std::pair<int, int>>& path) {
    if (!actor) {
        return 0;
    }
    return path_footprint_terrain_damage([&game](int x, int y) { return game.board.get_square(x, y); }, *actor, path);
}

}  // namespace

MoveGoalResolution resolve_move_goal(const GameState& game, const std::shared_ptr<Entity>& actor, int goal_x, int goal_y) {
    MoveGoalResolution out;
    if (!actor || !actor->position) {
        out.message = "Unit has no position";
        return out;
    }
    if (!entity_can_move(*actor)) {
        out.message = "Buildings cannot move";
        return out;
    }
    if (core_cracker_shutdown_blocks_actions(*actor)) {
        out.message = "Core Cracker must Prime Core (3 gallantry, slow) before moving";
        return out;
    }
    if (const auto u = std::dynamic_pointer_cast<const Unit>(actor)) {
        if (deployment_fatigue_blocks_move(*u) && u->moves_remaining_this_turn <= 0) {
            out.message = "Cannot move on the turn this unit was deployed";
            return out;
        }
        if (u->moves_remaining_this_turn <= 0) {
            out.message = "No moves remaining this turn";
            return out;
        }
        if (game.turn_manager.current_phase == TurnPhase::BonusAttackDeclaration
            && !unit_may_move_during_bonus_attack_declaration(*u)) {
            out.message = "Only units with bonus attacks or bonus moves may move during bonus attack declaration";
            return out;
        }

        if (entity_is_rooted(*u)) {
            out.message = "Unit is rooted and cannot move";
            return out;
        }
        if (entity_is_stunned(*u)) {
            out.message = "Unit is stunned and cannot move";
            return out;
        }
    }
    if (*actor->position == std::make_pair(goal_x, goal_y)) {
        out.message = "Already at target position";
        return out;
    }
    const int max_move = pathfind_movement_budget_for_entity(actor);
    const auto [cur_ax, cur_ay] = *actor->position;
    const std::vector<std::pair<int, int>> anchor_candidates = entity_anchor_positions_covering_cell(*actor, goal_x, goal_y);

    float best_total = std::numeric_limits<float>::infinity();
    int best_damage = std::numeric_limits<int>::max();
    int best_len = 0;
    int best_ax = 0;
    int best_ay = 0;
    bool found = false;

    for (const auto& [ax, ay] : anchor_candidates) {
        if (ax == cur_ax && ay == cur_ay) {
            continue;
        }
        const auto path = game.board.find_path(actor, ax, ay, &game);
        if (!path || !path_respects_taunt_stop(game, *actor, *path)) {
            continue;
        }
        if (!move_destination_allowed_under_taunt(game, *actor, cur_ax, cur_ay, ax, ay)) {
            continue;
        }
        const float total = path_movement_total(game, actor, *path);
        if (total > static_cast<float>(max_move) + kMovePathTotalEpsilon) {
            continue;
        }
        const int damage = path_terrain_damage_total(game, actor, *path);
        const int plen = static_cast<int>(path->size());
        if (!found || damage < best_damage ||
            (damage == best_damage && total < best_total - kMovePathTotalEpsilon) ||
            (damage == best_damage && std::abs(total - best_total) <= kMovePathTotalEpsilon && plen < best_len) ||
            (damage == best_damage && std::abs(total - best_total) <= kMovePathTotalEpsilon && plen == best_len &&
                (ax < best_ax || (ax == best_ax && ay < best_ay)))) {
            found = true;
            best_total = total;
            best_damage = damage;
            best_len = plen;
            best_ax = ax;
            best_ay = ay;
        }
    }

    if (!found) {
        // Emergency min-move fallback: only fires when the unit has NO normal destinations
        // at all (budget too small for available terrain - e.g. every adjacent tile is rough
        // terrain and budget == kMinimumMoveSteps).  We verify this by checking whether
        // find_reachable_anchors returns any anchor other than the current position; if it
        // does, the player just clicked an unreachable goal and we reject normally.
        // When truly stuck, accept any anchor candidate that is exactly one step away,
        // bypassing the terrain-cost check.  Taunt, landing, and diagonal-corner rules
        // still apply.  find_path's emergency scan returns the 1-step path.
        const auto normal_anchors = game.board.find_reachable_anchors(actor, &game);
        const bool has_normal_destinations = std::any_of(
            normal_anchors.begin(), normal_anchors.end(),
            [&](const std::pair<int, int>& c) { return c.first != cur_ax || c.second != cur_ay; });

        if (!has_normal_destinations) {
            for (const auto& [ax, ay] : anchor_candidates) {
                if (ax == cur_ax && ay == cur_ay) continue;
                if (std::abs(ax - cur_ax) > 1 || std::abs(ay - cur_ay) > 1) continue;
                const auto path = game.board.find_path(actor, ax, ay, &game);
                if (!path || !path_respects_taunt_stop(game, *actor, *path)) continue;
                if (!move_destination_allowed_under_taunt(game, *actor, cur_ax, cur_ay, ax, ay)) continue;
                const int damage = path_terrain_damage_total(game, actor, *path);
                const int plen = static_cast<int>(path->size());
                const float total = path_movement_total(game, actor, *path);
                if (!found || damage < best_damage ||
                    (damage == best_damage && total < best_total - kMovePathTotalEpsilon) ||
                    (damage == best_damage && std::abs(total - best_total) <= kMovePathTotalEpsilon && plen < best_len) ||
                    (damage == best_damage && std::abs(total - best_total) <= kMovePathTotalEpsilon && plen == best_len &&
                        (ax < best_ax || (ax == best_ax && ay < best_ay)))) {
                    found = true;
                    best_total = total;
                    best_damage = damage;
                    best_len = plen;
                    best_ax = ax;
                    best_ay = ay;
                }
            }
        }
    }

    if (!found) {
        out.message = "No valid path to target";
        return out;
    }
    out.ok = true;
    out.resolved_ax = best_ax;
    out.resolved_ay = best_ay;
    out.message = "ok";
    return out;
}

std::vector<std::pair<int, int>> gather_reachable_move_goal_cells(const GameState& game, const std::shared_ptr<Entity>& actor) {
    std::vector<std::pair<int, int>> out;
    if (!actor || !actor->position) {
        return out;
    }
    if (!entity_can_move(*actor)) {
        return out;
    }
    if (auto u = std::dynamic_pointer_cast<const Unit>(actor)) {
        if (u->moves_remaining_this_turn <= 0) {
            return out;
        }
        if (entity_is_rooted(*u)) {
            return out;
        }
        if (entity_is_stunned(*u)) {
            return out;
        }
    }
    const auto [cur_ax, cur_ay] = *actor->position;
    const auto anchors = game.board.find_reachable_anchors(actor, &game);
    const auto offs = entity_shape_offsets(*actor);
    std::set<std::pair<int, int>> cells;
    for (const auto& [ax, ay] : anchors) {
        if (ax == cur_ax && ay == cur_ay) {
            continue;
        }
        // Reachable anchors already respect taunt pathing in find_reachable_anchors; only validate destination leash.
        if (!move_destination_allowed_under_taunt(game, *actor, cur_ax, cur_ay, ax, ay)) {
            continue;
        }
        for (const auto& [dx, dy] : offs) {
            cells.insert({ax + dx, ay + dy});
        }
    }

    // Emergency min-move: if the normal BFS produced no destinations, highlight the
    // physically-accessible 1-step anchors so the player can still move 1 tile (even onto
    // rough terrain when that is the only option).
    if (cells.empty()) {
        for (const auto& [ax, ay] : game.board.find_emergency_step_anchors(actor, &game)) {
            for (const auto& [dx, dy] : offs) {
                cells.insert({ax + dx, ay + dy});
            }
        }
    }

    out.reserve(cells.size());
    for (const auto& c : cells) {
        out.push_back(c);
    }
    return out;
}

bool rotation_fits_at_anchor(const GameState& game, const std::shared_ptr<Entity>& actor, int ax, int ay, int quarter_turns_cw,
                             std::vector<std::pair<int, int>>* out_normalized_shape) {
    if (!actor) {
        return false;
    }
    int n = quarter_turns_cw % 4;
    if (n < 0) {
        n += 4;
    }
    if (n == 0) {
        for (const auto& [dx, dy] : entity_shape_offsets(*actor)) {
            auto sq = game.board.get_square(ax + dx, ay + dy);
            if (!sq || !entity_can_land_on_square(*actor, *sq)) {
                return false;
            }
        }
        if (out_normalized_shape) {
            *out_normalized_shape = entity_shape_offsets(*actor);
        }
        return game.board.can_place_shape_at(ax, ay, entity_shape_offsets(*actor), actor);
    }
    auto trial = entity_shape_offsets(*actor);
    rotate_shape_offsets_n_quarters_cw(trial, quarter_turns_cw);
    if (!game.board.can_place_shape_at(ax, ay, trial, actor)) {
        return false;
    }
    for (const auto& [dx, dy] : trial) {
        auto sq = game.board.get_square(ax + dx, ay + dy);
        if (!sq || !entity_can_land_on_square(*actor, *sq)) {
            return false;
        }
    }
    if (out_normalized_shape) {
        *out_normalized_shape = std::move(trial);
    }
    return true;
}

}  // namespace tactics
