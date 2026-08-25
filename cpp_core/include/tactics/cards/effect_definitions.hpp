#pragma once

#include "tactics/cards/cards.hpp"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace tactics {

struct SpellCardDefinition;

/** Per-card tweaks applied after loading an ability from the catalog. */
struct AbilityOverridePatch {
    std::optional<std::string> name;
    std::optional<EffectSpeed> speed;
    std::optional<std::map<EnergyType, int>> energy_cost;
    std::optional<std::string> effect_key;
    /** When set, replaces matching keys in the catalog ability's effect_payload. */
    std::map<std::string, int> effect_payload{};
    std::vector<std::string> add_keywords{};
    std::optional<bool> requires_board_target{};
    std::optional<BoardTargetKind> board_target_kind{};
    std::optional<bool> consumes_attack_action{};
    std::optional<std::vector<std::string>> require_target_unit_types{};
    std::optional<std::vector<std::string>> bonus_damage_unit_types{};
    std::optional<int> bonus_damage_amount{};
};

void apply_ability_override_patch(AbilitySpec& ability, const AbilityOverridePatch& patch);

/**
 * Resolves activated abilities from catalog ids + optional per-card overrides.
 * @return false if an id is unknown or an override targets an id not listed in ability_ids.
 */
bool resolve_activated_abilities_for_card(const std::vector<std::string>& ability_ids,
    const std::map<std::string, AbilityOverridePatch>& overrides, std::vector<AbilitySpec>& out, std::string& err);

/**
 * Merges passive catalog ids (in order) then inline passives from the unit definition.
 * @return false if a passive catalog id is unknown.
 */
bool resolve_passive_abilities_for_unit(const std::vector<std::string>& passive_ids,
    const std::vector<PassiveAbilitySpec>& inline_passives, std::vector<PassiveAbilitySpec>& out, std::string& err);

/**
 * When spell.effect_ref is set, copies effect template fields from the ability catalog.
 * Spell.speed and spell-level targeting fields still override catalog defaults when present in JSON.
 */
bool finalize_spell_definition(SpellCardDefinition& spell, std::string& err);

}  // namespace tactics
