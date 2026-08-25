#include "tactics/bot/legal_action_generator.hpp"

#include "tactics/actions/actions.hpp"
#include "tactics/actions/board_targeting.hpp"
#include "tactics/actions/move_resolution.hpp"
#include "tactics/board/board_layout.hpp"
#include "tactics/cards/ability_catalog.hpp"
#include "tactics/cards/card_runtime.hpp"
#include "tactics/cards/focus_spell.hpp"
#include "tactics/common/effect_keys.hpp"
#include "tactics/combat/combat_resolver.hpp"
#include "tactics/combat/directional_area.hpp"
#include "tactics/core/board_target_policy.hpp"
#include "tactics/effects/effect_traits.hpp"
#include "tactics/entities/entity.hpp"
#include "tactics/energy/energy_zone.hpp"

#include <algorithm>
#include <set>

namespace tactics::bot {
namespace {

/** True if max is unlimited (0) or count is still below the cap. */
bool under_limit(const std::size_t count, const std::size_t max)
{
    return max == 0 || count < max;
}

std::shared_ptr<Unit> find_unit_by_id(const GameState& game, const std::string& entity_id)
{
    const auto it = game.board.all_entities_map.find(entity_id);
    if (it == game.board.all_entities_map.end()) {
        return nullptr;
    }
    return std::dynamic_pointer_cast<Unit>(it->second);
}

std::vector<int> sample_x_amounts(const GameState& game, const int player_id, const EnergyType energy_type,
    const int x_min)
{
    std::vector<int> out;
    if (x_min <= 0) {
        return out;
    }
    out.push_back(x_min);
    for (int extra = 1; extra <= 2; ++extra) {
        const int candidate = x_min + extra;
        std::map<EnergyType, int> cost{{energy_type, candidate}};
        if (game.turn_manager.can_afford(game, player_id, cost)) {
            out.push_back(candidate);
        }
    }
    return out;
}

std::shared_ptr<Entity> resolve_entity_ptr(const GameState& game, const std::string& entity_id)
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

bool spell_cast_probe(GameState& game, const CardInstanceId cid, const int player_id,
    const std::shared_ptr<Entity>& focus_caster, const CardPlayZone zone, const std::map<std::string, int>& targets,
    const int x_amount, const std::vector<std::map<std::string, int>>& multicast_targets,
    const std::string& stack_target_id, const int mode_index = -1)
{
    CastSpellAction probe(cid, player_id, targets, stack_target_id, focus_caster, zone);
    if (x_amount > 0) {
        probe.set_x_amount(x_amount);
    }
    if (mode_index >= 0) {
        probe.set_mode_index(mode_index);
    }
    if (!multicast_targets.empty()) {
        probe.set_multicast_targets(multicast_targets);
    }
    const auto cost = probe.get_cost(game);
    if (!cost.empty() && !game.turn_manager.can_afford(game, player_id, cost)) {
        return false;
    }
    return probe.validate(game).ok;
}

void push_spell_action(std::vector<BotAction>& out, const CardInstanceId cid, const int player_id, const CardPlayZone zone,
    const std::map<std::string, int>& targets, const int x_amount,
    const std::vector<std::map<std::string, int>>& multicast_targets, const std::string& focus_caster_entity_id,
    const std::string& stack_target_id, const int mode_index = -1)
{
    BotAction action;
    action.kind = zone == CardPlayZone::Reserves ? BotActionKind::CastSpellReserve : BotActionKind::CastSpell;
    action.player_id = player_id;
    action.card_id = cid;
    action.play_zone = zone;
    action.spell_targets = targets;
    action.spell_x_amount = x_amount;
    action.spell_mode = mode_index;
    action.multicast_spell_targets = multicast_targets;
    action.focus_caster_entity_id = focus_caster_entity_id;
    action.stack_target_id = stack_target_id;
    if (const auto xit = targets.find(effect_keys::kCellX); xit != targets.end()) {
        action.x = xit->second;
    }
    if (const auto yit = targets.find(effect_keys::kCellY); yit != targets.end()) {
        action.y = yit->second;
    }
    out.push_back(action);
}

bool try_push_spell(GameState& game, std::vector<BotAction>& out, const CardInstanceId cid, const int player_id,
    const CardPlayZone zone, const std::map<std::string, int>& targets, const int x_amount,
    const std::vector<std::map<std::string, int>>& multicast_targets, const std::string& focus_caster_entity_id,
    const std::string& stack_target_id, const std::size_t max_spell_actions)
{
    if (!under_limit(out.size(), max_spell_actions)) {
        return false;
    }
    if (!spell_cast_probe(game, cid, player_id, resolve_entity_ptr(game, focus_caster_entity_id), zone, targets,
            x_amount, multicast_targets, stack_target_id)) {
        return false;
    }
    push_spell_action(out, cid, player_id, zone, targets, x_amount, multicast_targets, focus_caster_entity_id,
        stack_target_id);
    return true;
}

std::vector<std::pair<int, int>> collect_highlight_cells(const BoardTargetHighlightCells& highlights)
{
    std::vector<std::pair<int, int>> cells;
    cells.reserve(highlights.enemy_cells.size() + highlights.other_cells.size());
    for (const auto& cell : highlights.enemy_cells) {
        cells.push_back(cell);
    }
    for (const auto& cell : highlights.other_cells) {
        cells.push_back(cell);
    }
    return cells;
}

std::vector<std::vector<std::pair<int, int>>> sample_multicast_cell_sets(
    const std::vector<std::pair<int, int>>& cells, const int multicast)
{
    std::vector<std::vector<std::pair<int, int>>> out;
    if (cells.empty() || multicast <= 1) {
        return out;
    }
    std::vector<std::pair<int, int>> distinct;
    distinct.reserve(cells.size());
    std::set<std::string> seen;
    for (const auto& [wx, wy] : cells) {
        const std::string sig = std::to_string(wx) + "," + std::to_string(wy);
        if (!seen.insert(sig).second) {
            continue;
        }
        distinct.push_back({wx, wy});
    }
    if (distinct.empty()) {
        return out;
    }
    const int take = std::min(multicast, static_cast<int>(distinct.size()));
    std::vector<std::pair<int, int>> prefix(distinct.begin(), distinct.begin() + take);
    out.push_back(prefix);
    if (distinct.size() >= 2 && multicast >= 2) {
        std::vector<std::pair<int, int>> alt{distinct[1], distinct[0]};
        if (static_cast<int>(alt.size()) < multicast) {
            alt.resize(static_cast<size_t>(multicast), distinct[0]);
        } else if (static_cast<int>(alt.size()) > multicast) {
            alt.resize(static_cast<size_t>(multicast));
        }
        out.push_back(alt);
    }
    return out;
}

void append_stack_target_spell_actions(GameState& game, const int player_id, const CardPlayZone zone,
    const CardInstanceId cid, const CardDefinition& def, std::vector<BotAction>& out, const std::size_t max_spell_actions)
{
    for (const auto& entry : game.attack_phase_queue()) {
        if (entry.is_attack) {
            continue;
        }
        const std::string& item_id = entry.spell_item.item_id;
        if (item_id.empty()) {
            continue;
        }
        const std::map<std::string, int> empty_targets;
        (void)try_push_spell(game, out, cid, player_id, zone, empty_targets, 0, {}, {}, item_id, max_spell_actions);
        if (!under_limit(out.size(), max_spell_actions)) {
            return;
        }
    }
    (void)def;
}

void append_player_seat_spell_actions(GameState& game, const int player_id, const CardPlayZone zone,
    const CardInstanceId cid, std::vector<BotAction>& out, const std::size_t max_spell_actions)
{
    for (const int seat : game.turn_manager.players) {
        const std::map<std::string, int> targets{{effect_keys::kTargetPlayerSeat, seat}};
        if (!try_push_spell(game, out, cid, player_id, zone, targets, 0, {}, {}, {}, max_spell_actions)) {
            continue;
        }
        if (!under_limit(out.size(), max_spell_actions)) {
            return;
        }
    }
}

void append_push_direction_spell_actions(GameState& game, const int player_id, const CardPlayZone zone,
    const CardInstanceId cid, const CardDefinition& def, std::vector<BotAction>& out, const std::size_t max_spell_actions)
{
    const SpellCardDefinition& spell = definition_spell(def);
    const BoardTargetKind target_kind = spell_board_target_kind(def);
    const auto entity_cells =
        gather_push_direction_spell_entity_cells(game, player_id, spell.effect_key, target_kind, spell.require_target_unit_types);
    const std::vector<int> x_samples = spell.x_cost_energy_type.has_value()
        ? sample_x_amounts(game, player_id, *spell.x_cost_energy_type, spell.x_cost_min)
        : std::vector<int>{0};

    for (const auto& bucket : {entity_cells.enemy_cells, entity_cells.other_cells}) {
        for (const auto& [tx, ty] : bucket) {
            const auto aim_cells = gather_push_direction_indicator_cells_for_target(game, tx, ty, spell.effect_payload);
            for (const auto& [ax, ay] : aim_cells.other_cells) {
                const std::map<std::string, int> targets{
                    {effect_keys::kCellX, tx},
                    {effect_keys::kCellY, ty},
                    {effect_keys::kAimX, ax},
                    {effect_keys::kAimY, ay},
                };
                for (const int x_amount : x_samples) {
                    if (try_push_spell(game, out, cid, player_id, zone, targets, x_amount, {}, {}, {}, max_spell_actions)) {
                        break;
                    }
                }
                if (!under_limit(out.size(), max_spell_actions)) {
                    return;
                }
            }
        }
    }
}

void append_focus_caster_spell_actions(GameState& game, const int player_id, const CardPlayZone zone,
    const CardInstanceId cid, const CardDefinition& def, std::vector<BotAction>& out, const std::size_t max_spell_actions)
{
    const SpellCardDefinition& spell = definition_spell(def);
    const BoardTargetKind target_kind = spell_board_target_kind(def);
    const std::vector<int> x_samples = spell.x_cost_energy_type.has_value()
        ? sample_x_amounts(game, player_id, *spell.x_cost_energy_type, spell.x_cost_min)
        : std::vector<int>{0};
    const int multicast = definition_multicast_amount(def);
    const bool per_copy = definition_spell_multicast_requires_per_copy_targets(def);

    game.board.for_each_entity([&](const std::shared_ptr<Entity>& ent_ptr) {
        if (!under_limit(out.size(), max_spell_actions)) {
            return;
        }
        const auto caster_unit = std::dynamic_pointer_cast<Unit>(ent_ptr);
        if (!caster_unit || !caster_unit->owner || *caster_unit->owner != player_id) {
            return;
        }
        if (spell_requires_focus_caster(def)) {
            if (!entity_valid_focus_spell_caster(*caster_unit)) {
                return;
            }
        } else if (!spell_requires_forced_damage_spell_focus_caster(game, player_id, def)
            || !cast_uses_forced_damage_spell_focus_caster(game, def, caster_unit)) {
            return;
        }

        const auto probe = [&](const int wx, const int wy) {
            return spell_probe_valid(game, cid, player_id, ent_ptr, zone, wx, wy);
        };
        const BoardTargetHighlightCells highlights = gather_spell_board_target_cells(
            game, caster_unit, player_id, spell.effect_key, spell.focus_range, target_kind, spell.effect_payload,
            spell.effect_string_payload, probe);
        const auto cells = collect_highlight_cells(highlights);

        if (per_copy && multicast > 1) {
            for (const auto& cell_set : sample_multicast_cell_sets(cells, multicast)) {
                std::vector<std::map<std::string, int>> multicast_targets;
                multicast_targets.reserve(cell_set.size());
                for (const auto& [wx, wy] : cell_set) {
                    multicast_targets.push_back({{effect_keys::kCellX, wx}, {effect_keys::kCellY, wy}});
                }
                const std::map<std::string, int> primary = multicast_targets.front();
                for (const int x_amount : x_samples) {
                    if (try_push_spell(game, out, cid, player_id, zone, primary, x_amount, multicast_targets,
                            ent_ptr->entity_id, {}, max_spell_actions)) {
                        break;
                    }
                }
            }
            return;
        }

        for (const auto& [wx, wy] : cells) {
            const std::map<std::string, int> targets{{effect_keys::kCellX, wx}, {effect_keys::kCellY, wy}};
            for (const int x_amount : x_samples) {
                if (try_push_spell(game, out, cid, player_id, zone, targets, x_amount, {}, ent_ptr->entity_id, {},
                        max_spell_actions)) {
                    break;
                }
            }
            if (!under_limit(out.size(), max_spell_actions)) {
                return;
            }
        }
    });
}

void append_standard_spell_actions(GameState& game, const int player_id, const CardPlayZone zone, const CardInstanceId cid,
    const CardDefinition& def, std::vector<BotAction>& out, const std::size_t max_spell_actions)
{
    const SpellCardDefinition& spell = definition_spell(def);
    const BoardTargetKind target_kind = spell_board_target_kind(def);
    const std::vector<int> x_samples = spell.x_cost_energy_type.has_value()
        ? sample_x_amounts(game, player_id, *spell.x_cost_energy_type, spell.x_cost_min)
        : std::vector<int>{0};
    const int multicast = definition_multicast_amount(def);
    const bool per_copy = definition_spell_multicast_requires_per_copy_targets(def);

    const auto probe = [&](const int wx, const int wy) {
        return spell_probe_valid(game, cid, player_id, nullptr, zone, wx, wy);
    };
    const BoardTargetHighlightCells highlights = gather_spell_board_target_cells(
        game, nullptr, player_id, spell.effect_key, spell.focus_range, target_kind, spell.effect_payload,
        spell.effect_string_payload, probe);
    const auto cells = collect_highlight_cells(highlights);

    if (per_copy && multicast > 1) {
        for (const auto& cell_set : sample_multicast_cell_sets(cells, multicast)) {
            std::vector<std::map<std::string, int>> multicast_targets;
            multicast_targets.reserve(cell_set.size());
            for (const auto& [wx, wy] : cell_set) {
                multicast_targets.push_back({{effect_keys::kCellX, wx}, {effect_keys::kCellY, wy}});
            }
            const std::map<std::string, int> primary = multicast_targets.front();
            for (const int x_amount : x_samples) {
                if (try_push_spell(game, out, cid, player_id, zone, primary, x_amount, multicast_targets, {}, {},
                        max_spell_actions)) {
                    break;
                }
            }
            if (!under_limit(out.size(), max_spell_actions)) {
                return;
            }
        }
        return;
    }

    for (const auto& [wx, wy] : cells) {
        const std::map<std::string, int> targets{{effect_keys::kCellX, wx}, {effect_keys::kCellY, wy}};
        for (const int x_amount : x_samples) {
            if (try_push_spell(game, out, cid, player_id, zone, targets, x_amount, {}, {}, {}, max_spell_actions)) {
                break;
            }
        }
        if (!under_limit(out.size(), max_spell_actions)) {
            return;
        }
    }
}

std::vector<std::pair<int, int>> deploy_anchors_for_entity(const GameState& game, const int player_id,
    const std::shared_ptr<Entity>& entity, const int unit_cost)
{
    std::vector<std::pair<int, int>> anchors;
    if (!entity) {
        return anchors;
    }

    // Candidate anchor cells: the home deploy zone, PLUS every cell adjacent to a friendly
    // unit. The adjacency set is what lets structures deploy *forward* (the engine allows a
    // structure adjacent to a friendly unit, and Command-keyword units likewise) - without
    // it a spearhead depot like Starforged Accumulator can never be placed near the enemy
    // base and the bot would only ever deploy in its back zone. can_deploy_entity_at applies
    // the actual rules (zone / adjacency / Command / spearhead); we just supply the cells.
    std::set<std::pair<int, int>> candidates;
    if (const std::optional<BoardRectZone> zone = deploy_zone_for_player(game.board_layout(), player_id)) {
        for (int dy = 0; dy < zone->height; ++dy) {
            for (int dx = 0; dx < zone->width; ++dx) {
                candidates.insert({zone->anchor_x + dx, zone->anchor_y + dy});
            }
        }
    }
    for (const auto& [_, ally] : game.board.all_entities_map) {
        if (!ally || !ally->owner || ally->current_health <= 0 || !entity_is_board_unit(*ally)) {
            continue;
        }
        if (teams_hostile(game, player_id, *ally->owner)) {
            continue;
        }
        for (int oy = -1; oy <= 1; ++oy) {
            for (int ox = -1; ox <= 1; ++ox) {
                if (ally->position) {
                    const auto [bx, by] = *ally->position;
                    for (const auto& [sx, sy] : entity_shape_offsets(*ally)) {
                        candidates.insert({bx + sx + ox, by + sy + oy});
                    }
                } else {
                    for (const auto& [cx, cy] : ally->occupied_positions) {
                        candidates.insert({cx + ox, cy + oy});
                    }
                }
            }
        }
    }

    for (const auto& [ax, ay] : candidates) {
        if (!game.can_deploy_entity_at(player_id, entity, ax, ay, unit_cost)) {
            continue;
        }
        if (!game.board.can_place_entity_at(entity, ax, ay)) {
            continue;
        }
        anchors.push_back({ax, ay});
    }
    return anchors;
}

void append_deploy_actions(GameState& game, const int player_id, const CardPlayZone zone,
    const std::vector<CardInstanceId>& cards, std::vector<BotAction>& out)
{
    const auto deck_it = game.players_decks.find(player_id);
    if (deck_it == game.players_decks.end()) {
        return;
    }
    Deck& deck = deck_it->second;
    for (std::size_t i = 0; i < cards.size(); ++i) {
        const CardInstanceId cid = cards[i];
        const CardInstance* inst = deck.pool.try_get(cid);
        if (!inst) {
            continue;
        }
        const CardDefinition* def = try_get_card_definition_ptr(inst->definition_id);
        if (!def || !definition_is_unit(*def)) {
            continue;
        }
        std::string play_reason;
        if (zone == CardPlayZone::Reserves) {
            if (!deck.can_play_card_from_reserves(cid, &play_reason)) {
                continue;
            }
        } else if (!deck.can_play_card_now(cid, &play_reason)) {
            continue;
        }
        std::string exalted_reason;
        if (!exalted_requirement_met(game, player_id, *def, &exalted_reason)) {
            continue;
        }
        auto temp = create_unit_from_definition(*def, *inst, player_id, "bot_deploy_probe");
        if (!temp) {
            continue;
        }
        const int unit_cost = definition_total_energy_cost(*def);
        const auto anchors = deploy_anchors_for_entity(game, player_id, temp, unit_cost);
        for (const auto& [ax, ay] : anchors) {
            DeployAction probe(cid, player_id, {ax, ay}, zone);
            // Affordability gate - mirror perform_action: only offer a deploy the player
            // can actually pay for (respecting discounts and tagged energy pools). Without
            // this the bot proposes unpayable deploys that fail and fall back to end_main.
            const auto cost = probe.get_cost(game);
            if (!cost.empty() && !game.turn_manager.can_afford(game, player_id, cost, probe.action_type)) {
                continue;
            }
            if (!probe.validate(game).ok) {
                continue;
            }
            BotAction action;
            action.kind = zone == CardPlayZone::Reserves ? BotActionKind::DeployReserve : BotActionKind::Deploy;
            action.player_id = player_id;
            action.card_id = cid;
            action.play_zone = zone;
            action.x = ax;
            action.y = ay;
            out.push_back(action);
        }
    }
}

/** True when the player has a pending (previewed, not-yet-confirmed) move at all.
 *  ANY non-move action is illegal until this is cleared, so it gates the rest of
 *  the action list. This must NOT depend on whether the moving unit is still
 *  alive: a pending move whose unit died still blocks the engine, and the only
 *  way out is to cancel it. */
bool has_pending_move(const GameState& game, const int player_id)
{
    return game.get_pending_move_for(player_id).has_value();
}

/** True when a pending move exists AND its unit is alive, so confirm/rotate make
 *  sense (a dead unit's move can only be cancelled). */
bool pending_move_is_actionable(const GameState& game, const int player_id)
{
    const std::optional<PendingMoveSelection> pending = game.get_pending_move_for(player_id);
    if (!pending) {
        return false;
    }
    const auto it = game.board.all_entities_map.find(pending->unit_entity_id);
    return it != game.board.all_entities_map.end() && it->second && it->second->current_health > 0;
}

void append_move_actions(const GameState& game, const int player_id, std::vector<BotAction>& out,
    const std::size_t max_move_actions, const bool skip_move_actions)
{
    if (skip_move_actions) {
        return;
    }
    if (has_pending_move(game, player_id)) {
        // Cancel is always available to clear the pending move (even if the unit died).
        BotAction cancel;
        cancel.kind = BotActionKind::MoveCancel;
        cancel.player_id = player_id;
        out.push_back(cancel);

        // Confirm/rotate only make sense while the unit is alive.
        if (pending_move_is_actionable(game, player_id)) {
            BotAction confirm;
            confirm.kind = BotActionKind::MoveConfirm;
            confirm.player_id = player_id;
            out.push_back(confirm);

            BotAction rotate_cw;
            rotate_cw.kind = BotActionKind::MoveRotate;
            rotate_cw.player_id = player_id;
            rotate_cw.quarter_turns_cw = 1;
            out.push_back(rotate_cw);
        }
        return;
    }

    for (const auto& [id, ent] : game.board.all_entities_map) {
        (void)id;
        if (!under_limit(out.size(), max_move_actions)) {
            return;
        }
        auto unit = std::dynamic_pointer_cast<Unit>(ent);
        if (!unit || !unit->owner || *unit->owner != player_id) {
            continue;
        }
        if (!entity_is_board_unit(*unit)) {
            continue;
        }
        if (!game.unit_may_move_this_phase(unit->entity_id)) {
            continue;
        }
        const auto goals = gather_reachable_move_goal_cells(game, unit);
        for (const auto& [gx, gy] : goals) {
            if (!under_limit(out.size(), max_move_actions)) {
                return;
            }
            BotAction action;
            action.kind = BotActionKind::MovePreview;
            action.player_id = player_id;
            action.entity_id = unit->entity_id;
            action.x = gx;
            action.y = gy;
            out.push_back(action);
        }
    }
}

void append_attack_actions(GameState& game, const int player_id, std::vector<BotAction>& out)
{
    for (const auto& [id, ent] : game.board.all_entities_map) {
        (void)id;
        auto unit = std::dynamic_pointer_cast<Unit>(ent);
        if (!unit || !unit->owner || *unit->owner != player_id) {
            continue;
        }
        if (!entity_may_attack_or_activate_abilities(*unit)) {
            continue;
        }
        if (!game.unit_may_queue_non_focus_batch_action_this_phase(unit->entity_id)) {
            continue;
        }
        const auto cells = gather_attackable_goal_cells(game, unit, player_id);
        for (const auto& [tx, ty] : cells) {
            bool melee_ok = validate_attack(game, unit, player_id, {tx, ty}, false).ok;
            bool ranged_ok = false;
            if (unit->attack_type == AttackType::Ranged || unit->attack_type == AttackType::Hybrid) {
                ranged_ok = validate_attack(game, unit, player_id, {tx, ty}, true).ok;
            }
            if (melee_ok) {
                BotAction action;
                action.kind = BotActionKind::DeclareAttack;
                action.player_id = player_id;
                action.entity_id = unit->entity_id;
                action.x = tx;
                action.y = ty;
                action.ranged = false;
                out.push_back(action);
            }
            if (ranged_ok) {
                BotAction action;
                action.kind = BotActionKind::DeclareAttack;
                action.player_id = player_id;
                action.entity_id = unit->entity_id;
                action.x = tx;
                action.y = ty;
                action.ranged = true;
                out.push_back(action);
            }
        }
    }
}

/** Generate cast actions for a modal spell: one per mode. Untargeted modes emit a single
 *  cast; targeted modes probe every living entity's cell (the cast validator filters to the
 *  mode's legal targets). Each action carries `spell_mode` so the executor casts that mode. */
void append_modal_spell_actions(GameState& game, const int player_id, const CardPlayZone zone,
    const CardInstanceId cid, const CardDefinition& def, std::vector<BotAction>& out, const std::size_t max_spell_actions)
{
    const SpellCardDefinition& spell = definition_spell(def);
    for (std::size_t mi = 0; mi < spell.modes.size(); ++mi) {
        if (!under_limit(out.size(), max_spell_actions)) {
            return;
        }
        const int mode_index = static_cast<int>(mi);
        if (!spell.modes[mi].requires_board_target) {
            const std::map<std::string, int> no_targets;
            if (spell_cast_probe(game, cid, player_id, nullptr, zone, no_targets, 0, {}, std::string{}, mode_index)) {
                push_spell_action(out, cid, player_id, zone, no_targets, 0, {}, std::string{}, std::string{}, mode_index);
            }
            continue;
        }
        for (const auto& [_, ent] : game.board.all_entities_map) {
            if (!under_limit(out.size(), max_spell_actions)) {
                return;
            }
            if (!ent || ent->current_health <= 0) {
                continue;
            }
            std::pair<int, int> cell;
            if (ent->position) {
                cell = *ent->position;
            } else if (!ent->occupied_positions.empty()) {
                cell = ent->occupied_positions.front();
            } else {
                continue;
            }
            std::map<std::string, int> targets;
            targets[effect_keys::kCellX] = cell.first;
            targets[effect_keys::kCellY] = cell.second;
            if (spell_cast_probe(game, cid, player_id, nullptr, zone, targets, 0, {}, std::string{}, mode_index)) {
                push_spell_action(out, cid, player_id, zone, targets, 0, {}, std::string{}, std::string{}, mode_index);
            }
        }
    }
}

void append_spell_actions(GameState& game, const int player_id, const CardPlayZone zone, std::vector<BotAction>& out,
    const std::size_t max_spell_actions)
{
    const auto deck_it = game.players_decks.find(player_id);
    if (deck_it == game.players_decks.end()) {
        return;
    }
    Deck& deck = deck_it->second;
    const std::vector<CardInstanceId>& cards =
        zone == CardPlayZone::Reserves ? deck.reserves : *game.players_hands.at(player_id);

    for (const CardInstanceId cid : cards) {
        if (!under_limit(out.size(), max_spell_actions)) {
            return;
        }
        const CardInstance* inst = deck.pool.try_get(cid);
        if (!inst) {
            continue;
        }
        const CardDefinition* def = try_get_card_definition_ptr(inst->definition_id);
        if (!def || !definition_is_spell(*def)) {
            continue;
        }
        std::string play_reason;
        if (zone == CardPlayZone::Reserves) {
            if (!deck.can_play_card_from_reserves(cid, &play_reason)) {
                continue;
            }
        } else if (!deck.can_play_card_now(cid, &play_reason)) {
            continue;
        }
        std::string exalted_reason;
        if (!exalted_requirement_met(game, player_id, *def, &exalted_reason)) {
            continue;
        }

        if (definition_spell_requires_stack_target(*def)) {
            append_stack_target_spell_actions(game, player_id, zone, cid, *def, out, max_spell_actions);
            continue;
        }
        if (definition_spell_requires_player_seat_target(*def)) {
            append_player_seat_spell_actions(game, player_id, zone, cid, out, max_spell_actions);
            continue;
        }
        const SpellCardDefinition& spell = definition_spell(*def);
        if (!spell.modes.empty()) {
            append_modal_spell_actions(game, player_id, zone, cid, *def, out, max_spell_actions);
            continue;
        }
        if (effect_key_uses_push_direction_aim(spell.effect_key)) {
            append_push_direction_spell_actions(game, player_id, zone, cid, *def, out, max_spell_actions);
            continue;
        }
        if (spell_requires_focus_caster(*def) || spell_requires_forced_damage_spell_focus_caster(game, player_id, *def)) {
            append_focus_caster_spell_actions(game, player_id, zone, cid, *def, out, max_spell_actions);
            continue;
        }
        append_standard_spell_actions(game, player_id, zone, cid, *def, out, max_spell_actions);
    }
}

bool ability_cast_probe(GameState& game, const std::shared_ptr<Unit>& unit, const int player_id,
    const std::string& ability_key, const std::map<std::string, int>& targets, const int x_amount,
    const std::string& stack_target_id)
{
    ActivateAbilityAction probe(unit, player_id, ability_key, targets, stack_target_id);
    if (x_amount > 0) {
        probe.set_x_amount(x_amount);
    }
    const auto cost = probe.get_cost(game);
    if (!cost.empty() && !game.turn_manager.can_afford(game, player_id, cost)) {
        return false;
    }
    return probe.validate(game).ok;
}

void push_ability_action(std::vector<BotAction>& out, const int player_id, const std::string& entity_id,
    const std::string& ability_key, const std::map<std::string, int>& targets, const int x_amount,
    const std::string& stack_target_id)
{
    BotAction action;
    action.kind = BotActionKind::ActivateAbility;
    action.player_id = player_id;
    action.entity_id = entity_id;
    action.ability_key = ability_key;
    action.spell_targets = targets;
    action.spell_x_amount = x_amount;
    action.stack_target_id = stack_target_id;
    if (const auto xit = targets.find(effect_keys::kCellX); xit != targets.end()) {
        action.x = xit->second;
    }
    if (const auto yit = targets.find(effect_keys::kCellY); yit != targets.end()) {
        action.y = yit->second;
    }
    out.push_back(action);
}

void append_ability_actions(GameState& game, const int player_id, std::vector<BotAction>& out,
    const std::size_t max_ability_actions)
{
    for (const auto& [id, ent] : game.board.all_entities_map) {
        (void)id;
        if (!under_limit(out.size(), max_ability_actions)) {
            return;
        }
        auto unit = std::dynamic_pointer_cast<Unit>(ent);
        if (!unit || !unit->owner || *unit->owner != player_id) {
            continue;
        }
        if (!entity_may_attack_or_activate_abilities(*unit)) {
            continue;
        }
        if (!game.unit_may_queue_non_focus_batch_action_this_phase(unit->entity_id)) {
            continue;
        }
        for (const auto& ability : unit->activated_abilities) {
            AbilitySpec spec;
            if (!try_get_ability_from_catalog(ability.key, spec)) {
                spec = ability;
            }

            if (spec.effect_key == "copy_allied_spell") {
                for (const auto& entry : game.attack_phase_queue()) {
                    if (entry.is_attack) {
                        continue;
                    }
                    const StackItem& item = entry.spell_item;
                    if (item.item_id.empty() || teams_hostile(game, player_id, item.controller_id)) {
                        continue;
                    }
                    if (item.source_type != "spell" && item.source_type != "focus_spell") {
                        continue;
                    }
                    const int x_amount = item.batched_spell_total_cost;
                    if (ability_cast_probe(game, unit, player_id, ability.key, {}, x_amount, item.item_id)) {
                        push_ability_action(out, player_id, unit->entity_id, ability.key, {}, x_amount, item.item_id);
                    }
                    if (!under_limit(out.size(), max_ability_actions)) {
                        return;
                    }
                }
                continue;
            }

            const std::vector<int> x_samples = spec.x_cost_energy_type.has_value()
                ? sample_x_amounts(game, player_id, *spec.x_cost_energy_type, spec.x_cost_min)
                : std::vector<int>{0};

            if (!ability_requires_board_target(spec) && !effect_uses_directional_aim(spec.effect_key)
                && !effect_key_targets_empty_cell(spec.effect_key)) {
                for (const int x_amount : x_samples) {
                    if (ability_cast_probe(game, unit, player_id, ability.key, {}, x_amount, {})) {
                        push_ability_action(out, player_id, unit->entity_id, ability.key, {}, x_amount, {});
                        break;
                    }
                }
                if (!under_limit(out.size(), max_ability_actions)) {
                    return;
                }
                continue;
            }

            const BoardTargetHighlightCells highlights =
                gather_ability_board_target_cells(game, unit, player_id, ability.key);
            for (const auto& bucket : {highlights.enemy_cells, highlights.other_cells}) {
                for (const auto& [wx, wy] : bucket) {
                    const std::map<std::string, int> targets{{effect_keys::kCellX, wx}, {effect_keys::kCellY, wy}};
                    for (const int x_amount : x_samples) {
                        if (ability_cast_probe(game, unit, player_id, ability.key, targets, x_amount, {})) {
                            push_ability_action(out, player_id, unit->entity_id, ability.key, targets, x_amount, {});
                            break;
                        }
                    }
                    if (!under_limit(out.size(), max_ability_actions)) {
                        return;
                    }
                }
            }
        }
    }
}

bool entity_anchor_cell(const Entity& ent, int& x, int& y)
{
    if (ent.position) {
        x = ent.position->first;
        y = ent.position->second;
        return true;
    }
    if (!ent.occupied_positions.empty()) {
        x = ent.occupied_positions.front().first;
        y = ent.occupied_positions.front().second;
        return true;
    }
    return false;
}

bool entity_is_territory_effect_target(const GameState& game, const int player_id, const Entity& ent,
    const TerritoryEffect& eff)
{
    if (ent.current_health <= 0) {
        return false;
    }
    if (!board_target_allows(game, eff.board_target_kind, player_id, ent)) {
        return false;
    }
    return board_target_entity_allowed_for_effect(ent, eff.board_target_kind, eff.effect_key);
}

void append_territory_effect_target_actions(GameState& game, const int player_id, const TerritoryEffect& eff,
    const BotActionKind kind, const int territory_index_1based, const int ability_index_1based,
    std::vector<BotAction>& out)
{
    constexpr int kMaxTargets = 12;
    int added = 0;
    for (const auto& [id, ent] : game.board.all_entities_map) {
        (void)id;
        if (!ent || !entity_is_territory_effect_target(game, player_id, *ent, eff)) {
            continue;
        }
        int x = 0;
        int y = 0;
        if (!entity_anchor_cell(*ent, x, y)) {
            continue;
        }
        BotAction action;
        action.kind = kind;
        action.player_id = player_id;
        action.entity_id = ent->entity_id;
        action.x = x;
        action.y = y;
        action.spell_targets = {{effect_keys::kCellX, x}, {effect_keys::kCellY, y}};
        action.energy_zone_index_1based = territory_index_1based;
        action.land_ability_index_1based = ability_index_1based;
        out.push_back(std::move(action));
        if (++added >= kMaxTargets) {
            break;
        }
    }
}

void append_use_land_actions(GameState& game, const int player_id, std::vector<BotAction>& out)
{
    const auto it = game.players_energy_zones.find(player_id);
    if (it == game.players_energy_zones.end()) {
        return;
    }
    const auto cp = game.turn_manager.current_player();
    const bool own_main = cp && *cp == player_id
        && (game.turn_manager.current_phase == TurnPhase::Main
            || game.turn_manager.current_phase == TurnPhase::SecondMain);
    for (int ti = 0; ti < static_cast<int>(it->second.size()); ++ti) {
        const EnergyZone& zone = it->second[static_cast<std::size_t>(ti)];
        if (zone.land_use_available <= 0 || zone.land_abilities.empty()) {
            continue;
        }
        for (int ai = 0; ai < static_cast<int>(zone.land_abilities.size()); ++ai) {
            const TerritoryAbility& ab = zone.land_abilities[static_cast<std::size_t>(ai)];
            // Auto-tap energy is spent automatically when paying costs - don't waste the
            // shared use as untargeted float.
            if (ab.is_auto_tap_energy()) {
                continue;
            }
            if (ab.is_special_ability() && !own_main) {
                continue;
            }
            if (!ab.cost.empty() && !game.turn_manager.can_afford(game, player_id, ab.cost)) {
                continue;
            }
            if (!ab.effect.effect_key.empty() && ab.effect.requires_target) {
                append_territory_effect_target_actions(game, player_id, ab.effect, BotActionKind::UseLand, ti + 1,
                    ai + 1, out);
                continue;
            }
            BotAction action;
            action.kind = BotActionKind::UseLand;
            action.player_id = player_id;
            action.energy_zone_index_1based = ti + 1;
            action.land_ability_index_1based = ai + 1;
            out.push_back(std::move(action));
        }
    }
}

void append_defend_dash_actions(GameState& game, const int player_id, std::vector<BotAction>& out)
{
    for (const auto& [id, ent] : game.board.all_entities_map) {
        (void)id;
        auto unit = std::dynamic_pointer_cast<Unit>(ent);
        if (!unit || !unit->owner || *unit->owner != player_id) {
            continue;
        }
        if (!entity_is_board_unit(*unit)) {
            continue;
        }
        DefendAction defend(unit, player_id);
        if (defend.validate(game).ok) {
            BotAction action;
            action.kind = BotActionKind::Defend;
            action.player_id = player_id;
            action.entity_id = unit->entity_id;
            out.push_back(action);
        }
        DashAction dash(unit, player_id);
        if (dash.validate(game).ok) {
            BotAction action;
            action.kind = BotActionKind::Dash;
            action.player_id = player_id;
            action.entity_id = unit->entity_id;
            out.push_back(action);
        }
        RecoverAction recover(unit, player_id);
        if (recover.validate(game).ok) {
            BotAction action;
            action.kind = BotActionKind::Recover;
            action.player_id = player_id;
            action.entity_id = unit->entity_id;
            out.push_back(action);
        }
    }
}

}  // namespace

std::optional<int> bot_acting_seat(const GameState& game)
{
    if (game.is_combat_visualization_paused()) {
        return std::nullopt;
    }
    if (game.IsAwaitingHandDiscard()) {
        return game.PendingDiscardPlayerId();
    }
    if (game.IsAwaitingScan()) {
        return game.PendingScanPlayerId();
    }
    if (game.IsAwaitingTerritoryTarget()) {
        return game.PendingTerritoryTargetPlayerId();
    }
    if (game.IsAwaitingTerritoryLoot()) {
        return game.PendingTerritoryLootPlayerId();
    }
    const auto phase = game.turn_manager.current_phase;
    if (phase == TurnPhase::SpellWindow || phase == TurnPhase::SecondSpellWindow || phase == TurnPhase::Defense
        || phase == TurnPhase::BonusDefense) {
        return game.reaction_window_priority_player();
    }
    return game.turn_manager.current_player();
}

std::vector<BotAction> generate_legal_actions(GameState& game, const int player_id, const LegalActionGenLimits& limits)
{
    std::vector<BotAction> out;
    if (game.is_combat_visualization_paused()) {
        BotAction resume;
        resume.kind = BotActionKind::ResumeCombatViz;
        resume.player_id = player_id;
        out.push_back(resume);
        return out;
    }

    if (game.IsPendingDiscardForPlayer(player_id)) {
        const auto* hand = game.players_hands.at(player_id);
        for (std::size_t i = 0; i < hand->size(); ++i) {
            BotAction action;
            action.kind = BotActionKind::DiscardHandCard;
            action.player_id = player_id;
            action.hand_index_1based = static_cast<int>(i) + 1;
            out.push_back(action);
        }
        return out;
    }

    if (game.IsPendingTerritoryTargetForPlayer(player_id)) {
        if (const TerritoryEffect* eff = game.pending_territory_front_effect(player_id)) {
            if (eff->requires_target) {
                append_territory_effect_target_actions(game, player_id, *eff, BotActionKind::ResolveTerritoryTarget, 0,
                    0, out);
            }
        }
        BotAction skip;
        skip.kind = BotActionKind::SkipTerritoryTarget;
        skip.player_id = player_id;
        out.push_back(skip);
        return out;
    }

    if (game.IsPendingTerritoryLootForPlayer(player_id)) {
        const auto* hand = game.players_hands.at(player_id);
        if (hand) {
            for (std::size_t i = 0; i < hand->size(); ++i) {
                BotAction discard;
                discard.kind = BotActionKind::TerritoryLootDiscard;
                discard.player_id = player_id;
                discard.hand_index_1based = static_cast<int>(i) + 1;
                out.push_back(discard);
            }
        }
        BotAction skip;
        skip.kind = BotActionKind::SkipTerritoryLoot;
        skip.player_id = player_id;
        out.push_back(skip);
        return out;
    }

    if (game.IsPendingScanForPlayer(player_id)) {
        const auto* peeked = game.pending_scan_peeked_for(player_id);
        if (peeked) {
            for (std::size_t i = 0; i < peeked->size(); ++i) {
                BotAction action;
                action.kind = BotActionKind::ScanDiscard;
                action.player_id = player_id;
                action.hand_index_1based = static_cast<int>(i) + 1;
                out.push_back(action);
            }
        }
        BotAction finish;
        finish.kind = BotActionKind::ScanFinish;
        finish.player_id = player_id;
        out.push_back(finish);
        return out;
    }

    const auto phase = game.turn_manager.current_phase;

    if (phase == TurnPhase::Energy) {
        const auto active = game.turn_manager.current_player();
        if (!active || *active != player_id) {
            return out;
        }
        const auto choices_it = game.turn_manager.pending_energy_choices.find(player_id);
        if (choices_it != game.turn_manager.pending_energy_choices.end()) {
            const auto& choices = choices_it->second;
            for (int i = 0; i < static_cast<int>(choices.size()); ++i) {
                BotAction action;
                action.kind = BotActionKind::ChooseEnergyZone;
                action.player_id = player_id;
                action.energy_zone_index_1based = i + 1;
                out.push_back(action);
            }
        }
        BotAction skip;
        skip.kind = BotActionKind::SkipEnergyZone;
        skip.player_id = player_id;
        out.push_back(skip);
        return out;
    }

    if (phase == TurnPhase::Main || phase == TurnPhase::SecondMain) {
        const auto active = game.turn_manager.current_player();
        if (!active || *active != player_id) {
            return out;
        }

        append_move_actions(game, player_id, out, limits.max_move_actions, limits.skip_move_actions);
        if (!has_pending_move(game, player_id)) {
            append_deploy_actions(game, player_id, CardPlayZone::Hand, *game.players_hands.at(player_id), out);
            const auto deck_it = game.players_decks.find(player_id);
            if (deck_it != game.players_decks.end()) {
                append_deploy_actions(game, player_id, CardPlayZone::Reserves, deck_it->second.reserves, out);
            }
            append_spell_actions(game, player_id, CardPlayZone::Hand, out, limits.max_spell_actions);
            append_spell_actions(game, player_id, CardPlayZone::Reserves, out, limits.max_spell_actions);
            append_ability_actions(game, player_id, out, limits.max_ability_actions);
            append_use_land_actions(game, player_id, out);
            // Defend/Dash are pre-emptive main-phase stances (they spend the attack budget),
            // not reactions - the engine rejects them inside defense windows.
            append_defend_dash_actions(game, player_id, out);

            if (game.can_pass_priority(player_id)) {
                BotAction end_main;
                end_main.kind = BotActionKind::EndMainPhase;
                end_main.player_id = player_id;
                out.push_back(end_main);
            }
        }
        return out;
    }

    if (phase == TurnPhase::AttackDeclaration || phase == TurnPhase::BonusAttackDeclaration) {
        const auto active = game.turn_manager.current_player();
        if (!active || *active != player_id) {
            return out;
        }
        append_move_actions(game, player_id, out, limits.max_move_actions, limits.skip_move_actions);
        if (!has_pending_move(game, player_id)) {
            append_attack_actions(game, player_id, out);
            // NB: attack_undeclare is deliberately NOT offered to bots. Like undo and
            // batch_cancel, it is a human error-recovery control only. Offering it lets
            // greedy/MCTS oscillate declare<->undeclare forever (a livelock). A bot
            // declares attacks and then commits; it never needs to take one back.
            append_spell_actions(game, player_id, CardPlayZone::Hand, out, limits.max_spell_actions);
            append_spell_actions(game, player_id, CardPlayZone::Reserves, out, limits.max_spell_actions);
            append_ability_actions(game, player_id, out, limits.max_ability_actions);
            append_use_land_actions(game, player_id, out);
            // Dash (+1 movement, spends the attack budget) is also legal in Attack
            // Declaration; Defend is main-phase-only and its validate rejects here.
            append_defend_dash_actions(game, player_id, out);

            BotAction commit;
            commit.kind = BotActionKind::CommitAttackDeclaration;
            commit.player_id = player_id;
            out.push_back(commit);
        }
        return out;
    }

    if (phase == TurnPhase::SpellWindow || phase == TurnPhase::SecondSpellWindow) {
        const bool can_pass = game.can_pass_spell_window(player_id);
        if (!can_pass) {
            append_spell_actions(game, player_id, CardPlayZone::Hand, out, limits.max_spell_actions);
            append_spell_actions(game, player_id, CardPlayZone::Reserves, out, limits.max_spell_actions);
            append_use_land_actions(game, player_id, out);
            return out;
        }
        BotAction pass;
        pass.kind = BotActionKind::PassPriority;
        pass.player_id = player_id;
        out.push_back(pass);
        append_spell_actions(game, player_id, CardPlayZone::Hand, out, limits.max_spell_actions);
        append_spell_actions(game, player_id, CardPlayZone::Reserves, out, limits.max_spell_actions);
        append_use_land_actions(game, player_id, out);
        return out;
    }

    if (phase == TurnPhase::Defense || phase == TurnPhase::BonusDefense) {
        // NB: Defend/Dash are NOT offered here - they are main-phase stances and the
        // engine's validate rejects them inside defense windows (they never appended).
        const bool can_pass = game.can_pass_defense_window(player_id);
        if (!can_pass) {
            append_spell_actions(game, player_id, CardPlayZone::Hand, out, limits.max_spell_actions);
            append_spell_actions(game, player_id, CardPlayZone::Reserves, out, limits.max_spell_actions);
            append_ability_actions(game, player_id, out, limits.max_ability_actions);
            append_use_land_actions(game, player_id, out);
            return out;
        }
        BotAction pass;
        pass.kind = BotActionKind::PassPriority;
        pass.player_id = player_id;
        out.push_back(pass);
        append_spell_actions(game, player_id, CardPlayZone::Hand, out, limits.max_spell_actions);
        append_spell_actions(game, player_id, CardPlayZone::Reserves, out, limits.max_spell_actions);
        append_ability_actions(game, player_id, out, limits.max_ability_actions);
        append_use_land_actions(game, player_id, out);
        return out;
    }

    return out;
}

}  // namespace tactics::bot