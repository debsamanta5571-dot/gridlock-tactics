#include "tactics/bot/bot_policy.hpp"

#include "tactics/bot/bot_economy.hpp"
#include "tactics/bot/bot_evaluator.hpp"
#include "tactics/bot/bot_match_outcome.hpp"
#include "tactics/bot/mcts_policy.hpp"

#include "tactics/attributes/attributes.hpp"
#include "tactics/board/aether.hpp"
#include "tactics/combat/combat_resolver.hpp"
#include "tactics/combat/directional_area.hpp"
#include "tactics/combat/low_cover.hpp"
#include "tactics/combat/taunt.hpp"
#include "tactics/core/board_target_policy.hpp"
#include "tactics/effects/effect_traits.hpp"
#include "tactics/entities/entity.hpp"

#include <algorithm>
#include <stdexcept>

namespace tactics::bot {
namespace {

/** Coarse ordering so MCTS tries combat and deploys before passes. */
int action_kind_base_priority(const BotActionKind kind)
{
    switch (kind) {
    case BotActionKind::ResumeCombatViz: return 100;
    case BotActionKind::DeclareAttack: return 90;
    case BotActionKind::Deploy:
    case BotActionKind::DeployReserve: return 80;
    case BotActionKind::MoveConfirm: return 75;
    case BotActionKind::MovePreview: return 70;
    case BotActionKind::CastSpell:
    case BotActionKind::CastSpellReserve: return 65;
    case BotActionKind::ActivateAbility: return 62;
    case BotActionKind::Defend: return 52;
    case BotActionKind::Dash: return 48;
    case BotActionKind::Recover: return 46;
    case BotActionKind::ChooseEnergyZone: return 50;
    case BotActionKind::SkipEnergyZone: return 45;
    case BotActionKind::UseLand: return 58;
    case BotActionKind::ResolveTerritoryTarget: return 85;
    case BotActionKind::SkipTerritoryTarget: return 12;
    case BotActionKind::TerritoryLootDiscard: return 15;
    case BotActionKind::SkipTerritoryLoot: return 18;
    case BotActionKind::CommitAttackDeclaration: return 30;
    case BotActionKind::EndMainPhase:
    case BotActionKind::PassPriority: return 20;
    case BotActionKind::DiscardHandCard: return 15;
    case BotActionKind::ScanDiscard: return 14;
    case BotActionKind::ScanFinish: return 16;
    case BotActionKind::AttackUndeclare: return 10;
    case BotActionKind::MoveCancel: return 10;
    case BotActionKind::MoveRotate: return 5;
    }
    return 0;
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

bool legal_contains_kind(const std::vector<BotAction>& legal, const BotActionKind kind)
{
    return std::any_of(legal.begin(), legal.end(), [&](const BotAction& a) { return a.kind == kind; });
}

/** Estimate the damage the target would deal back if we attack it now.
 *  A melee attack puts us in the target's melee range, so a melee/hybrid defender
 *  with a reaction available counters for its attack. A ranged attack from range is
 *  only answered when the target has return_fire. Structures/bases never counter. */
int estimate_counter_damage(const GameState& game, const Entity& attacker, const Entity& target,
    const bool ranged_attack)
{
    const auto* td = dynamic_cast<const Unit*>(&target);
    if (!td) {
        return 0;  // structures and bases do not counterattack
    }
    if (td->reactions_remaining_this_turn <= 0 && !has_vigilance(*td)) {
        return 0;  // out of reactions this turn
    }
    if (!ranged_attack) {
        // Melee strike - a melee or hybrid defender fights back.
        if (td->attack_type == AttackType::Melee || td->attack_type == AttackType::Hybrid) {
            return unit_nominal_attack(*td);
        }
        return 0;
    }
    // Ranged strike - only return_fire units answer.
    if (has_return_fire(*td)) {
        return unit_nominal_attack(*td);
    }
    (void)game;
    (void)attacker;
    return 0;
}

/** Expected fraction of an attack that actually lands on the intended target, mirroring the
 *  engine's real miss/redirect rolls so the bot values a screened shot correctly. Three
 *  effects stack, each a coin flip:
 *   - Evasive target: flat 50% hit.
 *   - Low-cover terrain on a ranged lane: 50% the shot is soaked by the cover.
 *   - Unit body-block: EACH living unit standing on a ranged lane (an enemy screen OR our
 *     own units in the way) has a 50% chance to eat the shot, so the odds of reaching the
 *     target fall by half per intervening body (0.5^n). trueshot/flying ignore cover and
 *     body-block entirely. This is what makes the bot prefer clear lanes, decline shots
 *     into a screened backline, and see that flanking needs an angle around the wall. */
double expected_hit_fraction(const GameState& game, const Entity& attacker, const Entity& target,
    const bool ranged_attack, const int tx, const int ty)
{
    double frac = 1.0;
    if (entity_effect_amount(target, "evasive") > 0) {
        frac *= 0.5;
    }
    if (ranged_attack && !ignores_low_cover_evasion(attacker)) {
        if (find_low_cover_for_ranged_evasion(game, attacker, {tx, ty}, target) != nullptr) {
            frac *= 0.5;
        }
        // Intervening units body-block the lane at 50% apiece (engine-accurate count).
        for (int blockers = ranged_body_block_count(game, attacker, {tx, ty}, target); blockers > 0; --blockers) {
            frac *= 0.5;
        }
    }
    return frac;
}

/** Chebyshev distance from a cell to the nearest hostile base footprint (999 if none). */
int distance_cell_to_enemy_base(const GameState& game, const int player_id, const int x, const int y)
{
    int best = 999;
    for (const auto& [_, ent] : game.board.all_entities_map) {
        if (!ent || !ent->owner || !entity_is_base(*ent) || !teams_hostile(game, player_id, *ent->owner)) {
            continue;
        }
        best = std::min(best, min_chebyshev_entity_to_cell(*ent, x, y));
    }
    return best;
}

/** Chebyshev distance from a cell to the nearest living hostile unit (999 if none). */
int distance_cell_to_nearest_enemy_unit(const GameState& game, const int player_id, const int x, const int y)
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

/** How far this unit can threaten: the larger of its attack reach (melee or ranged)
 *  and the range of any damaging activated ability. Used to reward moves that bring a
 *  target into striking distance so the unit can act next turn. */
int unit_threat_range(const Unit& u)
{
    int reach = std::max(1, u.melee_range);
    if (u.attack_type == AttackType::Ranged || u.attack_type == AttackType::Hybrid) {
        reach = std::max(reach, u.ranged_range + u.bonus_ranged_length);
    }
    for (const AbilitySpec& ability : u.activated_abilities) {
        if (effect_key_deals_damage(ability.effect_key)) {
            reach = std::max(reach, ability.range_max > 0 ? ability.range_max : reach);
        }
    }
    return reach;
}

/** Count living units within `radius` (Chebyshev) of a cell. `want_hostile` selects the
 *  side; when counting friendlies, `exclude_id` drops the moving unit itself. */
int count_units_near_cell(const GameState& game, const int player_id, const int x, const int y,
    const int radius, const bool want_hostile, const std::string& exclude_id)
{
    int n = 0;
    for (const auto& [_, ent] : game.board.all_entities_map) {
        if (!ent || !ent->owner || ent->current_health <= 0 || !entity_is_board_unit(*ent)) {
            continue;
        }
        const bool hostile = teams_hostile(game, player_id, *ent->owner);
        if (hostile != want_hostile) {
            continue;
        }
        if (!want_hostile && ent->entity_id == exclude_id) {
            continue;
        }
        if (min_chebyshev_entity_to_cell(*ent, x, y) <= radius) {
            ++n;
        }
    }
    return n;
}

/** Chebyshev distance from a cell to the nearest *friendly* unit (excluding self); 999
 *  if the unit would be alone. Used to pull an isolated unit back toward the army. */
int distance_cell_to_nearest_friendly_unit(const GameState& game, const int player_id, const int x, const int y,
    const std::string& exclude_id)
{
    int best = 999;
    for (const auto& [_, ent] : game.board.all_entities_map) {
        if (!ent || !ent->owner || ent->current_health <= 0 || !entity_is_board_unit(*ent)) {
            continue;
        }
        if (teams_hostile(game, player_id, *ent->owner) || ent->entity_id == exclude_id) {
            continue;
        }
        best = std::min(best, min_chebyshev_entity_to_cell(*ent, x, y));
    }
    return best;
}

/** A unit's tactical role, used to position it in a formation rather than as a blob. */
enum class UnitRole { Tank, Ranged, Support };

/** Support = heals/repairs an ally or projects a positive allied aura. Such units want to
 *  sit behind the line, safe and in range of the units they help. */
bool unit_is_support(const Unit& u)
{
    for (const AbilitySpec& ability : u.activated_abilities) {
        if (ability.effect_key.find("heal") != std::string::npos
            || ability.effect_key.find("repair") != std::string::npos) {
            return true;
        }
    }
    for (const PassiveAbilitySpec& passive : u.passive_abilities) {
        if (passive.applies_to == "allied_units" && passive.is_positive) {
            return true;
        }
    }
    return false;
}

UnitRole classify_unit_role(const Unit& u)
{
    if (unit_is_support(u)) {
        return UnitRole::Support;
    }
    if (u.attack_type == AttackType::Ranged
        || (u.attack_type == AttackType::Hybrid && (u.ranged_range + u.bonus_ranged_length) >= 2)) {
        return UnitRole::Ranged;
    }
    return UnitRole::Tank;  // melee bodies form the frontline
}

/** Chebyshev distance from a cell to the nearest *friendly* base (999 if none). Teammate
 *  bases count as own, so this works in 2v2. */
int distance_cell_to_own_base(const GameState& game, const int player_id, const int x, const int y)
{
    int best = 999;
    for (const auto& [_, ent] : game.board.all_entities_map) {
        if (!ent || !ent->owner || !entity_is_base(*ent) || teams_hostile(game, player_id, *ent->owner)) {
            continue;
        }
        best = std::min(best, min_chebyshev_entity_to_cell(*ent, x, y));
    }
    return best;
}

/** Is a destination screened from the nearest enemy - is a friendly body or a low-cover
 *  object sitting on the enemy-facing side? A ranged/support unit tucked behind the
 *  frontline keeps its range and lets the screen eat the first blows: melee must chew
 *  through the body in front, and low cover soaks ranged fire. Units count as cover. */
bool destination_is_screened(const GameState& game, const int player_id, const int x, const int y,
    const std::string& self)
{
    int ex = 0;
    int ey = 0;
    int best = 999;
    for (const auto& [_, ent] : game.board.all_entities_map) {
        if (!ent || !ent->owner || ent->current_health <= 0 || !entity_is_board_unit(*ent)) {
            continue;
        }
        if (!teams_hostile(game, player_id, *ent->owner)) {
            continue;
        }
        const int d = min_chebyshev_entity_to_cell(*ent, x, y);
        if (d >= best) {
            continue;
        }
        if (ent->position) {
            best = d;
            ex = ent->position->first;
            ey = ent->position->second;
        } else if (!ent->occupied_positions.empty()) {
            best = d;
            ex = ent->occupied_positions.front().first;
            ey = ent->occupied_positions.front().second;
        }
    }
    if (best == 999) {
        return false;
    }
    const int sdx = (ex > x) - (ex < x);
    const int sdy = (ey > y) - (ey < y);
    // The screen sits between us and the enemy: the step toward the enemy and its two
    // orthogonal neighbours.
    const std::pair<int, int> forward[] = {{x + sdx, y + sdy}, {x + sdx, y}, {x, y + sdy}};
    for (const auto& [cx, cy] : forward) {
        if (cx == x && cy == y) {
            continue;
        }
        const std::shared_ptr<Entity> occ = game.board.entity_at(cx, cy);
        if (!occ || occ->current_health <= 0) {
            continue;
        }
        if (entity_is_low_cover(*occ)) {
            return true;
        }
        if (occ->owner && !teams_hostile(game, player_id, *occ->owner) && occ->entity_id != self
            && entity_is_board_unit(*occ)) {
            return true;  // a friendly body is screening us
        }
    }
    return false;
}

/** Prophylaxis: when an enemy is bearing down on one of our bases, reward a body that
 *  interposes on its approach - standing on the line between the threat and the base to
 *  screen it - denying the opponent's actual plan rather than chasing our own. Gated to a
 *  real, close threat so it never makes the bot passive while we are safe. */
int base_defense_interpose_bonus(const GameState& game, const int player_id, const int x, const int y)
{
    int threat_to_base = 999;
    std::pair<int, int> threat_cell{0, 0};
    for (const auto& [_, ent] : game.board.all_entities_map) {
        if (!ent || !ent->owner || ent->current_health <= 0 || !entity_is_board_unit(*ent)) {
            continue;
        }
        if (!teams_hostile(game, player_id, *ent->owner)) {
            continue;
        }
        std::pair<int, int> rep;
        if (ent->position) {
            rep = *ent->position;
        } else if (!ent->occupied_positions.empty()) {
            rep = ent->occupied_positions.front();
        } else {
            continue;
        }
        const int d = distance_cell_to_own_base(game, player_id, rep.first, rep.second);
        if (d < threat_to_base) {
            threat_to_base = d;
            threat_cell = rep;
        }
    }
    if (threat_to_base > 5) {
        return 0;  // no enemy is near our base - stay on offense
    }
    const int cell_to_base = distance_cell_to_own_base(game, player_id, x, y);
    const int threat_to_cell = std::max(std::abs(threat_cell.first - x), std::abs(threat_cell.second - y));
    // Interpose: nearer the base than the threat is, and roughly on its path to the base.
    if (cell_to_base < threat_to_base && threat_to_cell + cell_to_base <= threat_to_base + 1) {
        return 60;
    }
    return 0;
}

/** True if this hostile can imminently reach one of our bases - within a move + a strike  - 
 *  i.e. a runner that has slipped behind our line and menaces the base next turn. */
bool enemy_threatens_own_base(const GameState& game, const int player_id, const Entity& enemy)
{
    std::pair<int, int> from;
    if (enemy.position) {
        from = *enemy.position;
    } else if (!enemy.occupied_positions.empty()) {
        from = enemy.occupied_positions.front();
    } else {
        return false;
    }
    const auto* u = dynamic_cast<const Unit*>(&enemy);
    const int reach = u ? unit_threat_range(*u) : 1;
    // Imminent only: it can strike the base this coming turn (in reach, or one step away).
    // Using full movement here flagged half the midgame and turtled the whole army.
    return distance_cell_to_own_base(game, player_id, from.first, from.second) <= reach + 1;
}

/** How badly our base needs a defender: summed attack of hostiles that can imminently reach
 *  it, discounted when a friendly body is already guarding the base. 0 when the base is safe.
 *  Drives a defensive recall so the bot doesn't win the board while its own door falls. */
int own_base_defense_need(const GameState& game, const int player_id)
{
    int threat = 0;
    int defenders = 0;
    int base_hp = 0;
    for (const auto& [_, ent] : game.board.all_entities_map) {
        if (!ent || !ent->owner || ent->current_health <= 0) {
            continue;
        }
        const bool hostile = teams_hostile(game, player_id, *ent->owner);
        if (entity_is_base(*ent)) {
            if (!hostile) {
                base_hp += ent->current_health;
            }
            continue;
        }
        if (!entity_is_board_unit(*ent)) {
            continue;
        }
        if (hostile) {
            if (enemy_threatens_own_base(game, player_id, *ent)) {
                const auto* u = dynamic_cast<const Unit*>(ent.get());
                threat += u ? std::max(1, unit_nominal_attack(*u)) : 1;
            }
        } else {
            std::pair<int, int> at;
            if (ent->position) {
                at = *ent->position;
            } else if (!ent->occupied_positions.empty()) {
                at = ent->occupied_positions.front();
            } else {
                continue;
            }
            if (distance_cell_to_own_base(game, player_id, at.first, at.second) <= 2) {
                ++defenders;
            }
        }
    }
    if (threat == 0) {
        return 0;
    }
    // Only a genuine emergency justifies pulling a unit off the plan: the base is already low
    // or the incoming firepower could level it in a couple of turns. A healthy base near an
    // enemy doesn't need a recall (killing the runner in combat handles the routine case)  - 
    // this is what stops the defensive pull from turtling both armies into a draw.
    if (base_hp > threat * 3) {
        return 0;
    }
    if (defenders >= 1) {
        threat /= 3;  // base is already partly covered - don't recall the whole army
    }
    return threat;
}

/** Can any living hostile unit plausibly strike this unit next turn (its threat range
 *  plus its movement reaches the unit)? Drives the pre-emptive Defend stance: brace when
 *  a hit is coming. */
bool enemy_can_strike_unit_soon(const GameState& game, const int player_id, const Entity& unit)
{
    std::pair<int, int> at;
    if (unit.position) {
        at = *unit.position;
    } else if (!unit.occupied_positions.empty()) {
        at = unit.occupied_positions.front();
    } else {
        return false;
    }
    for (const auto& [_, ent] : game.board.all_entities_map) {
        if (!ent || !ent->owner || ent->current_health <= 0 || !entity_is_board_unit(*ent)) {
            continue;
        }
        if (!teams_hostile(game, player_id, *ent->owner)) {
            continue;
        }
        const auto* eu = dynamic_cast<const Unit*>(ent.get());
        if (!eu) {
            continue;
        }
        const int reach = unit_threat_range(*eu) + std::max(0, eu->movement + eu->aura_bonus_movement);
        if (min_chebyshev_entity_to_cell(*ent, at.first, at.second) <= reach) {
            return true;
        }
    }
    return false;
}

/** Does any living hostile unit field a damaging area/blast (directional-aim) ability? If
 *  so, bunching our army up is dangerous - one shot hits several units - so movement should
 *  spread out near the enemy instead of massing. */
bool enemy_has_aoe_threat(const GameState& game, const int player_id)
{
    for (const auto& [_, ent] : game.board.all_entities_map) {
        if (!ent || !ent->owner || ent->current_health <= 0 || !entity_is_board_unit(*ent)) {
            continue;
        }
        if (!teams_hostile(game, player_id, *ent->owner)) {
            continue;
        }
        for (const AbilitySpec& ability : ent->activated_abilities) {
            if (effect_key_deals_damage(ability.effect_key) && effect_uses_directional_aim(ability.effect_key)) {
                return true;
            }
        }
    }
    return false;
}

/** Terrain quality of a destination cell: heavily avoid the void and damaging tiles (gas,
 *  fire, spikes); a mild bonus for standing in a trench (defensive terrain). */
int cell_terrain_score(const GameState& game, const int x, const int y)
{
    const auto sq = game.board.get_square(x, y);
    if (!sq) {
        return 0;
    }
    int score = 0;
    for (const SquareModifier& mod : sq->modifiers) {
        if (mod.is_void) {
            score -= 500;  // stepping into the void removes the unit - almost never worth it
        }
        score -= std::max(0, mod.damage_on_enter) * 10;  // damaging tile
        if (mod.name.find("trench") != std::string::npos) {
            score += 12;  // dug-in defensive terrain
        }
    }
    return score;
}

/** The highest-value target this attacker could reach with its threat range right now (an
 *  enemy base counts as the top objective). Used to penalise wasting a valuable attacker on a
 *  lesser target when a better one is in reach - e.g. a Sentinel swinging at a 1-HP token. */
double best_reachable_target_value(const GameState& game, const Entity& attacker)
{
    const auto* attacker_unit = dynamic_cast<const Unit*>(&attacker);
    if (!attacker_unit || !attacker.owner) {
        return 0.0;
    }
    std::pair<int, int> from;
    if (attacker.position) {
        from = *attacker.position;
    } else if (!attacker.occupied_positions.empty()) {
        from = attacker.occupied_positions.front();
    } else {
        return 0.0;
    }
    const int reach = unit_threat_range(*attacker_unit);
    double best = 0.0;
    for (const auto& [_, ent] : game.board.all_entities_map) {
        if (!ent || !ent->owner || ent->current_health <= 0) {
            continue;
        }
        if (!teams_hostile(game, *attacker.owner, *ent->owner)) {
            continue;
        }
        if (min_chebyshev_entity_to_cell(*ent, from.first, from.second) > reach) {
            continue;
        }
        if (entity_is_base(*ent)) {
            best = std::max(best, 60.0);  // the base is always the best thing to be hitting
        } else {
            best = std::max(best, std::min(piece_value(*ent), 45.0));
        }
    }
    return best;
}

/** Quality of a proposed move destination. A wounded, valuable unit retreats to safety.
 *  Otherwise the score models *coordinated* maneuver rather than a lone rush: units are
 *  rewarded for massing (staying near allies), for advancing only with local support or
 *  local numerical superiority, and are penalized for over-extending alone into a
 *  numerically superior enemy. An isolated unit is pulled back toward the army so the
 *  bot builds up a formation before committing to a push. */
int move_destination_score(const GameState& game, const BotAction& action)
{
    const auto it = game.board.all_entities_map.find(action.entity_id);
    if (it == game.board.all_entities_map.end() || !it->second) {
        return 0;
    }
    const Entity& unit = *it->second;
    const std::string& self = unit.entity_id;

    int aether_adj = 0;
    if (aether_would_kill_entity(game, action.player_id, unit, action.x, action.y)) {
        aether_adj -= 220;
    } else if (unit.position) {
        const auto [cx, cy] = *unit.position;
        if (aether_would_kill_entity(game, action.player_id, unit, cx, cy)
            && !is_aether_world_cell(game, action.x, action.y)) {
            aether_adj += 90;
        }
    }

    const BoardCellBounds bounds = game.board.cell_bounds();
    const int span = std::max(1, std::max(bounds.span_x(), bounds.span_y()));
    const int to_base = distance_cell_to_enemy_base(game, action.player_id, action.x, action.y);
    const int advance = (to_base < 999) ? std::max(0, span - to_base) : 0;
    const int to_enemy = distance_cell_to_nearest_enemy_unit(game, action.player_id, action.x, action.y);

    const int base_hp = std::max(1, entity_effective_base_health(unit));
    const bool wounded = unit.current_health * 2 < base_hp;
    const bool valuable = piece_value(unit) >= 12.0;

    // Terrain quality of the destination - avoid the void / damaging tiles, prefer a trench.
    // Applies on every path, including a retreat (don't flee into a hazard).
    const int terrain = cell_terrain_score(game, action.x, action.y);

    if (wounded && valuable) {
        // Preserve the asset: reward distance from the nearest threat, keep a mild
        // forward pull so it does not run to a useless corner.
        const int safety = (to_enemy < 999) ? std::min(to_enemy, span) : span;
        return safety * 4 + advance + terrain + aether_adj;
    }

    // Local force picture around the destination (radius 2 = "supporting distance").
    const int support = count_units_near_cell(game, action.player_id, action.x, action.y, 2, false, self);
    const int enemies = count_units_near_cell(game, action.player_id, action.x, action.y, 2, true, self);

    int score = 0;

    // Endgame clock for maneuver: in a long, even game the formation caution below keeps
    // both armies brawling mid-board forever while sudden death looms (it compares base
    // HP). Ramp the advance weight with the round so late-game movement seeks the enemy
    // base - someone eventually breaks through instead of trading to a full-HP draw.
    const int round = game.turn_manager.round_number;
    int advance_weight = 3 + std::min(6, std::max(0, round - 10) / 3);
    if (const int sd_urgency = sudden_death_base_hp_urgency(game, action.player_id); sd_urgency > 0) {
        advance_weight += std::min(8, sd_urgency / 35);
    }

    // Cohesion: an army fights better massed than dribbled forward piecemeal.
    score += std::min(support, 3) * 15;

    // Spread vs area damage: if a hostile fields a blast/AoE ability, bunching into contact
    // invites a multi-hit that erases the formation - discourage clustering next to enemies.
    if (enemies > 0 && support >= 2 && enemy_has_aoe_threat(game, action.player_id)) {
        score -= (support - 1) * 18;
    }

    if (enemies == 0) {
        // No contact - free to advance and stage forward.
        score += advance * advance_weight;
    } else if (support >= enemies) {
        // Local superiority (counting this unit, at least even) - commit the push.
        score += advance * advance_weight + 20;
    } else {
        // Outnumbered here - do not throw the unit away. Weak forward pull, and a penalty
        // that grows with how badly it is outmatched, so the bot waits and masses instead.
        score += advance;
        score -= (enemies - support) * 25;
    }

    // Strike positioning - only worth it if the unit won't be stranded alone.
    if (const auto* u = dynamic_cast<const Unit*>(&unit)) {
        const int reach = unit_threat_range(*u);
        if (to_enemy <= reach && (support >= 1 || enemies <= 1)) {
            score += 45;  // an enemy is in strike range and we aren't isolated
        }
        if (to_base <= reach) {
            score += 90;  // the enemy base is in strike range - always worth threatening
        }

        // Role-aware formation: tanks take the front, ranged and support stay behind it.
        const UnitRole role = classify_unit_role(*u);
        switch (role) {
        case UnitRole::Ranged: {
            const int band = std::max(2, u->ranged_range + u->bonus_ranged_length);
            if (to_enemy <= 1) {
                score -= 60;  // a ranged unit in melee wastes its reach and eats counters
            } else if (to_enemy <= band) {
                score += 40;  // hold the ranged band - hit from safety
            }
            break;
        }
        case UnitRole::Support:
            // Hang back near the allies it supports; punish exposure to the enemy.
            score += std::min(to_enemy, span) * 3;
            score += std::min(support, 3) * 20;
            if (to_enemy <= 2) {
                score -= 45;
            }
            break;
        case UnitRole::Tank:
            if (to_enemy <= 2) {
                score += 30;  // hold the front line and screen the softer units
            }
            break;
        }

        // Cover: a ranged/support unit tucked behind the frontline (a friendly body or low
        // cover on the enemy-facing side) keeps its range while the screen eats the first
        // blows. Only rewarded out of melee, where the screen actually matters.
        if ((role == UnitRole::Ranged || role == UnitRole::Support) && to_enemy >= 2
            && destination_is_screened(game, action.player_id, action.x, action.y, self)) {
            score += 40;
        }

        // Prophylaxis: screen our own base against an incoming enemy. A tank body-blocking
        // the approach is worth more than the softer roles doing the same.
        const int interpose = base_defense_interpose_bonus(game, action.player_id, action.x, action.y);
        score += (role == UnitRole::Tank) ? interpose : interpose / 2;

        // Taunt: a taunt body wants to be at the front, in contact, forcing enemies to
        // engage it instead of the softer units it stands in front of.
        if (has_taunt(*u)) {
            if (to_enemy <= 2) {
                score += 45;
            }
            score += std::min(support, 3) * 8;
        }
    }

    // Defensive recall: if a hostile is about to reach our under-defended base, pull a body
    // back to guard it - the harder it can hit, the stronger the pull toward home. This is
    // what stops the bot winning the board while its own door caves in.
    if (const int def_need = own_base_defense_need(game, action.player_id); def_need > 0) {
        const int to_own_base = distance_cell_to_own_base(game, action.player_id, action.x, action.y);
        if (to_own_base < 999) {
            // Pull *one* body home - modest so it doesn't sap the whole offence into a turtle
            // (the `defenders>=1` discount in own_base_defense_need keeps it to one recall).
            score += std::max(0, span - to_own_base) * std::min(def_need, 6);
        }
    }

    // Terrain quality applies wherever the unit ends up.
    score += terrain;

    // Objective tiles (scanner / omni-energy / aether): reward moving a control-eligible unit
    // onto a key square it would start (or contest) controlling - card advantage, energy ramp,
    // or escalating base damage. The over-extension penalty above still applies, so the bot
    // commits to an objective only when the payout justifies the exposure. See BOT_PHILOSOPHY.md.
    {
        constexpr double kObjectivePolicyScale = 5.0;
        score += static_cast<int>(
            objective_cell_capture_value(game, action.player_id, unit, action.x, action.y) * kObjectivePolicyScale);
    }

    // Regroup: a unit with no nearby allies is pulled toward the army's body.
    if (support == 0) {
        const int to_friends = distance_cell_to_nearest_friendly_unit(game, action.player_id, action.x, action.y, self);
        if (to_friends < 999) {
            score += std::max(0, span - to_friends) * 2;
        }
    }

    return score + aether_adj;
}

int combat_action_score(const GameState& game, const BotObservation& obs, const std::vector<BotAction>& legal,
    const BotAction& action)
{
    int score = 0;

    // Board material advantage: when we field clearly more value than the enemy we can
    // afford trades and should PRESS - force the game toward the enemy base instead of
    // hoarding a winning army. Measured in piece-value points (~one good unit = 15-40).
    const int board_adv = obs.own_board_value - obs.enemy_board_value;
    const bool ahead = board_adv >= 15;
    const bool big_lead = board_adv >= 45;

    if (action.kind == BotActionKind::DeclareAttack) {
        score += 300;  // attacking at all is good baseline tempo
        if (ahead) {
            score += 40;  // press: initiate combat more readily when we hold the board
        }

        const auto attacker_it = game.board.all_entities_map.find(action.entity_id);
        const Entity* attacker_ent = (attacker_it != game.board.all_entities_map.end()) ? attacker_it->second.get() : nullptr;
        const bool attacker_is_base = attacker_ent && entity_is_base(*attacker_ent);

        // Identify the hostile target under the aimed cell.
        const Entity* target = nullptr;
        for (const auto& [_, ent] : game.board.all_entities_map) {
            if (!ent || !ent->owner || !teams_hostile(game, action.player_id, *ent->owner)) {
                continue;
            }
            if (cell_on_entity_footprint(*ent, action.x, action.y)) {
                target = ent.get();
                break;
            }
        }

        if (attacker_is_base) {
            // High-value turret shot: costs the base's attack action but the base cannot
            // move or trade - fire whenever a hostile is in range.
            score += 520;
            if (target) {
                if (entity_is_base(*target)) {
                    score += 350;
                } else {
                    const double tv = piece_value(*target);
                    score += static_cast<int>(std::min(tv, 45.0) * 5.0);
                    if (enemy_threatens_own_base(game, action.player_id, *target)) {
                        score += 320;
                    }
                    if (const auto* base_unit = dynamic_cast<const Unit*>(attacker_ent)) {
                        const DamageRange dr = unit_effective_ranged_damage_range(*base_unit);
                        const int effective_hp = target->current_health + std::max(0, armor_value(*target));
                        if (dr.max >= effective_hp) {
                            score += 220;
                        }
                    }
                }
            }
        } else if (target && entity_is_base(*target)) {
            score += 500;  // the win condition - always the top target
            if (ahead) {
                score += big_lead ? 250 : 120;  // convert a board lead into base damage
            }
            // Endgame clock: sudden death decides standoffs by comparing base HP, so as a
            // game runs long, chipping the base outranks yet another even unit trade. This
            // breaks mirror-match turtles where neither side ever gains the material lead
            // the press term needs.
            score += std::min(180, std::max(0, obs.round_number - 8) * 9);
            // Sudden-death tiebreak: when every deck is empty and our team trails on base
            // HP, race to close the gap before the clock expires (no urgency when ahead).
            score += sudden_death_base_hp_urgency(game, action.player_id);
        } else if (target) {
            const Entity* attacker = attacker_ent;

            // Expected-hit discount: attacking an evasive target (or a ranged shot into
            // cover) is a coin flip, so its value is worth roughly half.
            const double hit = attacker
                ? expected_hit_fraction(game, *attacker, *target, action.ranged, action.x, action.y)
                : 1.0;

            // Value of removing this target - a Sentinel/engine is worth far more than a
            // token. Capped so it never eclipses a base, and scaled by the odds of landing.
            const double tv = piece_value(*target);
            score += static_cast<int>(std::min(tv, 40.0) * 4.0 * hit);  // up to +160

            // Opportunity cost: don't feed a valuable attacker into a near-worthless target
            // when a better one is in reach (the seed-7 mistake - a Sentinel swinging at a
            // 1-HP token while real threats stand next to it). Penalty scales with the
            // attacker's own worth and how much better the best reachable target is.
            if (attacker) {
                const double best_alt = best_reachable_target_value(game, *attacker);
                if (best_alt > tv + 6.0) {
                    const double our_value = piece_value(*attacker);
                    score -= static_cast<int>(std::min(our_value, 30.0) * std::min(best_alt - tv, 40.0) * 0.35);
                }
            }

            // Flanking: punching through to the enemy's soft backline (support/ranged) is
            // worth more than trading blows with the frontline tank it hides behind. There
            // is no flank bonus for hitting the wall itself.
            if (const auto* target_unit = dynamic_cast<const Unit*>(target)) {
                switch (classify_unit_role(*target_unit)) {
                case UnitRole::Support: score += static_cast<int>(70 * hit); break;  // kill the healers/buffers
                case UnitRole::Ranged:  score += static_cast<int>(45 * hit); break;  // silence the shooters
                case UnitRole::Tank:    break;                                        // trading with the wall isn't a flank
                }
            }

            // Focus fire: concentrate on an already-wounded enemy to remove the piece - this
            // also finishes what an ally began, which is how a team gangs a target down (2v2).
            if (target->current_health * 2 < std::max(1, entity_effective_base_health(*target))) {
                score += static_cast<int>(45 * hit);
            }
            // Roadblock removal: killing an enemy taunt frees our other attackers and opens
            // movement that the taunt was leashing.
            if (const auto* tu = dynamic_cast<const Unit*>(target); tu && has_taunt(*tu)) {
                score += 70;
            }

            // Base defense: an enemy about to reach our base is a priority target - cut down
            // the runner even while we are pressing elsewhere (the seed-8 backdoor).
            if (enemy_threatens_own_base(game, action.player_id, *target)) {
                score += 170;
            }

            const auto* attacker_unit = dynamic_cast<const Unit*>(attacker);
            int atk = attacker_unit ? unit_nominal_attack(*attacker_unit) : 0;
            atk += attacker ? std::max(0, entity_effect_amount(*attacker, "next_damage_bonus")) : 0;
            // Precise lethal: damage must beat HP *plus armor* to actually kill.
            const int effective_hp = target->current_health + std::max(0, armor_value(*target));
            const bool kills = atk >= effective_hp;

            // Overkill guard: if attacks already declared this turn are enough to kill this
            // target, another strike on it is wasted - steer attackers to a fresh target.
            int pending_damage = 0;
            for (const GameState::AttackDeclaration& decl : game.pending_attack_declarations()) {
                if (!cell_on_entity_footprint(*target, decl.target_x, decl.target_y)) {
                    continue;
                }
                const auto dit = game.board.all_entities_map.find(decl.attacker_id);
                if (dit != game.board.all_entities_map.end() && dit->second) {
                    if (const auto* du = dynamic_cast<const Unit*>(dit->second.get())) {
                        pending_damage += unit_nominal_attack(*du);
                    }
                }
            }
            if (pending_damage >= effective_hp) {
                score -= 220;  // already lethal - do not pile on
            }

            if (kills) {
                // A secured kill is worth more the more the target is worth - removing a
                // Sentinel is not the same as stepping on a 1-HP token, so scale by target
                // value instead of a flat bonus (which used to over-reward chip kills).
                score += static_cast<int>((40.0 + std::min(tv, 40.0) * 6.0) * hit);
            } else if (atk >= effective_hp - 1) {
                score += static_cast<int>(40 * hit);   // nearly lethal
            }

            // Counterattack risk: is this attack *bad*? If the return blow would kill our
            // unit, weigh what we gain against what we lose. This is how a good player
            // decides an attack is not worth it - the counter hurts more than it helps.
            // Penalties are sized to rank a bad attack below a neutral play, but not so
            // harsh that the bot refuses every trade and stalls the game.
            if (attacker) {
                const int counter = estimate_counter_damage(game, *attacker, *target, action.ranged);
                const bool we_die = counter >= attacker->current_health;
                if (we_die) {
                    const double our_value = piece_value(*attacker);
                    // When we hold a board lead, trades favor us - every unit-for-unit
                    // exchange shrinks the board toward our advantage - so soften the
                    // penalties and let the bot cash its lead in through combat.
                    const double trade_scale = ahead ? 0.5 : 1.0;
                    if (kills) {
                        if (our_value > tv) {
                            score -= static_cast<int>((our_value - tv) * 2.0 * trade_scale);
                        }
                    } else {
                        score -= static_cast<int>(160 * trade_scale);
                    }
                }
            }
        }
    }

    if (action.kind == BotActionKind::CommitAttackDeclaration) {
        if (!game.pending_attack_declarations().empty()) {
            score += 220;
            score += sudden_death_base_hp_urgency(game, action.player_id) / 2;
        }
        if (obs.phase == TurnPhase::AttackDeclaration || obs.phase == TurnPhase::BonusAttackDeclaration) {
            if (legal_contains_kind(legal, BotActionKind::MovePreview)) {
                score -= 350;
            }
        }
    }

    if (obs.phase == TurnPhase::AttackDeclaration || obs.phase == TurnPhase::BonusAttackDeclaration) {
        if (action.kind == BotActionKind::MovePreview) {
            score += 200;
        }
    }

    // Destination quality: which cell a move goes to matters. Advance healthy units
    // toward the enemy base; retreat wounded, valuable ones to safety. Applies in the
    // maneuvering phases (Main / Second Main / Attack Declaration).
    if (action.kind == BotActionKind::MovePreview
        && (obs.phase == TurnPhase::Main || obs.phase == TurnPhase::SecondMain
            || obs.phase == TurnPhase::AttackDeclaration || obs.phase == TurnPhase::BonusAttackDeclaration)) {
        score += move_destination_score(game, action);
    }

    if (action.kind == BotActionKind::EndMainPhase) {
        if (legal_contains_kind(legal, BotActionKind::MovePreview)) {
            score -= 200;
        }
    }

    // Defend/Dash are pre-emptive main-phase stances that SPEND THE ATTACK BUDGET  - 
    // taking one means this unit does not attack this turn, so the default is a penalty
    // and the bonus only fires when bracing is genuinely the best use of the action.
    if (action.kind == BotActionKind::Defend || action.kind == BotActionKind::Dash
        || action.kind == BotActionKind::Recover) {
        const auto uit = game.board.all_entities_map.find(action.entity_id);
        const auto* unit = (uit != game.board.all_entities_map.end() && uit->second)
            ? dynamic_cast<const Unit*>(uit->second.get())
            : nullptr;
        if (unit) {
            std::pair<int, int> at{0, 0};
            if (unit->position) {
                at = *unit->position;
            } else if (!unit->occupied_positions.empty()) {
                at = unit->occupied_positions.front();
            }
            const int to_enemy = distance_cell_to_nearest_enemy_unit(game, action.player_id, at.first, at.second);
            // The attack budget's opportunity cost includes moving into range first - a
            // unit that could move-and-strike this turn should do that, not brace. (Gating
            // on static reach alone made half the army defend every turn and turtled games
            // into full-HP standoffs.)
            const int mobile_reach = unit_threat_range(*unit) + std::max(0, unit->movement + unit->aura_bonus_movement);
            const bool can_attack_something = to_enemy <= mobile_reach
                || distance_cell_to_enemy_base(game, action.player_id, at.first, at.second) <= mobile_reach;
            const bool expects_hit = enemy_can_strike_unit_soon(game, action.player_id, *unit);

            if (action.kind == BotActionKind::Defend) {
                const int base_hp = std::max(1, entity_effective_base_health(*unit));
                const bool wounded = unit->current_health * 2 < base_hp;
                // Brace only when it protects something worth protecting AND the attack
                // budget genuinely has no use: a hit is coming, no strike is available.
                const bool worth_protecting = wounded || piece_value(*unit) >= 12.0 || has_taunt(*unit);
                if (expects_hit && !can_attack_something && worth_protecting) {
                    score += 90;
                    if (wounded) {
                        score += 40;  // wounded - the +1 armor buys the most here
                    }
                    if (has_taunt(*unit)) {
                        score += 30;  // a bracing taunt is exactly what taunt is for
                    }
                } else {
                    score -= 90;  // attacking (or staying flexible) beats a pointless stance
                }
                if (const int sd_urgency = sudden_death_base_hp_urgency(game, action.player_id); sd_urgency > 0) {
                    score -= sd_urgency / 2;  // trailing on base HP - spend the attack budget racing
                }
            } else if (action.kind == BotActionKind::Dash) {
                // +1 movement is worth it only when no strike is available even after
                // moving - the extra step closes distance for next turn.
                if (!can_attack_something && to_enemy < 999) {
                    score += 25;
                } else {
                    score -= 90;
                }
            } else {  // Recover
                const int max_hp = std::max(1, entity_effective_base_health(*unit));
                const bool wounded = unit->current_health < max_hp;
                if (!expects_hit && wounded && !can_attack_something) {
                    score += 70;
                } else {
                    score -= 90;
                }
            }
        }
    }

    if (action.kind == BotActionKind::AttackUndeclare) {
        if (legal_contains_kind(legal, BotActionKind::DeclareAttack)) {
            score -= 220;
        } else {
            score -= 60;
        }
    }

    if (action.kind == BotActionKind::MoveCancel || action.kind == BotActionKind::MoveRotate) {
        score -= 120;
    }

    // Opponent threat assessment: if the enemy is tapped out or empty-handed they cannot
    // punish an over-commit with a reflex answer, so it is safe to press. Expressed as a
    // bonus for aggression when safe (rather than a penalty when unsafe, which risks
    // passivity) - when they *can* react, the printed counterattack/miss risk already
    // captured above stands as the default caution.
    if (!obs.opponent_can_react && obs.on_active_turn) {
        if (action.kind == BotActionKind::DeclareAttack) {
            score += 35;  // no answer available - attack freely
        } else if (action.kind == BotActionKind::Deploy || action.kind == BotActionKind::DeployReserve) {
            score += 20;  // safe to develop the board / over-commit
        }
    }

    if (obs.phase == TurnPhase::Energy) {
        if (action.kind == BotActionKind::ChooseEnergyZone) {
            score += 400;
        }
        if (action.kind == BotActionKind::SkipEnergyZone) {
            score -= 500;
        }
    }

    return score;
}

int greedy_action_score(const GameState& game, const BotObservation& obs, const BotEconomySnapshot& economy,
    const std::vector<BotAction>& legal, const BotAction& action)
{
    return action_kind_base_priority(action.kind) + combat_action_score(game, obs, legal, action)
        + score_bot_action_economy(game, obs, economy, legal, action);
}

}  // namespace

BotAction RandomLegalPolicy::choose(const GameState&, const BotObservation&, const std::vector<BotAction>& legal,
    std::mt19937& rng) const
{
    if (legal.empty()) {
        throw std::runtime_error("RandomLegalPolicy: no legal actions");
    }
    std::uniform_int_distribution<std::size_t> dist(0, legal.size() - 1);
    return legal[dist(rng)];
}

int score_bot_action(const GameState& game, const BotObservation& obs, const std::vector<BotAction>& legal,
    const BotAction& action)
{
    const BotEconomySnapshot economy = build_bot_economy_snapshot(game, obs);
    return greedy_action_score(game, obs, economy, legal, action);
}

std::vector<int> score_bot_actions(const GameState& game, const BotObservation& obs,
    const std::vector<BotAction>& legal)
{
    // Build the economy snapshot once, then score every action against it.
    const BotEconomySnapshot economy = build_bot_economy_snapshot(game, obs);
    std::vector<int> scores;
    scores.reserve(legal.size());
    for (const BotAction& action : legal) {
        scores.push_back(greedy_action_score(game, obs, economy, legal, action));
    }
    return scores;
}

std::unique_ptr<IBotPolicy> make_bot_policy(const std::string& name, const BotPolicyOptions& options)
{
    // There is only one bot now: MCTS, which internally uses the shared action scorer
    // (score_bot_actions) as its prior, rollout, and non-tactical decision-maker. The old
    // "greedy" name is kept as an alias so existing scripts keep working, but it resolves
    // to the same unified MCTS bot.
    if (name == "mcts" || name == "greedy") {
        return std::make_unique<MctsPolicy>(options.mcts);
    }
    if (name == "random" || name.empty()) {
        return std::make_unique<RandomLegalPolicy>();
    }
    throw std::invalid_argument("Unknown bot policy: " + name);
}

}  // namespace tactics::bot