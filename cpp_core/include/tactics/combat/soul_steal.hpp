#pragma once

#include "tactics/common/types.hpp"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace tactics {

class GameState;
struct AbilitySpec;
struct Card;
struct Entity;

/** Living bases owned by seats on the same team as `controller_id`. */
std::vector<std::shared_ptr<Entity>> allied_bases_for_controller(const GameState& game, int controller_id);

/** Validates optional `heal_base_x` / `heal_base_y` when multiple allied bases exist. */
ActionResult validate_soul_steal_heal_base_target(const GameState& game, int controller_id, const std::map<std::string, int>& targets,
                                                  bool needs_soul_steal);

/** Picks the allied base to heal (auto when only one; otherwise from `heal_base_*` targets). */
std::shared_ptr<Entity> resolve_soul_steal_heal_base(const GameState& game, int controller_id, const std::map<std::string, int>& targets);

}  // namespace tactics
