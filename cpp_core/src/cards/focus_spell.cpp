#include "tactics/cards/focus_spell.hpp"

#include "tactics/cards/card_catalog.hpp"
#include "tactics/cards/card_runtime.hpp"
#include "tactics/core/board_target_policy.hpp"
#include "tactics/core/stack.hpp"
#include "tactics/core/stack_targets.hpp"
#include "tactics/attributes/attributes.hpp"
#include "tactics/combat/combat_resolver.hpp"
#include "tactics/combat/directional_area.hpp"
#include "tactics/combat/soul_steal.hpp"
#include "tactics/combat/taunt.hpp"
#include "tactics/common/effect_keys.hpp"
#include "tactics/core/game_state.hpp"
#include "tactics/effects/effect_registry.hpp"
#include "tactics/effects/effect_traits.hpp"

namespace tactics {
namespace {

// Delegate to the canonical effect-traits table (effect_traits.cpp) rather than re-listing damaging
// effect keys here. This keeps focus-spell soul-steal / pierce / lifesteal interactions automatically
// in sync whenever a new damaging effect is registered as a trait.
bool spell_effect_deals_damage(const std::string& effect_key)
{
    return effect_key_deals_damage(effect_key);
}

const Entity* focus_spell_range_los_actor(const GameState& game, const Entity& caster, std::optional<Unit>& pose_storage)
{
    pose_storage.reset();
    const auto it = game.board.all_entities_map.find(caster.entity_id);
    if (it == game.board.all_entities_map.end()) {
        return &caster;
    }
    const auto unit = std::dynamic_pointer_cast<Unit>(it->second);
    if (!unit) {
        return &caster;
    }
    const std::shared_ptr<Unit> pose = game.unit_at_validation_pose(unit);
    if (!pose || pose.get() == unit.get()) {
        return &caster;
    }
    pose_storage = *pose;
    return &*pose_storage;
}

std::shared_ptr<Entity> entity_at_cell_owned_by(const GameState& game, int wx, int wy, int player_id)
{
    const auto ent = game.board.entity_at(wx, wy);
    if (!ent || !ent->owner || *ent->owner != player_id) {
        return nullptr;
    }
    return ent;
}


bool entity_satisfies_caster_attack_types(const Entity& entity, const std::vector<std::string>& required)
{
    if (required.empty()) {
        return true;
    }
    const auto* unit = dynamic_cast<const Unit*>(&entity);
    if (!unit) {
        return false;
    }
    for (const std::string& tag : required) {
        if (tag == "melee" && unit->attack_type == AttackType::Melee) {
            return true;
        }
        if (tag == "ranged" && unit->attack_type == AttackType::Ranged) {
            return true;
        }
        if (tag == "hybrid" && unit->attack_type == AttackType::Hybrid) {
            return true;
        }
    }
    return false;
}


bool prefer_ranged_attack_against_cell(const Unit& unit, std::pair<int, int> target_cell)
{
    const int dist = min_chebyshev_entity_to_cell(unit, target_cell.first, target_cell.second);
    const AttackProfile ranged = attack_profile_for_unit(unit, true);
    if (ranged.use_ranged && dist >= ranged.range_min && dist <= ranged.range_max) {
        return true;
    }
    return false;
}

int effective_caster_ranged_range_max(const Unit& unit)
{
    const PassiveStatGrant temp = temporary_stat_grants_for_entity(unit);
    return unit.ranged_range + temp.bonus_ranged_range;
}

std::optional<int> passive_forced_damage_spell_focus_range(const Entity& entity)
{
    if (entity_is_silenced(entity) || entity.current_health <= 0) {
        return std::nullopt;
    }
    for (const PassiveAbilitySpec& passive : entity.passive_abilities) {
        if (entity_passive_is_suppressed(entity, passive.key)) {
            continue;
        }
        if (passive.forces_damage_spell_focus_casting && passive.forced_damage_spell_focus_range > 0) {
            return passive.forced_damage_spell_focus_range;
        }
    }
    return std::nullopt;
}

}  // namespace

std::shared_ptr<Entity> resolve_focus_caster_from_targets(
    const GameState& game, int player_id, const std::map<std::string, int>& targets, const std::shared_ptr<Entity>& explicit_caster)
{
    if (explicit_caster) {
        if (!entity_valid_focus_spell_caster(*explicit_caster)) {
            return nullptr;
        }
        return explicit_caster;
    }
    const auto x_it = targets.find(effect_keys::kFocusCasterX);
    const auto y_it = targets.find(effect_keys::kFocusCasterY);
    if (x_it == targets.end() || y_it == targets.end()) {
        return nullptr;
    }
    const auto caster = entity_at_cell_owned_by(game, x_it->second, y_it->second, player_id);
    if (!caster || !entity_valid_focus_spell_caster(*caster)) {
        return nullptr;
    }
    return caster;
}

bool spell_subject_to_forced_damage_spell_focus(const CardDefinition& def)
{
    if (!definition_is_spell(def) || spell_is_focus(def)) {
        return false;
    }
    const SpellCardDefinition& spell = definition_spell(def);
    return spell_effect_deals_damage(spell.effect_key);
}

bool spell_requires_forced_damage_spell_focus_caster(const GameState& game, const int player_id, const CardDefinition& def)
{
    return spell_subject_to_forced_damage_spell_focus(def)
        && player_has_forced_damage_spell_focus_caster(game, player_id);
}

bool cast_uses_forced_damage_spell_focus_caster(const GameState& game, const CardDefinition& def, const std::shared_ptr<Entity>& caster)
{
    if (!caster || !spell_subject_to_forced_damage_spell_focus(def)) {
        return false;
    }
    if (!entity_valid_focus_spell_caster(*caster)) {
        return false;
    }
    if (!game.board.all_entities_map.contains(caster->entity_id)) {
        return false;
    }
    return passive_forced_damage_spell_focus_range(*caster).has_value();
}

bool player_has_forced_damage_spell_focus_caster(const GameState& game, const int player_id)
{
    bool found = false;
    game.board.for_each_entity([&](const std::shared_ptr<Entity>& ent) {
        if (found || !ent || !ent->owner || *ent->owner != player_id || !entity_valid_focus_spell_caster(*ent)) {
            return;
        }
        if (passive_forced_damage_spell_focus_range(*ent)) {
            found = true;
        }
    });
    return found;
}

std::optional<int> entity_forced_damage_spell_focus_range(const Entity& entity)
{
    return passive_forced_damage_spell_focus_range(entity);
}

ActionResult validate_focus_spell_cast(GameState& game, int player_id, const CardDefinition& def, const SpellCardDefinition& spell,
    const Entity& caster, const std::map<std::string, int>& targets, const std::string& stack_target_id,
    const std::optional<int> focus_range_override)
{
    if (!entity_owned_by(caster, player_id)) {
        return {false, "Focus caster must be your unit", {}};
    }
    if (!entity_valid_focus_spell_caster(caster)) {
        return {false, "Player bases cannot cast focus spells", {}};
    }
    if (!caster.position && caster.occupied_positions.empty()) {
        return {false, "Focus caster is not on the board", {}};
    }
    if (entity_is_stunned(caster)) {
        return {false, "Stunned units cannot cast focus spells", {}};
    }
    if (entity_is_jammed(caster)) {
        return {false, "Jammed units cannot cast focus spells", {}};
    }
    if (!entity_satisfies_caster_attack_types(caster, spell.require_caster_attack_types)) {
        return {false, "This focus spell requires a ranged or hybrid caster unit", {}};
    }

    if (effect_uses_directional_aim(spell.effect_key)) {
        int max_range = 4;
        if (const auto it = spell.effect_payload.find("max_range"); it != spell.effect_payload.end()) {
            max_range = it->second;
        }
        const bool allow_diagonals = [&]() {
            const auto it = spell.effect_payload.find("cardinal_only");
            return !(it != spell.effect_payload.end() && it->second != 0);
        }();
        std::optional<Unit> validation_pose;
        const Entity* range_los_actor = focus_spell_range_los_actor(game, caster, validation_pose);
        const auto aim_vr =
            validate_directional_area_damage_ability_target(game, player_id, *range_los_actor, targets, max_range, allow_diagonals);
        if (!aim_vr.ok) {
            return aim_vr;
        }
        return {true, "Can cast " + definition_name(def), {}};
    }

    TargetDefinition target_def = target_definition_for_effect_key(spell.effect_key);
    target_def.board_target_kind = definition_spell_board_target_kind(def);
    target_def.require_target_unit_types = spell.require_target_unit_types;
    if (definition_spell_requires_mandatory_board_cell(def)) {
        target_def.domain = TargetDomain::BoardEntityCell;
        target_def.requirement = TargetRequirement::Required;
    }
    const auto target_result =
        validate_targets_against_definition(game, player_id, target_def, targets, stack_target_id, spell.effect_key);
    if (!target_result.ok) {
        return target_result;
    }
    // The caster is not a valid target of its own focus spell unless `may_target_self` is set
    // (the `{RANGE_SELF}`/`{ADJACENT_SELF}` tag). Entity-target spells only; AoE/cell spells are exempt.
    {
        const auto mts = spell.effect_payload.find("may_target_self");
        const bool may_target_self = mts != spell.effect_payload.end() && mts->second != 0;
        if (!may_target_self && !definition_spell_requires_mandatory_board_cell(def)) {
            const auto x_it = targets.find(effect_keys::kCellX);
            const auto y_it = targets.find(effect_keys::kCellY);
            if (x_it != targets.end() && y_it != targets.end()) {
                const auto target_ent = game.board.entity_at(x_it->second, y_it->second);
                if (target_ent && target_ent->entity_id == caster.entity_id) {
                    return {false, "Can't target caster", {}};
                }
            }
        }
    }
    // Stunned units cannot be targeted by focus spells (entity-targeted only; cell/area spells are exempt).
    if (!definition_spell_requires_mandatory_board_cell(def)) {
        const std::shared_ptr<Entity>* stun_target_ptr = nullptr;
        std::shared_ptr<Entity> stun_target_cell;
        if (!stack_target_id.empty()) {
            const auto it = game.board.all_entities_map.find(stack_target_id);
            if (it != game.board.all_entities_map.end()) stun_target_ptr = &it->second;
        } else {
            const auto x_it = targets.find(effect_keys::kCellX);
            const auto y_it = targets.find(effect_keys::kCellY);
            if (x_it != targets.end() && y_it != targets.end()) {
                stun_target_cell = game.board.entity_at(x_it->second, y_it->second);
                if (stun_target_cell) stun_target_ptr = &stun_target_cell;
            }
        }
        if (stun_target_ptr && *stun_target_ptr && entity_is_stunned(**stun_target_ptr)) {
            return {false, "Cannot target a stunned unit with a focus spell", {}};
        }
    }
    if (definition_spell_requires_mandatory_board_cell(def)) {
        const auto x_it = targets.find(effect_keys::kCellX);
        const auto y_it = targets.find(effect_keys::kCellY);
        if (x_it != targets.end() && y_it != targets.end()) {
            const auto target_ent = game.board.entity_at(x_it->second, y_it->second);
            if (target_ent && !taunt_allows_board_target(game, &caster, player_id, *target_ent)) {
                return {false, "Must target adjacent taunt", {}};
            }
        }
    }

    if (!definition_spell_requires_mandatory_board_cell(def)) {
        return {true, "Can cast " + definition_name(def), {}};
    }

    const auto x_it = targets.find(effect_keys::kCellX);
    const auto y_it = targets.find(effect_keys::kCellY);
    if (x_it == targets.end() || y_it == targets.end()) {
        return {false, "Focus spell requires a board target", {}};
    }
    const std::pair<int, int> target_cell{x_it->second, y_it->second};
    if (spell.use_caster_attack_range) {
        const auto caster_it = game.board.all_entities_map.find(caster.entity_id);
        if (caster_it == game.board.all_entities_map.end() || !caster_it->second) {
            return {false, "Focus attack spell caster is not on the board", {}};
        }
        auto caster_unit = std::dynamic_pointer_cast<Unit>(caster_it->second);
        if (!caster_unit) {
            return {false, "Focus attack spells require a unit caster", {}};
        }
        const int saved_attacks = caster_unit->attacks_remaining_this_turn;
        if (saved_attacks <= 0) {
            caster_unit->attacks_remaining_this_turn = 1;
        }
        const bool prefer_ranged = prefer_ranged_attack_against_cell(*caster_unit, target_cell);
        const auto attack_vr = validate_attack(game, caster_unit, player_id, target_cell, prefer_ranged);
        caster_unit->attacks_remaining_this_turn = saved_attacks;
        if (!attack_vr.ok) {
            return attack_vr;
        }
        const bool needs_soul_steal =
            focus_spell_grants_soul_steal(def, spell, caster) && spell_effect_deals_damage(spell.effect_key);
        auto soul_vr = validate_soul_steal_heal_base_target(game, player_id, targets, needs_soul_steal);
        if (!soul_vr.ok) {
            return soul_vr;
        }
        return {true, "Can cast " + definition_name(def), {}};
    }

    std::optional<Unit> validation_pose;
    const Entity* range_los_actor = focus_spell_range_los_actor(game, caster, validation_pose);
    int max_range = focus_range_override.value_or(spell_focus_range(spell));
    if (spell.use_caster_ranged_range) {
        const auto* caster_unit = dynamic_cast<const Unit*>(range_los_actor);
        if (!caster_unit || (caster_unit->attack_type != AttackType::Ranged
                && caster_unit->attack_type != AttackType::Hybrid)) {
            return {false, "This focus spell requires a ranged or hybrid caster unit", {}};
        }
        max_range = effective_caster_ranged_range_max(*caster_unit);
    }
    if (max_range > 0) {
        const int dist = min_chebyshev_entity_to_cell(*range_los_actor, target_cell.first, target_cell.second);
        if (dist > max_range) {
            return {false, "Focus spell target is out of range", {}};
        }
    }
    if (!entity_has_line_of_sight_to_cell(game, *range_los_actor, target_cell)) {
        return {false, "Focus spell target is not in line of sight", {}};
    }

    const bool needs_soul_steal = focus_spell_grants_soul_steal(def, spell, caster) && spell_effect_deals_damage(spell.effect_key);
    auto soul_vr = validate_soul_steal_heal_base_target(game, player_id, targets, needs_soul_steal);
    if (!soul_vr.ok) {
        return soul_vr;
    }
    return {true, "Can cast " + definition_name(def), {}};
}

bool focus_spell_pierces_damage_prevention(const CardDefinition& def, const SpellCardDefinition& spell, const Entity& caster)
{
    (void)def;
    return spell_effect_deals_damage(spell.effect_key) && has_pierce(caster);
}

bool focus_spell_grants_lifesteal(const CardDefinition& def, const SpellCardDefinition& spell, const Entity& caster)
{
    (void)def;
    return spell_effect_deals_damage(spell.effect_key) && has_lifesteal(caster);
}

bool focus_spell_grants_soul_steal(const CardDefinition& def, const SpellCardDefinition& spell, const Entity& caster)
{
    (void)def;
    return spell_effect_deals_damage(spell.effect_key) && has_soul_steal(caster);
}

bool queued_focus_spell_still_valid(GameState& game, const StackItem& item)
{
    if (item.source_entity_id.empty()) {
        return false;
    }
    const auto src_it = game.board.all_entities_map.find(item.source_entity_id);
    if (src_it == game.board.all_entities_map.end() || !src_it->second) {
        return false;
    }
    const Entity& caster = *src_it->second;

    CardDefinition def;
    SpellCardDefinition spell;
    bool found_def = false;
    for (const std::string& key : list_card_catalog_keys_sorted()) {
        if (!try_get_card_definition(key, def) || !def.spell) {
            continue;
        }
        if (definition_name(def) != item.source_name || def.spell->effect_key != item.effect_key) {
            continue;
        }
        spell = *def.spell;
        found_def = true;
        break;
    }
    if (!found_def) {
        spell.effect_key = item.effect_key;
        spell.effect_payload = item.payload;
        spell.effect_string_payload = item.string_payload;
        spell.require_target_unit_types = item.require_target_unit_types;
        if (const auto it = item.payload.find("focus_range"); it != item.payload.end()) {
            spell.focus_range = it->second;
        }
        spell.use_caster_ranged_range = item.payload.contains("use_caster_ranged_range");
        spell.use_caster_attack_range = item.payload.contains("use_caster_attack_range");
        def.name = item.source_name;
        def.spell = spell;
    }

    const std::optional<int> forced_range = spell_is_focus(def)
        ? std::nullopt
        : entity_forced_damage_spell_focus_range(caster);
    if (!spell_is_focus(def) && !forced_range) {
        return false;
    }

    const auto vr = validate_focus_spell_cast(game, item.controller_id, def, spell, caster, item.targets, {},
        forced_range);
    return vr.ok;
}

}  // namespace tactics