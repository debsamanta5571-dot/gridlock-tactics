#pragma once

#include "tactics/entities/entity.hpp"

#include <memory>

namespace tactics {

class GameState;

inline bool has_frenzy(const Entity& e) { return entity_has_attribute(e, "frenzy"); }

/** Restore default move/attack budgets (same as turn start). */
void reset_unit_move_and_attack_budget(Unit& unit);

/** When `killer` dealt lethal damage to a hostile board unit, Frenzy may refresh its actions (once per turn). */
void try_trigger_frenzy_on_unit_kill(GameState& game, const std::shared_ptr<Entity>& killer, const Entity& victim);

/** Apply deferred Frenzy after a killing basic attack consumed its attack budget. */
void try_trigger_deferred_frenzy_after_attack_kill(GameState& game, const std::shared_ptr<Entity>& killer, const Entity& victim);

}  // namespace tactics
