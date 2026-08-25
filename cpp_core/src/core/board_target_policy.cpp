#include "tactics/core/board_target_policy.hpp"

#include "tactics/effects/effect_traits.hpp"

#include <unordered_set>

namespace tactics {
namespace {

bool effect_allows_base_direct_target(const std::string& effect_key)
{
    static const std::unordered_set<std::string> k_allowed{
        "repair_structure_adjacent",
        "bunker_buster_strike",
        "core_cracker_breach",
    };
    return k_allowed.count(effect_key) > 0;
}

bool effect_allows_structure_ally_target(const std::string& effect_key)
{
    static const std::unordered_set<std::string> k_allowed{
        "repair_structure_adjacent",
        "grant_reactive_armor",
        "grant_next_damage_bonus",
        "grant_next_damage_bonus_adjacent",
        "grant_on_damage_apply_overload_adjacent",
        "grant_on_damage_apply_jammed_adjacent",
        "grant_medical_override",
    };
    return k_allowed.count(effect_key) > 0;
}

}  // namespace

bool board_target_entity_allowed_for_effect(const Entity& target, const BoardTargetKind kind, const std::string& effect_key)
{
    if (entity_is_base(target)) {
        return effect_allows_base_direct_target(effect_key);
    }
    if (entity_is_structure(target) || entity_is_breakable_obstacle(target) || target.entity_type == "obstacle") {
        if (kind == BoardTargetKind::Enemy) {
            return true;
        }
        if (kind == BoardTargetKind::Ally || kind == BoardTargetKind::Own || kind == BoardTargetKind::NonSelf
            || kind == BoardTargetKind::Any) {
            return effect_allows_structure_ally_target(effect_key);
        }
        return false;
    }
    if (entity_is_pickup(target)) {
        return kind == BoardTargetKind::Enemy;
    }
    if (!entity_is_board_unit(target)) {
        return false;
    }
    return true;
}

}  // namespace tactics