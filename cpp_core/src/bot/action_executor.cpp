#include "tactics/bot/action_executor.hpp"

#include "tactics/actions/actions.hpp"
#include "tactics/cards/ability_catalog.hpp"
#include "tactics/common/effect_keys.hpp"
#include "tactics/combat/directional_area.hpp"
#include "tactics/effects/effect_traits.hpp"
#include "tactics/entities/entity.hpp"

#include <algorithm>

namespace tactics::bot {
namespace {

bool ability_effect_forbids_board_targets(const std::string& effect_key)
{
    return effect_key == "grant_permanent_stat_growth_self" || effect_key == "grant_next_damage_bonus_self"
        || effect_key == "grant_first_strike_self" || effect_key == "apply_overload_self";
}

bool ability_action_needs_board_targets(const AbilitySpec& spec)
{
    if (ability_effect_forbids_board_targets(spec.effect_key)) {
        return false;
    }
    return ability_requires_board_target(spec) || effect_uses_directional_aim(spec.effect_key)
        || effect_key_targets_empty_cell(spec.effect_key);
}

std::map<std::string, int> resolve_ability_targets_for_bot_action(const BotAction& action)
{
    if (!action.spell_targets.empty()) {
        return action.spell_targets;
    }
    AbilitySpec spec;
    if (!try_get_ability_from_catalog(action.ability_key, spec)) {
        if (action.x != 0 || action.y != 0) {
            return {{effect_keys::kCellX, action.x}, {effect_keys::kCellY, action.y}};
        }
        return {};
    }
    if (!ability_action_needs_board_targets(spec)) {
        return {};
    }
    return {{effect_keys::kCellX, action.x}, {effect_keys::kCellY, action.y}};
}

std::shared_ptr<Entity> resolve_entity(const GameState& game, const std::string& entity_id)
{
    if (entity_id.empty()) {
        return nullptr;
    }
    const auto it = game.board.all_entities_map.find(entity_id);
    if (it == game.board.all_entities_map.end()) {
        return nullptr;
    }
    return it->second;
}

}  // namespace

BotActionResult execute_bot_action(GameState& game, BotSession& session, const BotAction& action)
{
    session.controlled_player = action.player_id;

    auto wrap = [](const ActionResult& r) -> BotActionResult {
        return {r.ok, r.message};
    };

    switch (action.kind) {
    case BotActionKind::ChooseEnergyZone: {
        const int idx = action.energy_zone_index_1based - 1;
        return wrap(game.choose_energy_zone(action.player_id, idx));
    }
    case BotActionKind::SkipEnergyZone:
        return wrap(game.skip_energy_zone(action.player_id));
    case BotActionKind::Deploy: {
        DeployAction deploy(action.card_id, action.player_id, {action.x, action.y}, CardPlayZone::Hand);
        return wrap(game.perform_action(action.player_id, deploy));
    }
    case BotActionKind::DeployReserve: {
        DeployAction deploy(action.card_id, action.player_id, {action.x, action.y}, CardPlayZone::Reserves);
        return wrap(game.perform_action(action.player_id, deploy));
    }
    case BotActionKind::MovePreview: {
        const auto it = game.board.all_entities_map.find(action.entity_id);
        if (it == game.board.all_entities_map.end()) {
            return {false, "Unit not found for move preview"};
        }
        auto unit = std::dynamic_pointer_cast<Unit>(it->second);
        if (!unit) {
            return {false, "Entity is not a unit"};
        }
        MovePreviewAction move(unit, action.player_id, {action.x, action.y});
        const ActionResult preview = game.perform_action(action.player_id, move);
        if (!preview.ok) {
            return wrap(preview);
        }
        // Bots commit the move in one step. The preview/confirm split is human
        // error-recovery UI; leaving the pending preview open as a separate bot decision
        // caused a preview↔cancel search livelock (the scorer liked the destination, the
        // leaf evaluator preferred cancelling, forever).
        MoveConfirmAction confirm(action.player_id);
        const ActionResult confirmed = game.perform_action(action.player_id, confirm);
        if (!confirmed.ok) {
            MoveCancelAction cancel(action.player_id);
            game.perform_action(action.player_id, cancel);
        }
        return wrap(confirmed);
    }
    case BotActionKind::MoveConfirm: {
        MoveConfirmAction move(action.player_id);
        return wrap(game.perform_action(action.player_id, move));
    }
    case BotActionKind::MoveCancel: {
        MoveCancelAction move(action.player_id);
        return wrap(game.perform_action(action.player_id, move));
    }
    case BotActionKind::MoveRotate: {
        MovePendingRotateAction rotate(action.player_id, action.quarter_turns_cw);
        return wrap(game.perform_action(action.player_id, rotate));
    }
    case BotActionKind::CastSpell:
    case BotActionKind::CastSpellReserve: {
        const CardPlayZone zone =
            action.kind == BotActionKind::CastSpellReserve ? CardPlayZone::Reserves : CardPlayZone::Hand;
        const std::shared_ptr<Entity> focus_caster = resolve_entity(game, action.focus_caster_entity_id);
        CastSpellAction cast(action.card_id, action.player_id, action.spell_targets, action.stack_target_id,
            focus_caster, zone);
        if (action.spell_x_amount > 0) {
            cast.set_x_amount(action.spell_x_amount);
        }
        if (action.spell_mode >= 0) {
            cast.set_mode_index(action.spell_mode);
        }
        if (!action.multicast_spell_targets.empty()) {
            cast.set_multicast_targets(action.multicast_spell_targets);
        }
        return wrap(game.perform_action(action.player_id, cast));
    }
    case BotActionKind::ActivateAbility: {
        const auto it = game.board.all_entities_map.find(action.entity_id);
        if (it == game.board.all_entities_map.end()) {
            return {false, "Unit not found for ability"};
        }
        auto unit = std::dynamic_pointer_cast<Unit>(it->second);
        if (!unit) {
            return {false, "Entity is not a unit"};
        }
        session.selected_unit = unit;
        const std::map<std::string, int> targets = resolve_ability_targets_for_bot_action(action);
        ActivateAbilityAction ability(unit, action.player_id, action.ability_key, targets, action.stack_target_id);
        if (action.spell_x_amount > 0) {
            ability.set_x_amount(action.spell_x_amount);
        }
        return wrap(game.perform_action(action.player_id, ability));
    }
    case BotActionKind::DeclareAttack: {
        const auto it = game.board.all_entities_map.find(action.entity_id);
        if (it == game.board.all_entities_map.end()) {
            return {false, "Unit not found for attack"};
        }
        auto unit = std::dynamic_pointer_cast<Unit>(it->second);
        if (!unit) {
            return {false, "Entity is not a unit"};
        }
        session.selected_unit = unit;
        AttackAction attack(unit, action.player_id, {action.x, action.y}, action.ranged);
        return wrap(game.perform_action(action.player_id, attack));
    }
    case BotActionKind::AttackUndeclare:
        return wrap(game.undeclare_attack(action.player_id, action.entity_id));
    case BotActionKind::CommitAttackDeclaration:
        return wrap(game.commit_attack_declaration(action.player_id));
    case BotActionKind::Defend: {
        const auto it = game.board.all_entities_map.find(action.entity_id);
        if (it == game.board.all_entities_map.end()) {
            return {false, "Unit not found for defend"};
        }
        auto unit = std::dynamic_pointer_cast<Unit>(it->second);
        if (!unit) {
            return {false, "Entity is not a unit"};
        }
        session.selected_unit = unit;
        DefendAction defend(unit, action.player_id);
        return wrap(game.perform_action(action.player_id, defend));
    }
    case BotActionKind::Dash: {
        const auto it = game.board.all_entities_map.find(action.entity_id);
        if (it == game.board.all_entities_map.end()) {
            return {false, "Unit not found for dash"};
        }
        auto unit = std::dynamic_pointer_cast<Unit>(it->second);
        if (!unit) {
            return {false, "Entity is not a unit"};
        }
        session.selected_unit = unit;
        DashAction dash(unit, action.player_id);
        return wrap(game.perform_action(action.player_id, dash));
    }
    case BotActionKind::Recover: {
        const auto it = game.board.all_entities_map.find(action.entity_id);
        if (it == game.board.all_entities_map.end()) {
            return {false, "Unit not found for recover"};
        }
        auto unit = std::dynamic_pointer_cast<Unit>(it->second);
        if (!unit) {
            return {false, "Entity is not a unit"};
        }
        session.selected_unit = unit;
        RecoverAction recover(unit, action.player_id);
        return wrap(game.perform_action(action.player_id, recover));
    }
    case BotActionKind::Undo:
        return wrap(game.undo_last_action(action.player_id));
    case BotActionKind::BatchCancel:
        return wrap(game.cancel_queued_batch_item_for_player(action.player_id, action.entity_id));
    case BotActionKind::EndMainPhase:
        return wrap(game.end_main_phase(action.player_id));
    case BotActionKind::PassPriority:
        return wrap(game.pass_priority(action.player_id));
    case BotActionKind::DiscardHandCard:
        return wrap(game.discard_hand_card_at(action.player_id, action.hand_index_1based));
    case BotActionKind::ScanDiscard:
        return wrap(game.scan_discard_at(action.player_id, action.hand_index_1based));
    case BotActionKind::ScanFinish:
        return wrap(game.scan_finish(action.player_id));
    case BotActionKind::SkipTerritoryTarget:
        return wrap(game.skip_territory_target(action.player_id));
    case BotActionKind::ResolveTerritoryTarget: {
        std::map<std::string, int> targets = action.spell_targets;
        if (targets.empty()) {
            targets = {{effect_keys::kCellX, action.x}, {effect_keys::kCellY, action.y}};
        }
        return wrap(game.resolve_territory_target(action.player_id, targets, action.entity_id));
    }
    case BotActionKind::SkipTerritoryLoot:
        return wrap(game.territory_loot_skip(action.player_id));
    case BotActionKind::TerritoryLootDiscard:
        return wrap(game.territory_loot_discard_at(action.player_id, action.hand_index_1based));
    case BotActionKind::UseLand: {
        std::map<std::string, int> targets = action.spell_targets;
        if (targets.empty() && (action.x != 0 || action.y != 0)) {
            targets = {{effect_keys::kCellX, action.x}, {effect_keys::kCellY, action.y}};
        }
        return wrap(game.use_land(action.player_id, action.energy_zone_index_1based - 1,
            std::max(0, action.land_ability_index_1based - 1), targets, action.entity_id));
    }
    case BotActionKind::ResumeCombatViz:
        return wrap(game.resume_combat_visualization());
    }
    return {false, "Unknown bot action kind"};
}

}  // namespace tactics::bot