#pragma once

#include "tactics/common/types.hpp"
#include "tactics/entities/entity.hpp"

#include <string>

namespace tactics {

/** Deployable combat unit (`entity_type == "unit"`). Excludes bases, structures, pickups, obstacles. */
inline bool entity_is_board_unit(const Entity& e) { return e.entity_type == "unit"; }

/** Units and player bases may declare attacks and use onboard activated abilities. */
inline bool entity_may_attack_or_activate_abilities(const Entity& e)
{
    return entity_is_board_unit(e) || entity_is_base(e);
}

/** Player bases cannot host focus spells or receive generic unit buffs. */
inline bool entity_valid_focus_spell_caster(const Entity& e)
{
    return entity_is_board_unit(e) && e.current_health > 0;
}

/**
 * Category gate after `board_target_allows`: units vs structures vs bases.
 * Bases and structures are excluded unless the effect key explicitly allows them
 * (see effect_registry / handler docs). Default ally buffs = units only.
 */
bool board_target_entity_allowed_for_effect(const Entity& target, BoardTargetKind kind, const std::string& effect_key);

}  // namespace tactics