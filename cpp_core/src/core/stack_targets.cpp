#include "tactics/core/stack_targets.hpp"

#include "tactics/core/board_target_policy.hpp"
#include "tactics/cards/unit_types.hpp"
#include "tactics/common/effect_keys.hpp"
#include "tactics/combat/combat_resolver.hpp"
#include "tactics/core/game_state.hpp"
#include "tactics/combat/taunt.hpp"
#include "tactics/effects/effect_registry.hpp"
#include "tactics/effects/effect_traits.hpp"
#include "tactics/entities/entity.hpp"

namespace tactics {

namespace {

bool stack_item_uses_direct_entity_targeting_impl(const StackItem& item)
{
    if (item.target_entity_id.empty()) {
        return false;
    }
    if (!effect_requires_board_target(item.effect_key)) {
        return false;
    }
    if (effect_key_targets_empty_cell(item.effect_key)) {
        return false;
    }
    if (effect_key_uses_directional_aim(item.effect_key)) {
        return false;
    }
    if (!effect_requires_entity_at_target_cell(item.effect_key)) {
        return false;
    }
    return true;
}

}  // namespace

bool stack_item_uses_direct_entity_targeting(const StackItem& item)
{
    return stack_item_uses_direct_entity_targeting_impl(item);
}

namespace {

bool batch_effect_default_range_is_one(const std::string& effect_key)
{
    return effect_key == "repair_structure_adjacent"
        || effect_key == "grant_next_damage_bonus_adjacent"
        || effect_key == "grant_on_damage_apply_overload_adjacent"
        || effect_key == "grant_on_damage_apply_jammed_adjacent"
        || effect_key == "grant_medical_override";
}

std::optional<std::pair<int, int>> stack_primary_entity_cell(const Entity& entity)
{
    if (!entity.occupied_positions.empty()) {
        return entity.occupied_positions.front();
    }
    if (entity.position) {
        return *entity.position;
    }
    return std::nullopt;
}

std::optional<AbilitySpec> stack_ability_spec_on_entity(const Entity& entity, const std::string& key)
{
    for (const AbilitySpec& ability : entity.activated_abilities) {
        if (ability.key == key) {
            return ability;
        }
    }
    return std::nullopt;
}

bool stack_entities_within_chebyshev_range(const Entity& source, const Entity& target, int max_range)
{
    if (max_range < 0) {
        return false;
    }
    if (source.entity_id == target.entity_id) {
        return true;
    }
    if (!target.occupied_positions.empty()) {
        for (const auto& [tx, ty] : target.occupied_positions) {
            if (min_chebyshev_entity_to_cell(source, tx, ty) <= max_range) {
                return true;
            }
        }
        return false;
    }
    if (target.position) {
        return min_chebyshev_entity_to_cell(source, target.position->first, target.position->second) <= max_range;
    }
    return false;
}

std::vector<std::pair<int, int>> stack_entity_cells(const Entity& entity)
{
    if (!entity.occupied_positions.empty()) {
        return entity.occupied_positions;
    }
    std::vector<std::pair<int, int>> out;
    if (!entity.position) {
        return out;
    }
    const auto [ax, ay] = *entity.position;
    for (const auto& [dx, dy] : entity_shape_offsets(entity)) {
        out.push_back({ax + dx, ay + dy});
    }
    return out;
}

bool stack_effect_target_type_still_valid(const Entity& target, const std::string& effect_key)
{
    if (effect_key == "repair_structure_adjacent") {
        return entity_is_quick_repairs_target(target);
    }
    const BoardTargetKind kind = effect_board_target_kind(effect_key);
    return board_target_entity_allowed_for_effect(target, kind, effect_key);
}

std::optional<int> stack_chebyshev_max_range_for_item(const StackItem& item, const AbilitySpec* spec)
{
    if (spec) {
        if (ability_uses_ranged_targeting(*spec)) {
            return std::nullopt;
        }
        const auto cardinal = spec->effect_payload.find("cardinal_only");
        if (cardinal != spec->effect_payload.end() && cardinal->second != 0) {
            return std::nullopt;
        }
        const auto range_it = spec->effect_payload.find("max_range");
        if (range_it != spec->effect_payload.end()) {
            return range_it->second;
        }
    }
    const auto payload_range = item.payload.find("max_range");
    if (payload_range != item.payload.end()) {
        return payload_range->second;
    }
    if (item.source_type == "focus_spell") {
        const auto focus_range = item.payload.find("focus_range");
        if (focus_range != item.payload.end()) {
            return focus_range->second;
        }
    }
    if (batch_effect_default_range_is_one(item.effect_key)) {
        return 1;
    }
    return std::nullopt;
}

bool stack_cardinal_range_effect_in_range(const Entity& source, const Entity& target, const StackItem& item)
{
    const auto cit = item.payload.find("cardinal_only");
    if (cit == item.payload.end() || cit->second == 0) {
        return true;
    }
    const int max_range = [&]() {
        const auto rit = item.payload.find("max_range");
        return rit != item.payload.end() ? rit->second : 1;
    }();
    for (const auto& [ax, ay] : stack_entity_cells(source)) {
        for (const auto& [tx, ty] : stack_entity_cells(target)) {
            const int dx = tx - ax;
            const int dy = ty - ay;
            if ((dx == 0 && std::abs(dy) <= max_range) || (dy == 0 && std::abs(dx) <= max_range)) {
                return true;
            }
        }
    }
    return false;
}

std::optional<ActionResult> stack_fizzle_if_source_target_out_of_range(
    const GameState& game, const StackItem& item, const Entity& target, const std::string& effect_label)
{
    if (!stack_item_uses_direct_entity_targeting_impl(item)) {
        return std::nullopt;
    }
    if (!stack_source_target_in_range(game, item, target)) {
        return ActionResult{true, effect_label + " target is out of range (fizzled)", {}};
    }
    return std::nullopt;
}

}  // namespace

bool stack_source_target_in_range(const GameState& game, const StackItem& item, const Entity& target)
{
    if (!stack_item_uses_direct_entity_targeting_impl(item)) {
        return true;
    }
    if (item.source_entity_id.empty()) {
        return true;
    }
    const auto src_it = game.board.all_entities_map.find(item.source_entity_id);
    if (src_it == game.board.all_entities_map.end() || !src_it->second) {
        return false;
    }
    const Entity& source = *src_it->second;

    if (!stack_effect_target_type_still_valid(target, item.effect_key)) {
        return false;
    }

    std::optional<AbilitySpec> spec;
    if (item.source_type == "ability" && !item.source_ability_key.empty()) {
        spec = stack_ability_spec_on_entity(source, item.source_ability_key);
    }

    if (spec && ability_uses_ranged_targeting(*spec)) {
        const auto cell = stack_primary_entity_cell(target);
        if (!cell) {
            return false;
        }
        return validate_ranged_damage_ability_target(game, source, *spec, *cell).ok;
    }

    if (!stack_cardinal_range_effect_in_range(source, target, item)) {
        return false;
    }

    if (const auto max_range = stack_chebyshev_max_range_for_item(item, spec ? &*spec : nullptr)) {
        return stack_entities_within_chebyshev_range(source, target, *max_range);
    }
    return true;
}

DamageType damage_type_from_stack_payload(const std::map<std::string, int>& payload)
{
    const auto it = payload.find(effect_keys::kDamageType);
    if (it == payload.end()) {
        return DamageType::Physical;
    }
    switch (it->second) {
    case 1:
        return DamageType::Magic;
    case 2:
        return DamageType::Pure;
    default:
        return DamageType::Physical;
    }
}

std::optional<ActionResult> stack_fizzle_if_immune(const Entity& target, const std::string& effect_key)
{
    if (entity_is_base(target)
            && (effect_key == "repair_structure_adjacent" || effect_key == "bunker_buster_strike"
                || effect_key == "core_cracker_breach")) {
        return std::nullopt;
    }
    if (!entity_immune_to_all_effects(target)) {
        return std::nullopt;
    }
    if (entity_is_pickup(target)
            && (effect_key_deals_damage(effect_key) || effect_key == "apply_fire")) {
        return std::nullopt;
    }
    return ActionResult{false, target.entity_id + " is immune to effects (fizzled)", {}};
}

StackBoardTargetResult resolve_stack_board_target(GameState& game, const StackItem& item, const std::string& effect_label)
{
    StackBoardTargetResult out;
    const auto xit = item.targets.find(effect_keys::kCellX);
    const auto yit = item.targets.find(effect_keys::kCellY);
    if (xit == item.targets.end() || yit == item.targets.end()) {
        out.status = {false, effect_label + " requires target position", {}};
        return out;
    }
    out.target = game.board.entity_at(xit->second, yit->second);
    if ((!out.target
            || (!item.target_entity_id.empty() && out.target->entity_id != item.target_entity_id))
            && stack_item_uses_direct_entity_targeting_impl(item)) {
        const auto tracked = game.board.all_entities_map.find(item.target_entity_id);
        if (tracked != game.board.all_entities_map.end() && tracked->second) {
            out.target = tracked->second;
        }
    }
    if (!out.target) {
        if (!effect_requires_entity_at_target_cell(item.effect_key)) {
            if (!game.board.get_square(xit->second, yit->second)) {
                out.status = {false, "Target cell is not on the board (fizzled)", {}};
                return out;
            }
            if (!item.source_entity_id.empty()) {
                const auto src_it = game.board.all_entities_map.find(item.source_entity_id);
                if (src_it == game.board.all_entities_map.end() || !src_it->second) {
                    out.status = {false, "Source gone (fizzled)", {}};
                    return out;
                }
                const Entity& source = *src_it->second;
                if (const auto max_range = stack_chebyshev_max_range_for_item(item, nullptr)) {
                    if (min_chebyshev_entity_to_cell(source, xit->second, yit->second) > *max_range) {
                        out.status = {true, effect_label + " target is out of range (fizzled)", {}};
                        return out;
                    }
                }
                if (effect_key_uses_lobbed_aoe_center(item.effect_key)
                        && !entity_has_line_of_sight_to_cell(game, source, {xit->second, yit->second})) {
                    out.status = {true, effect_label + " target is not in line of sight (fizzled)", {}};
                    return out;
                }
            }
            out.status = {true, "", {}};
            return out;
        }
        out.status = {false, "Target no longer exists (fizzled)", {}};
        return out;
    }
    if (!item.target_entity_id.empty() && out.target->entity_id != item.target_entity_id) {
        out.status = {false, "Original target is no longer at that position (fizzled)", {}};
        return out;
    }
    if (!board_target_allows(game, item.board_target_kind, item.controller_id, *out.target)) {
        out.status = {false, "Original target is no longer legal (fizzled)", {}};
        return out;
    }
    if (!board_target_entity_allowed_for_effect(*out.target, item.board_target_kind, item.effect_key)) {
        out.status = {false, out.target->entity_id + " is not a valid target for this effect (fizzled)", {}};
        return out;
    }
    if (!entity_satisfies_unit_type_filter(*out.target, item.require_target_unit_types)) {
        out.status = {false, out.target->entity_id + " does not have a required unit type (fizzled)", {}};
        return out;
    }
    const TargetDefinition target_def = target_definition_for_effect_key(item.effect_key);
    if (!target_def.area_effect && enemy_direct_target_blocked_by_stealth(game, item.controller_id, *out.target)) {
        out.status = {false, out.target->entity_id + " is stealthed (fizzled)", {}};
        return out;
    }
    std::shared_ptr<Entity> acting_unit;
    if (!item.source_entity_id.empty()) {
        const auto src_it = game.board.all_entities_map.find(item.source_entity_id);
        if (src_it != game.board.all_entities_map.end()) {
            acting_unit = src_it->second;
        }
    }
    if (!taunt_allows_board_target(game, acting_unit.get(), item.controller_id, *out.target)) {
        out.status = {false, out.target->entity_id + " is protected by taunt (fizzled)", {}};
        return out;
    }
    if (auto fizzle = stack_fizzle_if_immune(*out.target, item.effect_key)) {
        out.status = *fizzle;
        return out;
    }
    if (auto range_fizzle = stack_fizzle_if_source_target_out_of_range(game, item, *out.target, effect_label)) {
        out.status = *range_fizzle;
        return out;
    }
    out.status = {true, "", {}};
    return out;
}

}  // namespace tactics