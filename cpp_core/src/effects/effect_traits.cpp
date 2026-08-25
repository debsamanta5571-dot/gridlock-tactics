#include "tactics/effects/effect_traits.hpp"

#include "tactics/effects/effect_registry.hpp"

#include <unordered_set>

namespace tactics {

namespace {

EffectTraits traits_from_definition(const EffectDefinition& def)
{
    // Traits are declared per-effect on the EffectDefinition (see effect_registry.cpp) - this just
    // mirrors them, so a new damaging/directional effect needs no edit here.
    EffectTraits t;
    t.deals_damage = def.deals_damage;
    t.uses_directional_aim = def.uses_directional_aim;
    t.uses_push_direction_aim = def.uses_push_direction_aim;
    t.area_effect = def.target.area_effect;
    t.scales_with_ability_damage = def.scales_with_ability_damage;
    t.movement_landing = def.movement_landing;
    t.targets_empty_cell = def.targets_empty_cell;
    return t;
}

}  // namespace

EffectTraits effect_traits_for_key(const std::string& effect_key)
{
    EffectDefinition def;
    if (try_get_effect_definition(effect_key, def)) {
        return traits_from_definition(def);
    }
    return {};
}

bool effect_key_deals_damage(const std::string& effect_key)
{
    return effect_traits_for_key(effect_key).deals_damage;
}

bool effect_key_uses_directional_aim(const std::string& effect_key)
{
    return effect_traits_for_key(effect_key).uses_directional_aim;
}

bool effect_key_uses_push_direction_aim(const std::string& effect_key)
{
    return effect_traits_for_key(effect_key).uses_push_direction_aim;
}

bool effect_key_scales_with_ability_damage(const std::string& effect_key)
{
    return effect_traits_for_key(effect_key).scales_with_ability_damage;
}

bool effect_key_is_movement_landing(const std::string& effect_key)
{
    return effect_traits_for_key(effect_key).movement_landing;
}

bool effect_key_targets_empty_cell(const std::string& effect_key)
{
    return effect_traits_for_key(effect_key).targets_empty_cell;
}


bool effect_key_uses_lobbed_aoe_center(const std::string& effect_key)
{
    const EffectTraits traits = effect_traits_for_key(effect_key);
    return traits.area_effect && !traits.uses_directional_aim;
}

bool effect_requires_entity_at_target_cell(const std::string& effect_key)
{
    if (effect_key_uses_directional_aim(effect_key) || effect_key_targets_empty_cell(effect_key)
            || effect_key_uses_lobbed_aoe_center(effect_key)) {
        return false;
    }
    const TargetDefinition target = target_definition_for_effect_key(effect_key);
    if (target.domain == TargetDomain::PlayerSeat || target.domain == TargetDomain::StackItem) {
        return false;
    }
    if (target.requirement != TargetRequirement::Required) {
        return false;
    }
    if (target.area_effect) {
        return target.board_target_kind == BoardTargetKind::Enemy;
    }
    static const std::unordered_set<std::string> k_tile_center_keys = {
        "gas_grenade",
        "gas_strike",
        "gas_chain_detonate",
        "shocking_stimulus_aoe",
        "grant_aoe_stat_buff_turn_end",
        "scorching_sphere",
    };
    return k_tile_center_keys.find(effect_key) == k_tile_center_keys.end();
}

bool effect_supports_aoe_blast_preview(const std::string& effect_key)
{
    if (effect_key_uses_directional_aim(effect_key) || effect_key_uses_push_direction_aim(effect_key)
            || effect_key_uses_lobbed_aoe_center(effect_key)) {
        return true;
    }
    if (effect_key_targets_empty_cell(effect_key)) {
        return false;
    }
    return !effect_requires_entity_at_target_cell(effect_key);
}

}  // namespace tactics
