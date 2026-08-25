#include "tactics/actions/actions.hpp"

#include "tactics/actions/board_targeting.hpp"
#include "tactics/cards/card_runtime.hpp"
#include "tactics/cards/focus_spell.hpp"
#include "tactics/attributes/attributes.hpp"
#include "tactics/board/grid.hpp"
#include "tactics/board/tile_modifiers.hpp"
#include "tactics/board/trench.hpp"
#include "tactics/combat/combat_resolver.hpp"
#include "tactics/combat/directional_area.hpp"
#include "tactics/combat/soul_steal.hpp"
#include "tactics/combat/taunt.hpp"
#include "tactics/common/effect_keys.hpp"
#include "tactics/core/game_state.hpp"
#include "tactics/effects/effect_registry.hpp"
#include "tactics/effects/effect_traits.hpp"

#include <algorithm>
#include <set>
#include <string>

namespace {

std::optional<std::string> multicast_target_signature(const std::map<std::string, int>& targets)
{
    const auto xit = targets.find(tactics::effect_keys::kCellX);
    const auto yit = targets.find(tactics::effect_keys::kCellY);
    if (xit != targets.end() && yit != targets.end()) {
        return std::to_string(xit->second) + "," + std::to_string(yit->second);
    }
    const auto pit = targets.find(tactics::effect_keys::kTargetPlayerSeat);
    if (pit != targets.end()) {
        return std::string{"player:"} + std::to_string(pit->second);
    }
    return std::nullopt;
}

bool multicast_target_maps_are_distinct(const std::vector<std::map<std::string, int>>& maps)
{
    std::set<std::string> seen;
    for (const auto& target_map : maps) {
        const auto sig = multicast_target_signature(target_map);
        if (!sig) {
            continue;
        }
        if (!seen.insert(*sig).second) {
            return false;
        }
    }
    return true;
}

std::optional<tactics::AbilitySpec> find_activated_ability(const tactics::Entity& entity, const std::string& key) {
    for (const auto& a : entity.activated_abilities) {
        if (a.key == key) return a;
    }
    return std::nullopt;
}

std::string target_entity_id_for_stack_item(const tactics::GameState& game, const std::map<std::string, int>& targets) {
    const auto xit = targets.find(tactics::effect_keys::kCellX);
    const auto yit = targets.find(tactics::effect_keys::kCellY);
    if (xit == targets.end() || yit == targets.end()) {
        return {};
    }
    auto ent = game.board.entity_at(xit->second, yit->second);
    return ent ? ent->entity_id : std::string{};
}

bool effect_deals_damage(const std::string& effect_key) {
    return tactics::effect_key_deals_damage(effect_key);
}

bool effect_scales_with_ability_damage(const std::string& effect_key) {
    return tactics::effect_key_scales_with_ability_damage(effect_key);
}

bool spell_cast_allowed_in_current_phase(const tactics::GameState& game, const tactics::EffectSpeed speed)
{
    const auto phase = game.turn_manager.current_phase;
    if (phase == tactics::TurnPhase::Main || phase == tactics::TurnPhase::SecondMain) {
        return true;
    }
    if (phase == tactics::TurnPhase::AttackDeclaration || phase == tactics::TurnPhase::BonusAttackDeclaration
            || phase == tactics::TurnPhase::SpellWindow || phase == tactics::TurnPhase::SecondSpellWindow
            || phase == tactics::TurnPhase::Defense || phase == tactics::TurnPhase::BonusDefense) {
        return speed == tactics::EffectSpeed::Reflex || speed == tactics::EffectSpeed::Blazing;
    }
    return false;
}

tactics::StackItem build_stack_item_from_effect(
    const std::string& source_type, const std::string& source_name, const std::string& source_entity_id,
    const int controller_id, const std::string& effect_key, const tactics::EffectSpeed speed,
    const std::map<std::string, int>& effect_payload, const std::map<std::string, std::string>& effect_string_payload,
    const std::map<std::string, int>& targets, const tactics::BoardTargetKind board_target_kind,
    const std::string& target_entity_id, const std::string& target_stack_item_id,
    const std::vector<std::string>& require_target_unit_types, const std::vector<std::string>& bonus_damage_unit_types,
    const int bonus_damage_amount)
{
    tactics::StackItem item;
    item.source_type = source_type;
    item.source_name = source_name;
    item.source_entity_id = source_entity_id;
    item.controller_id = controller_id;
    item.effect_key = effect_key;
    item.speed = speed;
    item.payload = effect_payload;
    item.string_payload = effect_string_payload;
    item.targets = targets;
    item.board_target_kind = board_target_kind;
    item.target_entity_id = target_entity_id;
    item.target_stack_item_id = target_stack_item_id;
    item.require_target_unit_types = require_target_unit_types;
    item.bonus_damage_unit_types = bonus_damage_unit_types;
    item.bonus_damage_amount = bonus_damage_amount;
    return item;
}

std::vector<std::pair<int, int>> entity_cells(const tactics::Entity& entity)
{
    if (!entity.occupied_positions.empty()) {
        return entity.occupied_positions;
    }
    std::vector<std::pair<int, int>> out;
    if (!entity.position) {
        return out;
    }
    const auto [ax, ay] = *entity.position;
    for (const auto& [dx, dy] : tactics::entity_shape_offsets(entity)) {
        out.push_back({ax + dx, ay + dy});
    }
    return out;
}

bool entities_within_chebyshev_range(const tactics::Entity& source, const tactics::Entity& target, const int max_range)
{
    if (max_range < 0 || source.entity_id == target.entity_id) {
        return false;
    }
    for (const auto& [tx, ty] : entity_cells(target)) {
        if (tactics::min_chebyshev_entity_to_cell(source, tx, ty) <= max_range) {
            return true;
        }
    }
    return false;
}

bool tune_up_target_allowed(const tactics::Entity& entity)
{
    return entity.entity_type == "unit" || tactics::entity_is_structure(entity);
}

std::optional<tactics::ActionResult> validate_custom_ability_target_rules(const tactics::Entity& actor,
    const tactics::AbilitySpec& ability, const std::shared_ptr<tactics::Entity>& target)
{
    if (!target) {
        return std::nullopt;
    }
    if (ability.effect_key == "repair_structure_adjacent") {
        if (!tactics::entity_is_quick_repairs_target(*target)) {
            return tactics::ActionResult{false, "Quick Repairs must target an adjacent allied structure or base", {}};
        }
        if (!entities_within_chebyshev_range(actor, *target, 1)) {
            return tactics::ActionResult{false, "Quick Repairs requires an adjacent allied structure or base", {}};
        }
    }
    if (ability.effect_key == "grant_next_damage_bonus_adjacent") {
        if (!tune_up_target_allowed(*target)) {
            return tactics::ActionResult{false, "Tune-Up must target an allied unit or structure", {}};
        }
        if (!entities_within_chebyshev_range(actor, *target, 1)) {
            return tactics::ActionResult{false, "Tune-Up requires an adjacent allied target", {}};
        }
    }
    if (ability.effect_key == "grant_on_damage_apply_overload_adjacent" ||
        ability.effect_key == "grant_on_damage_apply_jammed_adjacent") {
        if (!tune_up_target_allowed(*target)) {
            return tactics::ActionResult{false, "Target must be an allied unit or structure", {}};
        }
        if (actor.entity_id != target->entity_id && !entities_within_chebyshev_range(actor, *target, 1)) {
            return tactics::ActionResult{false, "Target must be adjacent or self", {}};
        }
    }
    // Payload-driven cardinal-range check: abilities with "cardinal_only": 1 in their payload
    // restrict targeting to entities that share a row or column with the actor within "max_range" steps.
    if (const auto cit = ability.effect_payload.find("cardinal_only");
        cit != ability.effect_payload.end() && cit->second != 0) {
        const int max_range = [&]() -> int {
            const auto rit = ability.effect_payload.find("max_range");
            return rit != ability.effect_payload.end() ? rit->second : 1;
        }();
        bool in_cardinal_range = false;
        for (const auto& [ax, ay] : entity_cells(actor)) {
            for (const auto& [tx, ty] : entity_cells(*target)) {
                const int dx = tx - ax;
                const int dy = ty - ay;
                if ((dx == 0 && std::abs(dy) <= max_range) || (dy == 0 && std::abs(dx) <= max_range)) {
                    in_cardinal_range = true;
                }
            }
        }
        if (!in_cardinal_range) {
            return tactics::ActionResult{false,
                "Target must be within cardinal range " + std::to_string(max_range), {}};
        }
    }
    return std::nullopt;
}

}  // namespace

namespace tactics {

MovePreviewAction::MovePreviewAction(std::shared_ptr<Unit> unit, int player_id, std::pair<int, int> goal_cell)
    : unit_(std::move(unit)), goal_(goal_cell) {
    this->player_id = player_id;
    action_type = ActionType::Move;
}

ActionResult MovePreviewAction::validate(GameState& game) {
    if (!unit_) return {false, "No unit", {}};
    if (!game.unit_may_move_this_phase(unit_->entity_id)) {
        return {false, "That unit cannot move after queuing an action this phase", {}};
    }
    return {true, "ok", {}};
}

ActionResult MovePreviewAction::execute(GameState& game) {
    return game.apply_move_preview(player_id, unit_, goal_.first, goal_.second);
}

MovePendingRotateAction::MovePendingRotateAction(int player_id, int delta_quarters_cw) : delta_quarters_cw_(delta_quarters_cw) {
    this->player_id = player_id;
    action_type = ActionType::Move;
}

ActionResult MovePendingRotateAction::validate(GameState& game) {
    static_cast<void>(game);
    return {true, "ok", {}};
}

ActionResult MovePendingRotateAction::execute(GameState& game) {
    return game.apply_pending_move_rotation_delta(player_id, delta_quarters_cw_);
}

MoveConfirmAction::MoveConfirmAction(int player_id) {
    this->player_id = player_id;
    action_type = ActionType::Move;
}

ActionResult MoveConfirmAction::validate(GameState& game) {
    static_cast<void>(game);
    return {true, "ok", {}};
}

ActionResult MoveConfirmAction::execute(GameState& game) {
    return game.confirm_pending_move(player_id);
}

MoveCancelAction::MoveCancelAction(int player_id) {
    this->player_id = player_id;
    action_type = ActionType::Move;
}

ActionResult MoveCancelAction::validate(GameState& game) {
    static_cast<void>(game);
    return {true, "ok", {}};
}

ActionResult MoveCancelAction::execute(GameState& game) {
    if (!game.has_pending_move_for(player_id)) {
        return {false, "No pending move to cancel", {}};
    }
    game.clear_pending_move_for(player_id);
    return {true, "Move cancelled", {}};
}

AttackAction::AttackAction(std::shared_ptr<Unit> actor, int player_id, std::pair<int, int> target, bool ranged,
                           std::optional<std::pair<int, int>> soul_steal_heal_base_cell)
    : actor_(std::move(actor)), target_(target), ranged_(ranged), soul_steal_heal_base_cell_(soul_steal_heal_base_cell) {
    this->player_id = player_id;
    action_type = ActionType::Combat;
}

ActionResult AttackAction::validate(GameState& game) {
    if (actor_ && !game.unit_may_queue_non_focus_batch_action_this_phase(actor_->entity_id)) {
        return {false, "That unit already queued an attack or ability this phase", {}};
    }
    auto vr = validate_attack(game, actor_, player_id, target_, ranged_);
    if (!vr.ok) {
        return vr;
    }
    std::map<std::string, int> soul_targets;
    if (soul_steal_heal_base_cell_) {
        soul_targets[effect_keys::kHealBaseX] = soul_steal_heal_base_cell_->first;
        soul_targets[effect_keys::kHealBaseY] = soul_steal_heal_base_cell_->second;
    }
    const bool needs_soul_steal = actor_ && has_soul_steal(*actor_);
    auto soul_vr = validate_soul_steal_heal_base_target(game, player_id, soul_targets, needs_soul_steal);
    if (!soul_vr.ok) {
        return soul_vr;
    }
    return vr;
}

ActionResult AttackAction::execute(GameState& game) {
    auto vr = validate(game);
    if (!vr.ok) return vr;
    // During Attack Declaration phase, queue the attack for later batch execution.
    const auto phase = game.turn_manager.current_phase;
    if (phase == TurnPhase::AttackDeclaration || phase == TurnPhase::BonusAttackDeclaration) {
        return game.declare_attack(player_id, actor_ ? actor_->entity_id : "", target_.first, target_.second, ranged_);
    }
    return {false, "Attacks must be declared during the Attack Declaration phase.", {}};
}

DefendAction::DefendAction(std::shared_ptr<Unit> actor, int player_id) : actor_(std::move(actor)) {
    this->player_id = player_id;
    action_type = ActionType::Ability;
}

ActionResult DefendAction::validate(GameState& game) {
    if (!actor_) {
        return {false, "No unit", {}};
    }
    if (entity_is_stunned(*actor_)) {
        return {false, "Stunned units cannot defend", {}};
    }
    if (core_cracker_shutdown_blocks_actions(*actor_)) {
        return {false, "Prime Core first", {}};
    }
    return validate_unit_defend_budget(game, *actor_, player_id);
}

ActionResult DefendAction::execute(GameState& game) {
    if (!actor_) {
        return {false, "No unit", {}};
    }
    const int attacks_before = actor_->attacks_remaining_this_turn;
    const int moves_before = actor_->moves_remaining_this_turn;
    const int standard_before = actor_->standard_moves_remaining_this_turn;
    const bool attacked_before = actor_->has_attacked_this_turn;
    auto res = apply_defend_stance(game, actor_, player_id);
    if (res.ok) {
        res.data["undo_unit_id"] = actor_->entity_id;
        res.data["undo_attacks_remaining"] = std::to_string(attacks_before);
        res.data["undo_moves_remaining"] = std::to_string(moves_before);
        res.data["undo_standard_moves_remaining"] = std::to_string(standard_before);
        res.data["undo_has_attacked"] = attacked_before ? "1" : "0";
    }
    return res;
}

DashAction::DashAction(std::shared_ptr<Unit> actor, int player_id) : actor_(std::move(actor)) {
    this->player_id = player_id;
    action_type = ActionType::Ability;
}

ActionResult DashAction::validate(GameState& game) {
    if (!actor_) {
        return {false, "No unit", {}};
    }
    if (entity_is_stunned(*actor_)) {
        return {false, "Stunned units cannot dash", {}};
    }
    if (core_cracker_shutdown_blocks_actions(*actor_)) {
        return {false, "Prime Core first", {}};
    }
    return validate_unit_dash_budget(game, *actor_, player_id);
}

ActionResult DashAction::execute(GameState& game) {
    if (!actor_) {
        return {false, "No unit", {}};
    }
    const int attacks_before = actor_->attacks_remaining_this_turn;
    const bool attacked_before = actor_->has_attacked_this_turn;
    auto res = apply_dash_movement(game, actor_, player_id);
    if (res.ok) {
        res.data["undo_unit_id"] = actor_->entity_id;
        res.data["undo_attacks_remaining"] = std::to_string(attacks_before);
        res.data["undo_has_attacked"] = attacked_before ? "1" : "0";
    }
    return res;
}

RecoverAction::RecoverAction(std::shared_ptr<Unit> actor, int player_id) : actor_(std::move(actor)) {
    this->player_id = player_id;
    action_type = ActionType::Ability;
}

ActionResult RecoverAction::validate(GameState& game) {
    if (!actor_) {
        return {false, "No unit", {}};
    }
    if (entity_is_stunned(*actor_)) {
        return {false, "Stunned units cannot recover", {}};
    }
    if (core_cracker_shutdown_blocks_actions(*actor_)) {
        return {false, "Prime Core first", {}};
    }
    return validate_unit_recover_budget(game, *actor_, player_id);
}

ActionResult RecoverAction::execute(GameState& game) {
    if (!actor_) {
        return {false, "No unit", {}};
    }
    const int attacks_before = actor_->attacks_remaining_this_turn;
    const int moves_before = actor_->moves_remaining_this_turn;
    const int standard_before = actor_->standard_moves_remaining_this_turn;
    const bool attacked_before = actor_->has_attacked_this_turn;
    auto res = apply_recover_stance(game, actor_, player_id);
    if (res.ok) {
        res.data["undo_unit_id"] = actor_->entity_id;
        res.data["undo_attacks_remaining"] = std::to_string(attacks_before);
        res.data["undo_moves_remaining"] = std::to_string(moves_before);
        res.data["undo_standard_moves_remaining"] = std::to_string(standard_before);
        res.data["undo_has_attacked"] = attacked_before ? "1" : "0";
    }
    return res;
}

DeployAction::DeployAction(const CardInstanceId card, const int player_id, const std::pair<int, int> target, const CardPlayZone zone)
    : card_id_(card), target_(target), zone_(zone)
{
    this->player_id = player_id;
    action_type = ActionType::Deploy;
}

std::map<EnergyType, int> DeployAction::get_cost(const GameState& game) const
{
    if (!card_id_.is_valid()) {
        return {};
    }
    const auto deck_it = game.players_decks.find(player_id);
    if (deck_it == game.players_decks.end()) {
        return {};
    }
    const CardInstance* inst = deck_it->second.pool.try_get(card_id_);
    if (!inst) {
        return {};
    }
    if (const CardDefinition* def = try_get_card_definition_ptr(inst->definition_id)) {
        auto cost = definition_energy_cost(*def);
        // Apply per-unit deploy discount (e.g. from Mobilize): subtract from neutral only.
        // Units with no neutral component in their cost are unaffected.
        const auto& disc_map = game.turn_manager.deploy_discount_per_unit;
        if (const auto dit = disc_map.find(player_id);
            dit != disc_map.end() && dit->second > 0) {
            if (auto it = cost.find(EnergyType::Neutral); it != cost.end() && it->second > 0) {
                it->second = std::max(0, it->second - dit->second);
            }
        }
        return cost;
    }
    return {};
}

ActionResult DeployAction::validate(GameState& game)
{
    if (!card_id_.is_valid()) {
        return {false, "No card", {}};
    }
    auto deck_it = game.players_decks.find(player_id);
    if (deck_it == game.players_decks.end()) {
        return {false, "No deck for player", {}};
    }
    Deck& deck = deck_it->second;
    std::string play_reason;
    if (zone_ == CardPlayZone::Reserves) {
        if (!deck.can_play_card_from_reserves(card_id_, &play_reason)) {
            return {false, play_reason, {}};
        }
    } else if (!deck.can_play_card_now(card_id_, &play_reason)) {
        return {false, play_reason, {}};
    }
    const CardInstance& inst = deck.pool.at(card_id_);
    const CardDefinition& def = definition_for_instance(deck.pool, inst);
    if (!definition_is_unit(def)) {
        return {false, "Not a unit card", {}};
    }
    std::string exalted_reason;
    if (!exalted_requirement_met(game, player_id, def, &exalted_reason)) {
        return {false, exalted_reason, {}};
    }
    auto temp = create_unit_from_definition(def, inst, player_id, "temp");
    const int unit_cost = definition_total_energy_cost(def);
    if (!game.can_deploy_entity_at(player_id, temp, target_.first, target_.second, unit_cost)) {
        if (entity_is_building(*temp) && spearhead_value(*temp) > 0) {
            return {false,
                "Spearhead " + std::to_string(spearhead_value(*temp)) + ": invalid location", {}};
        }
        if (entity_is_building(*temp)) {
            return {false, "Not in deploy zone or next to ally", {}};
        }
        return {false, "Not in deploy zone or Command range", {}};
    }
    if (!game.board.can_place_entity_at(temp, target_.first, target_.second)) {
        return {false, "Space blocked", {}};
    }
    return {true, "Can deploy " + definition_name(def), {}};
}

ActionResult DeployAction::execute(GameState& game)
{
    const auto vr = validate(game);
    if (!vr.ok) {
        return vr;
    }
    Deck& deck = game.players_decks.at(player_id);
    const CardInstance& inst = deck.pool.at(card_id_);
    const CardDefinition& def = definition_for_instance(deck.pool, inst);
    auto unit = create_unit_from_definition(def, inst, player_id, "_pending");
    if (!unit) {
        return {false, "Failed to create unit from card", {}};
    }
    game.assign_monotonic_entity_id(unit, definition_name(def));
    if (!game.board.place_entity(unit, target_.first, target_.second)) {
        return {false, "Failed to place unit on board", {}};
    }
    apply_deployment_fatigue(game, unit);
    game.mark_core_cracker_deployed(unit);
    // Snapshot stockpile charge state BEFORE play_card decrements it, so undo can restore it.
    // Stockpile units (e.g. Basic Infantry tokens) stay in hand after deploy rather than going
    // to in_play - undo restores the spent charge instead of pulling the card off the board.
    const CardInstance& inst_snap = deck.pool.at(card_id_);
    const bool had_stockpile = inst_snap.stockpile_amount > 0;
    const int stk_remaining = inst_snap.stockpile_remaining;
    const bool stk_used = inst_snap.stockpile_used_this_turn;
    const bool stk_double = inst_snap.stockpile_double_play_used_this_turn;
    if (zone_ == CardPlayZone::Reserves) {
        deck.play_card_from_reserves(card_id_);
    } else {
        deck.play_card(card_id_);
    }
    game.register_unit_deployed(player_id, card_id_, unit);
    ActionResult out{true, "Successfully deployed " + definition_name(def), {}};
    out.data["undo_deploy_entity_id"] = unit->entity_id;
    if (had_stockpile) {
        out.data["undo_spell_had_stockpile"] = "1";
        out.data["undo_stockpile_remaining"] = std::to_string(stk_remaining);
        out.data["undo_stockpile_used_this_turn"] = stk_used ? "1" : "0";
        out.data["undo_stockpile_double_play_used"] = stk_double ? "1" : "0";
    }
    return out;
}

CastSpellAction::CastSpellAction(const CardInstanceId card, const int player_id, std::map<std::string, int> targets, const CardPlayZone zone)
    : card_id_(card), targets_(std::move(targets)), zone_(zone)
{
    this->player_id = player_id;
    action_type = ActionType::Spell;
}

CastSpellAction::CastSpellAction(const CardInstanceId card, const int player_id, std::map<std::string, int> targets, std::string stack_target_id,
    const CardPlayZone zone)
    : CastSpellAction(card, player_id, std::move(targets), zone)
{
    stack_target_id_ = std::move(stack_target_id);
}

CastSpellAction::CastSpellAction(const CardInstanceId card, const int player_id, std::map<std::string, int> targets,
    std::shared_ptr<Entity> focus_caster, const CardPlayZone zone)
    : CastSpellAction(card, player_id, std::move(targets), zone)
{
    focus_caster_ = std::move(focus_caster);
}

CastSpellAction::CastSpellAction(const CardInstanceId card, const int player_id, std::map<std::string, int> targets, std::string stack_target_id,
    std::shared_ptr<Entity> focus_caster, const CardPlayZone zone)
    : CastSpellAction(card, player_id, std::move(targets), std::move(stack_target_id), zone)
{
    focus_caster_ = std::move(focus_caster);
}

std::map<EnergyType, int> CastSpellAction::get_cost(const GameState& game) const
{
    if (!card_id_.is_valid()) {
        return {};
    }
    const auto deck_it = game.players_decks.find(player_id);
    if (deck_it == game.players_decks.end()) {
        return {};
    }
    const CardInstance* inst = deck_it->second.pool.try_get(card_id_);
    if (!inst) {
        return {};
    }
    const CardDefinition* def = try_get_card_definition_ptr(inst->definition_id);
    if (!def) {
        return {};
    }
    auto cost = definition_energy_cost(*def);
    // X-cost: add the chosen x_amount to the variable energy type.
    if (definition_is_spell(*def)) {
        const SpellCardDefinition& spell = definition_spell(*def);
        if (spell.x_cost_energy_type.has_value() && x_amount_ > 0) {
            cost[*spell.x_cost_energy_type] += x_amount_;
        }
    }
    return cost;
}

void CastSpellAction::set_multicast_targets(std::vector<std::map<std::string, int>> targets)
{
    multicast_targets_ = std::move(targets);
    if (!multicast_targets_.empty()) {
        targets_ = multicast_targets_.front();
    }
}

std::vector<std::map<std::string, int>> CastSpellAction::resolved_multicast_target_maps(const CardDefinition& def) const
{
    const int multicast = definition_multicast_amount(def);
    if (multicast <= 1) {
        return {targets_};
    }
    if (definition_spell_multicast_requires_per_copy_targets(def)) {
        if (!multicast_targets_.empty()) {
            if (multicast_targets_.size() > static_cast<size_t>(multicast)) {
                return {};
            }
            return multicast_targets_;
        }
        if (!targets_.empty()) {
            return {targets_};
        }
        return {};
    }
    return std::vector<std::map<std::string, int>>(static_cast<size_t>(multicast), targets_);
}

namespace {

/** The effective effect for a spell cast, resolving a modal spell's chosen mode. For a
 *  non-modal spell (`modes` empty) this is just the spell's own fields. This single seam is
 *  what makes the whole cast pipeline (validation, targeting, stack) mode-aware. */
struct EffectiveSpellMode {
    std::string effect_key;
    std::map<std::string, int> effect_payload;
    std::map<std::string, std::string> effect_string_payload;
    BoardTargetKind board_target_kind{BoardTargetKind::Enemy};
    bool requires_board_target{false};
};

EffectiveSpellMode effective_spell_mode(const CardDefinition& def, const SpellCardDefinition& spell, const int mode_index)
{
    EffectiveSpellMode e;
    e.effect_key = spell.effect_key;
    e.effect_payload = spell.effect_payload;
    e.effect_string_payload = spell.effect_string_payload;
    e.board_target_kind = definition_spell_board_target_kind(def);
    e.requires_board_target = definition_spell_requires_mandatory_board_cell(def);
    if (!spell.modes.empty() && mode_index >= 0 && mode_index < static_cast<int>(spell.modes.size())) {
        const SpellMode& m = spell.modes[static_cast<std::size_t>(mode_index)];
        e.effect_key = m.effect_key;
        e.effect_payload = m.effect_payload;
        e.effect_string_payload = m.effect_string_payload;
        if (m.board_target_kind) {
            e.board_target_kind = *m.board_target_kind;
        }
        e.requires_board_target = m.requires_board_target;
    }
    return e;
}

}  // namespace

ActionResult CastSpellAction::validate_spell_target_map(GameState& game, const CardDefinition& def,
    const SpellCardDefinition& spell, const std::map<std::string, int>& targets) const
{
    // Modal spell: a mode must be chosen before the card can be cast; then all targeting
    // below uses the chosen mode's effect + target kind via `eff`.
    if (!spell.modes.empty() && (mode_index_ < 0 || mode_index_ >= static_cast<int>(spell.modes.size()))) {
        return {false, "Choose a mode for this card", {}};
    }
    const EffectiveSpellMode eff = effective_spell_mode(def, spell, mode_index_);
    const auto caster = resolve_focus_caster_from_targets(game, player_id, targets_, focus_caster_);
    if (spell_requires_focus_caster(def)) {
        if (!caster) {
            return {false, "Select a unit to cast from", {}};
        }
        return validate_focus_spell_cast(game, player_id, def, spell, *caster, targets, stack_target_id_);
    }
    if (spell_requires_forced_damage_spell_focus_caster(game, player_id, def)) {
        if (!caster || !cast_uses_forced_damage_spell_focus_caster(game, def, caster)) {
            return {false, "Select a unit with Insatiable Focus", {}};
        }
        const auto forced_range = entity_forced_damage_spell_focus_range(*caster);
        if (!forced_range) {
            return {false, "Selected unit cannot cast this damaging spell", {}};
        }
        return validate_focus_spell_cast(game, player_id, def, spell, *caster, targets, stack_target_id_, forced_range);
    }
    if (eff.effect_key == "place_trench") {
        const auto xit = targets.find(effect_keys::kCellX);
        const auto yit = targets.find(effect_keys::kCellY);
        if (xit == targets.end() || yit == targets.end()) {
            return {false, "This spell requires a target cell position", {}};
        }
        return validate_place_trench_target(game, player_id, xit->second, yit->second);
    }
    if (eff.effect_key == "gas_strike") {
        const auto xit = targets.find(effect_keys::kCellX);
        const auto yit = targets.find(effect_keys::kCellY);
        if (xit == targets.end() || yit == targets.end()) {
            return {false, "gas_strike: requires a target cell", {}};
        }
        if (!game.board.get_square(xit->second, yit->second)) {
            return {false, "gas_strike: target cell is not on the board", {}};
        }
        return {true, "Can cast " + definition_name(def), {}};
    }
    if (eff.effect_key == "grant_aoe_stat_buff_turn_end") {
        const auto xit = targets.find(effect_keys::kCellX);
        const auto yit = targets.find(effect_keys::kCellY);
        if (xit == targets.end() || yit == targets.end()) {
            return {false, "This spell requires a target cell position", {}};
        }
        if (!game.board.get_square(xit->second, yit->second)) {
            return {false, "Target cell is not on the board", {}};
        }
        return {true, "Can cast " + definition_name(def), {}};
    }
    if (effect_key_uses_push_direction_aim(eff.effect_key)) {
        const auto push_vr = validate_push_direction_spell_target(
            game, player_id, eff.effect_key, eff.board_target_kind, spell.require_target_unit_types,
            eff.effect_payload, targets);
        if (!push_vr.ok) {
            return push_vr;
        }
        return {true, "Can cast " + definition_name(def), {}};
    }
    TargetDefinition target_def = target_definition_for_effect_key(eff.effect_key);
    target_def.board_target_kind = eff.board_target_kind;
    target_def.require_target_unit_types = spell.require_target_unit_types;
    if (eff.requires_board_target) {
        target_def.domain = TargetDomain::BoardEntityCell;
        target_def.requirement = TargetRequirement::Required;
    }
    const auto target_result =
        validate_targets_against_definition(game, player_id, target_def, targets, stack_target_id_, eff.effect_key);
    if (!target_result.ok) {
        return target_result;
    }
    if (const auto mit = eff.effect_payload.find("max_deploy_cost"); mit != eff.effect_payload.end()) {
        const auto xit = targets.find(effect_keys::kCellX);
        const auto yit = targets.find(effect_keys::kCellY);
        if (xit != targets.end() && yit != targets.end()) {
            const auto target_ent = game.board.entity_at(xit->second, yit->second);
            if (!target_ent || !entity_satisfies_max_deploy_cost(*target_ent, mit->second)) {
                return {false,
                    "Target must be a unit with total energy cost " + std::to_string(mit->second) + " or less", {}};
            }
        }
    }
    if (eff.requires_board_target && target_def.domain == TargetDomain::BoardEntityCell) {
        const auto xit = targets.find(effect_keys::kCellX);
        const auto yit = targets.find(effect_keys::kCellY);
        if (xit != targets.end() && yit != targets.end()) {
            const auto target_ent = game.board.entity_at(xit->second, yit->second);
            const Entity* taunt_actor = focus_caster_.get();
            if (!taunt_actor && caster && cast_uses_forced_damage_spell_focus_caster(game, def, caster)) {
                taunt_actor = caster.get();
            }
            if (target_ent && !taunt_allows_board_target(game, taunt_actor, player_id, *target_ent)) {
                return {false, "Must target adjacent taunt", {}};
            }
        }
    }
    const bool needs_soul_steal = definition_has_soul_steal(def) && effect_deals_damage(eff.effect_key);
    auto soul_vr = validate_soul_steal_heal_base_target(game, player_id, targets, needs_soul_steal);
    if (!soul_vr.ok) {
        return soul_vr;
    }
    return {true, "Can cast " + definition_name(def), {}};
}

ActionResult CastSpellAction::validate(GameState& game)
{
    if (!card_id_.is_valid()) {
        return {false, "No card", {}};
    }
    auto deck_it = game.players_decks.find(player_id);
    if (deck_it == game.players_decks.end()) {
        return {false, "No deck for player", {}};
    }
    Deck& deck = deck_it->second;
    std::string play_reason;
    if (zone_ == CardPlayZone::Reserves) {
        if (!deck.can_play_card_from_reserves(card_id_, &play_reason)) {
            return {false, play_reason, {}};
        }
    } else if (!deck.can_play_card_now(card_id_, &play_reason)) {
        return {false, play_reason, {}};
    }
    const CardDefinition& def = definition_for_instance(deck.pool, card_id_);
    std::string exalted_reason;
    if (!exalted_requirement_met(game, player_id, def, &exalted_reason)) {
        return {false, exalted_reason, {}};
    }
    const SpellCardDefinition& spell = definition_spell(def);
    const EffectSpeed cast_speed = effective_spell_cast_speed(game, player_id, def);
    if (!spell_cast_allowed_in_current_phase(game, cast_speed)) {
        return {false, "This spell's speed cannot be played in the current phase", {}};
    }
    // X-cost validation: chosen amount must be >= the spell's minimum X.
    if (spell.x_cost_energy_type.has_value() && x_amount_ < spell.x_cost_min) {
        return {false, "X must be at least " + std::to_string(spell.x_cost_min), {}};
    }
    const auto target_maps = resolved_multicast_target_maps(def);
    const int multicast = definition_multicast_amount(def);
    if (target_maps.empty()) {
        if (multicast > 1 && definition_spell_multicast_requires_per_copy_targets(def)) {
            return {false,
                "Multicast " + std::to_string(multicast)
                + " requires at least one target (up to " + std::to_string(multicast) + " distinct targets)",
                {}};
        }
        return {false, "Multicast spell target is invalid", {}};
    }
    if (multicast > 1 && definition_spell_multicast_requires_per_copy_targets(def)
            && !multicast_target_maps_are_distinct(target_maps)) {
        return {false, "Multicast requires a different target for each copy", {}};
    }
    for (const auto& target_map : target_maps) {
        const auto target_result = validate_spell_target_map(game, def, spell, target_map);
        if (!target_result.ok) {
            return target_result;
        }
    }
    return {true, "Can cast " + definition_name(def), {}};
}

ActionResult CastSpellAction::execute(GameState& game)
{
    const auto vr = validate(game);
    if (!vr.ok) {
        return vr;
    }
    Deck& deck = game.players_decks.at(player_id);
    const CardDefinition& def = definition_for_instance(deck.pool, card_id_);
    const SpellCardDefinition& spell = definition_spell(def);
    const EffectiveSpellMode eff = effective_spell_mode(def, spell, mode_index_);  // modal: resolve chosen mode
    std::shared_ptr<Entity> focus_caster;
    if (spell_requires_focus_caster(def)) {
        focus_caster = resolve_focus_caster_from_targets(game, player_id, targets_, focus_caster_);
    } else if (spell_requires_forced_damage_spell_focus_caster(game, player_id, def)) {
        focus_caster = resolve_focus_caster_from_targets(game, player_id, targets_, focus_caster_);
    }
    const bool cast_as_focus_spell = spell_requires_focus_caster(def)
        || spell_requires_forced_damage_spell_focus_caster(game, player_id, def);
    const auto target_maps = resolved_multicast_target_maps(def);
    const int multicast = static_cast<int>(target_maps.size());
    const std::string multicast_cast_id = multicast > 1
        ? ("mcast_" + std::to_string(card_id_.value) + "_" + game.stack_manager.allocate_item_id())
        : std::string{};
    std::vector<std::string> batch_item_ids;
    batch_item_ids.reserve(static_cast<size_t>(multicast));
    ActionResult res{true, "", {}};
    for (int copy = 0; copy < multicast; ++copy) {
        const auto& copy_targets = target_maps[static_cast<size_t>(copy)];
        const EffectSpeed cast_speed = effective_spell_cast_speed(game, player_id, def);
        StackItem copy_item = build_stack_item_from_effect(
            cast_as_focus_spell ? "focus_spell" : "spell",
            definition_name(def),
            std::string{},
            player_id,
            eff.effect_key,
            cast_speed,
            eff.effect_payload,
            eff.effect_string_payload,
            copy_targets,
            eff.board_target_kind,
            target_entity_id_for_stack_item(game, copy_targets),
            stack_target_id_,
            spell.require_target_unit_types,
            spell.bonus_damage_unit_types,
            spell.bonus_damage_amount);
        copy_item.played_from_reserves = zone_ == CardPlayZone::Reserves;
        copy_item.chain = spell.chain;
        if (spell.x_cost_energy_type.has_value() && x_amount_ > 0) {
            copy_item.payload[effect_keys::kPayloadAmount] = x_amount_;
        }
        if (focus_caster) {
            copy_item.source_entity_id = focus_caster->entity_id;
            if (spell_requires_focus_caster(def)) {
                copy_item.payload["focus_range"] = spell.focus_range;
            } else if (const auto forced_range = entity_forced_damage_spell_focus_range(*focus_caster)) {
                copy_item.payload["focus_range"] = *forced_range;
            }
            if (spell.use_caster_ranged_range) {
                copy_item.payload["use_caster_ranged_range"] = 1;
            }
            if (spell.use_caster_attack_range) {
                copy_item.payload["use_caster_attack_range"] = 1;
            }
            copy_item.pierces_damage_prevention = focus_spell_pierces_damage_prevention(def, spell, *focus_caster);
            copy_item.heals_on_damage_dealt = focus_spell_grants_lifesteal(def, spell, *focus_caster);
            const bool grants_soul_steal = focus_spell_grants_soul_steal(def, spell, *focus_caster);
            copy_item.heals_allied_base_on_damage_dealt = grants_soul_steal;
        } else {
            copy_item.pierces_damage_prevention = definition_has_keyword(def, "pierce") && effect_deals_damage(eff.effect_key);
            const bool grants_soul_steal = definition_has_soul_steal(def) && effect_deals_damage(eff.effect_key);
            copy_item.heals_allied_base_on_damage_dealt = grants_soul_steal;
        }
        if (copy_item.heals_allied_base_on_damage_dealt) {
            if (const auto heal_base = resolve_soul_steal_heal_base(game, player_id, copy_targets)) {
                copy_item.soul_steal_heal_base_entity_id = heal_base->entity_id;
            }
        }
        copy_item.item_id.clear();
        copy_item.batched_spell_total_cost = batched_spell_total_cost_for_cast(def, x_amount_);
        if (!multicast_cast_id.empty()) {
            copy_item.multicast_cast_id = multicast_cast_id;
        }
        auto add_res = game.stack_manager.add_item(game, std::move(copy_item));
        if (!add_res.ok) {
            return add_res;
        }
        const auto bit = add_res.data.find("batch_item_id");
        if (bit != add_res.data.end()) {
            batch_item_ids.push_back(bit->second);
            game.apply_passive_reactive_on_allied_damaging_spell_played(
                player_id, spell.effect_key, bit->second);
        }
        game.apply_passive_reactive_on_owner_spell_played(player_id, cast_speed);
        if (copy == 0) {
            res = std::move(add_res);
        }
    }
    const CardInstance& inst_snap = deck.pool.at(card_id_);
    if (inst_snap.stockpile_amount > 0) {
        res.data["undo_spell_had_stockpile"] = "1";
        res.data["undo_stockpile_remaining"] = std::to_string(inst_snap.stockpile_remaining);
        res.data["undo_stockpile_used_this_turn"] = inst_snap.stockpile_used_this_turn ? "1" : "0";
        res.data["undo_stockpile_double_play_used"] = inst_snap.stockpile_double_play_used_this_turn ? "1" : "0";
    }
    if (zone_ == CardPlayZone::Reserves) {
        deck.play_card_from_reserves(card_id_);
    } else {
        deck.play_card(card_id_);
    }
    if (!batch_item_ids.empty()) {
        res.data["batch_item_id"] = batch_item_ids.front();
        if (batch_item_ids.size() > 1) {
            std::string extra_ids;
            for (size_t i = 1; i < batch_item_ids.size(); ++i) {
                if (i > 1) {
                    extra_ids += ",";
                }
                extra_ids += batch_item_ids[i];
            }
            res.data["multicast_batch_item_ids"] = std::move(extra_ids);
        }
    }
    std::string cast_msg = "Casted " + definition_name(def) + " (batched)";
    if (multicast > 1) {
        cast_msg += ", multicast x" + std::to_string(multicast);
    }
    cast_msg += ".";
    ActionResult out{true, std::move(cast_msg), {}};
    out.data = std::move(res.data);
    return out;
}

ActivateAbilityAction::ActivateAbilityAction(std::shared_ptr<Entity> actor, int player_id, std::string ability_key, std::map<std::string, int> targets)
    : actor_(std::move(actor)), ability_key_(std::move(ability_key)), targets_(std::move(targets)) {
    this->player_id = player_id;
    action_type = ActionType::Ability;
}

ActivateAbilityAction::ActivateAbilityAction(
    std::shared_ptr<Entity> actor, int player_id, std::string ability_key, std::map<std::string, int> targets, std::string stack_target_id)
    : ActivateAbilityAction(std::move(actor), player_id, std::move(ability_key), std::move(targets)) {
    stack_target_id_ = std::move(stack_target_id);
}

std::map<EnergyType, int> ActivateAbilityAction::get_cost(const GameState& game) const
{
    (void)game;
    if (!actor_) return {};
    const auto spec = find_activated_ability(*actor_, ability_key_);
    if (!spec) return {};
    std::map<EnergyType, int> total = spec->energy_cost;
    if (spec->barrage && !spec->barrage_cost.empty()) {
        const auto cnt_it = actor_->barrage_cast_counts_this_turn.find(ability_key_);
        const int cast_count = (cnt_it != actor_->barrage_cast_counts_this_turn.end()) ? cnt_it->second : 0;
        if (cast_count > 0) {
            for (const auto& [type, amount] : spec->barrage_cost) {
                total[type] += amount * cast_count;
            }
        }
    }
    if (spec->x_cost_energy_type.has_value() && x_amount_ > 0) {
        total[*spec->x_cost_energy_type] += x_amount_;
    }
    return total;
}

ActionResult ActivateAbilityAction::validate(GameState& game) {
    if (!actor_) return {false, "No unit", {}};
    if (!entity_owned_by(*actor_, player_id)) return {false, "Not your unit", {}};
    if (entity_is_jammed(*actor_)) {
        return {false, "Jammed units cannot use abilities", {}};
    }
    if (entity_is_stunned(*actor_)) {
        return {false, "Stunned units cannot use abilities", {}};
    }
    if (entity_has_defend_stance(*actor_)) {
        return {false, "Defending units cannot use abilities", {}};
    }
    if (deployment_fatigue_blocks_abilities(*actor_)) {
        return {false, "Can't use abilities same turn deployed", {}};
    }
    if (core_cracker_shutdown_blocks_actions(*actor_) && !core_cracker_prime_ability_key(ability_key_)) {
        return {false, "Prime Core first", {}};
    }
    // Check for abilities explicitly disabled by a temporary weapon-mode effect.
    for (const TemporaryEntityEffect& teff : actor_->temporary_effects) {
        if (std::find(teff.disable_ability_keys.begin(), teff.disable_ability_keys.end(), ability_key_)
                != teff.disable_ability_keys.end()) {
            return {false, "Ability '" + ability_key_ + "' is disabled", {}};
        }
    }
    const auto spec = find_activated_ability(*actor_, ability_key_);
    if (!spec) return {false, "Ability '" + ability_key_ + "' not found", {}};
    if (!spell_cast_allowed_in_current_phase(game, spec->speed)) {
        return {false, "This ability's speed cannot be used in the current phase", {}};
    }
    if (spec->speed != EffectSpeed::Blazing
        && !spec->no_phase_batch_lock
        && !game.unit_may_queue_non_focus_batch_action_this_phase(actor_->entity_id)) {
        return {false, "That unit already queued an attack or ability this phase", {}};
    }
    if (!entity_can_use_ability(*actor_, *spec)) {
        return {false, "No uses remaining for that ability", {}};
    }
    if (ability_consumes_attack_action(*spec)) {
        const auto unit = std::dynamic_pointer_cast<Unit>(actor_);
        if (!unit) {
            return {false, "Attack-cost abilities require a unit", {}};
        }
        auto attack_vr = validate_unit_attack_budget(*unit, player_id);
        if (!attack_vr.ok) {
            return attack_vr;
        }
    }
    ability_ = *spec;
    if (spec->effect_key == "magus_charge_strike" || spec->effect_key == "magus_charge_surrounding_burst") {
        if (entity_effect_amount(*actor_, "magus_charge") <= 0) {
            return {false, "Requires at least 1 Arcane Charge", {}};
        }
    }
    if (spec->x_cost_energy_type.has_value() && x_amount_ < spec->x_cost_min) {
        return {false, "X must be at least " + std::to_string(spec->x_cost_min), {}};
    }
    if (tactics::effect_uses_directional_aim(spec->effect_key)) {
        int max_range = 5;
        if (const auto it = spec->effect_payload.find("max_range"); it != spec->effect_payload.end()) {
            max_range = it->second;
        }
        const bool allow_diagonals = [&]() {
            const auto it = spec->effect_payload.find("cardinal_only");
            return !(it != spec->effect_payload.end() && it->second != 0);
        }();
        const auto aim_vr = validate_directional_area_damage_ability_target(game, player_id, *actor_, targets_, max_range, allow_diagonals);
        if (!aim_vr.ok) {
            return aim_vr;
        }
    } else if (spec->effect_key == "grant_next_damage_bonus_self") {
        if (!targets_.empty()) {
            return {false, "Overcharge Burst does not take a board target", {}};
        }
    } else if (spec->effect_key == "grant_attack_damage_turn_self"
               || spec->effect_key == "grant_ranged_range_turn_self") {
        if (!targets_.empty()) {
            return {false, "This ability does not take a board target", {}};
        }
    } else if (spec->effect_key == "grant_permanent_stat_growth_self") {
        if (!targets_.empty()) {
            return {false, "Starforged Growth does not take a board target", {}};
        }
    } else if (spec->effect_key == "grant_first_strike_self") {
        if (!targets_.empty()) {
            return {false, "This ability does not take a board target", {}};
        }
    } else if (spec->effect_key == "apply_overload_self") {
        if (!targets_.empty()) {
            return {false, "Volt Surge does not take a board target", {}};
        }
    } else if (spec->effect_key == "copy_allied_spell") {
        if (stack_target_id_.empty()) {
            return {false, "Echo Spell requires a batched spell target (stack <item_id>)", {}};
        }
        if (!spec->x_cost_energy_type.has_value()) {
            return {false, "Echo Spell is missing X-cost configuration", {}};
        }
        if (x_amount_ < spec->x_cost_min) {
            return {false, "X must be at least " + std::to_string(spec->x_cost_min), {}};
        }
        const StackItem* stack_target = game.find_batched_item(stack_target_id_);
        if (!stack_target) {
            return {false, "No batched spell with id '" + stack_target_id_ + "'", {}};
        }
        if (stack_target->source_type != "spell" && stack_target->source_type != "focus_spell") {
            return {false, "Echo Spell can only copy batched spells", {}};
        }
        if (teams_hostile(game, player_id, stack_target->controller_id)) {
            return {false, "Echo Spell can only copy allied spells", {}};
        }
        if (stack_target->batched_spell_total_cost != x_amount_) {
            return {false, "Chosen X must match the target spell's total energy cost (" + std::to_string(stack_target->batched_spell_total_cost) + ")", {}};
        }
    } else if (spec->effect_key == "gas_chain_detonate") {
        const auto xit = targets_.find(effect_keys::kCellX);
        const auto yit = targets_.find(effect_keys::kCellY);
        if (xit == targets_.end() || yit == targets_.end()) {
            return {false, "Gas Detonation requires a target cell", {}};
        }
        const int tx = xit->second;
        const int ty = yit->second;
        if (!game.board.get_square(tx, ty)) {
            return {false, "Target cell is not on the board", {}};
        }
        const auto sq = game.board.get_square(tx, ty);
        const auto* ov = square_overlay_modifier(*sq);
        if (!ov || ov->name != kGasCloudOverlayName) {
            return {false, "Gas Detonation must target a tile with a gas cloud", {}};
        }
        const auto range_los = validate_ranged_damage_ability_target(game, *actor_, *spec, {tx, ty});
        if (!range_los.ok) {
            return range_los;
        }
        const std::string& label = spec->name.empty() ? spec->key : spec->name;
        return {true, "Can activate " + label, {}};
    } else if (effect_key_targets_empty_cell(spec->effect_key)) {
        // Empty-cell placement: requires coordinates but no entity at the destination.
        const auto xit = targets_.find(effect_keys::kCellX);
        const auto yit = targets_.find(effect_keys::kCellY);
        if (xit == targets_.end() || yit == targets_.end()) {
            return {false, "This ability requires a target cell position", {}};
        }
        const int tx = xit->second;
        const int ty = yit->second;
        if (!game.board.get_square(tx, ty)) {
            return {false, "Target cell is not on the board", {}};
        }
        // Range + LOS via the ranged-targeting helper (only fires when uses_ranged_targeting is set).
        const auto range_los = validate_ranged_damage_ability_target(game, *actor_, *spec, {tx, ty});
        if (!range_los.ok) { return range_los; }
        if (game.board.entity_at(tx, ty)) {
            return {false, "Target cell must be empty", {}};
        }
        if (actor_->position) {
            const int range_cap = spec->range_max > 0 ? spec->range_max : 4;
            if (min_chebyshev_entity_to_cell(*actor_, tx, ty) > range_cap) {
                return {false, "Target cell is out of range", {}};
            }
        }
        const std::string& label = spec->name.empty() ? spec->key : spec->name;
        return {true, "Can activate " + label, {}};
    } else {
        TargetDefinition target_def = target_definition_for_effect_key(spec->effect_key);
        target_def.board_target_kind = ability_board_target_kind(*spec);
        target_def.require_target_unit_types = spec->require_target_unit_types;
        if (ability_requires_board_target(*spec)) {
            target_def.domain = TargetDomain::BoardEntityCell;
            target_def.requirement = TargetRequirement::Required;
        }
        const auto target_result =
            validate_targets_against_definition(game, player_id, target_def, targets_, stack_target_id_, spec->effect_key);
        if (!target_result.ok) {
            return target_result;
        }
    }
    if (ability_requires_board_target(*spec) && !effect_uses_directional_aim(spec->effect_key)) {
        const auto xit = targets_.find(effect_keys::kCellX);
        const auto yit = targets_.find(effect_keys::kCellY);
        if (xit != targets_.end() && yit != targets_.end()) {
            const auto target_ent = game.board.entity_at(xit->second, yit->second);
            if (const auto special = validate_custom_ability_target_rules(*actor_, *spec, target_ent)) {
                return *special;
            }
            if (target_ent && !taunt_allows_board_target(game, actor_.get(), player_id, *target_ent)) {
                return {false, "Must target adjacent taunt", {}};
            }
            // validate_ranged_damage_ability_target is a no-op when !ability_uses_ranged_targeting,
            // so call it unconditionally - range/LOS must be enforced for any ability that opts in.
            const auto range_los = validate_ranged_damage_ability_target(game, *actor_, *spec, {xit->second, yit->second});
            if (!range_los.ok) {
                return range_los;
            }
        }
    }
    const bool needs_soul_steal =
        effect_deals_damage(spec->effect_key) && (ability_has_soul_steal(*spec) || has_soul_steal(*actor_));
    auto soul_vr = validate_soul_steal_heal_base_target(game, player_id, targets_, needs_soul_steal);
    if (!soul_vr.ok) {
        return soul_vr;
    }
    const std::string& label = spec->name.empty() ? spec->key : spec->name;
    return {true, "Can activate " + label, {}};
}

ActionResult ActivateAbilityAction::execute(GameState& game) {
    auto vr = validate(game);
    if (!vr.ok) return vr;
    auto& a = *ability_;
    if (a.effect_key == "copy_allied_spell") {
        const StackItem* target = game.find_batched_item(stack_target_id_);
        if (!target) {
            return {false, "Copy target is no longer in the batch queue", {}};
        }
        StackItem copy = *target;
        copy.item_id.clear();
        copy.source_name = target->source_name + " (copy)";
        copy.controller_id = player_id;
        copy.target_stack_item_id.clear();
        copy.multicast_cast_id.clear();
        const auto add_res = game.stack_manager.add_item(game, std::move(copy));
        if (!add_res.ok) {
            return add_res;
        }
        game.apply_passive_reactive_on_owner_spell_played(player_id, target->speed);
        StackItem marker = build_stack_item_from_effect(
            "ability",
            a.name.empty() ? a.key : a.name,
            actor_ ? actor_->entity_id : std::string{},
            player_id,
            a.effect_key,
            a.speed,
            a.effect_payload,
            a.effect_string_payload,
            targets_,
            ability_board_target_kind(a),
            target_entity_id_for_stack_item(game, targets_),
            stack_target_id_,
            a.require_target_unit_types,
            a.bonus_damage_unit_types,
            a.bonus_damage_amount);
        marker.source_ability_key = ability_key_;
        marker.no_phase_batch_lock = a.no_phase_batch_lock;
        const auto marker_res = game.stack_manager.add_item(game, std::move(marker));
        if (!marker_res.ok) {
            return marker_res;
        }
        ActionResult out{true, "Copied allied spell " + target->source_name + " (batched).", {}};
        out.data = add_res.data;
        if (const auto bit = marker_res.data.find("batch_item_id"); bit != marker_res.data.end()) {
            out.data["batch_item_id"] = bit->second;
        }
        return out;
    }
    StackItem item = build_stack_item_from_effect(
        "ability",
        a.name.empty() ? a.key : a.name,
        actor_ ? actor_->entity_id : std::string{},
        player_id,
        a.effect_key,
        a.speed,
        a.effect_payload,
        a.effect_string_payload,
        targets_,
        ability_board_target_kind(a),
        target_entity_id_for_stack_item(game, targets_),
        stack_target_id_,
        a.require_target_unit_types,
        a.bonus_damage_unit_types,
        a.bonus_damage_amount);
    item.source_ability_key = ability_key_;
    item.pierces_damage_prevention = effect_deals_damage(a.effect_key) && (ability_has_pierce(a) || (actor_ && has_pierce(*actor_)));
    // Propagate aura ability-damage bonus: fires for damaging effects and for heals that
    // explicitly opt in via scales_with_ability_damage (e.g. heal_boosted).
    if (actor_ && (effect_deals_damage(a.effect_key) || effect_scales_with_ability_damage(a.effect_key))) {
        item.ability_damage_bonus = actor_->aura_bonus_ability_damage;
    }
    item.heals_on_damage_dealt = effect_deals_damage(a.effect_key) && ability_has_lifesteal(a);
    const bool grants_soul_steal =
        effect_deals_damage(a.effect_key) && (ability_has_soul_steal(a) || (actor_ && has_soul_steal(*actor_)));
    item.heals_allied_base_on_damage_dealt = grants_soul_steal;
    if (grants_soul_steal) {
        if (const auto heal_base = resolve_soul_steal_heal_base(game, player_id, targets_)) {
            item.soul_steal_heal_base_entity_id = heal_base->entity_id;
        }
    }
    item.consumes_attack_action = ability_consumes_attack_action(a);
    item.chain = a.chain;
    item.no_phase_batch_lock = a.no_phase_batch_lock;
    // Doublecast echo: if the actor carries next_ability_doubled, push a Reflex copy of this
    // ability immediately after the original so it fires a second time with the same targets.
    // The echo fires LIFO-first (it's pushed last), then the original fires.
    // echo.consumes_attack_action = false so the attack budget is only spent once.
    const int doubled_stacks = (actor_ && !entity_is_silenced(*actor_))
        ? consume_first_entity_effect(*actor_, "next_ability_doubled")
        : 0;
    StackItem echo;
    if (doubled_stacks > 0) {
        echo = item;
        echo.speed = EffectSpeed::Reflex;        // avoids Channeled stack-non-empty constraint
        echo.consumes_attack_action = false;   // attack budget spent on original only
    }
    auto res = game.stack_manager.add_item(game, std::move(item));
    if (!res.ok) return res;
    if (doubled_stacks > 0) {
        game.stack_manager.add_item(game, std::move(echo));
    }
    ActionResult out{true, "Activated " + (a.name.empty() ? a.key : a.name) + " (batched).", {}};
    out.data = res.data;
    if (res.message.find("[BURST]") != std::string::npos) {
        out.message = res.message;
    }
    return out;
}

}  // namespace tactics
