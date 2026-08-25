#include "tactics/combat/combat_resolver.hpp"

#include "tactics/combat/frenzy.hpp"
#include "tactics/combat/evasive.hpp"
#include "tactics/combat/low_cover.hpp"

#include "tactics/attributes/attributes.hpp"
#include "tactics/board/adjacency.hpp"
#include "tactics/combat/taunt.hpp"
#include "tactics/board/board.hpp"
#include "tactics/board/trench.hpp"
#include "tactics/combat/soul_steal.hpp"
#include "tactics/common/effect_keys.hpp"
#include "tactics/core/game_state.hpp"
#include "tactics/core/passive_action_order.hpp"

#include <algorithm>
#include <cmath>
#include <climits>
#include <cstdlib>
#include <random>
#include <set>

namespace tactics {
namespace {

inline constexpr const char* kNextDamageBonusEffectKey = "next_damage_bonus";

bool entity_is_single_tile_footprint(const Entity& e)
{
    return entity_shape_offsets(e).size() == 1;
}

/** Swaps two 1x1 units on the board (used by push/displacement). */
bool swap_single_tile_units(GameState& game, const std::shared_ptr<Unit>& a, const std::shared_ptr<Unit>& b)
{
    if (!a || !b || !a->position || !b->position) {
        return false;
    }
    if (!entity_is_single_tile_footprint(*a) || !entity_is_single_tile_footprint(*b)) {
        return false;
    }
    const auto pos_a = *a->position;
    const auto pos_b = *b->position;
    if (!game.board.remove_entity(a)) {
        return false;
    }
    if (!game.board.remove_entity(b)) {
        game.board.place_entity(a, pos_a.first, pos_a.second);
        return false;
    }
    if (!game.board.place_entity(b, pos_a.first, pos_a.second)) {
        game.board.place_entity(a, pos_a.first, pos_a.second);
        game.board.place_entity(b, pos_b.first, pos_b.second);
        return false;
    }
    if (!game.board.place_entity(a, pos_b.first, pos_b.second)) {
        game.board.remove_entity(b);
        game.board.place_entity(a, pos_a.first, pos_a.second);
        game.board.place_entity(b, pos_b.first, pos_b.second);
        return false;
    }
    return true;
}

std::shared_ptr<Unit> find_valiant_guard_interceptor(
    GameState& game, const Entity& victim, const int attacker_owner)
{
    if (victim.entity_type != "unit" || !victim.owner) {
        return nullptr;
    }
    if (!teams_hostile(game, attacker_owner, *victim.owner)) {
        return nullptr;
    }
    if (!entity_is_single_tile_footprint(victim)) {
        return nullptr;
    }

    std::vector<std::shared_ptr<Unit>> candidates;
    for (const auto& [_, ent] : game.board.all_entities_map) {
        if (!ent || ent->current_health <= 0 || ent->entity_type != "unit" || !ent->owner) {
            continue;
        }
        if (teams_hostile(game, *victim.owner, *ent->owner)) {
            continue;
        }
        auto guard = std::dynamic_pointer_cast<Unit>(ent);
        if (!guard || entity_is_silenced(*guard) || entity_is_stunned(*guard)) {
            continue;
        }
        const bool has_valiant_guard = std::any_of(
            guard->temporary_effects.begin(), guard->temporary_effects.end(),
            [](const TemporaryEntityEffect& eff) {
                return eff.effect_id == effect_keys::kValiantGuardEffectId;
            });
        if (!has_valiant_guard) {
            continue;
        }
        bool victim_surrounds_guard = false;
        for (const auto& [cx, cy] : entity_surrounding_cells(*guard)) {
            const auto cell_ent = game.board.entity_at(cx, cy);
            if (cell_ent && cell_ent->entity_id == victim.entity_id) {
                victim_surrounds_guard = true;
                break;
            }
        }
        if (!victim_surrounds_guard) {
            continue;
        }
        candidates.push_back(guard);
    }
    if (candidates.empty()) {
        return nullptr;
    }
    const auto order_ranks = compute_passive_action_order_ranks(game.board);
    std::sort(candidates.begin(), candidates.end(),
        [&order_ranks](const std::shared_ptr<Unit>& lhs, const std::shared_ptr<Unit>& rhs) {
            const auto rank = [&order_ranks](const std::string& id) {
                const auto it = order_ranks.find(id);
                return it == order_ranks.end() ? INT_MAX : it->second;
            };
            const int rl = rank(lhs->entity_id);
            const int rr = rank(rhs->entity_id);
            if (rl != rr) {
                return rl < rr;
            }
            return lhs->entity_id < rhs->entity_id;
        });
    return candidates.front();
}

void consume_valiant_guard_effect(Unit& guard)
{
    guard.temporary_effects.erase(
        std::remove_if(guard.temporary_effects.begin(), guard.temporary_effects.end(),
            [](const TemporaryEntityEffect& eff) {
                return eff.effect_id == effect_keys::kValiantGuardEffectId;
            }),
        guard.temporary_effects.end());
}

bool maybe_valiant_guard_intercept(
    GameState& game,
    const int attacker_owner,
    std::shared_ptr<Entity>& target,
    std::shared_ptr<Unit>& target_unit,
    Unit& defender_pre_hit)
{
    if (!target || !target_unit) {
        return false;
    }
    auto guard = find_valiant_guard_interceptor(game, *target, attacker_owner);
    if (!guard) {
        return false;
    }
    if (!swap_single_tile_units(game, guard, target_unit)) {
        return false;
    }
    consume_valiant_guard_effect(*guard);
    target = guard;
    target_unit = guard;
    defender_pre_hit = *guard;
    return true;
}

struct CounterattackProfile {
    bool ok{false};
    bool is_melee_attack{false};
    DamageRange damage_range{};
    int crit_chance_percent{kDefaultCritChancePercent};
    DamageType damage_type{DamageType::Physical};
};

const Entity* entity_for_ranged_combat_resolution(const GameState& game, const Entity& actor, std::optional<Unit>& pose_storage)
{
    pose_storage.reset();
    const auto it = game.board.all_entities_map.find(actor.entity_id);
    if (it == game.board.all_entities_map.end()) {
        return &actor;
    }
    const auto unit = std::dynamic_pointer_cast<Unit>(it->second);
    if (!unit) {
        return &actor;
    }
    const std::shared_ptr<Unit> pose = game.unit_at_validation_pose(unit);
    if (!pose || pose.get() == unit.get()) {
        return &actor;
    }
    pose_storage = *pose;
    return &*pose_storage;
}

bool square_blocks_los_for_combat(const GridSquare& sq)
{
    for (const auto& mod : sq.modifiers) {
        if (mod.blocks_line_of_sight) {
            return true;
        }
    }
    return false;
}

bool line_of_sight_clear_ignoring_units(const GameState& game, const LOSResult& los)
{
    if (!los.query_valid || los.path.size() < 2) {
        return false;
    }
    for (const auto& [x, y] : los.path) {
        if ((x == los.path.front().first && y == los.path.front().second)
            || (x == los.path.back().first && y == los.path.back().second)) {
            continue;
        }
        const auto sq = game.board.get_square(x, y);
        if (!sq) {
            return false;
        }
        if (square_blocks_los_for_combat(*sq)) {
            return false;
        }
        if (sq->entity && sq->entity->entity_type != "unit" && entity_blocks_line_of_sight(*sq->entity)) {
            return false;
        }
    }
    return true;
}

bool has_line_of_sight_from_footprint(const GameState& game, const Entity& actor, std::pair<int, int> target)
{
    if (!actor.position) {
        return false;
    }
    const auto [ax, ay] = *actor.position;
    for (const auto& [dx, dy] : entity_shape_offsets(actor)) {
        const int cx = ax + dx, cy = ay + dy;
        // A unit trivially has LOS to any cell it occupies (used for self-targeting heals/buffs).
        if (cx == target.first && cy == target.second) {
            return true;
        }
        const auto los = game.board.line_of_sight({cx, cy}, target);
        if (line_of_sight_clear_ignoring_units(game, los)) {
            return true;
        }
    }
    return false;
}

int melee_range_for(const Unit& actor)
{
    return has_reach(actor) ? std::max(actor.melee_range, 2) : actor.melee_range;
}

std::vector<std::pair<int, int>> footprint_cells_for_entity(const Entity& entity)
{
    if (!entity.position) {
        return entity.occupied_positions;
    }
    std::vector<std::pair<int, int>> out;
    const auto [ax, ay] = *entity.position;
    for (const auto& [dx, dy] : entity_shape_offsets(entity)) {
        out.push_back({ax + dx, ay + dy});
    }
    return out;
}

bool has_line_of_sight_to_entity(const GameState& game, const Entity& actor, const Entity& target)
{
    for (const auto& cell : footprint_cells_for_entity(target)) {
        if (has_line_of_sight_from_footprint(game, actor, cell)) {
            return true;
        }
    }
    return false;
}

int consume_next_damage_bonus(Entity* source)
{
    if (!source || entity_is_silenced(*source)) {
        return 0;
    }
    return std::max(0, consume_first_entity_effect(*source, kNextDamageBonusEffectKey));
}

int min_chebyshev_between_entities(const Entity& a, const Entity& b)
{
    const auto a_cells = footprint_cells_for_entity(a);
    const auto b_cells = footprint_cells_for_entity(b);
    int best = INT_MAX;
    for (const auto& [ax, ay] : a_cells) {
        for (const auto& [bx, by] : b_cells) {
            best = std::min(best, std::max(std::abs(ax - bx), std::abs(ay - by)));
        }
    }
    return best == INT_MAX ? 0 : best;
}

// Deployment fatigue does not restrict counterattacks - including ranged return-fire shots.
CounterattackProfile counterattack_profile_for(const GameState& game, const Unit& defender, const Unit& attacker)
{
    CounterattackProfile out;
    if (defender.entity_type != "unit" || !defender.position || !attacker.position) {
        return out;
    }
    if (core_cracker_shutdown_blocks_actions(defender)) {
        return out;
    }
    if (defender.reactions_remaining_this_turn <= 0 && !has_vigilance(defender)) {
        return out;
    }
    const int dist = min_chebyshev_between_entities(defender, attacker);
    if ((defender.attack_type == AttackType::Melee || defender.attack_type == AttackType::Hybrid) && dist <= melee_range_for(defender)) {
        out.ok = true;
        out.is_melee_attack = true;
        out.damage_range = unit_effective_melee_damage_range(defender);
        out.crit_chance_percent = defender.crit_chance_percent;
        return out;
    }
    if (has_return_fire(defender)) {
        const AttackProfile ranged = attack_profile_for_unit(defender, true);
        const bool in_melee_range = dist <= melee_range_for(defender);
        const bool in_ranged_band =
            ranged.use_ranged && dist >= ranged.range_min && dist <= ranged.range_max;
        const bool deadzone_blocks_melee_return_fire =
            ranged.range_min > 0 && in_melee_range;
        if (ranged.use_ranged && in_ranged_band && !deadzone_blocks_melee_return_fire &&
            (!ranged.requires_line_of_sight || ignores_attack_line_of_sight(defender) ||
                has_line_of_sight_to_entity(game, defender, attacker))) {
            out.ok = true;
            out.damage_range = ranged.damage_range;
            out.crit_chance_percent = defender.crit_chance_percent;
            out.damage_type = ranged.damage_type;
        }
    }
    return out;
}

// Forced counterattack profile for Relentless: the defender fights back regardless of
// reactions remaining, stun, LOS, return_fire, or range. Does NOT consume a reaction.
// Priority: melee if in range → ranged if available → melee regardless of range.
CounterattackProfile forced_counterattack_profile_for(const Unit& defender, const Unit& attacker)
{
    CounterattackProfile out;
    if (!defender.position || !attacker.position) {
        return out;
    }
    const int dist = min_chebyshev_between_entities(defender, attacker);
    // Melee in range: prefer it first.
    if ((defender.attack_type == AttackType::Melee || defender.attack_type == AttackType::Hybrid)
            && dist <= melee_range_for(defender)) {
        out.ok = true;
        out.is_melee_attack = true;
        out.damage_range = unit_effective_melee_damage_range(defender);
        out.crit_chance_percent = defender.crit_chance_percent;
        return out;
    }
    // Ranged: no LOS / return_fire / deadzone check required.
    if (defender.attack_type == AttackType::Ranged || defender.attack_type == AttackType::Hybrid) {
        out.ok = true;
        out.is_melee_attack = false;
        out.damage_range = unit_effective_ranged_damage_range(defender);
        out.crit_chance_percent = defender.crit_chance_percent;
        return out;
    }
    // Melee only but out of range: still forced to fight back.
    if (defender.attack_type == AttackType::Melee) {
        out.ok = true;
        out.is_melee_attack = true;
        out.damage_range = unit_effective_melee_damage_range(defender);
        out.crit_chance_percent = defender.crit_chance_percent;
    }
    return out;
}

// Covering Fire reaction profile - like counterattack but ranged units do NOT need the
// return_fire keyword (covering fire is an actively set-up reaction, not a passive one).
struct CoveringFireProfile {
    bool ok{false};
    bool is_melee{false};
    DamageRange damage_range{};
    int crit_chance_percent{kDefaultCritChancePercent};
    DamageType damage_type{DamageType::Physical};
};

CoveringFireProfile covering_fire_profile_for(const GameState& game, const Unit& covering, const Entity& attacker, const int bonus_range = 0)
{
    CoveringFireProfile out;
    if (!covering.position || !attacker.position) {
        return out;
    }
    if (entity_is_stunned(covering)) {
        return out;
    }
    if (core_cracker_shutdown_blocks_actions(covering)) {
        return out;
    }
    if (covering.reactions_remaining_this_turn <= 0 && !has_vigilance(covering)) {
        return out;
    }
    const int dist = min_chebyshev_between_entities(covering, attacker);
    // Hybrid units always use their ranged profile for covering fire.
    if (covering.attack_type == AttackType::Hybrid || covering.attack_type == AttackType::Ranged) {
        const AttackProfile ranged = attack_profile_for_unit(covering, true);
        if (ranged.use_ranged && dist >= ranged.range_min && dist <= ranged.range_max + bonus_range &&
                (!ranged.requires_line_of_sight || ignores_attack_line_of_sight(covering) ||
                    has_line_of_sight_to_entity(game, covering, attacker))) {
            out.ok = true;
            out.is_melee = false;
            out.damage_range = ranged.damage_range;
            out.crit_chance_percent = covering.crit_chance_percent;
            out.damage_type = ranged.damage_type;
        }
        return out;
    }
    // Melee-only covering fire.
    if (covering.attack_type == AttackType::Melee && dist <= melee_range_for(covering)) {
        out.ok = true;
        out.is_melee = true;
        out.damage_range = unit_effective_melee_damage_range(covering);
        out.crit_chance_percent = covering.crit_chance_percent;
    }
    return out;
}

struct CanonicalShotPath {
    bool valid{false};
    int shooter_x{0};
    int shooter_y{0};
    std::vector<std::pair<int, int>> path;
};

CanonicalShotPath canonical_ranged_shot_path(const GameState& game, const Entity& attacker, std::pair<int, int> target_cell)
{
    CanonicalShotPath best;
    int best_shooter_dist = INT_MAX;
    int best_path_len = INT_MAX;
    for (const auto& [sx, sy] : footprint_cells_for_entity(attacker)) {
        const LOSResult los = game.board.line_of_sight({sx, sy}, target_cell);
        if (!line_of_sight_clear_ignoring_units(game, los)) {
            continue;
        }
        const int shooter_dist = std::max(std::abs(sx - target_cell.first), std::abs(sy - target_cell.second));
        const int path_len = static_cast<int>(los.path.size());
        if (shooter_dist < best_shooter_dist || (shooter_dist == best_shooter_dist && path_len < best_path_len)) {
            best_shooter_dist = shooter_dist;
            best_path_len = path_len;
            best.valid = true;
            best.shooter_x = sx;
            best.shooter_y = sy;
            best.path = los.path;
        }
    }
    return best;
}

void apply_damage_granted_heals(const int damage_dealt, const DamagePacket& packet)
{
    if (damage_dealt <= 0) {
        return;
    }
    Entity* const source = packet.source ? packet.source.get() : nullptr;
    if (source && damage_source_has_lifesteal(source, packet.ability_grants_lifesteal) && source->current_health > 0 &&
        !entity_is_base(*source)) {
        apply_entity_heal(*source, damage_dealt);
    }
    if (packet.soul_steal_heal_base && packet.soul_steal_heal_base->current_health > 0 &&
        damage_source_has_soul_steal(source, packet.ability_grants_soul_steal)) {
        apply_entity_heal(*packet.soul_steal_heal_base, damage_dealt);
    }
}

}  // namespace

AttackRollResult roll_attack_damage(std::mt19937& rng, DamageRange range, int crit_chance_percent)
{
    AttackRollResult result;
    if (range.max < range.min) {
        std::swap(range.min, range.max);
    }
    if (range.max <= 0 && range.min <= 0) {
        return result;
    }
    std::uniform_int_distribution<int> damage_dist(range.min, range.max);
    result.rolled_damage = damage_dist(rng);
    const int clamped_crit = std::clamp(crit_chance_percent, 0, 100);
    std::uniform_int_distribution<int> crit_dist(1, 100);
    result.is_crit = clamped_crit > 0 && crit_dist(rng) <= clamped_crit;
    result.final_damage = result.is_crit
        ? multiply_rounded_down(range.max, kCritDamageMultiplier)
        : result.rolled_damage;
    return result;
}

int apply_entity_heal(Entity& target, int amount)
{
    if (amount <= 0 || target.current_health <= 0) {
        return 0;
    }
    const int cap = entity_effective_base_health(target);
    const int before = target.current_health;
    target.current_health = std::min(cap, target.current_health + amount);
    return target.current_health - before;
}

AttackProfile attack_profile_for_unit(const Unit& actor, bool prefer_ranged)
{
    AttackProfile profile;
    profile.use_ranged = prefer_ranged || actor.attack_type == AttackType::Ranged;
    if (actor.attack_type == AttackType::Melee) {
        profile.use_ranged = false;
    }

    if (profile.use_ranged) {
        const PassiveStatGrant temp = temporary_stat_grants_for_entity(actor);
        profile.range_min = actor.ranged_deadzone + temp.bonus_ranged_deadzone;
        profile.range_max = actor.ranged_range + temp.bonus_ranged_range;
        profile.damage_range = unit_effective_ranged_damage_range(actor);
        profile.requires_line_of_sight = true;
    } else {
        profile.range_min = 0;
        profile.range_max = melee_range_for(actor);
        profile.damage_range = unit_effective_melee_damage_range(actor);
        profile.requires_line_of_sight = false;
    }
    profile.pierces_damage_prevention = has_pierce(actor);
    return profile;
}

ActionResult validate_attack(GameState& game, const std::shared_ptr<Unit>& actor, int player_id, std::pair<int, int> target_cell, bool prefer_ranged)
{
    if (!actor) return {false, "No attacker", {}};
    if (!entity_owned_by(*actor, player_id)) return {false, "Not your unit", {}};
    const std::shared_ptr<Unit> pose = game.unit_at_validation_pose(actor);
    if (!pose || !pose->position) return {false, "Unit has no position", {}};
    if (game.board.entity_at(target_cell.first, target_cell.second) == actor) return {false, "Cannot attack self", {}};
    {
        const bool has_regular = pose->attacks_remaining_this_turn > 0;
        const bool is_bonus_phase = (game.turn_manager.current_phase == TurnPhase::BonusAttackDeclaration);
        const bool has_bonus = pose->bonus_attacks_remaining_this_turn > 0;
        if (!has_regular && !(is_bonus_phase && has_bonus)) {
            return {false, "No attacks remaining this turn", {}};
        }
    }
    if (deployment_fatigue_blocks_attack(*pose)) {
        return {false, "Cannot attack on the turn this unit was deployed", {}};
    }
    if (entity_is_stunned(*pose)) {
        return {false, "Stunned units cannot attack", {}};
    }
    if (core_cracker_shutdown_blocks_actions(*pose)) {
        return {false, "Core Cracker must Prime Core (3 gallantry, slow) before attacking", {}};
    }
    if (pose->attack_type == AttackType::Utility) return {false, "Utility units cannot attack", {}};

    auto target = game.board.entity_at(target_cell.first, target_cell.second);
    if (!target || !pose->owner) {
        return {false, "No valid enemy target", {}};
    }
    // Breakable obstacles and pickups are valid targets regardless of ownership.
    // All other entities require a hostile owner.
    if (!entity_is_breakable_obstacle(*target) && !entity_is_pickup(*target)
            && (!target->owner || !teams_hostile(game, *pose->owner, *target->owner))) {
        return {false, "No valid enemy target", {}};
    }
    if (enemy_direct_target_blocked_by_stealth(game, player_id, *target)) {
        return {false, "Stealthed units cannot be directly targeted", {}};
    }
    if (!taunt_allows_board_target(game, pose.get(), player_id, *target)) {
        return {false, "Taunt: must target a directly adjacent enemy taunt unit", {}};
    }

    const int dist = min_chebyshev_entity_to_cell(*pose, target_cell.first, target_cell.second);
    const AttackProfile profile = attack_profile_for_unit(*pose, prefer_ranged);
    // Voluntary attacks only - counterattacks/reactions never call validate_attack.
    if (deployment_fatigue_blocks_ranged_attack(*pose) && profile.use_ranged) {
        return {false, "Cannot make ranged attacks on the turn this unit was deployed", {}};
    }
    if (pose->attack_type == AttackType::Melee && dist > profile.range_max) return {false, "Melee only", {}};
    if (pose->attack_type == AttackType::Ranged && (dist > profile.range_max || dist < profile.range_min)) return {false, "Out of ranged limits", {}};
    if (pose->attack_type == AttackType::Hybrid && (dist > profile.range_max || dist < profile.range_min)) {
        return {false, profile.use_ranged ? "Hybrid ranged out of range/deadzone" : "Hybrid melee out of range", {}};
    }
    if (profile.requires_line_of_sight && !ignores_attack_line_of_sight(*pose) &&
        !has_line_of_sight_from_footprint(game, *pose, target_cell)) {
        return {false, "LOS blocked", {}};
    }
    // Volley: ranged attacks must be in a cardinal direction from the nearest attacker cell.
    if (entity_has_volley(*pose) && profile.use_ranged) {
        const auto [ax0, ay0] = *pose->position;
        int nearest_ax = ax0, nearest_ay = ay0;
        int best_dist = std::abs(target_cell.first - ax0) + std::abs(target_cell.second - ay0);
        for (const auto& [dx, dy] : entity_shape_offsets(*pose)) {
            const int cx = ax0 + dx, cy = ay0 + dy;
            const int d = std::abs(target_cell.first - cx) + std::abs(target_cell.second - cy);
            if (d < best_dist) { best_dist = d; nearest_ax = cx; nearest_ay = cy; }
        }
        const int rdx = target_cell.first - nearest_ax;
        const int rdy = target_cell.second - nearest_ay;
        // Cardinal requires exactly one of dx/dy to be zero.
        if (rdx != 0 && rdy != 0) {
            return {false, "Volley: can only attack in cardinal directions", {}};
        }
    }
    return {true, "Valid attack", {}};
}

bool entity_has_line_of_sight_to_cell(const GameState& game, const Entity& actor, std::pair<int, int> target)
{
    return has_line_of_sight_from_footprint(game, actor, target);
}

namespace {

/** The living units that would body-block a ranged shot from `shooter` at `target_cell`
 *  aimed at `intended_target`, in path order: units on the shot path (excluding the shooter
 *  and the target) that block line of sight, minus an ally standing adjacent to the shooter
 *  (which never screens its own shooter). Each independently gets a 50% intercept roll. */
std::vector<std::shared_ptr<Entity>> collect_ranged_body_block_candidates(const GameState& game, const Entity& shooter,
    const std::pair<int, int> target_cell, const Entity& intended_target)
{
    std::vector<std::shared_ptr<Entity>> out;
    const CanonicalShotPath shot = canonical_ranged_shot_path(game, shooter, target_cell);
    if (!shot.valid || shot.path.size() < 2) {
        return out;
    }
    int target_index = static_cast<int>(shot.path.size());
    for (int i = 0; i < static_cast<int>(shot.path.size()); ++i) {
        if (min_chebyshev_entity_to_cell(intended_target, shot.path[i].first, shot.path[i].second) == 0) {
            target_index = std::min(target_index, i);
        }
    }
    if (target_index >= static_cast<int>(shot.path.size())) {
        return out;
    }
    for (int i = 1; i < target_index; ++i) {
        const auto sq = game.board.get_square(shot.path[i].first, shot.path[i].second);
        if (!sq || !sq->entity || sq->entity->entity_id == shooter.entity_id
            || sq->entity->entity_id == intended_target.entity_id || sq->entity->entity_type != "unit"
            || sq->entity->current_health <= 0 || does_not_block_line_of_sight(*sq->entity)) {
            continue;
        }
        // Adjacent allies to the shooter never body-block.
        const bool allied =
            shooter.owner && sq->entity->owner && !teams_hostile(game, *shooter.owner, *sq->entity->owner);
        if (allied && min_chebyshev_between_entities(shooter, *sq->entity) <= 1) {
            continue;
        }
        out.push_back(sq->entity);
    }
    return out;
}

}  // namespace

bool maybe_redirect_ranged_attack_to_blocking_unit(
    GameState& game,
    const Entity& attacker,
    const std::pair<int, int> target_cell,
    const Entity& intended_target,
    DamagePacket& packet,
    const AbilitySpec* ability)
{
    if (packet.amount <= 0 || !packet.target || intended_target.entity_type != "unit" || intended_target.current_health <= 0) {
        return false;
    }
    if (damage_source_ignores_low_cover_evasion(attacker, ability)) {
        return false;
    }
    std::optional<Unit> shooter_pose_storage;
    const Entity& shooter = *entity_for_ranged_combat_resolution(game, attacker, shooter_pose_storage);

    std::uniform_int_distribution<int> dist(1, 100);
    for (const std::shared_ptr<Entity>& blocker :
        collect_ranged_body_block_candidates(game, shooter, target_cell, intended_target)) {
        if (dist(game.rng()) <= 50) {
            packet.target = blocker;
            return true;
        }
    }
    return false;
}

int ranged_body_block_count(
    const GameState& game,
    const Entity& attacker,
    const std::pair<int, int> target_cell,
    const Entity& intended_target)
{
    if (intended_target.entity_type != "unit" || intended_target.current_health <= 0) {
        return 0;
    }
    if (damage_source_ignores_low_cover_evasion(attacker, nullptr)) {
        return 0;  // trueshot / flying punch straight through
    }
    std::optional<Unit> shooter_pose_storage;
    const Entity& shooter = *entity_for_ranged_combat_resolution(game, attacker, shooter_pose_storage);
    return static_cast<int>(collect_ranged_body_block_candidates(game, shooter, target_cell, intended_target).size());
}

namespace {

const Entity* actor_for_ranged_targeting_validation(const GameState& game, const Entity& actor, std::optional<Unit>& pose_storage)
{
    return entity_for_ranged_combat_resolution(game, actor, pose_storage);
}

void ability_ranged_range_limits(const Entity& actor, const AbilitySpec& ability, int& range_min, int& range_max)
{
    range_min = std::max(0, ability.range_min);
    range_max = ability.range_max;
    if (range_max <= 0) {
        if (const auto* unit = dynamic_cast<const Unit*>(&actor); unit && unit->ranged_range > 0) {
            const PassiveStatGrant temp = temporary_stat_grants_for_entity(*unit);
            range_max = unit->ranged_range + temp.bonus_ranged_range;
            if (ability.range_min <= 0) {
                range_min = std::max(0, unit->ranged_deadzone + temp.bonus_ranged_deadzone);
            }
        }
    }
    if (range_max <= 0) {
        range_max = 4;
    }
}

}  // namespace

int consume_all_next_damage_bonus(Entity& source)
{
    if (entity_is_silenced(source)) {
        return 0;
    }
    return std::max(0, consume_all_entity_effect(source, "next_damage_bonus"));
}

int compute_keyword_damage_bonuses(const GameState& game, const Entity& source, const Entity& victim, const bool from_reaction)
{
    int coordinated_bonus = 0;
    int entrenched_bonus = 0;
    int berserk_bonus = 0;
    int defender_bonus = 0;
    const int coord_val = coordinated_value(source);
    if (coord_val > 0) {
        for (const auto& [eid, ent] : game.board.all_entities_map) {
            if (!ent || eid == source.entity_id) {
                continue;
            }
            if (!entities_allied(*ent, source)) {
                continue;
            }
            if (ent->attacked_targets_this_turn.count(victim.entity_id)) {
                coordinated_bonus = coord_val;
                break;
            }
        }
    }
    if (!source.has_moved_this_turn) {
        entrenched_bonus = entrenched_value(source);
    }
    if (const auto* src_unit = dynamic_cast<const Unit*>(&source)) {
        if (src_unit->base_health > 0 && src_unit->current_health * 2 <= src_unit->base_health) {
            berserk_bonus = berserk_value(source);
        }
    }
    if (from_reaction) {
        defender_bonus = defender_value(source);
    }
    return coordinated_bonus + entrenched_bonus + berserk_bonus + defender_bonus;
}

ActionResult validate_ranged_damage_ability_target(
    const GameState& game, const Entity& actor, const AbilitySpec& ability, const std::pair<int, int> target_cell)
{
    if (!ability_uses_ranged_targeting(ability)) {
        return {true, "No ranged ability targeting rules", {}};
    }
    int range_min = 0;
    int range_max = 0;
    ability_ranged_range_limits(actor, ability, range_min, range_max);
    std::optional<Unit> validation_pose;
    const Entity* range_los_actor = actor_for_ranged_targeting_validation(game, actor, validation_pose);
    const int dist = min_chebyshev_entity_to_cell(*range_los_actor, target_cell.first, target_cell.second);
    if (dist > range_max) {
        return {false, "Ability target is out of range", {}};
    }
    if (dist < range_min) {
        return {false, "Ability target is inside minimum range", {}};
    }
    if (!damage_source_ignores_attack_line_of_sight(*range_los_actor, &ability)
        && !entity_has_line_of_sight_to_cell(game, *range_los_actor, target_cell)) {
        return {false, "Ability target is not in line of sight", {}};
    }
    return {true, "Ranged ability target allowed", {}};
}

ActionResult apply_heal_packet(GameState& game, const HealPacket& packet)
{
    if (!packet.target) {
        return {false, "Heal target disappeared", {}};
    }
    if (!game.board.all_entities_map.contains(packet.target->entity_id)) {
        return {false, "Heal target is no longer on the board", {}};
    }
    const int healed = apply_entity_heal(*packet.target, packet.amount);
    if (healed > 0) {
        game.mark_passive_auras_dirty();
        game.try_record_ability_resolve_viz_hit(*packet.target, healed, true);
    }
    const std::string prefix = packet.source_label.empty() ? "Heal" : packet.source_label;
    return {true, prefix + " healed " + packet.target->entity_id + " for " + std::to_string(healed), {}};
}

ActionResult apply_damage_packet(GameState& game, DamagePacket& packet)
{
    if (!packet.target) {
        return {false, "Damage target disappeared", {}};
    }
    const Entity victim_entity = *packet.target;
    Entity* const damage_source = packet.source ? packet.source.get() : nullptr;
    Entity* const soul_steal_base = packet.soul_steal_heal_base ? packet.soul_steal_heal_base.get() : nullptr;
    const int thorns_retaliation = packet.from_melee_attack ? thorns_value(*packet.target) : 0;
    // Crit Immunity: crits deal max ×1.5; immune targets take the rolled value instead.
    const bool crit_blocked = packet.is_crit && has_crit_immunity(*packet.target);
    const int effective_amount = crit_blocked ? packet.rolled_amount : packet.amount;
    const int bonus_damage = (effective_amount > 0 && !packet.suppress_source_damage_bonuses)
                                 ? consume_next_damage_bonus(damage_source)
                                 : 0;
    // Medical Override: when the source carries this buff and the target is an ally, convert the
    // damage into healing for that ally. Applies to every damage path - single-target, AoE, attacks.
    if (damage_source && entity_has_medical_override(*damage_source)
        && effective_amount + bonus_damage > 0
        && packet.target->owner && damage_source->owner
        && !teams_hostile(game, *damage_source->owner, *packet.target->owner)) {
        HealPacket hp;
        hp.target  = packet.target;
        hp.amount  = effective_amount + bonus_damage;
        hp.source_label = packet.source_label.empty() ? "Medical Override"
                                                      : packet.source_label + " (Medical Override)";
        return apply_heal_packet(game, hp);
    }
    const int bb_bonus = (packet.from_basic_attack && damage_source && packet.target->entity_type == "base")
                             ? base_breaker_bonus(*damage_source) : 0;
    // Vulnerable: each stack on the target adds 1 to incoming damage from attacks and abilities.
    // DoTs bypass apply_damage_packet entirely, so they are unaffected.
    const int vuln_bonus = entity_vulnerable_stacks(*packet.target);
    // Coordinated: bonus when another ally has already damaged this target this turn.
    // Entrenched: bonus when the attacker has not moved this turn.
    // Berserk: bonus when the attacker is below half max HP.
    // Defender: bonus on all reactions (counterattack, return fire, covering fire).
    int keyword_bonus = 0;
    if (damage_source && effective_amount > 0 && !packet.suppress_source_damage_bonuses) {
        keyword_bonus = compute_keyword_damage_bonuses(game, *damage_source, victim_entity, packet.from_reaction);
    }
    const int terrain_armor_bonus = trench_armor_bonus_for_entity(
        [&game](int x, int y) { return game.board.get_square(x, y); }, *packet.target);
    const int raw_incoming = effective_amount + bonus_damage + bb_bonus + vuln_bonus + keyword_bonus;
    packet.damage_display = incoming_damage_display_amount(*packet.target, raw_incoming,
        packet.pierces_damage_prevention, packet.damage_type, terrain_armor_bonus);
    const int damage = apply_incoming_damage(*packet.target, raw_incoming, packet.pierces_damage_prevention,
        packet.damage_type, terrain_armor_bonus);
    if (damage > 0 || packet.damage_display > 0) {
        game.try_record_ability_resolve_viz_hit(*packet.target,
            packet.damage_display > 0 ? packet.damage_display : damage, false);
    }
    if (damage > 0 && entity_has_recover_stance(*packet.target)) {
        add_entity_effect(*packet.target, effect_keys::kRecoverStanceDamageTakenKey, 1, "recover");
    }
    if (damage_source && damage > 0) {
        game.apply_on_damage_dealt_primer_statuses(*damage_source, packet.target, damage, packet.from_basic_attack);
        // Record this unit attacked this target (feeds the Coordinated keyword check).
        if (!damage_source->owner || !packet.target->owner
                || teams_hostile(game, *damage_source->owner, *packet.target->owner)) {
            damage_source->attacked_targets_this_turn.insert(victim_entity.entity_id);
        }
    }
    // Reactive Armor (spell): while reactive_armor_grant is active, each damage instance adds +1
    // Armor to that same temp effect (starts at 1 from the cast).
    if (damage > 0 && packet.target->current_health > 0
            && entity_has_temporary_effect_id(*packet.target, "reactive_armor_grant")) {
        Entity& v = *packet.target;
        auto it = std::find_if(v.temporary_effects.begin(), v.temporary_effects.end(),
            [](const TemporaryEntityEffect& e) { return e.effect_id == "reactive_armor_grant"; });
        if (it != v.temporary_effects.end()) {
            bool bumped = false;
            for (PassiveAttributeGrant& g : it->granted_attributes) {
                if (g.key == "armor") {
                    g.amount = std::make_optional(std::min(5, (g.amount ? *g.amount : 0) + 1));
                    bumped = true;
                }
            }
            if (!bumped) {
                it->granted_attributes.push_back({"armor", 1});
            }
        }
    }
    // Defective Graft: one-shot marker - next enemy hit triggers 5 damage + 2 overload to all surroundings.
    if (damage > 0 && packet.target->current_health > 0 && damage_source && packet.source
            && entity_has_temporary_effect_id(*packet.target, "explosive_graft")) {
        if (packet.target->owner && damage_source->owner
                && teams_hostile(game, *packet.target->owner, *damage_source->owner)) {
            auto& teffs = packet.target->temporary_effects;
            teffs.erase(std::remove_if(teffs.begin(), teffs.end(),
                [](const TemporaryEntityEffect& e) { return e.effect_id == "explosive_graft"; }),
                teffs.end());
            const auto [tx, ty] = *packet.target->position;
            const int dx[] = {-1,-1,-1, 0, 0, 1, 1, 1};
            const int dy[] = {-1, 0, 1,-1, 1,-1, 0, 1};
            for (int i = 0; i < 8; ++i) {
                const auto sq = game.board.get_square(tx + dx[i], ty + dy[i]);
                if (!sq || !sq->occupied || !sq->entity) continue;
                DamagePacket graft_pkt;
                graft_pkt.target = sq->entity;
                graft_pkt.amount = 5;
                graft_pkt.from_basic_attack = false;
                (void)apply_damage_packet(game, graft_pkt);
                if (sq->entity->current_health > 0
                        && game.board.all_entities_map.contains(sq->entity->entity_id)) {
                    const std::optional<int> graft_owner = packet.target->owner
                        ? std::optional<int>(*packet.target->owner) : std::nullopt;
                    game.apply_overload_stacks(sq->entity, 2, graft_owner);
                }
            }
        }
    }
    apply_damage_granted_heals(damage, packet);
    int thorns_dealt = 0;
    if (thorns_retaliation > 0 && damage_source && game.board.all_entities_map.contains(damage_source->entity_id)) {
        thorns_dealt = apply_incoming_damage(*damage_source, thorns_retaliation, true, DamageType::Physical);
        if (damage_source->current_health <= 0) {
            game.destroy_board_entity(packet.source);
        }
    }
    bool survived_lethal = false;
    if (packet.target->current_health <= 0 && packet.target->survive_lethal_percent > 0) {
        std::uniform_int_distribution<int> survival_roll(1, 100);
        if (survival_roll(game.rng()) <= packet.target->survive_lethal_percent) {
            survived_lethal = true;
            packet.target->current_health = 1;
            // If this save came from a Sentinel Veil aura (survive_lethal_percent was granted by
            // an allied_units aura, not a self passive), mark the flag so the aura won't re-grant
            // survive_lethal_percent to this unit until the next turn start resets the flag.
            packet.target->death_shield_used_this_turn = true;
            if (!entity_is_stunned(*packet.target)) {
                add_entity_effect(*packet.target, "stunned", 1);
            }
            // Permanent stat gain on survival (e.g. Valiant Resolute's Iron Will +2/+2).
            if (auto* unit = dynamic_cast<Unit*>(packet.target.get())) {
                const int atk = packet.target->survive_lethal_bonus_attack;
                const int hp  = packet.target->survive_lethal_bonus_health;
                if (atk > 0) {
                    unit->melee_damage     += atk;
                    unit->melee_damage_min += atk;
                    unit->melee_damage_max += atk;
                    if (unit->ranged_damage > 0) {
                        unit->ranged_damage     += atk;
                        unit->ranged_damage_min += atk;
                        unit->ranged_damage_max += atk;
                    }
                }
                if (hp > 0) {
                    unit->base_health    += hp;
                    unit->current_health  = std::min(unit->base_health, unit->current_health + hp);
                }
            }
            game.mark_passive_auras_dirty();
        }
    }
    if (!survived_lethal && packet.target->current_health <= 0) {
        const Entity victim_snapshot = *packet.target;
        const std::shared_ptr<Entity> killer = packet.source;
        if (!packet.defer_frenzy_refresh) {
            try_trigger_frenzy_on_unit_kill(game, killer, victim_snapshot);
        }
        if (killer) {
            game.apply_passive_reactive_on_enemy_unit_killed(*killer, victim_snapshot);
        }
        game.destroy_board_entity(packet.target);
    } else if (!survived_lethal) {
        game.mark_passive_auras_dirty();
    }
    std::string msg = packet.source_label.empty() ? ("Dealt " + std::to_string(damage) + " damage to " + packet.target->entity_id)
                                                  : (packet.source_label + " dealt " + std::to_string(damage) + " damage to " + packet.target->entity_id);
    if (packet.is_crit && !crit_blocked && damage > 0) {
        msg += " (crit! " + std::to_string(packet.amount) + ")";
    } else if (crit_blocked && damage > 0) {
        msg += " (crit negated)";
    } else if (packet.rolled_amount > 0 && packet.rolled_amount != damage) {
        msg += " (rolled " + std::to_string(packet.rolled_amount) + ")";
    }
    if (bonus_damage > 0) {
        msg += " (Tune-Up +" + std::to_string(bonus_damage) + ")";
    }
    if (damage_source && damage > 0 && damage_source_has_lifesteal(damage_source, packet.ability_grants_lifesteal)) {
        msg += " (lifesteal)";
    }
    if (soul_steal_base && damage > 0 && damage_source_has_soul_steal(damage_source, packet.ability_grants_soul_steal)) {
        msg += " (soul steal -> " + soul_steal_base->entity_id + ")";
    }
    if (thorns_dealt > 0 && damage_source) {
        msg += " (thorns " + std::to_string(thorns_dealt) + " -> " + damage_source->entity_id + ")";
    }
    if (damage > 0 && packet.from_melee_attack && damage_source
            && game.board.all_entities_map.contains(damage_source->entity_id)) {
        game.apply_passive_reactive_on_damage_taken(victim_entity, *damage_source, true, damage);
    }
    if (survived_lethal) {
        msg += " (Iron Will: " + packet.target->entity_id + " survived at 1 HP";
        const int atk = packet.target->survive_lethal_bonus_attack;
        const int hp  = packet.target->survive_lethal_bonus_health;
        if (atk > 0 || hp > 0) {
            msg += ", gained +" + std::to_string(atk) + "/+" + std::to_string(hp);
        }
        msg += "!)";
    }
    packet.damage_dealt = damage;
    if (!packet.from_basic_attack) {
        game.note_exalted_ability_damage_dealt(damage);
    }
    if (damage_source && damage > 0) {
        if (packet.defer_reactive) {
            // Queue for deferred fire after the exchange (or phase batch for tithe-style survive).
            if (game.entity_has_phase_survive_damage_reactive(*damage_source)) {
                game.queue_phase_pending_reactive(damage_source->entity_id, victim_entity, damage);
            } else {
                game.queue_pending_reactive(damage_source->entity_id, victim_entity, damage);
            }
        } else if (game.board.all_entities_map.contains(damage_source->entity_id) &&
                   damage_source->current_health > 0) {
            game.apply_passive_reactive_on_damage_dealt(*damage_source, victim_entity, damage);
        }
        // Concordia's Promise (Lady Concordia): board-wide scan for nearby allies of the victim
        // that react to an ally taking damage from a hostile source.
        if (victim_entity.entity_type == "unit") {
            game.apply_passive_reactive_on_ally_took_damage(victim_entity, *damage_source, damage);
        }
    }
    return {true, msg, {}};
}

ActionResult resolve_attack(GameState& game, const std::shared_ptr<Unit>& actor, int player_id, std::pair<int, int> target_cell, bool prefer_ranged,
    bool allow_counterattack, std::optional<std::pair<int, int>> soul_steal_heal_base_cell,
    bool skip_coordinated_fire)
{
    auto vr = validate_attack(game, actor, player_id, target_cell, prefer_ranged);
    if (!vr.ok) return vr;
    const bool attacker_shadowstrike = has_shadowstrike(*actor);
    if (attacker_shadowstrike) {
        allow_counterattack = false;
    }
    if (entity_is_stealthed(*actor)) {
        remove_entity_effect(*actor, "stealth");
    }
    auto target = game.board.entity_at(target_cell.first, target_cell.second);
    const Entity victim_snapshot = target ? *target : Entity{};
    // Snapshot the defender as a Unit *before* any damage is applied. A lethal primary hit
    // removes the defender (destroy_board_entity resets its position/occupied cells), which
    // would otherwise make counterattack_profile_for() bail on the missing position and skip
    // the strike-back. A dying defender still counterattacks, using its pre-removal geometry.
    std::shared_ptr<Unit> target_unit = std::dynamic_pointer_cast<Unit>(target);
    Unit defender_pre_hit = target_unit ? *target_unit : Unit{};
    std::string victim_id = target ? target->entity_id : std::string{};
    const bool valiant_guard_intercepted = target && target_unit
        && maybe_valiant_guard_intercept(game, player_id, target, target_unit, defender_pre_hit);
    if (valiant_guard_intercepted) {
        victim_id = target->entity_id;
    }
    const AttackProfile profile = attack_profile_for_unit(*actor, prefer_ranged);
    // Lambda: roll attack damage and apply the Precise keyword override (always max of range).
    auto roll_precise = [&](DamageRange range, int crit_pct, const Entity& shooter) -> AttackRollResult {
        AttackRollResult r = roll_attack_damage(game.rng(), range, crit_pct);
        if (has_precise(shooter)) {
            r.rolled_damage = range.max;
            r.final_damage = r.is_crit
                ? multiply_rounded_down(range.max, kCritDamageMultiplier)
                : range.max;
        }
        return r;
    };
    const AttackRollResult roll = roll_precise(profile.damage_range, actor->crit_chance_percent, *actor);
    bool viz_attack_crit = false;
    bool viz_counter_crit = false;
    int viz_attack_damage = 0;
    int viz_counter_damage = 0;
    if (game.combat_visualization_enabled() && !skip_coordinated_fire) {
        game.clear_last_combat_viz_encounter_result();
    }
    DamagePacket packet;
    packet.source = actor;
    packet.target = target;
    packet.amount = roll.final_damage;
    packet.rolled_amount = roll.rolled_damage;
    packet.is_crit = roll.is_crit;
    packet.damage_type = profile.damage_type;
    packet.pierces_damage_prevention = profile.pierces_damage_prevention;
    packet.from_melee_attack = !profile.use_ranged;
    packet.from_basic_attack = true;
    packet.defer_frenzy_refresh = true;
    packet.defer_reactive = true;
    packet.source_label = actor->entity_id;
    if (damage_source_has_soul_steal(actor.get(), false)) {
        std::map<std::string, int> soul_targets;
        if (soul_steal_heal_base_cell) {
            soul_targets[effect_keys::kHealBaseX] = soul_steal_heal_base_cell->first;
            soul_targets[effect_keys::kHealBaseY] = soul_steal_heal_base_cell->second;
        }
        packet.soul_steal_heal_base = resolve_soul_steal_heal_base(game, player_id, soul_targets);
    }
    if (target && target->entity_type == "unit") {
        const int evasive_stacks = evasive_stack_count(*target);
        if (evasive_stacks > 0 && roll_evasive_whiff(game.rng(), evasive_stacks)) {
            return {true, target->entity_id + " evaded the attack (Evasive)", {}};
        }
    }
    bool body_blocked = false;
    bool evaded_to_cover = false;
    if (profile.use_ranged && target && target->entity_type == "unit") {
        body_blocked = maybe_redirect_ranged_attack_to_blocking_unit(game, *actor, target_cell, *target, packet, nullptr);
        if (!body_blocked) {
            evaded_to_cover = maybe_redirect_ranged_attack_to_low_cover(game, *actor, target_cell, *target, packet);
        }
    }
    auto damage_result = apply_damage_packet(game, packet);
    if (!damage_result.ok) {
        return damage_result;
    }
    viz_attack_damage = packet.damage_display;
    if (roll.is_crit) {
        viz_attack_crit = true;
    }
    if (valiant_guard_intercepted) {
        damage_result.message += " (Valiant Guard)";
    }
    if (body_blocked) {
        damage_result.message += " (body-blocked in line of fire)";
    } else if (evaded_to_cover) {
        damage_result.message += " (evaded behind low cover)";
    }
    // Lambda: fire a counterattack from a splash victim (cleave/whirlwind) against `actor`.
    // Uses the standard counterattack_profile_for, so the victim must have a valid attack in
    // range with LOS (or return_fire for ranged), a reaction available, and not be stunned.
    // Consumes one reaction stack on success, same as a normal counterattack.
    auto fire_splash_counterattack = [&](const std::shared_ptr<Entity>& splash, const std::string& context) {
        if (!allow_counterattack) return;
        if (!game.board.all_entities_map.contains(actor->entity_id) || actor->current_health <= 0) return;
        if (!splash || splash->current_health <= 0) return;
        auto splash_unit = std::dynamic_pointer_cast<Unit>(splash);
        if (!splash_unit) return;
        const CounterattackProfile ca = counterattack_profile_for(game, *splash_unit, *actor);
        if (!ca.ok) return;
        const AttackRollResult ca_roll = roll_precise(ca.damage_range, ca.crit_chance_percent, *splash_unit);
        DamagePacket ca_pkt;
        ca_pkt.source = splash_unit; ca_pkt.target = actor;
        ca_pkt.amount = ca_roll.final_damage; ca_pkt.rolled_amount = ca_roll.rolled_damage;
        ca_pkt.is_crit = ca_roll.is_crit; ca_pkt.damage_type = ca.damage_type;
        ca_pkt.pierces_damage_prevention = has_pierce(*splash_unit);
        ca_pkt.from_melee_attack = ca.is_melee_attack; ca_pkt.from_basic_attack = true;
        ca_pkt.from_reaction = true;
        ca_pkt.source_label = splash_unit->entity_id + " (" + context + " counterattack)";
        if (!ca.is_melee_attack && actor->position) {
            const bool ca_blocked = maybe_redirect_ranged_attack_to_blocking_unit(
                game, *splash_unit, *actor->position, *actor, ca_pkt, nullptr);
            if (!ca_blocked) {
                maybe_redirect_ranged_attack_to_low_cover(game, *splash_unit, *actor->position, *actor, ca_pkt);
            }
        }
        apply_damage_packet(game, ca_pkt);
        splash_unit->reactions_remaining_this_turn = std::max(0, splash_unit->reactions_remaining_this_turn - 1);
    };

    // Lambda: whirlwind splash - strike all enemies within the attack range of the attacker
    // (not the primary target). Uses profile.range_max so range-extended units (e.g. a ranged
    // unit with Whirlwind from whirlwind_spray) reach all valid targets.
    // Applies to melee and ranged attacks alike. Victims counterattack if in range.
    // Called for the primary hit and for every multistrike/relentless extra strike.
    auto do_whirlwind = [&](const AttackRollResult& r, const std::string& primary_vid) {
        if (!entity_has_attribute(*actor, "whirlwind")) return;
        if (!actor->position) return;
        if (!game.board.all_entities_map.contains(actor->entity_id)) return;
        const int ww_range = std::max(1, profile.range_max);
        const auto [awx, awy] = *actor->position;
        std::set<std::string> already_hit;
        already_hit.insert(primary_vid); // primary target never receives a second hit via whirlwind
        for (int dy = -ww_range; dy <= ww_range; ++dy) {
            for (int dx = -ww_range; dx <= ww_range; ++dx) {
                if (dx == 0 && dy == 0) continue;
                if (std::max(std::abs(dx), std::abs(dy)) > ww_range) continue;
                const int cx = awx + dx, cy = awy + dy;
                if (!game.board.all_entities_map.contains(actor->entity_id)) goto ww_done;
                const auto splash = game.board.entity_at(cx, cy);
                if (!splash) continue;
                if (already_hit.count(splash->entity_id)) continue;
                if (!splash->owner || !teams_hostile(game, player_id, *splash->owner)) continue;
                if (splash->current_health <= 0) continue;
                already_hit.insert(splash->entity_id);
                if (splash->entity_type == "unit") {
                    const int ev = evasive_stack_count(*splash);
                    if (ev > 0 && roll_evasive_whiff(game.rng(), ev)) continue;
                }
                DamagePacket wp;
                wp.source = actor;
                wp.target = splash;
                wp.amount = r.final_damage;
                wp.rolled_amount = r.rolled_damage;
                wp.is_crit = r.is_crit;
                wp.damage_type = profile.damage_type;
                wp.pierces_damage_prevention = profile.pierces_damage_prevention;
                wp.from_melee_attack = true;
                wp.from_basic_attack = true;
                wp.defer_frenzy_refresh = true;
                wp.defer_reactive = true;
                wp.source_label = actor->entity_id + " (whirlwind)";
                apply_damage_packet(game, wp);
                fire_splash_counterattack(splash, "whirlwind");
            }
        }
        ww_done:;
    };
    // Lambda: cleave splash - strike enemies on the two cells perpendicular to the
    // attack direction (the "flanking" cells beside the target). Does NOT hit the
    // cell directly behind the target or any cell the attacker occupies.
    // Called for the primary hit and for every multistrike/relentless extra strike.
    auto do_cleave = [&](const AttackRollResult& r, const Entity& prim_target) {
        if (!entity_has_attribute(*actor, "cleave")) return;
        if (!prim_target.position) return;
        if (!actor->position) return;
        if (!game.board.all_entities_map.contains(actor->entity_id)) return;

        // Find the attacker cell nearest to the target to determine attack direction.
        const auto [tx, ty] = *prim_target.position;
        const auto [ax0, ay0] = *actor->position;
        int nearest_ax = ax0, nearest_ay = ay0;
        int best_dist = std::abs(tx - ax0) + std::abs(ty - ay0);
        for (const auto& [dx, dy] : entity_shape_offsets(*actor)) {
            int cx2 = ax0 + dx, cy2 = ay0 + dy;
            int d = std::abs(tx - cx2) + std::abs(ty - cy2);
            if (d < best_dist) { best_dist = d; nearest_ax = cx2; nearest_ay = cy2; }
        }

        // Compute unit direction vector from nearest attacker cell to target.
        // Pure cardinal: one component is zero. True diagonal: both components are ±1.
        // When the raw vector is "off-axis" (|dx| ≠ |dy| and neither is 0), collapse to
        // whichever axis dominates - but equal components stay diagonal.
        int raw_dx = tx - nearest_ax;
        int raw_dy = ty - nearest_ay;
        int dir_x, dir_y;
        if (raw_dx == 0) {
            dir_x = 0; dir_y = (raw_dy > 0) ? 1 : (raw_dy < 0 ? -1 : 0);
        } else if (raw_dy == 0) {
            dir_x = (raw_dx > 0) ? 1 : -1; dir_y = 0;
        } else if (std::abs(raw_dx) == std::abs(raw_dy)) {
            // True diagonal.
            dir_x = (raw_dx > 0) ? 1 : -1;
            dir_y = (raw_dy > 0) ? 1 : -1;
        } else if (std::abs(raw_dx) > std::abs(raw_dy)) {
            dir_x = (raw_dx > 0) ? 1 : -1; dir_y = 0;
        } else {
            dir_x = 0; dir_y = (raw_dy > 0) ? 1 : -1;
        }

        // Cleave cells depend on attack angle:
        //   Cardinal: the two cells perpendicular (±90°) to the attack direction.
        //   Diagonal: the two cells that "frame" the diagonal path from the attacker's
        //             side - i.e. target + (-dir_x, 0) and target + (0, -dir_y).
        std::pair<int,int> cleave_cells[2];
        if (dir_x != 0 && dir_y != 0) {
            cleave_cells[0] = {tx - dir_x, ty};
            cleave_cells[1] = {tx,          ty - dir_y};
        } else {
            cleave_cells[0] = {tx + (-dir_y), ty + dir_x};
            cleave_cells[1] = {tx +   dir_y,  ty - dir_x};
        }

        std::set<std::pair<int, int>> actor_own;
        for (const auto& [dx, dy] : entity_shape_offsets(*actor)) {
            actor_own.emplace(ax0 + dx, ay0 + dy);
        }

        std::set<std::string> cleave_hit;
        cleave_hit.insert(prim_target.entity_id);
        for (const auto& [cx, cy] : cleave_cells) {
            if (actor_own.count({cx, cy})) continue;
            if (!game.board.all_entities_map.contains(actor->entity_id)) break;
            const auto splash = game.board.entity_at(cx, cy);
            if (!splash) continue;
            if (cleave_hit.count(splash->entity_id)) continue;
            if (!splash->owner || !teams_hostile(game, player_id, *splash->owner)) continue;
            if (splash->current_health <= 0) continue;
            cleave_hit.insert(splash->entity_id);
            if (splash->entity_type == "unit") {
                const int ev = evasive_stack_count(*splash);
                if (ev > 0 && roll_evasive_whiff(game.rng(), ev)) continue;
            }
            DamagePacket cp;
            cp.source = actor;
            cp.target = splash;
            cp.amount = r.final_damage;
            cp.rolled_amount = r.rolled_damage;
            cp.is_crit = r.is_crit;
            cp.damage_type = profile.damage_type;
            cp.pierces_damage_prevention = profile.pierces_damage_prevention;
            cp.from_melee_attack = !profile.use_ranged;
            cp.from_basic_attack = true;
            cp.defer_frenzy_refresh = true;
            cp.defer_reactive = true;
            cp.source_label = actor->entity_id + " (cleave)";
            apply_damage_packet(game, cp);
            fire_splash_counterattack(splash, "cleave");
        }
    };
    // Volley N: after the primary hit (depth 1), fires a reverse cone expanding outward.
    // At each depth d (2..N): (2d-1) tiles wide, damage reduced by 33% per depth beyond 1.
    // d=2: 3 tiles at 67% damage; d=3: 5 tiles at ~45% damage; etc.
    // Each cell has independent LOS - if blocked by a nearer entity, the shot misses silently.
    auto do_volley = [&](const AttackRollResult& r, const Entity& prim_target) {
        if (!entity_has_volley(*actor)) return;
        if (!profile.use_ranged) return;
        if (!prim_target.position || !actor->position) return;
        if (!game.board.all_entities_map.contains(actor->entity_id)) return;

        const int volley_n = std::max(2, entity_attribute_amount(*actor, "volley"));

        // Cardinal direction from nearest attacker cell to primary target.
        const auto [tx, ty] = *prim_target.position;
        const auto [ax0, ay0] = *actor->position;
        int nearest_ax = ax0, nearest_ay = ay0;
        {
            int best_dist = std::abs(tx - ax0) + std::abs(ty - ay0);
            for (const auto& [odx, ody] : entity_shape_offsets(*actor)) {
                const int cx = ax0 + odx, cy = ay0 + ody;
                const int d = std::abs(tx - cx) + std::abs(ty - cy);
                if (d < best_dist) { best_dist = d; nearest_ax = cx; nearest_ay = cy; }
            }
        }
        const int dir_x = (tx != nearest_ax) ? ((tx > nearest_ax) ? 1 : -1) : 0;
        const int dir_y = (ty != nearest_ay) ? ((ty > nearest_ay) ? 1 : -1) : 0;
        // Perpendicular direction (90° CCW from fire direction).
        const int perp_x = -dir_y;
        const int perp_y =  dir_x;

        std::set<std::string> volley_hit;
        volley_hit.insert(prim_target.entity_id); // primary already hit at full damage

        for (int d = 2; d <= volley_n; ++d) {
            if (!game.board.all_entities_map.contains(actor->entity_id)) break;

            // Damage falloff: 67% per depth step beyond primary (d=1).
            const float mult = std::pow(0.67f, static_cast<float>(d - 1));
            const int depth_damage        = std::max(1, multiply_rounded_down(r.final_damage, mult));
            const int depth_rolled_damage = std::max(1, multiply_rounded_down(r.rolled_damage, mult));

            // Row of (2d-1) cells at depth d from the attacker.
            for (int k = -(d - 1); k <= (d - 1); ++k) {
                if (!game.board.all_entities_map.contains(actor->entity_id)) break;
                const int cx = nearest_ax + d * dir_x + k * perp_x;
                const int cy = nearest_ay + d * dir_y + k * perp_y;

                const auto splash = game.board.entity_at(cx, cy);
                if (!splash || splash->current_health <= 0) continue;
                if (volley_hit.count(splash->entity_id)) continue;
                if (!splash->owner || !teams_hostile(game, player_id, *splash->owner)) continue;
                // Independent LOS - if blocked by a closer entity, shot misses silently.
                if (profile.requires_line_of_sight && !ignores_attack_line_of_sight(*actor)
                        && !has_line_of_sight_from_footprint(game, *actor, {cx, cy})) {
                    continue;
                }
                volley_hit.insert(splash->entity_id);
                if (splash->entity_type == "unit") {
                    const int ev = evasive_stack_count(*splash);
                    if (ev > 0 && roll_evasive_whiff(game.rng(), ev)) continue;
                }
                DamagePacket vp;
                vp.source = actor;
                vp.target = splash;
                vp.amount = depth_damage;
                vp.rolled_amount = depth_rolled_damage;
                vp.is_crit = r.is_crit;
                vp.damage_type = profile.damage_type;
                vp.pierces_damage_prevention = profile.pierces_damage_prevention;
                vp.from_melee_attack = false;
                vp.from_basic_attack = true;
                vp.defer_frenzy_refresh = true;
                vp.defer_reactive = true;
                vp.source_label = actor->entity_id + " (volley d" + std::to_string(d) + ")";
                apply_damage_packet(game, vp);
            }
        }
    };

    // Primary-hit area effects.
    do_whirlwind(roll, victim_id);
    if (target && target->position) { do_cleave(roll, *target); }
    if (target && target->position) { do_volley(roll, *target); }

    // Lambda: fire one deferred strike (multistrike / inner relentless-multistrike hit).
    // Builds a DamagePacket with defer_frenzy_refresh and defer_reactive, then calls
    // do_whirlwind and do_cleave for the same strike. Captures packet for lifesteal fields.
    auto do_deferred_strike = [&](const AttackRollResult& r,
                                  const std::shared_ptr<Entity>& tgt,
                                  const std::string& lbl) {
        if (!game.board.all_entities_map.contains(actor->entity_id) || actor->current_health <= 0) return;
        if (!tgt || tgt->current_health <= 0) return;
        if (tgt->entity_type == "unit") {
            const int evasive_stacks = evasive_stack_count(*tgt);
            if (evasive_stacks > 0 && roll_evasive_whiff(game.rng(), evasive_stacks)) {
                return;
            }
        }
        DamagePacket sp;
        sp.source                    = actor;
        sp.target                    = tgt;
        sp.amount                    = r.final_damage;
        sp.rolled_amount             = r.rolled_damage;
        sp.is_crit                   = r.is_crit;
        sp.damage_type               = profile.damage_type;
        sp.pierces_damage_prevention = profile.pierces_damage_prevention;
        sp.from_melee_attack         = !profile.use_ranged;
        sp.from_basic_attack         = true;
        sp.defer_frenzy_refresh      = true;
        sp.defer_reactive            = true;
        // Carry ability-granted lifesteal/soul-steal so every strike heals.
        sp.ability_grants_lifesteal  = packet.ability_grants_lifesteal;
        sp.ability_grants_soul_steal = packet.ability_grants_soul_steal;
        sp.soul_steal_heal_base      = packet.soul_steal_heal_base;
        sp.source_label              = lbl;
        apply_damage_packet(game, sp);
        if (game.board.all_entities_map.contains(actor->entity_id) && actor->current_health > 0) {
            do_whirlwind(r, tgt->entity_id);
            if (tgt->position) { do_cleave(r, *tgt); }
            if (tgt->position) { do_volley(r, *tgt); }
        }
    };

    // Lambda: run the multistrike extra-strikes loop for a given primary target and hit label.
    // Whirlwind and cleave fire on every extra strike via do_deferred_strike.
    auto do_multistrike_for = [&](const std::string& target_id, const std::string& hit_label) {
        const int ms_val = multistrike_value(*actor);
        for (int mi = 0; mi < ms_val; ++mi) {
            if (!game.board.all_entities_map.contains(actor->entity_id) || actor->current_health <= 0) break;
            const auto ms_tit = game.board.all_entities_map.find(target_id);
            if (target_id.empty() || ms_tit == game.board.all_entities_map.end()
                    || !ms_tit->second || ms_tit->second->current_health <= 0) break;
            const AttackRollResult ms_roll = roll_precise(profile.damage_range, actor->crit_chance_percent, *actor);
            do_deferred_strike(ms_roll, ms_tit->second,
                hit_label + " (multistrike " + std::to_string(mi + 1) + ")");
        }
    };

    // Multistrike X: after the primary hit (and its area effects), strike the same target X
    // more times. Whirlwind and cleave repeat on every extra strike. There is ONE counterattack
    // and ONE set of reactions for the entire attack sequence, not per strike.
    // Suppressed while silenced. Composes with Relentless (each relentless hit runs multistrike).
    do_multistrike_for(victim_id, actor->entity_id);
    auto defender = std::dynamic_pointer_cast<Unit>(target);

    const bool victim_killed_by_attack =
        !victim_id.empty() && !game.board.all_entities_map.contains(victim_id);
    const bool first_strike_skip_counter =
        victim_killed_by_attack && has_first_strike(*actor);

    // Relentless X: when the actor has this keyword ALL X attacks use a forced counterattack
    // (including the primary). Only triggers against units - suppressed vs structures and bases.
    // The spell reaction window was already opened when the StackItem was pushed - no new windows
    // are opened for any hit in this sequence.
    const int relentless_val = (defender != nullptr) ? relentless_value(*actor) : 0;

    // Lambda: forced counterattack from the primary target against `atk`. Ignores ALL normal
    // counterattack requirements (reactions, stun, LOS, return_fire, range). Does NOT consume
    // a reaction stack � this is an enforced fight-back, not a voluntary reaction.
    // Uses the defender snapshot so a lethal hit can still force one fight-back.
    auto fire_forced_counterattack = [&](Unit& atk) {
        if (!defender || victim_snapshot.entity_type != "unit") return;
        if (!game.board.all_entities_map.contains(atk.entity_id) || atk.current_health <= 0) return;
        const CounterattackProfile fc = forced_counterattack_profile_for(*defender, atk);
        if (!fc.ok) return;
        const AttackRollResult fc_roll = roll_precise(fc.damage_range, fc.crit_chance_percent, *defender);
        DamagePacket fc_pkt;
        fc_pkt.source = defender; fc_pkt.target = actor;
        fc_pkt.amount = fc_roll.final_damage; fc_pkt.rolled_amount = fc_roll.rolled_damage;
        fc_pkt.is_crit = fc_roll.is_crit; fc_pkt.damage_type = fc.damage_type;
        fc_pkt.pierces_damage_prevention = has_pierce(*defender);
        fc_pkt.from_melee_attack = fc.is_melee_attack; fc_pkt.from_basic_attack = true;
        fc_pkt.from_reaction = true;
        fc_pkt.source_label = defender->entity_id + " (forced counterattack)";
        apply_damage_packet(game, fc_pkt);
        if (fc_roll.is_crit) {
            viz_counter_crit = true;
        }
        // Reaction NOT consumed � this is a forced fight-back.
    };

    // Lambda: fire all pending covering-fire stacks on `atk` from the entity at `def_id`.
    // A stack is consumed ONLY if the covering unit successfully fires (has a reaction available,
    // is in range, and has LOS). Dead-source stacks are silently pruned. One stack per unique
    // source fires per attack. Unused stacks are left intact for future attacks this turn.
    // When the covered unit was removed by a lethal hit, read stacks from `defender_pre_hit`.
    auto fire_covering_fire = [&](const std::string& def_id, Unit& atk) {
        const auto dit = game.board.all_entities_map.find(def_id);
        const bool defender_on_board = dit != game.board.all_entities_map.end()
            && dit->second && dit->second->current_health > 0;
        std::vector<TemporaryEntityEffect> dead_snapshot;
        std::vector<TemporaryEntityEffect>* effs = nullptr;
        if (defender_on_board) {
            effs = &dit->second->temporary_effects;
        } else if (!def_id.empty() && defender_pre_hit.entity_id == def_id) {
            dead_snapshot = defender_pre_hit.temporary_effects;
            effs = &dead_snapshot;
        } else {
            return;
        }
        const bool live_defender = defender_on_board;
        std::set<std::string> checked;
        for (auto it = effs->begin(); it != effs->end() && game.board.all_entities_map.contains(atk.entity_id); ) {
            if (it->effect_id != "covering_fire" || it->source_id.empty() || checked.count(it->source_id)) {
                ++it; continue;
            }
            checked.insert(it->source_id);
            const auto src = game.board.all_entities_map.find(it->source_id);
            if (src == game.board.all_entities_map.end() || !src->second
                    || src->second->current_health <= 0) {
                it = effs->erase(it);
                continue;
            }
            auto cv = std::dynamic_pointer_cast<Unit>(src->second);
            if (!cv) { ++it; continue; }
            // Read any per-stack range bonus before we try the profile (and potentially erase).
            const int cover_bonus_range = it->stat_grants.bonus_ranged_range;
            const CoveringFireProfile cvp = covering_fire_profile_for(game, *cv, atk, cover_bonus_range);
            if (!cvp.ok) { ++it; continue; }   // can't fire this turn - leave stack intact
            if (live_defender) {
                it = effs->erase(it);
            } else {
                ++it;
            }
            const AttackRollResult cvr = roll_precise(cvp.damage_range, cvp.crit_chance_percent, *cv);
            DamagePacket cvpkt;
            cvpkt.source = cv; cvpkt.target = actor;
            cvpkt.amount = cvr.final_damage; cvpkt.rolled_amount = cvr.rolled_damage;
            cvpkt.is_crit = cvr.is_crit; cvpkt.damage_type = cvp.damage_type;
            cvpkt.pierces_damage_prevention = has_pierce(*cv);
            cvpkt.from_melee_attack = cvp.is_melee; cvpkt.from_basic_attack = true;
            cvpkt.from_reaction = true;
            cvpkt.source_label = cv->entity_id + " (covering fire)";
            bool cv_evasive_whiff = false;
            if (atk.entity_type == "unit") {
                const int evasive_stacks = evasive_stack_count(atk);
                if (evasive_stacks > 0 && roll_evasive_whiff(game.rng(), evasive_stacks)) {
                    cv_evasive_whiff = true;
                }
            }
            if (!cv_evasive_whiff) {
                // Ranged covering fire follows the same path as any other ranged shot:
                // body-blocking intercepts at 50% per cell, low-cover evasion applies.
                if (!cvp.is_melee && atk.position) {
                    const bool cvblocked = maybe_redirect_ranged_attack_to_blocking_unit(
                        game, *cv, *atk.position, atk, cvpkt, nullptr);
                    if (!cvblocked) {
                        maybe_redirect_ranged_attack_to_low_cover(game, *cv, *atk.position, atk, cvpkt);
                    }
                }
                apply_damage_packet(game, cvpkt);
            }
            cv->reactions_remaining_this_turn = std::max(0, cv->reactions_remaining_this_turn - 1);
        }
    };

    // Lambda: fire coordinated-fire shots at the same target, skipping cascade.
    // Coordinators are snapshotted before the loop to avoid iterator invalidation from
    // destroy_board_entity calls inside resolve_attack.
    auto fire_coordinated = [&](std::pair<int, int> tc, const std::string& vid) {
        if (vid.empty() || skip_coordinated_fire) return;
        std::vector<std::shared_ptr<Unit>> coords;
        for (auto& [ceid, ce] : game.board.all_entities_map) {
            if (!ce || ceid == actor->entity_id) continue;
            if (ce->current_health <= 0) continue;
            if (!ce->owner || teams_hostile(game, player_id, *ce->owner)) continue;
            auto cu = std::dynamic_pointer_cast<Unit>(ce);
            if (!cu || cu->coordinated_fire_shots_remaining <= 0) continue;
            if (entity_is_stunned(*cu)) continue;
            coords.push_back(cu);
        }
        for (auto& cu : coords) {
            if (cu->current_health <= 0 || !game.board.all_entities_map.count(cu->entity_id)) continue;
            const auto tit = game.board.all_entities_map.find(vid);
            if (tit == game.board.all_entities_map.end() || !tit->second
                    || tit->second->current_health <= 0) break;
            const int saved = cu->attacks_remaining_this_turn;
            cu->attacks_remaining_this_turn = 1;
            cu->coordinated_fire_shots_remaining--;
            // Apply per-shot damage override for this call only, then remove it immediately after.
            const bool has_cf_dmg = cu->coordinated_fire_damage_min > 0 && cu->coordinated_fire_damage_max > 0;
            if (has_cf_dmg) {
                TemporaryEntityEffect cf_override;
                cf_override.effect_id = "cf_damage_override";
                cf_override.expire_on = "never";
                cf_override.stat_grants.override_ranged_damage_min = cu->coordinated_fire_damage_min;
                cf_override.stat_grants.override_ranged_damage_max = cu->coordinated_fire_damage_max;
                cu->temporary_effects.push_back(std::move(cf_override));
            }
            resolve_attack(game, cu, *cu->owner, tc, true, true, std::nullopt, /*skip_coordinated_fire=*/true);
            if (has_cf_dmg) {
                auto orit = std::find_if(cu->temporary_effects.begin(), cu->temporary_effects.end(),
                    [](const TemporaryEntityEffect& e) { return e.effect_id == "cf_damage_override"; });
                if (orit != cu->temporary_effects.end()) cu->temporary_effects.erase(orit);
            }
            cu->attacks_remaining_this_turn = saved;
        }
    };

    auto fire_ally_reactions = [&]() {
        if (attacker_shadowstrike) return;
        if (victim_id.empty()) return;
        if (!game.board.all_entities_map.contains(actor->entity_id) || actor->current_health <= 0) return;
        fire_covering_fire(victim_id, *actor);
        fire_coordinated(target_cell, victim_id);
    };

    if (relentless_val > 0 && !victim_killed_by_attack) {
        // Relentless path: forced counterattack after every hit, including the primary.
        // Primary attack was already applied above; handle its reactions here.
        if (!first_strike_skip_counter && !attacker_shadowstrike) {
            fire_forced_counterattack(*actor);
        }
        fire_ally_reactions();
        // Additional attacks: primary is #1, loop runs #2..X (total = relentless_val).
        // Each relentless hit also triggers whirlwind, cleave, and multistrike (same as primary).
        for (int ri = 1; ri < relentless_val; ++ri) {
            if (!game.board.all_entities_map.contains(actor->entity_id) || actor->current_health <= 0) break;
            const auto tit = game.board.all_entities_map.find(victim_id);
            if (victim_id.empty() || tit == game.board.all_entities_map.end()
                    || !tit->second || tit->second->current_health <= 0) break;
            auto ri_target = tit->second;
            const std::string ri_label = actor->entity_id + " (relentless " + std::to_string(ri) + ")";
            // Fresh attack roll (same profile as primary).
            const AttackRollResult ri_roll = roll_precise(profile.damage_range, actor->crit_chance_percent, *actor);
            DamagePacket ri_pkt;
            ri_pkt.source = actor; ri_pkt.target = ri_target;
            ri_pkt.amount = ri_roll.final_damage; ri_pkt.rolled_amount = ri_roll.rolled_damage;
            ri_pkt.is_crit = ri_roll.is_crit; ri_pkt.damage_type = profile.damage_type;
            ri_pkt.pierces_damage_prevention = profile.pierces_damage_prevention;
            ri_pkt.from_melee_attack = !profile.use_ranged; ri_pkt.from_basic_attack = true;
            ri_pkt.ability_grants_lifesteal  = packet.ability_grants_lifesteal;
            ri_pkt.ability_grants_soul_steal = packet.ability_grants_soul_steal;
            ri_pkt.soul_steal_heal_base      = packet.soul_steal_heal_base;
            ri_pkt.source_label = ri_label;
            apply_damage_packet(game, ri_pkt);
            // Area effects and multistrike for this relentless hit.
            if (game.board.all_entities_map.contains(actor->entity_id) && actor->current_health > 0) {
                do_whirlwind(ri_roll, victim_id);
                if (ri_target->position) { do_cleave(ri_roll, *ri_target); }
                if (ri_target->position) { do_volley(ri_roll, *ri_target); }
                do_multistrike_for(victim_id, ri_label);
            }
            // Forced CA fires once per relentless hit, after all area effects and multistrike.
            if (!first_strike_skip_counter && !attacker_shadowstrike) {
                fire_forced_counterattack(*actor);
            }
            fire_ally_reactions();
        }
    } else {
        // Normal path: standard counterattack with full requirements.
        // Defender may have died to the primary hit; counter still uses their pre-removal stats
        // unless First Strike suppressed the defender's counterattack.
        if (!first_strike_skip_counter) {
            const Unit& counter_src =
                (defender && game.board.all_entities_map.contains(defender->entity_id) && defender->position)
                    ? *defender
                    : defender_pre_hit;
            const CounterattackProfile counter_profile = (allow_counterattack &&
                game.board.all_entities_map.contains(actor->entity_id) && actor->current_health > 0 &&
                defender && victim_snapshot.entity_type == "unit")
                ? counterattack_profile_for(game, counter_src, *actor)
                : CounterattackProfile{};
            if (counter_profile.ok) {
                const AttackRollResult counter_roll =
                    roll_precise(counter_profile.damage_range, counter_profile.crit_chance_percent, counter_src);
                DamagePacket counter_packet;
                counter_packet.source = defender;
                counter_packet.target = actor;
                counter_packet.amount = counter_roll.final_damage;
                counter_packet.rolled_amount = counter_roll.rolled_damage;
                counter_packet.is_crit = counter_roll.is_crit;
                counter_packet.damage_type = counter_profile.damage_type;
                counter_packet.pierces_damage_prevention = has_pierce(*defender);
                counter_packet.from_melee_attack = counter_profile.is_melee_attack;
                counter_packet.from_basic_attack = true;
                counter_packet.from_reaction = true;
                counter_packet.source_label = defender->entity_id + " counterattack";
                bool counter_evaded_to_cover = false;
                bool counter_body_blocked = false;
                bool counter_evasive_whiff = false;
                if (actor && actor->entity_type == "unit") {
                    const int evasive_stacks = evasive_stack_count(*actor);
                    if (evasive_stacks > 0 && roll_evasive_whiff(game.rng(), evasive_stacks)) {
                        counter_evasive_whiff = true;
                    }
                }
                ActionResult counter_result{true, "", {}};
                if (!counter_evasive_whiff) {
                    if (!counter_profile.is_melee_attack && actor && actor->entity_type == "unit" && actor->position) {
                        counter_body_blocked =
                            maybe_redirect_ranged_attack_to_blocking_unit(game, *defender, *actor->position, *actor, counter_packet, nullptr);
                        if (!counter_body_blocked) {
                            counter_evaded_to_cover =
                                maybe_redirect_ranged_attack_to_low_cover(game, *defender, *actor->position, *actor, counter_packet);
                        }
                    }
                    counter_result = apply_damage_packet(game, counter_packet);
                } else {
                    counter_result.message = actor->entity_id + " evaded the counterattack (Evasive)";
                }
                if (counter_body_blocked && counter_result.ok) {
                    counter_result.message += " (body-blocked in line of fire)";
                } else if (counter_evaded_to_cover && counter_result.ok) {
                    counter_result.message += " (evaded behind low cover)";
                }
                if (!counter_result.ok) return counter_result;
                viz_counter_damage = counter_packet.damage_display;
                if (counter_roll.is_crit) {
                    viz_counter_crit = true;
                }
                defender->reactions_remaining_this_turn = std::max(0, defender->reactions_remaining_this_turn - 1);
            }
        }
        // Covering fire and coordinated fire still fire when First Strike suppresses counterattack.
        fire_ally_reactions();
    }

    if (game.combat_visualization_enabled() && !skip_coordinated_fire) {
        game.record_combat_viz_encounter_result(
            {viz_attack_crit, viz_counter_crit, viz_attack_damage, viz_counter_damage});
    }

    consume_unit_attack_budget(*actor);
    if (!victim_id.empty() && !game.board.all_entities_map.contains(victim_id)) {
        try_trigger_deferred_frenzy_after_attack_kill(game, actor, victim_snapshot);
    }
    // Deferred reactive: fire after the full exchange (including counterattack) so a
    // counterattack kill suppresses the proc. fire_pending_reactives() checks source alive.
    game.fire_pending_reactives();
    return {true, "Attack resolved", {}};
}

void consume_unit_attack_budget(Unit& actor)
{
    actor.attacks_remaining_this_turn = std::max(0, actor.attacks_remaining_this_turn - 1);
    actor.has_attacked_this_turn = true;
}

ActionResult validate_unit_attack_budget(const Unit& actor, int player_id)
{
    if (!entity_owned_by(actor, player_id)) {
        return {false, "Not your unit", {}};
    }
    if (!actor.position) {
        return {false, "Unit has no position", {}};
    }
    if (actor.attacks_remaining_this_turn <= 0) {
        return {false, "No attacks remaining this turn", {}};
    }
    return {true, "Attack action available", {}};
}

ActionResult validate_unit_base_attack_action_budget(const GameState& game, const Unit& actor, int player_id)
{
    if (game.turn_manager.current_phase == TurnPhase::BonusAttackDeclaration) {
        return {false, "Cannot spend a base attack action during bonus attack declaration", {}};
    }
    return validate_unit_attack_budget(actor, player_id);
}

void consume_move_action_on_confirm(Unit& actor)
{
    if (actor.standard_moves_remaining_this_turn > 0) {
        actor.standard_moves_remaining_this_turn = 0;
    }
    actor.moves_remaining_this_turn = std::max(0, actor.moves_remaining_this_turn - 1);
    actor.has_moved_this_turn = true;
}

void consume_standard_move_if_unused(Unit& actor)
{
    if (actor.standard_moves_remaining_this_turn > 0) {
        actor.standard_moves_remaining_this_turn = 0;
        actor.moves_remaining_this_turn = std::max(0, actor.moves_remaining_this_turn - 1);
    }
}

ActionResult validate_unit_dash_budget(const GameState& game, const Unit& actor, int player_id)
{
    if (deployment_fatigue_blocks_attack_actions(actor)) {
        return {false, "Cannot dash on the turn this unit was deployed", {}};
    }
    const auto phase = game.turn_manager.current_phase;
    if (phase != TurnPhase::Main && phase != TurnPhase::SecondMain && phase != TurnPhase::AttackDeclaration) {
        return {false, "Cannot dash in the current phase", {}};
    }
    return validate_unit_base_attack_action_budget(game, actor, player_id);
}

ActionResult validate_unit_defend_budget(const GameState& game, const Unit& actor, int player_id)
{
    const auto phase = game.turn_manager.current_phase;
    if (phase != TurnPhase::Main && phase != TurnPhase::SecondMain) {
        return {false, "Defend can only be used during main phase", {}};
    }
    if (entity_has_defend_stance(actor)) {
        return {false, "Already defending", {}};
    }
    if (entity_has_recover_stance(actor)) {
        return {false, "Cannot defend while recovering", {}};
    }
    if (deployment_fatigue_blocks_attack_actions(actor)) {
        return {false, "Cannot defend on the turn this unit was deployed", {}};
    }
    return validate_unit_base_attack_action_budget(game, actor, player_id);
}

ActionResult grant_defend_stance_effect(GameState& game, const std::shared_ptr<Unit>& actor)
{
    if (!actor) {
        return {false, "No unit", {}};
    }
    if (entity_has_defend_stance(*actor)) {
        return {false, "Already defending", {}};
    }
    TemporaryEntityEffect defend;
    defend.effect_id = effect_keys::kDefendStanceEffectId;
    defend.name = "Defend";
    defend.rules_text = "+1 armor until the start of your next turn. Cannot use abilities while defending.";
    defend.expire_on = "owner_turn_start";
    defend.remaining_turns = 1;
    game.add_temporary_effect(actor, std::move(defend));
    return {true, actor->entity_id + " is defending (+1 armor until next turn)", {}};
}

ActionResult apply_defend_stance_from_stack(GameState& game, const StackItem& item)
{
    if (item.source_entity_id.empty()) {
        return {false, "Defend requires a source unit", {}};
    }
    const auto it = game.board.all_entities_map.find(item.source_entity_id);
    if (it == game.board.all_entities_map.end() || !it->second) {
        return {true, "Defend fizzled: source unit gone", {}};
    }
    auto unit = std::dynamic_pointer_cast<Unit>(it->second);
    if (!unit) {
        return {false, "Defend requires a unit", {}};
    }
    if (entity_has_defend_stance(*unit)) {
        return {false, "Already defending", {}};
    }
    if (deployment_fatigue_blocks_attack_actions(*unit)) {
        return {false, "Cannot defend on the turn this unit was deployed", {}};
    }
    auto vr = grant_defend_stance_effect(game, unit);
    if (!vr.ok) {
        return vr;
    }
    consume_standard_move_if_unused(*unit);
    return vr;
}

ActionResult apply_defend_stance(GameState& game, const std::shared_ptr<Unit>& actor, int player_id)
{
    auto vr = validate_unit_defend_budget(game, *actor, player_id);
    if (!vr.ok) {
        return vr;
    }
    vr = grant_defend_stance_effect(game, actor);
    if (!vr.ok) {
        return vr;
    }
    consume_standard_move_if_unused(*actor);
    consume_unit_attack_budget(*actor);
    return vr;
}

ActionResult apply_dash_movement(GameState& game, const std::shared_ptr<Unit>& actor, int player_id)
{
    auto vr = validate_unit_dash_budget(game, *actor, player_id);
    if (!vr.ok) {
        return vr;
    }
    TemporaryEntityEffect dash;
    dash.effect_id = effect_keys::kDashMovementEffectId;
    dash.name = "Dash";
    dash.rules_text = "+1 movement this turn.";
    dash.expire_on = "owner_turn_end";
    dash.remaining_turns = 1;
    dash.stat_grants.bonus_movement = 1;
    game.add_temporary_effect(actor, std::move(dash));
    consume_unit_attack_budget(*actor);
    return {true, actor->entity_id + " dashed (+1 movement this turn)", {}};
}

ActionResult validate_unit_recover_budget(const GameState& game, const Unit& actor, int player_id)
{
    const auto phase = game.turn_manager.current_phase;
    if (phase != TurnPhase::Main && phase != TurnPhase::SecondMain) {
        return {false, "Recover can only be used during main phase", {}};
    }
    if (entity_has_recover_stance(actor)) {
        return {false, "Already recovering", {}};
    }
    if (entity_has_defend_stance(actor)) {
        return {false, "Cannot recover while defending", {}};
    }
    if (deployment_fatigue_blocks_attack_actions(actor)) {
        return {false, "Cannot recover on the turn this unit was deployed", {}};
    }
    const int max_hp = entity_effective_base_health(actor);
    if (actor.current_health >= max_hp) {
        return {false, "Already at full health", {}};
    }
    return validate_unit_base_attack_action_budget(game, actor, player_id);
}

ActionResult grant_recover_stance_effect(GameState& game, const std::shared_ptr<Unit>& actor)
{
    if (!actor) {
        return {false, "No unit", {}};
    }
    if (entity_has_recover_stance(*actor)) {
        return {false, "Already recovering", {}};
    }
    remove_entity_effect(*actor, effect_keys::kRecoverStanceDamageTakenKey);
    TemporaryEntityEffect recover;
    recover.effect_id = effect_keys::kRecoverStanceEffectId;
    recover.name = "Recover";
    recover.rules_text = "If you take no damage before your next turn, heal 2 HP at turn start.";
    recover.expire_on = "owner_turn_start";
    recover.remaining_turns = 1;
    game.add_temporary_effect(actor, std::move(recover));
    return {true, actor->entity_id + " is recovering (heal 2 at turn start if unhurt)", {}};
}

ActionResult apply_recover_stance(GameState& game, const std::shared_ptr<Unit>& actor, int player_id)
{
    auto vr = validate_unit_recover_budget(game, *actor, player_id);
    if (!vr.ok) {
        return vr;
    }
    vr = grant_recover_stance_effect(game, actor);
    if (!vr.ok) {
        return vr;
    }
    consume_standard_move_if_unused(*actor);
    consume_unit_attack_budget(*actor);
    return vr;
}

ActionResult apply_deployment_fatigue(GameState& game, const std::shared_ptr<Unit>& unit)
{
    if (!unit) {
        return {false, "No unit", {}};
    }
    if (entity_skips_deployment_fatigue(*unit)) {
        return {true, unit->entity_id + " deploys with Charge (no deployment fatigue)", {}};
    }
    if (entity_has_deployment_fatigue(*unit)) {
        return {true, "Already deployment fatigued", {}};
    }
    TemporaryEntityEffect fatigue;
    fatigue.effect_id = effect_keys::kDeploymentFatigueEffectId;
    fatigue.name = "Deployment fatigue";
    const bool has_haste = entity_has_attribute(*unit, "haste");
    const bool has_surge = entity_has_attribute(*unit, "surge");
    if (has_haste && has_surge) {
        fatigue.rules_text =
            "Haste + Surge: may move, attack, defend, dash, and use abilities this turn.";
    } else if (has_haste) {
        fatigue.rules_text =
            "Haste: may move normally but cannot attack, defend, dash, or use abilities this turn.";
    } else if (has_surge) {
        fatigue.rules_text =
            "Surge: may attack, defend, dash, and use abilities but cannot move this turn.";
    } else {
        fatigue.rules_text =
            "Cannot move, defend, dash, use abilities, or make ranged attacks this turn. Can still make melee attacks and ranged counterattacks (Return Fire, etc.). Bonus move actions can be spent to move.";
    }
    fatigue.expire_on = "owner_turn_start";
    fatigue.remaining_turns = 1;
    fatigue.granted_attributes.push_back(PassiveAttributeGrant{"deployment_fatigue", std::nullopt});
    game.add_temporary_effect(unit, std::move(fatigue));
    unit->has_moved_this_turn = false;
    if (entity_can_move(*unit)) {
        unit->moves_remaining_this_turn =
            deployment_fatigue_blocks_move(*unit) ? unit->bonus_moves : 1 + unit->bonus_moves;
    }
    refresh_standard_moves_remaining(*unit);
    if (deployment_fatigue_blocks_attack(*unit)) {
        unit->attacks_remaining_this_turn = 0;
    }
    return {true, unit->entity_id + " is deployment fatigued", {}};
}

void consume_attack_action_from_stack_item(GameState& game, const StackItem& item)
{
    if (!item.consumes_attack_action || item.source_entity_id.empty()) {
        return;
    }
    const auto it = game.board.all_entities_map.find(item.source_entity_id);
    if (it == game.board.all_entities_map.end() || !it->second) {
        return;
    }
    if (auto unit = std::dynamic_pointer_cast<Unit>(it->second)) {
        consume_unit_attack_budget(*unit);
    }
}

std::vector<std::pair<int, int>> gather_attackable_goal_cells(
    GameState& game, const std::shared_ptr<Unit>& actor, const int player_id)
{
    std::vector<std::pair<int, int>> out;
    if (!actor || !actor->owner) {
        return out;
    }
    if (actor->attacks_remaining_this_turn <= 0) {
        return out;
    }
    std::shared_ptr<Unit> pose = game.unit_at_validation_pose(actor);
    if (!pose) {
        pose = actor;
    }
    std::set<std::pair<int, int>> cells;
    for (const auto& [_, ent] : game.board.all_entities_map) {
        const auto target_unit = std::dynamic_pointer_cast<Unit>(ent);
        if (!target_unit || target_unit == actor || !target_unit->owner ||
            !teams_hostile(game, *actor->owner, *target_unit->owner)) {
            continue;
        }
        std::vector<std::pair<int, int>> target_cells = target_unit->occupied_positions;
        if (target_cells.empty() && target_unit->position) {
            const auto [ax, ay] = *target_unit->position;
            for (const auto& [dx, dy] : entity_shape_offsets(*target_unit)) {
                target_cells.push_back({ax + dx, ay + dy});
            }
        }
        // Early-exit per enemy once they are attackable (skip extra cells on a large footprint).
        bool can_attack = false;
        for (const auto& [wx, wy] : target_cells) {
            if (validate_attack(game, pose, player_id, {wx, wy}, false).ok) {
                can_attack = true;
                break;
            }
            if (pose->attack_type == AttackType::Ranged || pose->attack_type == AttackType::Hybrid) {
                if (validate_attack(game, pose, player_id, {wx, wy}, true).ok) {
                    can_attack = true;
                    break;
                }
            }
        }
        if (can_attack) {
            for (const auto& c : target_cells) {
                cells.insert(c);
            }
        }
    }
    out.reserve(cells.size());
    for (const auto& c : cells) {
        out.push_back(c);
    }
    return out;
}

}  // namespace tactics
