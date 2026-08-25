#include "tactics/bot/bot_economy.hpp"

#include "tactics/actions/actions.hpp"
#include "tactics/board/aether.hpp"
#include "tactics/bot/bot_evaluator.hpp"
#include "tactics/bot/bot_match_outcome.hpp"
#include "tactics/cards/ability_catalog.hpp"
#include "tactics/cards/card_runtime.hpp"
#include "tactics/combat/directional_area.hpp"
#include "tactics/core/board_target_policy.hpp"
#include "tactics/effects/effect_traits.hpp"
#include "tactics/energy/energy_zone.hpp"
#include "tactics/entities/entity.hpp"

#include <algorithm>
#include <limits>
#include <set>

namespace tactics::bot {
namespace {

int sum_energy_map(const std::map<EnergyType, int>& pool)
{
    int total = 0;
    for (const auto& [_, amount] : pool) {
        total += std::max(0, amount);
    }
    return total;
}

int untapped_zone_pips(const GameState& game, const int player_id)
{
    const auto it = game.players_energy_zones.find(player_id);
    if (it == game.players_energy_zones.end()) {
        return 0;
    }
    int total = 0;
    for (const EnergyZone& zone : it->second) {
        for (const auto& [_, amount] : zone.available_auto_energy()) {
            total += std::max(0, amount);
        }
    }
    return total;
}

bool cell_on_entity_footprint(const Entity& ent, const int x, const int y)
{
    if (!ent.occupied_positions.empty()) {
        for (const auto& [wx, wy] : ent.occupied_positions) {
            if (wx == x && wy == y) {
                return true;
            }
        }
        return false;
    }
    if (!ent.position) {
        return false;
    }
    const auto [ax, ay] = *ent.position;
    for (const auto& [dx, dy] : entity_shape_offsets(ent)) {
        if (ax + dx == x && ay + dy == y) {
            return true;
        }
    }
    return false;
}

bool own_base_at_cell(const GameState& game, const int player_id, const int x, const int y)
{
    for (const auto& [_, ent] : game.board.all_entities_map) {
        if (!ent || !ent->owner || *ent->owner != player_id || !entity_is_base(*ent)) {
            continue;
        }
        if (cell_on_entity_footprint(*ent, x, y)) {
            return true;
        }
    }
    return false;
}

int count_hostiles_in_cells(const GameState& game, const int acting_player,
    const std::vector<std::pair<int, int>>& cells)
{
    int hits = 0;
    std::set<std::string> seen;
    for (const auto& [cx, cy] : cells) {
        for (const auto& [_, ent] : game.board.all_entities_map) {
            if (!ent || !ent->owner || ent->current_health <= 0) {
                continue;
            }
            if (!teams_hostile(game, acting_player, *ent->owner)) {
                continue;
            }
            if (!cell_on_entity_footprint(*ent, cx, cy)) {
                continue;
            }
            if (seen.insert(ent->entity_id).second) {
                ++hits;
            }
        }
    }
    return hits;
}

int damaged_friendly_at_cell(const GameState& game, const int acting_player, const int x, const int y)
{
    for (const auto& [_, ent] : game.board.all_entities_map) {
        if (!ent || !ent->owner || *ent->owner != acting_player) {
            continue;
        }
        if (!cell_on_entity_footprint(*ent, x, y)) {
            continue;
        }
        if (ent->current_health < entity_effective_base_health(*ent)) {
            return std::max(0, entity_effective_base_health(*ent) - ent->current_health);
        }
    }
    return 0;
}

bool target_cell_is_hostile(const GameState& game, const int acting_player, const int x, const int y)
{
    for (const auto& [_, ent] : game.board.all_entities_map) {
        if (!ent || !ent->owner) {
            continue;
        }
        if (teams_hostile(game, acting_player, *ent->owner) && cell_on_entity_footprint(*ent, x, y)) {
            return true;
        }
    }
    return false;
}

bool target_cell_is_friendly(const GameState& game, const int acting_player, const int x, const int y)
{
    for (const auto& [_, ent] : game.board.all_entities_map) {
        if (!ent || !ent->owner) {
            continue;
        }
        if (!teams_hostile(game, acting_player, *ent->owner) && cell_on_entity_footprint(*ent, x, y)) {
            return true;
        }
    }
    return false;
}

bool legal_contains_kind(const std::vector<BotAction>& legal, const BotActionKind kind)
{
    return std::any_of(legal.begin(), legal.end(), [&](const BotAction& a) { return a.kind == kind; });
}

/** Chebyshev distance from a cell to the nearest living enemy unit (999 if none). */
int distance_cell_to_nearest_enemy_from_cell(const GameState& game, const int player_id, const int x, const int y)
{
    int best = 999;
    for (const auto& [_, ent] : game.board.all_entities_map) {
        if (!ent || !ent->owner || ent->current_health <= 0 || !entity_is_board_unit(*ent)) {
            continue;
        }
        if (!teams_hostile(game, player_id, *ent->owner)) {
            continue;
        }
        best = std::min(best, min_chebyshev_entity_to_cell(*ent, x, y));
    }
    return best;
}

/** The hostile entity whose footprint covers (x, y), or null. Used to weight a
 *  single-target spell/ability by *what* it hits - a Sentinel is not a token. */
const Entity* hostile_entity_at_cell(const GameState& game, const int acting_player, const int x, const int y)
{
    for (const auto& [_, ent] : game.board.all_entities_map) {
        if (!ent || !ent->owner || !teams_hostile(game, acting_player, *ent->owner)) {
            continue;
        }
        if (cell_on_entity_footprint(*ent, x, y)) {
            return ent.get();
        }
    }
    return nullptr;
}

/** Bonus for a single-target hostile effect, scaled by the target's worth (capped so a
 *  premium target adds up to ~+90 but never eclipses lethal/objective terms). */
int target_value_bonus(const GameState& game, const int acting_player, const int x, const int y)
{
    const Entity* target = hostile_entity_at_cell(game, acting_player, x, y);
    if (!target || entity_is_base(*target)) {
        return 0;
    }
    return static_cast<int>(std::min(piece_value(*target), 45.0) * 2.0);
}

/** Value of landing a status debuff on an enemy - higher for effects that deny actions
 *  (stun/silence/jammed/root) than for damage-over-time or setup debuffs. 0 if the
 *  effect key is not a recognised hostile debuff. */
/** A representative magnitude for an effect, read from common payload fields. Lets the
 *  scorer value effects it doesn't specifically recognise (so new cards aren't invisible). */
int effect_payload_magnitude(const std::map<std::string, int>& payload)
{
    static const char* const kMagnitudeKeys[] = {
        "amount", "damage", "value", "heal", "healing", "stacks", "duration", "turns", "shield", "armor",
    };
    int magnitude = 0;
    for (const char* key : kMagnitudeKeys) {
        if (const auto it = payload.find(key); it != payload.end()) {
            magnitude = std::max(magnitude, std::abs(it->second));
        }
    }
    return magnitude;
}

int enemy_debuff_value(const std::string& effect_key, const std::map<std::string, int>& payload)
{
    // Action-denial: the target loses a turn / its abilities - the strongest tempo swing.
    static const std::set<std::string> kDenial = {
        "apply_stunned", "apply_rooted", "apply_silenced", "apply_silenced_owner_turn_end",
        "apply_jammed", "apply_jammed_grant_stats",
    };
    // Setup / amplifiers: make the target easier to finish.
    static const std::set<std::string> kSetup = {
        "apply_overload", "apply_overload_ally_draw", "apply_vulnerable", "apply_vulnerable_turn_end",
    };
    // Damage-over-time: chip value.
    static const std::set<std::string> kDot = {
        "apply_bleed", "apply_fire", "apply_poison",
    };
    if (kDenial.count(effect_key)) {
        return 170;
    }
    if (kSetup.count(effect_key)) {
        return 130;
    }
    if (kDot.count(effect_key)) {
        return 90;
    }
    // Fallback: any other status *application* aimed at an enemy is still a debuff worth
    // playing - value it from the payload so a newly-authored `apply_…` is not invisible.
    if (effect_key.rfind("apply_", 0) == 0) {
        return 70 + std::min(60, effect_payload_magnitude(payload) * 8);
    }
    return 0;
}

/** Value of a beneficial effect aimed at a friendly unit that has no bespoke handling
 *  (heal/repair/grant_next_damage/grant_permanent_stat_growth are scored explicitly). A
 *  positive-looking key - a shield, regen, or any new `grant_…`/`buff_…` - is still worth
 *  playing, valued from the payload so new support cards are not invisible. */
int ally_buff_value(const std::string& effect_key, const std::map<std::string, int>& payload)
{
    static const char* const kPositivePrefixes[] = {
        "grant_", "buff_", "restore_", "apply_shield", "apply_regen", "apply_armor", "apply_haste",
    };
    for (const char* prefix : kPositivePrefixes) {
        if (effect_key.rfind(prefix, 0) == 0) {
            return 45 + std::min(70, effect_payload_magnitude(payload) * 8);
        }
    }
    return 0;
}

int count_friendly_board_units(const GameState& game, const int player_id)
{
    int count = 0;
    for (const auto& [_, ent] : game.board.all_entities_map) {
        if (!ent || !ent->owner || *ent->owner != player_id) {
            continue;
        }
        if (entity_is_board_unit(*ent) && ent->current_health > 0) {
            ++count;
        }
    }
    return count;
}

void scan_spell_options(const GameState& game, const int player_id, const CardPlayZone zone,
    int& reflex_spells, int& channeled_spells, int& cheapest_reflex)
{
    const auto deck_it = game.players_decks.find(player_id);
    if (deck_it == game.players_decks.end()) {
        return;
    }
    const Deck& deck = deck_it->second;
    const std::vector<CardInstanceId>& cards =
        zone == CardPlayZone::Reserves ? deck.reserves : *game.players_hands.at(player_id);

    for (const CardInstanceId cid : cards) {
        const CardInstance* inst = deck.pool.try_get(cid);
        if (!inst) {
            continue;
        }
        const CardDefinition* def = try_get_card_definition_ptr(inst->definition_id);
        if (!def || !definition_is_spell(*def)) {
            continue;
        }
        std::string reason;
        if (zone == CardPlayZone::Reserves) {
            if (!deck.can_play_card_from_reserves(cid, &reason)) {
                continue;
            }
        } else if (!deck.can_play_card_now(cid, &reason)) {
            continue;
        }
        if (!exalted_requirement_met(game, player_id, *def, &reason)) {
            continue;
        }
        const EffectSpeed speed = effective_spell_cast_speed(game, player_id, *def);
        const int cost = definition_total_energy_cost(*def);
        if (speed == EffectSpeed::Reflex || speed == EffectSpeed::Blazing) {
            ++reflex_spells;
            if (cost > 0) {
                cheapest_reflex = std::min(cheapest_reflex, cost);
            }
        } else if (speed == EffectSpeed::Channeled) {
            ++channeled_spells;
        }
    }
}

void scan_reflex_abilities_on_board(const GameState& game, const int player_id, int& reflex_abilities,
    int& cheapest_reflex)
{
    for (const auto& [_, ent] : game.board.all_entities_map) {
        auto unit = std::dynamic_pointer_cast<Unit>(ent);
        if (!unit || !unit->owner || *unit->owner != player_id || !entity_is_board_unit(*unit)) {
            continue;
        }
        for (const auto& ability : unit->activated_abilities) {
            AbilitySpec spec;
            if (!try_get_ability_from_catalog(ability.key, spec)) {
                spec = ability;
            }
            if (spec.speed != EffectSpeed::Reflex && spec.speed != EffectSpeed::Blazing) {
                continue;
            }
            if (!entity_can_use_ability(*unit, spec)) {
                continue;
            }
            const int cost = sum_energy_map(spec.energy_cost);
            if (cost > 0 && !game.turn_manager.can_afford(game, player_id, spec.energy_cost, ActionType::Ability)) {
                continue;
            }
            ++reflex_abilities;
            if (cost > 0) {
                cheapest_reflex = std::min(cheapest_reflex, cost);
            }
        }
    }
}

bool phase_allows_effect_speed(const TurnPhase phase, const EffectSpeed speed)
{
    switch (speed) {
    case EffectSpeed::Channeled:
        return phase == TurnPhase::Main || phase == TurnPhase::SecondMain;
    case EffectSpeed::Reflex:
    case EffectSpeed::Blazing:
        return phase == TurnPhase::Main || phase == TurnPhase::SecondMain
            || phase == TurnPhase::AttackDeclaration || phase == TurnPhase::BonusAttackDeclaration
            || phase == TurnPhase::SpellWindow || phase == TurnPhase::SecondSpellWindow
            || phase == TurnPhase::Defense || phase == TurnPhase::BonusDefense;
    }
    return false;
}

std::optional<EffectSpeed> spell_speed_for_action(const GameState& game, const BotAction& action)
{
    if (!action.card_id.is_valid()) {
        return std::nullopt;
    }
    const auto deck_it = game.players_decks.find(action.player_id);
    if (deck_it == game.players_decks.end()) {
        return std::nullopt;
    }
    const CardInstance* inst = deck_it->second.pool.try_get(action.card_id);
    if (!inst) {
        return std::nullopt;
    }
    const CardDefinition* def = try_get_card_definition_ptr(inst->definition_id);
    if (!def || !definition_is_spell(*def)) {
        return std::nullopt;
    }
    return effective_spell_cast_speed(game, action.player_id, *def);
}

std::optional<EffectSpeed> ability_speed_for_action(const BotAction& action)
{
    if (action.ability_key.empty()) {
        return std::nullopt;
    }
    AbilitySpec spec;
    if (!try_get_ability_from_catalog(action.ability_key, spec)) {
        return std::nullopt;
    }
    return spec.speed;
}

std::optional<std::string> effect_key_for_spell_action(const GameState& game, const BotAction& action)
{
    if (!action.card_id.is_valid()) {
        return std::nullopt;
    }
    const auto deck_it = game.players_decks.find(action.player_id);
    if (deck_it == game.players_decks.end()) {
        return std::nullopt;
    }
    const CardInstance* inst = deck_it->second.pool.try_get(action.card_id);
    if (!inst) {
        return std::nullopt;
    }
    const CardDefinition* def = try_get_card_definition_ptr(inst->definition_id);
    if (!def || !definition_is_spell(*def)) {
        return std::nullopt;
    }
    return definition_spell(*def).effect_key;
}

void fill_spell_payload_for_action(const GameState& game, const BotAction& action, std::string& effect_key,
    std::map<std::string, int>& payload, std::map<std::string, std::string>& string_payload)
{
    if (!action.card_id.is_valid()) {
        return;
    }
    const auto deck_it = game.players_decks.find(action.player_id);
    if (deck_it == game.players_decks.end()) {
        return;
    }
    const CardInstance* inst = deck_it->second.pool.try_get(action.card_id);
    if (!inst) {
        return;
    }
    const CardDefinition* def = try_get_card_definition_ptr(inst->definition_id);
    if (!def || !definition_is_spell(*def)) {
        return;
    }
    const SpellCardDefinition& spell = definition_spell(*def);
    effect_key = spell.effect_key;
    payload = spell.effect_payload;
    string_payload = spell.effect_string_payload;
}

void fill_ability_payload_for_action(const BotAction& action, std::string& effect_key,
    std::map<std::string, int>& payload, std::map<std::string, std::string>& string_payload)
{
    if (action.ability_key.empty()) {
        return;
    }
    AbilitySpec spec;
    if (!try_get_ability_from_catalog(action.ability_key, spec)) {
        return;
    }
    effect_key = spec.effect_key;
    payload = spec.effect_payload;
    string_payload = spec.effect_string_payload;
}

int score_base_onboard_buff_ability(const GameState& game, const BotAction& action, const std::string& effect_key,
    const std::map<std::string, int>& payload)
{
    const auto ent_it = game.board.all_entities_map.find(action.entity_id);
    if (ent_it == game.board.all_entities_map.end() || !ent_it->second || !entity_is_base(*ent_it->second)) {
        return -280;
    }
    int bonus_damage = 0;
    int bonus_range = 0;
    if (effect_key == "grant_attack_damage_turn_self") {
        bonus_damage = std::max(0, effect_payload_magnitude(payload));
    } else if (effect_key == "grant_ranged_range_turn_self") {
        bonus_range = std::max(0, effect_payload_magnitude(payload));
    } else {
        return -280;
    }
    int score = -280;
    const int defense_need = own_base_defense_need(game, action.player_id);
    if (defense_need > 0) {
        score += 100 + std::min(defense_need, 14) * 14;
    }
    const double kill_value = base_turret_buff_kill_value(game, action.player_id, bonus_damage, bonus_range);
    if (kill_value >= 18.0) {
        score += 180 + static_cast<int>(std::min(kill_value, 42.0) * 5.0);
    }
    return score;
}

int proactive_effect_value(const GameState& game, const BotObservation& obs, const BotAction& action,
    const std::string& effect_key, const std::map<std::string, int>& payload, const int hostile_hits)
{
    int score = 0;
    const bool attack_decl = obs.phase == TurnPhase::AttackDeclaration
        || obs.phase == TurnPhase::BonusAttackDeclaration;
    const bool main_phase = obs.phase == TurnPhase::Main || obs.phase == TurnPhase::SecondMain;

    if (effect_key == "grant_attack_damage_turn_self" || effect_key == "grant_ranged_range_turn_self") {
        const auto ent_it = game.board.all_entities_map.find(action.entity_id);
        if (ent_it != game.board.all_entities_map.end() && ent_it->second && entity_is_base(*ent_it->second)) {
            return score_base_onboard_buff_ability(game, action, effect_key, payload);
        }
    }

    if (effect_key == "grant_permanent_stat_growth_self") {
        if (attack_decl) {
            score += 240;
        } else if (main_phase) {
            score += 140;
        } else if (obs.in_reaction_window) {
            score += 80;
        }
    }

    if (effect_key == "heal" || effect_key == "repair_structure_adjacent") {
        const int missing = damaged_friendly_at_cell(game, action.player_id, action.x, action.y);
        if (missing > 0) {
            score += 130 + std::min(missing, 6) * 18;
        } else if (main_phase) {
            score -= 30;
        }
    }

    if (effect_key == "grant_next_damage_bonus_adjacent" && main_phase) {
        int existing_boost = 0;
        for (const auto& [_, ent] : game.board.all_entities_map) {
            if (!ent || !ent->owner || !cell_on_entity_footprint(*ent, action.x, action.y)) {
                continue;
            }
            existing_boost = std::max(existing_boost, entity_effect_amount(*ent, "next_damage_bonus"));
        }
        if (obs.pending_attack_count > 0 && existing_boost < 2) {
            score += 140 - existing_boost * 35;
        } else if (existing_boost == 0) {
            score += 35;
        } else {
            score -= 60 + existing_boost * 25;
        }
    }

    if (!effect_key.empty() && effect_key_deals_damage(effect_key)) {
        if (effect_uses_directional_aim(effect_key)) {
            if (hostile_hits >= 3) {
                score += 280 + hostile_hits * 40;
            } else if (hostile_hits == 2) {
                score += 200;
            } else if (hostile_hits == 1) {
                score += 80;
                if (attack_decl || main_phase) {
                    score -= 90;
                }
            } else if (target_cell_is_hostile(game, action.player_id, action.x, action.y)) {
                score += 50;
                if (attack_decl || main_phase) {
                    score -= 120;
                }
            }
        } else if (target_cell_is_hostile(game, action.player_id, action.x, action.y)) {
            score += attack_decl ? 180 : 140;
            score += target_value_bonus(game, action.player_id, action.x, action.y);  // kill the Sentinel, not the token
        }
    }

    // Status/disruption on an enemy - recognised debuffs, plus a payload-based fallback so a
    // newly-authored enemy-aimed effect is not invisible. Never debuff your own units.
    const bool tgt_hostile = target_cell_is_hostile(game, action.player_id, action.x, action.y);
    const bool tgt_friendly = target_cell_is_friendly(game, action.player_id, action.x, action.y);
    if (const int debuff = enemy_debuff_value(effect_key, payload); debuff > 0) {
        if (tgt_hostile) {
            score += debuff;
        } else if (tgt_friendly) {
            score -= 200;  // never debuff your own units
        }
    } else if (tgt_hostile && !effect_key.empty() && !effect_key_deals_damage(effect_key)) {
        score += 40 + std::min(40, effect_payload_magnitude(payload) * 6);  // unknown enemy-aimed effect
    }

    if (const int sd_urgency = sudden_death_base_hp_urgency(game, action.player_id); sd_urgency > 0) {
        if (effect_key == "grant_permanent_stat_growth_self") {
            score -= std::min(200, sd_urgency * 2 / 3);
        }
        if ((effect_key == "heal" || effect_key == "repair_structure_adjacent")
            && own_base_at_cell(game, action.player_id, action.x, action.y)) {
            score -= std::min(180, sd_urgency);
        }
    }

    // Beneficial effect on a friendly unit not scored above (shield/regen/new grant_…), so
    // new support cards aren't invisible. Excludes the keys already handled explicitly.
    static const std::set<std::string> kHandledFriendly = {
        "heal", "repair_structure_adjacent", "grant_next_damage_bonus_adjacent",
        "grant_permanent_stat_growth_self",
    };
    if (tgt_friendly && !kHandledFriendly.count(effect_key)) {
        score += ally_buff_value(effect_key, payload);
    }

    return score;
}

int reaction_effect_value(const GameState& game, const BotObservation& obs, const BotAction& action,
    const std::string& effect_key, const std::map<std::string, int>& payload, const int hostile_hits)
{
    int score = 0;
    if (obs.must_respond_reaction_window) {
        score += 240;
    }

    // A denial/setup debuff (or any recognised/fallback enemy-aimed status) on the attacker
    // or another enemy is a strong reaction play.
    if (const int debuff = enemy_debuff_value(effect_key, payload); debuff > 0
        && target_cell_is_hostile(game, action.player_id, action.x, action.y)) {
        score += debuff;
    }

    if (!effect_key.empty() && effect_key_deals_damage(effect_key)) {
        if (effect_uses_directional_aim(effect_key)) {
            score += 120 + hostile_hits * 90;
        } else if (target_cell_is_hostile(game, action.player_id, action.x, action.y)) {
            score += 300;
            score += target_value_bonus(game, action.player_id, action.x, action.y);  // prefer the valuable target
        }
        if (target_cell_is_friendly(game, action.player_id, action.x, action.y)) {
            score -= 450;
        }
    }

    if (obs.phase == TurnPhase::Defense || obs.phase == TurnPhase::BonusDefense) {
        // Defend/Dash are main-phase stances, never legal (or offered) in defense
        // windows - their scoring lives in combat_action_score (bot_policy.cpp).
        if (target_cell_is_friendly(game, action.player_id, action.x, action.y) && !effect_key_deals_damage(effect_key)) {
            score += 150;
        }
    }

    if (effect_key == "grant_permanent_stat_growth_self" && obs.in_reaction_window) {
        score += 70;
    }

    return score;
}

int deploy_curve_score(const GameState& game, const BotAction& action, const BotEconomySnapshot& economy)
{
    const int units = count_friendly_board_units(game, action.player_id);
    if (units == 0) {
        return 280;
    }

    int deploy_cost = 0;
    if (action.card_id.is_valid()) {
        const auto deck_it = game.players_decks.find(action.player_id);
        if (deck_it != game.players_decks.end()) {
            const CardInstance* inst = deck_it->second.pool.try_get(action.card_id);
            if (inst) {
                if (const CardDefinition* def = try_get_card_definition_ptr(inst->definition_id)) {
                    deploy_cost = definition_total_energy_cost(*def);
                }
            }
        }
    }

    if (units <= 2 && economy.total_spendable >= deploy_cost) {
        return 140 - units * 25;
    }
    if (units <= 4 && economy.float_energy_total >= deploy_cost) {
        return 40;
    }
    if (units >= 8) {
        return -220;
    }
    if (units >= 6) {
        return -120;
    }
    return -80;
}

int end_main_economy_score(const BotEconomySnapshot& economy, const std::vector<BotAction>& legal)
{
    int score = 0;
    const bool can_deploy = legal_contains_kind(legal, BotActionKind::Deploy)
        || legal_contains_kind(legal, BotActionKind::DeployReserve);
    const bool can_spell = legal_contains_kind(legal, BotActionKind::CastSpell)
        || legal_contains_kind(legal, BotActionKind::CastSpellReserve);
    const bool can_ability = legal_contains_kind(legal, BotActionKind::ActivateAbility);

    if (economy.float_energy_total > 0) {
        score -= economy.float_energy_total * 35;
        if (economy.should_reserve_reflex) {
            score += 40;
        }
    }

    if (economy.should_reserve_reflex && economy.total_spendable >= economy.reflex_reserve_target) {
        score += 80;
    } else if (economy.bluff_reserve > 0 && economy.total_spendable >= economy.bluff_reserve) {
        // Keeping the bluff pip is worth a little - but only a little, so a real play
        // (deploy/cast worth 100+) still wins over sandbagging.
        score += 25;
        // Any spendable beyond the single bluff pip is still tempo we are wasting.
        if (economy.total_spendable > economy.bluff_reserve) {
            score -= (economy.total_spendable - economy.bluff_reserve) * 25;
        }
    } else if (!economy.should_reserve_reflex && economy.total_spendable > 0) {
        score -= economy.total_spendable * 25;
    }

    if (can_deploy && economy.total_spendable >= 2) {
        score -= 60;
    }
    if (legal_contains_kind(legal, BotActionKind::UseLand)) {
        score -= 45;
    }
    if ((can_spell || can_ability) && economy.channeled_spell_options > 0 && economy.float_energy_total > 0) {
        score -= 50;
    }

    if (!can_deploy && !can_spell && !can_ability) {
        score += 100;
    }

    return score;
}

/** Value of adding a candidate energy zone this turn, weighted by how well its colours fill
 *  the colours our hand actually needs (matched against what we already produce). Neutral /
 *  Omni are flexible (they pay for anything), so they get a flat value. This stops the bot
 *  from stranding itself off-colour - picking pips it cannot spend on the cards it holds. */
bool groundwork_matches(const GameState& game, const int player, const GroundworkTrigger& gw)
{
    const auto it = game.last_conquered_territory.find(player);
    if (it == game.last_conquered_territory.end() || !it->second.has_value) {
        return false;
    }
    return it->second.was_basic && it->second.color == gw.color;
}

int score_territory_effect_key(const std::string& key, const int friendly_units)
{
    if (key == "scan") {
        return 35;
    }
    if (key == "grant_permanent_stat_growth") {
        return friendly_units > 0 ? 70 : 12;
    }
    if (key == "grant_next_damage_bonus") {
        return friendly_units > 0 ? 55 : 10;
    }
    if (key == "spawn_card_unit_deploy_zone") {
        return 85;
    }
    if (key == "grant_player_base_bonus_health" || key == "grant_player_base_max_health") {
        return 45;
    }
    if (key == "optional_discard_draw") {
        return 18;
    }
    if (key == "draw_focus_spell_cards") {
        return 40;
    }
    return key.empty() ? 0 : 20;
}

int energy_zone_color_bonus(const GameState& game, const BotAction& action)
{
    const int player = action.player_id;
    const auto choices_it = game.turn_manager.pending_energy_choices.find(player);
    if (choices_it == game.turn_manager.pending_energy_choices.end()) {
        return 0;
    }
    const int idx = action.energy_zone_index_1based - 1;
    if (idx < 0 || idx >= static_cast<int>(choices_it->second.size())) {
        return 0;
    }
    const EnergyZone& zone = choices_it->second[static_cast<std::size_t>(idx)];

    std::map<EnergyType, int> demand;
    std::map<EnergyType, int> supply;
    const auto deck_it = game.players_decks.find(player);
    if (const auto* hand = game.players_hands.at(player); hand && deck_it != game.players_decks.end()) {
        for (const CardInstanceId cid : *hand) {
            if (const CardInstance* inst = deck_it->second.pool.try_get(cid)) {
                if (const CardDefinition* def = try_get_card_definition_ptr(inst->definition_id)) {
                    for (const auto& [color, amount] : definition_energy_cost(*def)) {
                        demand[color] += std::max(0, amount);
                    }
                }
            }
        }
    }
    int placed = 0;
    if (const auto zit = game.players_energy_zones.find(player); zit != game.players_energy_zones.end()) {
        placed = static_cast<int>(zit->second.size());
        for (const EnergyZone& z : zit->second) {
            for (const auto& [color, amount] : z.potential_auto_energy()) {
                supply[color] += std::max(0, amount);
            }
        }
    }

    int bonus = 0;
    for (const auto& [color, amount] : zone.potential_auto_energy()) {
        if (amount <= 0) {
            continue;
        }
        if (color == EnergyType::Neutral || color == EnergyType::Omni) {
            bonus += amount * 12;
            continue;
        }
        const int deficit = std::max(0, demand[color] - supply[color]);
        bonus += std::min(amount, deficit) * 30 + amount * 4;
    }

    if (zone.is_basic) {
        bonus += placed < 3 ? 28 : 12;
    }
    if (zone.enters_depleted) {
        bonus -= 6;
    }

    const int friendly_units = count_friendly_board_units(game, player);
    for (const GroundworkTrigger& gw : zone.groundwork) {
        const bool matches = groundwork_matches(game, player, gw);
        if (gw.destroy_if_unmet && !matches) {
            bonus -= 420;
            continue;
        }
        if (!matches) {
            continue;
        }
        if (gw.ignore_depleted) {
            bonus += 36;
        }
        if (gw.effect) {
            bonus += score_territory_effect_key(gw.effect->effect_key, friendly_units);
        }
    }
    for (const TerritoryEffect& eff : zone.enter_effects) {
        bonus += score_territory_effect_key(eff.effect_key, friendly_units);
    }
    for (const TerritoryAbility& ab : zone.land_abilities) {
        if (ab.produces_flux) {
            bonus += sum_energy_map(ab.energy_produced) * 10;
        }
        if (ab.is_special_ability()) {
            bonus += score_territory_effect_key(ab.effect.effect_key, friendly_units) / 2;
        }
    }
    return bonus;
}

int score_use_land_action(const GameState& game, const BotAction& action, const BotEconomySnapshot& economy)
{
    const auto zit = game.players_energy_zones.find(action.player_id);
    if (zit == game.players_energy_zones.end()) {
        return 0;
    }
    const int ti = action.energy_zone_index_1based - 1;
    const int ai = action.land_ability_index_1based - 1;
    if (ti < 0 || ti >= static_cast<int>(zit->second.size())) {
        return 0;
    }
    const EnergyZone& zone = zit->second[static_cast<std::size_t>(ti)];
    if (ai < 0 || ai >= static_cast<int>(zone.land_abilities.size())) {
        return 0;
    }
    const TerritoryAbility& ab = zone.land_abilities[static_cast<std::size_t>(ai)];
    int score = 8;
    const int produced = sum_energy_map(ab.energy_produced);
    if (ab.produces_flux) {
        score += produced * 18;
        if (economy.channeled_spell_options > 0 || economy.reflex_spell_options > 0) {
            score += 45;
        }
    }
    if (ab.is_special_ability()) {
        score += score_territory_effect_key(ab.effect.effect_key, count_friendly_board_units(game, action.player_id));
        if (ab.effect.requires_target) {
            const auto eit = game.board.all_entities_map.find(action.entity_id);
            const Entity* target = eit != game.board.all_entities_map.end() ? eit->second.get() : nullptr;
            if (target && target->owner && *target->owner == action.player_id) {
                score += 40 + std::min(20, target->current_health);
            } else if (target && target->owner && teams_hostile(game, action.player_id, *target->owner)) {
                score -= 220;
            }
        }
    }
    if (ab.sacrifice_self) {
        score -= 20;
        if (zone.is_basic) {
            score -= 25;
        }
    }
    score -= sum_energy_map(ab.cost) * 4;
    return score;
}

int score_territory_target_action(const GameState& game, const BotAction& action)
{
    const TerritoryEffect* eff = game.pending_territory_front_effect(action.player_id);
    const auto eit = game.board.all_entities_map.find(action.entity_id);
    const Entity* target = eit != game.board.all_entities_map.end() ? eit->second.get() : nullptr;
    if (!target) {
        const auto cell = game.board.entity_at(action.x, action.y);
        target = cell.get();
    }
    if (!target) {
        return -40;
    }
    const bool own = target->owner && *target->owner == action.player_id;
    const bool enemy = target->owner && teams_hostile(game, action.player_id, *target->owner);
    const std::string key = eff ? eff->effect_key : std::string{};
    const bool is_buff = key.find("grant_") == 0 || key == "grant_next_damage_bonus"
        || key == "grant_permanent_stat_growth";
    if (is_buff) {
        if (own) {
            return 130 + std::min(24, target->current_health);
        }
        if (enemy) {
            return -260;
        }
        return 40;
    }
    if (own) {
        return 50;
    }
    if (enemy) {
        return 20;
    }
    return 10;
}

}  // namespace

BotEconomySnapshot build_bot_economy_snapshot(const GameState& game, const BotObservation& obs)
{
    BotEconomySnapshot snap;
    const int player_id = obs.acting_player;

    const auto float_it = game.turn_manager.player_energy.find(player_id);
    if (float_it != game.turn_manager.player_energy.end()) {
        snap.float_energy_total = sum_energy_map(float_it->second);
    }
    snap.untapped_zone_pips = untapped_zone_pips(game, player_id);
    snap.total_spendable = snap.float_energy_total + snap.untapped_zone_pips;

    int cheapest_reflex = std::numeric_limits<int>::max();
    scan_spell_options(game, player_id, CardPlayZone::Hand, snap.reflex_spell_options, snap.channeled_spell_options,
        cheapest_reflex);
    int reserves_reflex = 0;
    int reserves_channeled = 0;
    scan_spell_options(game, player_id, CardPlayZone::Reserves, reserves_reflex, reserves_channeled, cheapest_reflex);
    snap.reflex_spell_options += reserves_reflex;
    snap.channeled_spell_options += reserves_channeled;
    scan_reflex_abilities_on_board(game, player_id, snap.reflex_ability_options, cheapest_reflex);

    snap.cheapest_reflex_cost = cheapest_reflex == std::numeric_limits<int>::max() ? 0 : cheapest_reflex;

    snap.on_active_turn = obs.active_player.has_value() && *obs.active_player == player_id;
    const bool proactive_phase = obs.phase == TurnPhase::Main || obs.phase == TurnPhase::SecondMain
        || obs.phase == TurnPhase::AttackDeclaration || obs.phase == TurnPhase::BonusAttackDeclaration;
    const int reflex_options = snap.reflex_spell_options + snap.reflex_ability_options;
    snap.should_reserve_reflex = snap.on_active_turn && proactive_phase && reflex_options > 0;
    if (snap.should_reserve_reflex && snap.cheapest_reflex_cost > 0) {
        snap.reflex_reserve_target = std::min(snap.cheapest_reflex_cost, snap.total_spendable);
    }

    // Bluff: even without a real reflex, holding one pip of reaction-capable energy while
    // we have cards in hand and the opponent has committed units makes them play around
    // an interaction we may not have (they can only see our energy + hand count). Gated to
    // mid-game onward and bounded to 1 pip so it never costs meaningful tempo.
    if (!snap.should_reserve_reflex && snap.on_active_turn && proactive_phase
        && obs.round_number >= 3 && obs.hand_size > 0 && snap.total_spendable >= 1) {
        bool opponent_has_units = false;
        for (const auto& [_, ent] : game.board.all_entities_map) {
            if (ent && ent->owner && ent->current_health > 0 && entity_is_board_unit(*ent)
                && teams_hostile(game, player_id, *ent->owner)) {
                opponent_has_units = true;
                break;
            }
        }
        if (opponent_has_units) {
            snap.bluff_reserve = 1;
        }
    }

    return snap;
}

int directional_action_hostile_hits(const GameState& game, const BotAction& action, const std::string& effect_key,
    const std::map<std::string, int>& payload, const std::map<std::string, std::string>& string_payload)
{
    if (!effect_uses_directional_aim(effect_key)) {
        return 0;
    }

    std::shared_ptr<Entity> actor;
    if (!action.focus_caster_entity_id.empty()) {
        const auto it = game.board.all_entities_map.find(action.focus_caster_entity_id);
        if (it != game.board.all_entities_map.end()) {
            actor = it->second;
        }
    } else if (!action.entity_id.empty()) {
        const auto it = game.board.all_entities_map.find(action.entity_id);
        if (it != game.board.all_entities_map.end()) {
            actor = it->second;
        }
    }
    if (!actor) {
        return 0;
    }

    const auto cells = preview_directional_effect_blast_cells(game, *actor, action.x, action.y, effect_key, payload,
        string_payload);
    return count_hostiles_in_cells(game, action.player_id, cells);
}

int bot_action_energy_cost(const GameState& game, const BotAction& action)
{
    switch (action.kind) {
    case BotActionKind::Deploy:
    case BotActionKind::DeployReserve: {
        if (!action.card_id.is_valid()) {
            return 0;
        }
        const auto deck_it = game.players_decks.find(action.player_id);
        if (deck_it == game.players_decks.end()) {
            return 0;
        }
        const CardInstance* inst = deck_it->second.pool.try_get(action.card_id);
        if (!inst) {
            return 0;
        }
        const CardDefinition* def = try_get_card_definition_ptr(inst->definition_id);
        return def ? definition_total_energy_cost(*def) : 0;
    }
    case BotActionKind::CastSpell:
    case BotActionKind::CastSpellReserve: {
        if (!action.card_id.is_valid()) {
            return 0;
        }
        const auto deck_it = game.players_decks.find(action.player_id);
        if (deck_it == game.players_decks.end()) {
            return 0;
        }
        const CardInstance* inst = deck_it->second.pool.try_get(action.card_id);
        if (!inst) {
            return 0;
        }
        const CardDefinition* def = try_get_card_definition_ptr(inst->definition_id);
        if (!def) {
            return 0;
        }
        return batched_spell_total_cost_for_cast(*def, action.spell_x_amount);
    }
    case BotActionKind::ActivateAbility: {
        if (action.ability_key.empty()) {
            return 0;
        }
        AbilitySpec spec;
        if (!try_get_ability_from_catalog(action.ability_key, spec)) {
            return 0;
        }
        int base = sum_energy_map(spec.energy_cost);
        if (spec.x_cost_energy_type.has_value() && action.spell_x_amount > 0) {
            base += action.spell_x_amount;
        }
        return base;
    }
    case BotActionKind::UseLand: {
        const auto zit = game.players_energy_zones.find(action.player_id);
        if (zit == game.players_energy_zones.end()) {
            return 0;
        }
        const int ti = action.energy_zone_index_1based - 1;
        const int ai = action.land_ability_index_1based - 1;
        if (ti < 0 || ti >= static_cast<int>(zit->second.size())) {
            return 0;
        }
        const EnergyZone& zone = zit->second[static_cast<std::size_t>(ti)];
        if (ai < 0 || ai >= static_cast<int>(zone.land_abilities.size())) {
            return 0;
        }
        return sum_energy_map(zone.land_abilities[static_cast<std::size_t>(ai)].cost);
    }
    default:
        return 0;
    }
}

int score_bot_action_economy(const GameState& game, const BotObservation& obs, const BotEconomySnapshot& economy,
    const std::vector<BotAction>& legal, const BotAction& action)
{
    int score = 0;

    if (action.kind == BotActionKind::ChooseEnergyZone) {
        score += energy_zone_color_bonus(game, action);  // pick colours the hand can spend
    }
    if (action.kind == BotActionKind::UseLand) {
        score += score_use_land_action(game, action, economy);
    }
    if (action.kind == BotActionKind::ResolveTerritoryTarget) {
        score += score_territory_target_action(game, action);
    }
    if (action.kind == BotActionKind::SkipTerritoryTarget) {
        score -= 80;
    }
    if (action.kind == BotActionKind::TerritoryLootDiscard) {
        score += obs.hand_size >= 7 ? 25 : -10;
    }
    if (action.kind == BotActionKind::SkipTerritoryLoot) {
        score += obs.hand_size < 7 ? 20 : -15;
    }

    if (action.kind == BotActionKind::Deploy || action.kind == BotActionKind::DeployReserve) {
        const int curve = deploy_curve_score(game, action, economy);

        // Is this an economy/engine card (energy generator, spawner, allied aura)?
        double engine = 0.0;
        if (action.card_id.is_valid()) {
            const auto deck_it = game.players_decks.find(action.player_id);
            if (deck_it != game.players_decks.end()) {
                if (const CardInstance* inst = deck_it->second.pool.try_get(action.card_id)) {
                    if (const CardDefinition* def = try_get_card_definition_ptr(inst->definition_id)) {
                        engine = card_engine_future_value(*def);
                    }
                }
            }
        }

        // Economy engines: worth building - but exactly one or two, and only into a safe
        // cell. A fragile forward depot dropped next to enemies just dies and gets redeployed
        // every turn (a stall). So the ramp bonus is HARD-gated: only when we field no engine
        // structure yet, only in the first ~2/3 of the game, and only when the deploy cell is
        // clear of nearby enemies. Otherwise the card scores on its (weak) base curve and the
        // bot correctly holds or discards it rather than throwing it away.
        bool engine_bonus_applies = false;
        if (engine > 0.0) {
            int existing_engines = 0;
            for (const auto& [_, ent] : game.board.all_entities_map) {
                if (ent && ent->owner && *ent->owner == action.player_id && ent->current_health > 0
                    && entity_is_building(*ent)) {
                    ++existing_engines;
                }
            }
            const bool safe_cell =
                distance_cell_to_nearest_enemy_from_cell(game, action.player_id, action.x, action.y) > 2;
            engine_bonus_applies = existing_engines == 0 && safe_cell && obs.round_number <= 28;
        }

        if (engine_bonus_applies) {
            const double round_factor = std::clamp((30.0 - obs.round_number) / 30.0, 0.0, 1.0);
            score += std::max(0, curve) + static_cast<int>(engine * 130.0 * round_factor);
        } else {
            score += curve;
        }

        if (action.card_id.is_valid()) {
            const auto deck_it = game.players_decks.find(action.player_id);
            if (deck_it != game.players_decks.end()) {
                if (const CardInstance* inst = deck_it->second.pool.try_get(action.card_id)) {
                    if (const CardDefinition* def = try_get_card_definition_ptr(inst->definition_id)) {
                        if (definition_is_unit(*def)) {
                            auto probe = create_unit_from_definition(
                                *def, *inst, action.player_id, "bot_aether_deploy_probe");
                            if (probe) {
                                if (aether_would_kill_entity(
                                        game, action.player_id, *probe, action.x, action.y)) {
                                    score -= 250;
                                } else {
                                    score += static_cast<int>(objective_cell_capture_value(
                                        game, action.player_id, *probe, action.x, action.y) * 5.0);
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    if (action.kind == BotActionKind::EndMainPhase) {
        score += end_main_economy_score(economy, legal);
    }

    if (obs.in_reaction_window) {
        if (action.kind == BotActionKind::CastSpell || action.kind == BotActionKind::CastSpellReserve
            || action.kind == BotActionKind::ActivateAbility) {
            std::string effect_key;
            std::map<std::string, int> payload;
            std::map<std::string, std::string> string_payload;
            if (action.kind == BotActionKind::ActivateAbility) {
                fill_ability_payload_for_action(action, effect_key, payload, string_payload);
            } else {
                fill_spell_payload_for_action(game, action, effect_key, payload, string_payload);
            }
            const int hostile_hits = directional_action_hostile_hits(game, action, effect_key, payload, string_payload);
            score += reaction_effect_value(game, obs, action, effect_key, payload, hostile_hits);
        } else if (action.kind != BotActionKind::PassPriority) {
            score -= 60;
        }

        if (action.kind == BotActionKind::PassPriority) {
            if (obs.must_respond_reaction_window) {
                score -= 850;
            } else {
                score += 140;
            }
        }
        return score;
    }

    if (action.kind == BotActionKind::CastSpell || action.kind == BotActionKind::CastSpellReserve
        || action.kind == BotActionKind::ActivateAbility) {
        std::optional<EffectSpeed> speed;
        std::string effect_key;
        std::map<std::string, int> payload;
        std::map<std::string, std::string> string_payload;

        if (action.kind == BotActionKind::ActivateAbility) {
            speed = ability_speed_for_action(action);
            fill_ability_payload_for_action(action, effect_key, payload, string_payload);
        } else {
            if (const auto key = effect_key_for_spell_action(game, action)) {
                effect_key = *key;
            }
            speed = spell_speed_for_action(game, action);
            fill_spell_payload_for_action(game, action, effect_key, payload, string_payload);
        }

        if (!speed.has_value() || !phase_allows_effect_speed(obs.phase, *speed)) {
            score -= 80;
        } else {
            const int hostile_hits =
                directional_action_hostile_hits(game, action, effect_key, payload, string_payload);
            score += proactive_effect_value(game, obs, action, effect_key, payload, hostile_hits);

            const int action_cost = bot_action_energy_cost(game, action);
            const bool is_reflex = *speed == EffectSpeed::Reflex || *speed == EffectSpeed::Blazing;
            if (is_reflex && economy.should_reserve_reflex) {
                const int hold_penalty = 70 + economy.reflex_reserve_target / 2;
                score -= hold_penalty;
                if (effect_key == "grant_permanent_stat_growth_self"
                    && (obs.phase == TurnPhase::AttackDeclaration
                        || obs.phase == TurnPhase::BonusAttackDeclaration)) {
                    score += hold_penalty + 40;
                }
                if (hostile_hits >= 2) {
                    score += hold_penalty;
                }
            } else if (!is_reflex && economy.on_active_turn) {
                score += 35;
            }

            if (!economy.should_reserve_reflex && economy.on_active_turn && economy.float_energy_total > 0) {
                score += std::min(action_cost, economy.float_energy_total) * 8;
            }
        }
    }

    return score;
}

}  // namespace tactics::bot