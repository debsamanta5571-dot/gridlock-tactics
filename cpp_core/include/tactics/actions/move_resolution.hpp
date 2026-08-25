#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace tactics {

class GameState;
struct Entity;

struct MoveGoalResolution {
    bool ok{false};
    std::string message;
    int resolved_ax{0};
    int resolved_ay{0};
};

/** Pick cheapest anchor whose footprint covers (goal_x, goal_y); path must exist within movement budget. */
MoveGoalResolution resolve_move_goal(const GameState& game, const std::shared_ptr<Entity>& actor, int goal_x, int goal_y);

/** World cells the unit could click for `move_preview` (empty if no moves left or no position). */
std::vector<std::pair<int, int>> gather_reachable_move_goal_cells(const GameState& game, const std::shared_ptr<Entity>& actor);

/** True if `actor`'s shape after `quarter_turns_cw` fits at world anchor (ax, ay). */
bool rotation_fits_at_anchor(const GameState& game, const std::shared_ptr<Entity>& actor, int ax, int ay, int quarter_turns_cw,
                             std::vector<std::pair<int, int>>* out_normalized_shape = nullptr);

}  // namespace tactics
