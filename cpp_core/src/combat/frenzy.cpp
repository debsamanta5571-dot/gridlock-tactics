#include "tactics/combat/frenzy.hpp"

#include "tactics/attributes/attributes.hpp"
#include "tactics/core/game_state.hpp"

namespace tactics {
namespace {

bool victim_qualifies_for_frenzy_kill(const GameState& game, const Entity& victim, const Entity& killer)
{
    if (!victim.owner || !killer.owner) {
        return false;
    }
    if (!teams_hostile(game, *killer.owner, *victim.owner)) {
        return false;
    }
    if (entity_is_base(victim) || entity_is_building(victim) || entity_is_breakable_obstacle(victim) || victim.entity_type == "obstacle") {
        return false;
    }
    return true;
}

}  // namespace

void reset_unit_move_and_attack_budget(Unit& unit)
{
    unit.has_moved_this_turn = false;
    if (entity_has_deployment_fatigue(unit)) {
        const DeploymentFatigueRestrictions r = deployment_fatigue_restrictions(unit);
        if (entity_can_move(unit)) {
            unit.moves_remaining_this_turn = r.blocks_move ? unit.bonus_moves : 1 + unit.bonus_moves;
        } else {
            unit.moves_remaining_this_turn = 0;
        }
        unit.attacks_remaining_this_turn = r.blocks_attack ? 0 : 1 + unit.bonus_attacks;
    } else {
        unit.moves_remaining_this_turn = entity_can_move(unit) ? 1 + unit.bonus_moves : 0;
        unit.attacks_remaining_this_turn = 1 + unit.bonus_attacks;
    }
    unit.has_attacked_this_turn = false;
    refresh_standard_moves_remaining(unit);
}

void try_trigger_frenzy_on_unit_kill(GameState& game, const std::shared_ptr<Entity>& killer, const Entity& victim)
{
    if (!killer || !killer->owner) {
        return;
    }
    if (!game.board.all_entities_map.contains(killer->entity_id)) {
        return;
    }
    auto killer_unit = std::dynamic_pointer_cast<Unit>(killer);
    if (!killer_unit || !has_frenzy(*killer_unit) || killer_unit->frenzy_triggered_this_turn) {
        return;
    }
    if (!victim_qualifies_for_frenzy_kill(game, victim, *killer_unit)) {
        return;
    }
    killer_unit->frenzy_triggered_this_turn = true;
    reset_unit_move_and_attack_budget(*killer_unit);
}

void try_trigger_deferred_frenzy_after_attack_kill(GameState& game, const std::shared_ptr<Entity>& killer, const Entity& victim)
{
    try_trigger_frenzy_on_unit_kill(game, killer, victim);
}

}  // namespace tactics
