#pragma once

#include "tactics/common/types.hpp"
#include "tactics/core/stack.hpp"
#include "tactics/entities/entity.hpp"

#include <memory>
#include <optional>
#include <random>
#include <string>
#include <utility>

namespace tactics {

class GameState;

/** Result of one damage roll, including crit. */
struct AttackRollResult {
    int rolled_damage{0};
    int final_damage{0};
    bool is_crit{false};
};

/** Melee or ranged profile used to resolve a basic attack. */
struct AttackProfile {
    bool use_ranged{false};
    int range_min{0};
    int range_max{1};
    DamageRange damage_range{};
    bool requires_line_of_sight{false};
    DamageType damage_type{DamageType::Physical};
    bool pierces_damage_prevention{false};
};

/** One packet of damage about to be applied to a target. */
struct DamagePacket {
    std::shared_ptr<Entity> source{};
    std::shared_ptr<Entity> target{};
    int amount{0};
    int rolled_amount{0};
    bool is_crit{false};
    DamageType damage_type{DamageType::Physical};
    bool pierces_damage_prevention{false};
    /** Stack ability keyword lifesteal (unit keyword read from `source`). */
    bool ability_grants_lifesteal{false};
    /** Stack ability keyword soul steal (unit keyword read from `source`). */
    bool ability_grants_soul_steal{false};
    /** Pre-resolved allied base to heal; auto-filled when only one allied base exists. */
    std::shared_ptr<Entity> soul_steal_heal_base{};
    std::string source_label;
    /** True for basic attacks / counterattacks using a melee profile (not ranged or return fire). */
    bool from_melee_attack{false};
    /** True for any basic attack (melee or ranged) or counterattack - false for ability damage. */
    bool from_basic_attack{false};
    /** When true, Frenzy refresh runs after the attack budget is spent (basic attacks only). */
    bool defer_frenzy_refresh{false};
    /**
     * When true, the reactive passive check (tithe, etc.) is deferred until after the full
     * attack exchange (including counterattack) so a counterattack kill suppresses the proc.
     * Set by resolve_attack on the initial attack packet only.
     */
    bool defer_reactive{false};
    /** True when this packet is fired as a reaction: counterattack, return fire, or covering fire.
     *  Used by the Defender keyword to grant bonus damage on all reaction shots. */
    bool from_reaction{false};
    /** When true, skip next_damage_bonus and keyword bonuses (coordinated, entrenched, berserk, defender). */
    bool suppress_source_damage_bonuses{false};
    /** Output: actual damage dealt after armor/prevention. Written by apply_damage_packet. */
    int damage_dealt{0};
    /** Output: post-mitigation damage before HP overkill clamp (combat viz). Written by apply_damage_packet. */
    int damage_display{0};
};

struct HealPacket {
    std::shared_ptr<Entity> target{};
    int amount{0};
    std::string source_label;
};

/** Restore HP up to effective max; returns amount actually healed (0 if target is at cap or dead). */
int apply_entity_heal(Entity& target, int amount);

/** Crit: max of damage range, then × this multiplier (rounded down). */
constexpr double kCritDamageMultiplier = 1.5;

AttackRollResult roll_attack_damage(std::mt19937& rng, DamageRange range, int crit_chance_percent);
void consume_unit_attack_budget(Unit& actor);
void consume_move_action_on_confirm(Unit& actor);
/** Defend: spend the standard move only if still unused; never spends bonus moves. */
void consume_standard_move_if_unused(Unit& actor);
ActionResult validate_unit_attack_budget(const Unit& actor, int player_id);
/** Base attack action only (not bonus-attack declaration phase). */
ActionResult validate_unit_base_attack_action_budget(const GameState& game, const Unit& actor, int player_id);
ActionResult validate_unit_dash_budget(const GameState& game, const Unit& actor, int player_id);
ActionResult validate_unit_defend_budget(const GameState& game, const Unit& actor, int player_id);
ActionResult grant_defend_stance_effect(GameState& game, const std::shared_ptr<Unit>& actor);
ActionResult apply_defend_stance(GameState& game, const std::shared_ptr<Unit>& actor, int player_id);
ActionResult apply_defend_stance_from_stack(GameState& game, const StackItem& item);
ActionResult apply_dash_movement(GameState& game, const std::shared_ptr<Unit>& actor, int player_id);
ActionResult validate_unit_recover_budget(const GameState& game, const Unit& actor, int player_id);
ActionResult grant_recover_stance_effect(GameState& game, const std::shared_ptr<Unit>& actor);
ActionResult apply_recover_stance(GameState& game, const std::shared_ptr<Unit>& actor, int player_id);
/** Applied when a unit enters the board from deploy; expires at owner's next turn start. */
ActionResult apply_deployment_fatigue(GameState& game, const std::shared_ptr<Unit>& unit);
void consume_attack_action_from_stack_item(GameState& game, const StackItem& item);
AttackProfile attack_profile_for_unit(const Unit& actor, bool prefer_ranged);
ActionResult validate_attack(GameState& game, const std::shared_ptr<Unit>& actor, int player_id, std::pair<int, int> target, bool prefer_ranged);
/** World cells attackable by `actor` this turn (union of valid target footprint cells). */
// Takes GameState& because validate_attack requires non-const even though it does not mutate.
std::vector<std::pair<int, int>> gather_attackable_goal_cells(
    GameState& game, const std::shared_ptr<Unit>& actor, int player_id);
ActionResult resolve_attack(GameState& game, const std::shared_ptr<Unit>& actor, int player_id, std::pair<int, int> target, bool prefer_ranged,
    bool allow_counterattack = true, std::optional<std::pair<int, int>> soul_steal_heal_base_cell = std::nullopt,
    bool skip_coordinated_fire = false);
ActionResult apply_damage_packet(GameState& game, DamagePacket& packet);
ActionResult apply_heal_packet(GameState& game, const HealPacket& packet);

/** Coordinated + entrenched + berserk (+ defender when from_reaction). Excludes next_damage_bonus stacks. */
int compute_keyword_damage_bonuses(const GameState& game, const Entity& source, const Entity& victim, bool from_reaction);

/** Consumes every next_damage_bonus stack on source; returns total removed. */
int consume_all_next_damage_bonus(Entity& source);

/** True if any footprint cell of `actor` has clear LOS to `target` (game coordinates). */
bool entity_has_line_of_sight_to_cell(const GameState& game, const Entity& actor, std::pair<int, int> target);

/**
 * For ranged shots through units: each blocking unit-cell before the intended target has a 50% intercept chance.
 * On intercept, retargets `packet` to that blocking unit and returns true.
 */
bool maybe_redirect_ranged_attack_to_blocking_unit(
    GameState& game,
    const Entity& attacker,
    std::pair<int, int> target_cell,
    const Entity& intended_target,
    DamagePacket& packet,
    const AbilitySpec* ability = nullptr);

/** How many living units would body-block a ranged shot from `attacker` at `target_cell`
 *  aimed at `intended_target` - the units on the shot path (excluding the shooter, the
 *  target, and an ally adjacent to the shooter) that each get an independent 50% intercept
 *  roll. 0 when the source ignores cover (trueshot/flying) or the lane is clear. The
 *  probability the shot actually reaches the target is 0.5^count. Read-only companion to
 *  maybe_redirect_ranged_attack_to_blocking_unit, for AI shot evaluation. */
int ranged_body_block_count(
    const GameState& game,
    const Entity& attacker,
    std::pair<int, int> target_cell,
    const Entity& intended_target);

/** Range and LOS for activated abilities with `ability_uses_ranged_targeting` (no-op when false). */
ActionResult validate_ranged_damage_ability_target(
    const GameState& game, const Entity& actor, const AbilitySpec& ability, std::pair<int, int> target_cell);

}  // namespace tactics