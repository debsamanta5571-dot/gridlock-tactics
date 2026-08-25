#include "tactics/bot/bot_action.hpp"

#include "tactics/cards/card_catalog.hpp"
#include "tactics/cards/card_runtime.hpp"
#include "tactics/common/effect_keys.hpp"
#include "tactics/core/game_state.hpp"
#include "tactics/energy/energy_zone.hpp"
#include "tactics/entities/entity.hpp"

#include <sstream>

namespace tactics::bot {
namespace {

std::string cell_label(const int x, const int y)
{
    return "(" + std::to_string(x) + "," + std::to_string(y) + ")";
}

std::string entity_display_name(const Entity& ent)
{
    if (!ent.source_card_id.empty()) {
        const CardDefinition* def = try_get_card_definition_ptr(ent.source_card_id);
        if (!def) {
            const auto pos = ent.source_card_id.rfind('_');
            if (pos != std::string::npos && pos > 0) {
                def = try_get_card_definition_ptr(ent.source_card_id.substr(0, pos));
            }
        }
        if (def) {
            return definition_name(*def);
        }
    }
    if (entity_is_base(ent)) {
        return "Base";
    }
    if (const auto* unit = dynamic_cast<const Unit*>(&ent); unit && !unit->unit_type.empty()) {
        return unit->unit_type;
    }
    return ent.entity_id;
}

std::string entity_position_label(const GameState& game, const std::string& entity_id)
{
    const auto it = game.board.all_entities_map.find(entity_id);
    if (it == game.board.all_entities_map.end() || !it->second) {
        return entity_id.empty() ? "?" : entity_id;
    }
    const Entity& ent = *it->second;
    std::string label = entity_display_name(ent);
    if (ent.position) {
        label += " at " + cell_label(ent.position->first, ent.position->second);
    }
    const int max_hp = entity_effective_base_health(ent);
    if (ent.current_health > 0 && max_hp > 0) {
        label += " [" + std::to_string(ent.current_health) + "/" + std::to_string(max_hp) + " HP]";
    }
    return label;
}

std::string entity_at_cell_label(const GameState& game, const int x, const int y)
{
    const std::shared_ptr<Entity> ent = game.board.entity_at(x, y);
    if (!ent) {
        return "empty " + cell_label(x, y);
    }
    std::string label = entity_display_name(*ent);
    if (entity_is_base(*ent)) {
        label += " (base)";
    }
    label += " " + cell_label(x, y);
    if (ent->current_health > 0) {
        label += " [" + std::to_string(ent->current_health) + " HP]";
    }
    return label;
}

std::string card_label_for_instance(const GameState& game, const int player_id, const CardInstanceId card_id)
{
    if (!card_id.is_valid()) {
        return "?";
    }
    const auto deck_it = game.players_decks.find(player_id);
    if (deck_it == game.players_decks.end()) {
        return "?";
    }
    const CardInstance* inst = deck_it->second.pool.try_get(card_id);
    if (!inst) {
        return "?";
    }
    const CardDefinition* def = try_get_card_definition_ptr(inst->definition_id);
    if (!def) {
        return "?";
    }
    return definition_name(*def);
}

std::string card_label_at_hand_index(const GameState& game, const int player_id, const int hand_index_1based)
{
    const auto* hand = game.players_hands.at(player_id);
    const int idx = hand_index_1based - 1;
    if (idx < 0 || static_cast<std::size_t>(idx) >= hand->size()) {
        return "hand#" + std::to_string(hand_index_1based);
    }
    return card_label_for_instance(game, player_id, (*hand)[static_cast<std::size_t>(idx)]);
}

}  // namespace

bool bot_actions_equivalent(const BotAction& a, const BotAction& b)
{
    return a.kind == b.kind && a.player_id == b.player_id && a.card_id == b.card_id && a.entity_id == b.entity_id
        && a.focus_caster_entity_id == b.focus_caster_entity_id && a.stack_target_id == b.stack_target_id
        && a.x == b.x && a.y == b.y && a.ranged == b.ranged && a.ability_key == b.ability_key
        && a.spell_x_amount == b.spell_x_amount && a.spell_mode == b.spell_mode
        && a.energy_zone_index_1based == b.energy_zone_index_1based
        && a.land_ability_index_1based == b.land_ability_index_1based
        && a.hand_index_1based == b.hand_index_1based && a.play_zone == b.play_zone
        && a.spell_targets == b.spell_targets;
}

const char* bot_action_kind_name(const BotActionKind kind)
{
    switch (kind) {
    case BotActionKind::ChooseEnergyZone: return "choose_energy_zone";
    case BotActionKind::SkipEnergyZone: return "skip_energy_zone";
    case BotActionKind::Deploy: return "deploy";
    case BotActionKind::DeployReserve: return "deploy_reserve";
    case BotActionKind::MovePreview: return "move_preview";
    case BotActionKind::MoveConfirm: return "move_confirm";
    case BotActionKind::MoveCancel: return "move_cancel";
    case BotActionKind::MoveRotate: return "move_rotate";
    case BotActionKind::CastSpell: return "cast_spell";
    case BotActionKind::CastSpellReserve: return "cast_spell_reserve";
    case BotActionKind::ActivateAbility: return "activate_ability";
    case BotActionKind::DeclareAttack: return "declare_attack";
    case BotActionKind::AttackUndeclare: return "attack_undeclare";
    case BotActionKind::CommitAttackDeclaration: return "attack_commit";
    case BotActionKind::EndMainPhase: return "end_main";
    case BotActionKind::PassPriority: return "pass";
    case BotActionKind::Defend: return "defend";
    case BotActionKind::Dash: return "dash";
    case BotActionKind::Recover: return "recover";
    case BotActionKind::Undo: return "undo";
    case BotActionKind::BatchCancel: return "batch_cancel";
    case BotActionKind::DiscardHandCard: return "discard";
    case BotActionKind::ScanDiscard: return "scan_discard";
    case BotActionKind::ScanFinish: return "scan_finish";
    case BotActionKind::SkipTerritoryTarget: return "land_target_skip";
    case BotActionKind::ResolveTerritoryTarget: return "land_target";
    case BotActionKind::SkipTerritoryLoot: return "territory_loot_skip";
    case BotActionKind::TerritoryLootDiscard: return "territory_loot_discard";
    case BotActionKind::UseLand: return "use_land";
    case BotActionKind::ResumeCombatViz: return "combat_viz_resume";
    }
    return "unknown";
}

std::string format_bot_action_detail(const tactics::GameState& game, const BotAction& action)
{
    std::ostringstream oss;
    switch (action.kind) {
    case BotActionKind::ChooseEnergyZone:
        oss << "place energy zone choice #" << action.energy_zone_index_1based;
        break;
    case BotActionKind::SkipEnergyZone:
        oss << "skip placing a zone";
        break;
    case BotActionKind::Deploy:
    case BotActionKind::DeployReserve:
        oss << "play " << card_label_for_instance(game, action.player_id, action.card_id)
            << (action.kind == BotActionKind::DeployReserve ? " from reserves" : " from hand")
            << " at " << cell_label(action.x, action.y);
        break;
    case BotActionKind::MovePreview:
        oss << "move " << entity_position_label(game, action.entity_id) << " -> " << cell_label(action.x, action.y);
        break;
    case BotActionKind::MoveConfirm: {
        const std::optional<PendingMoveSelection> pending = game.get_pending_move_for(action.player_id);
        if (pending) {
            oss << "confirm move " << entity_position_label(game, pending->unit_entity_id) << " -> "
                << cell_label(pending->goal_x, pending->goal_y);
        } else {
            oss << "confirm pending move";
        }
        break;
    }
    case BotActionKind::MoveCancel:
        oss << "cancel pending move";
        break;
    case BotActionKind::MoveRotate:
        oss << "rotate pending move " << action.quarter_turns_cw << " quarter-turn(s) CW";
        break;
    case BotActionKind::CastSpell:
    case BotActionKind::CastSpellReserve:
        oss << "cast " << card_label_for_instance(game, action.player_id, action.card_id)
            << (action.kind == BotActionKind::CastSpellReserve ? " from reserves" : "");
        if (!action.focus_caster_entity_id.empty()) {
            oss << " via " << entity_position_label(game, action.focus_caster_entity_id);
        }
        if (const auto xit = action.spell_targets.find(effect_keys::kCellX);
            xit != action.spell_targets.end()) {
            const auto yit = action.spell_targets.find(effect_keys::kCellY);
            const int ty = yit != action.spell_targets.end() ? yit->second : action.y;
            oss << " targeting " << entity_at_cell_label(game, xit->second, ty);
        } else if (action.x != 0 || action.y != 0) {
            oss << " targeting " << entity_at_cell_label(game, action.x, action.y);
        }
        if (action.spell_x_amount > 0) {
            oss << " (X=" << action.spell_x_amount << ")";
        }
        break;
    case BotActionKind::ActivateAbility:
        oss << action.ability_key << " with " << entity_position_label(game, action.entity_id);
        if (const auto xit = action.spell_targets.find(effect_keys::kCellX);
            xit != action.spell_targets.end()) {
            const auto yit = action.spell_targets.find(effect_keys::kCellY);
            const int ty = yit != action.spell_targets.end() ? yit->second : action.y;
            oss << " -> " << entity_at_cell_label(game, xit->second, ty);
        }
        break;
    case BotActionKind::DeclareAttack:
        oss << (action.ranged ? "ranged " : "melee ") << entity_position_label(game, action.entity_id)
            << " attacks " << entity_at_cell_label(game, action.x, action.y);
        break;
    case BotActionKind::AttackUndeclare:
        oss << "undeclare attack from " << entity_position_label(game, action.entity_id);
        break;
    case BotActionKind::CommitAttackDeclaration:
        oss << "lock attack declarations and open defense";
        break;
    case BotActionKind::EndMainPhase:
        oss << "end main phase";
        break;
    case BotActionKind::PassPriority:
        oss << "pass priority";
        break;
    case BotActionKind::Defend:
        oss << entity_position_label(game, action.entity_id) << " defends";
        break;
    case BotActionKind::Dash:
        oss << entity_position_label(game, action.entity_id) << " dashes";
        break;
    case BotActionKind::Recover:
        oss << entity_position_label(game, action.entity_id) << " recovers";
        break;
    case BotActionKind::Undo:
        oss << "undo last action";
        break;
    case BotActionKind::BatchCancel:
        oss << "cancel queued batch item " << action.entity_id;
        break;
    case BotActionKind::DiscardHandCard:
        oss << "discard " << card_label_at_hand_index(game, action.player_id, action.hand_index_1based);
        break;
    case BotActionKind::ScanDiscard:
        oss << "scan discard #" << action.hand_index_1based;
        break;
    case BotActionKind::ScanFinish:
        oss << "scan finish";
        break;
    case BotActionKind::SkipTerritoryTarget:
        oss << "skip territory target";
        break;
    case BotActionKind::ResolveTerritoryTarget:
        oss << "territory target " << entity_at_cell_label(game, action.x, action.y);
        break;
    case BotActionKind::SkipTerritoryLoot:
        oss << "skip territory loot";
        break;
    case BotActionKind::TerritoryLootDiscard:
        oss << "territory loot discard " << card_label_at_hand_index(game, action.player_id, action.hand_index_1based);
        break;
    case BotActionKind::UseLand: {
        std::string land = "territory #" + std::to_string(action.energy_zone_index_1based);
        if (const auto zit = game.players_energy_zones.find(action.player_id); zit != game.players_energy_zones.end()) {
            const int ti = action.energy_zone_index_1based - 1;
            if (ti >= 0 && ti < static_cast<int>(zit->second.size())) {
                const tactics::EnergyZone& z = zit->second[static_cast<std::size_t>(ti)];
                land = z.name;
                const int ai = action.land_ability_index_1based - 1;
                if (ai >= 0 && ai < static_cast<int>(z.land_abilities.size()) && !z.land_abilities[static_cast<std::size_t>(ai)].name.empty()) {
                    land += " - " + z.land_abilities[static_cast<std::size_t>(ai)].name;
                }
            }
        }
        oss << "use land " << land;
        if (action.x != 0 || action.y != 0 || !action.spell_targets.empty()) {
            oss << " on " << entity_at_cell_label(game, action.x, action.y);
        }
        break;
    }
    case BotActionKind::ResumeCombatViz:
        oss << "resume combat visualization";
        break;
    }
    return oss.str();
}

}  // namespace tactics::bot