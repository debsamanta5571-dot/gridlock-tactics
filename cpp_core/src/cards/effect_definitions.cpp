#include "tactics/cards/effect_definitions.hpp"

#include "tactics/cards/ability_catalog.hpp"
#include "tactics/cards/card_catalog.hpp"
#include "tactics/cards/effect_definitions_io.hpp"
#include "tactics/cards/passive_catalog.hpp"
#include "tactics/effects/effect_registry.hpp"

namespace tactics {

void apply_ability_override_patch(AbilitySpec& ability, const AbilityOverridePatch& patch)
{
    if (patch.name.has_value()) {
        ability.name = *patch.name;
    }
    if (patch.speed.has_value()) {
        ability.speed = *patch.speed;
    }
    if (patch.energy_cost.has_value()) {
        ability.energy_cost = *patch.energy_cost;
    }
    if (patch.effect_key.has_value()) {
        ability.effect_key = *patch.effect_key;
    }
    for (const auto& [key, value] : patch.effect_payload) {
        ability.effect_payload[key] = value;
    }
    for (const auto& keyword : patch.add_keywords) {
        add_ability_attribute(ability, keyword);
    }
    if (patch.requires_board_target.has_value()) {
        ability.requires_board_target = *patch.requires_board_target;
    }
    if (patch.board_target_kind.has_value()) {
        ability.board_target_kind = *patch.board_target_kind;
    }
    if (patch.consumes_attack_action.has_value()) {
        ability.consumes_attack_action = *patch.consumes_attack_action;
    }
    if (patch.require_target_unit_types.has_value()) {
        ability.require_target_unit_types = *patch.require_target_unit_types;
    }
    if (patch.bonus_damage_unit_types.has_value()) {
        ability.bonus_damage_unit_types = *patch.bonus_damage_unit_types;
    }
    if (patch.bonus_damage_amount.has_value()) {
        ability.bonus_damage_amount = *patch.bonus_damage_amount;
    }
}

bool resolve_activated_abilities_for_card(const std::vector<std::string>& ability_ids,
    const std::map<std::string, AbilityOverridePatch>& overrides, std::vector<AbilitySpec>& out, std::string& err)
{
    out.clear();
    ensure_builtin_ability_catalog_loaded();
    for (const auto& override_entry : overrides) {
        if (std::find(ability_ids.begin(), ability_ids.end(), override_entry.first) == ability_ids.end()) {
            err = "ability_overrides references ability \"" + override_entry.first + "\" not listed in abilities";
            return false;
        }
    }
    for (const auto& id : ability_ids) {
        AbilitySpec ability;
        if (!try_get_ability_from_catalog(id, ability)) {
            err = "unknown ability \"" + id + "\"";
            return false;
        }
        const auto patch_it = overrides.find(id);
        if (patch_it != overrides.end()) {
            apply_ability_override_patch(ability, patch_it->second);
        }
        if (!is_known_effect_key(ability.effect_key)) {
            err = "ability \"" + id + "\" references unknown effect \"" + ability.effect_key + "\"";
            return false;
        }
        out.push_back(std::move(ability));
    }
    return true;
}

bool resolve_passive_abilities_for_unit(const std::vector<std::string>& passive_ids,
    const std::vector<PassiveAbilitySpec>& inline_passives, std::vector<PassiveAbilitySpec>& out, std::string& err)
{
    out.clear();
    ensure_builtin_passive_catalog_loaded();
    for (const auto& id : passive_ids) {
        PassiveAbilitySpec passive;
        if (!try_get_passive_from_catalog(id, passive)) {
            err = "unknown passive \"" + id + "\"";
            return false;
        }
        out.push_back(std::move(passive));
    }
    for (const auto& passive : inline_passives) {
        out.push_back(passive);
    }
    return true;
}

bool finalize_spell_definition(SpellCardDefinition& spell, std::string& err)
{
    if (spell.effect_ref.empty()) {
        if (spell.effect_key.empty()) {
            err = "spell requires effect_key or effect_ref";
            return false;
        }
        if (!is_known_effect_key(spell.effect_key)) {
            err = "spell references unknown effect \"" + spell.effect_key + "\"";
            return false;
        }
        return true;
    }
    ensure_builtin_ability_catalog_loaded();
    AbilitySpec tmpl;
    if (!try_get_ability_from_catalog(spell.effect_ref, tmpl)) {
        err = "spell effect_ref references unknown ability \"" + spell.effect_ref + "\"";
        return false;
    }
    if (spell.effect_key.empty() || spell.effect_key == "generic_effect") {
        spell.effect_key = tmpl.effect_key;
    }
    if (spell.effect_payload.empty()) {
        spell.effect_payload = tmpl.effect_payload;
    }
    if (!spell.explicit_speed) {
        spell.speed = tmpl.speed;
    }
    if (!spell.requires_mandatory_board_cell.has_value() && tmpl.requires_board_target.has_value()) {
        spell.requires_mandatory_board_cell = *tmpl.requires_board_target;
    }
    if (!spell.board_target_kind.has_value() && tmpl.board_target_kind.has_value()) {
        spell.board_target_kind = *tmpl.board_target_kind;
    }
    if (spell.require_target_unit_types.empty()) {
        spell.require_target_unit_types = tmpl.require_target_unit_types;
    }
    if (spell.bonus_damage_unit_types.empty() && spell.bonus_damage_amount == 0) {
        spell.bonus_damage_unit_types = tmpl.bonus_damage_unit_types;
        spell.bonus_damage_amount = tmpl.bonus_damage_amount;
    }
    if (!is_known_effect_key(spell.effect_key)) {
        err = "spell references unknown effect \"" + spell.effect_key + "\"";
        return false;
    }
    return true;
}

}  // namespace tactics
