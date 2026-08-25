#pragma once

#include "tactics/common/types.hpp"
#include "tactics/core/stack.hpp"
#include "tactics/entities/entity.hpp"

#include <map>
#include <memory>
#include <optional>
#include <string>

namespace tactics {

class GameState;

struct StackBoardTargetResult {
    ActionResult status{false, "", {}};
    std::shared_ptr<Entity> target{};
};

/** True when the item pins a specific board entity (not just a tile or aim cell). */
bool stack_item_uses_direct_entity_targeting(const StackItem& item);

/** Resolve stack item board coordinates to a live entity with legality checks. */
StackBoardTargetResult resolve_stack_board_target(GameState& game, const StackItem& item, const std::string& effect_label);

/**
 * Re-validate source→target range/LOS using current board positions.
 * Used when queued abilities are revalidated (pre-resolution cancel+refund or mid-batch fizzle).
 */
bool stack_source_target_in_range(const GameState& game, const StackItem& item, const Entity& target);

/** If the target is immune to all effects, returns a fizzle `ActionResult` (ok=true). */
std::optional<ActionResult> stack_fizzle_if_immune(const Entity& target, const std::string& effect_key = {});

DamageType damage_type_from_stack_payload(const std::map<std::string, int>& payload);

}  // namespace tactics