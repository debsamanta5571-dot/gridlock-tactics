#pragma once

#include "tactics/common/effect_keys.hpp"
#include "tactics/common/types.hpp"
#include "tactics/effects/effect_registry.hpp"

#include <algorithm>
#include <climits>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace tactics {

/** Named stat bonus attached to an entity. */
struct Modifier {
    std::string name;
    std::string source;
    int bonus_attack{0};
    int bonus_movement{0};
    int bonus_ranged_length{0};
    int bonus_health{0};
};

/** One activated ability on a card or unit. */
struct AbilitySpec {
    std::string key;
    std::string name;
    EffectSpeed speed{EffectSpeed::Channeled};
    std::map<EnergyType, int> energy_cost;
    std::string effect_key;
    std::map<std::string, int> effect_payload;
    /** String-valued payload fields (e.g. "shape" for directional_damage). */
    std::map<std::string, std::string> effect_string_payload;
    /** Ability-only keywords such as `pierce` / `lifesteal` (need not be on the unit). */
    std::vector<std::string> keywords{};
    /** When set, overrides inference from `effect_key` for CLI/UI targeting. */
    std::optional<bool> requires_board_target{};
    /** When the ability uses a board cell, which entity ownership/team relation is legal (default from `effect_key`). */
    std::optional<BoardTargetKind> board_target_kind{};
    /** Spends one of the unit's attack actions when the ability resolves (or on Burst execute). */
    bool consumes_attack_action{false};
    /** Board target must have at least one listed type (OR). Empty = no restriction. */
    std::vector<std::string> require_target_unit_types{};
    /** Extra damage on deal_damage when the target matches any listed type (OR). */
    std::vector<std::string> bonus_damage_unit_types{};
    int bonus_damage_amount{0};
    /** When set, overrides default inference for LOS/range checks on damaging abilities. */
    std::optional<bool> uses_ranged_targeting{};
    /** Chebyshev max/min for ranged ability targeting; 0 max falls back to unit ranged_range or 4. */
    int range_max{0};
    int range_min{0};
    /**
     * Uses allowed per owner turn. Omit in catalog for default: 1 (non-barrage) or unlimited (barrage).
     * Explicit `0` = unlimited uses per turn.
     */
    int uses_per_turn{-1};
    /**
     * Uses allowed for the whole match (never refresh). Omit for unlimited.
     * Tracked in `Entity::ability_uses_remaining_game`.
     */
    int uses_per_game{-1};
    /**
     * Barrage keyword: this ability may be cast multiple times per turn.
     * First cast costs `energy_cost`. Each subsequent cast costs `energy_cost + barrage_cost × cast_count`.
     * Cast count is tracked in `Entity::barrage_cast_counts_this_turn` for incremental cost.
     */
    bool barrage{false};
    std::map<EnergyType, int> barrage_cost{};
    /**
     * Chain: after the primary effect resolves, BFS flood-fill through connected entities
     * that satisfy `board_target_kind` and apply the same effect to each.
     * See `StackItem::chain` for the full runtime contract.
     */
    bool chain{false};
    /**
     * When true, this ability bypasses the per-unit phase batch commitment check.
     * Use for combo-chain abilities (e.g. debilitator) that intentionally cast
     * multiple times from the same unit in one phase.
     */
    bool no_phase_batch_lock{false};
    /** Human-readable description shown in UI (full/advanced detail). */
    std::string description;
    /** Normal (default) description; when empty, UI falls back to `description`. */
    std::string normal_description;
    /** X-cost ability: player chooses X at activation; pay extra of this energy type (in addition to energy_cost). */
    std::optional<EnergyType> x_cost_energy_type{};
    int x_cost_min{0};
};

struct PassiveAttributeGrant {
    std::string key;
    std::optional<int> amount{};
};

struct PassiveStatGrant {
    int bonus_attack{0};
    int bonus_health{0};
    int bonus_melee_damage{0};
    int bonus_ranged_damage{0};
    int bonus_movement{0};
    /** Extra move actions granted per turn (expires with the temp effect). */
    int bonus_moves{0};
    /** Extra attack actions granted per turn (expires with the temp effect). */
    int bonus_attacks{0};
    /** When this temporary effect expires, grant this many next_damage_bonus stacks to the bearer. */
    int on_expire_next_damage_bonus{0};
    /** Extra flat damage added to activated ability damage rolls (aura bonus from e.g. The Boss). */
    int bonus_ability_damage{0};
    /**
     * Temporary bonus to the source entity's `aura_range` on all range-limited aura passives.
     * Used by Amplify (The Boss) to extend the passive aura radius for one turn.
     */
    int bonus_aura_range{0};
    /**
     * Temporary amplifier on the aura source: adds to the `bonus_attack` grant broadcast to allied
     * units by this entity's `allied_units` passives. Used by Inspire (Inspiring Commander).
     */
    int bonus_aura_attack{0};
    /**
     * Temporary amplifier on the aura source: adds to the `bonus_health` grant broadcast to allied
     * units by this entity's `allied_units` passives. Used by Inspire (Inspiring Commander).
     */
    int bonus_aura_health{0};
    /**
     * Temporary amplifier on the aura source: adds to the `bonus_ability_damage` grant broadcast
     * to allied units by this entity's `allied_units` passives. Used by Press the Advantage
     * (The Boss) to temporarily strengthen the Command Presence aura.
     */
    int bonus_aura_ability_damage{0};
    /**
     * Temporary bonus to the bearer's basic ranged attack range and to abilities that inherit
     * the unit's ranged range (where ability.range_max == 0).
     * Used by Artillery Mode (Voltcrusher Dreadnought) to extend range until start of next turn.
     */
    int bonus_ranged_range{0};
    /**
     * Temporary bonus to the bearer's basic ranged attack deadzone (minimum range) and to
     * abilities that inherit the unit's ranged deadzone (where ability.range_min == 0).
     * Used by Artillery Mode to add a deadzone-1 restriction until start of next turn.
     */
    int bonus_ranged_deadzone{0};
    /**
     * When >0, replaces the bearer's base ranged_damage_min/max for attack rolls and
     * counterattack/covering-fire profiles.  Additive bonuses (aura, bonus_ranged_damage) still
     * apply on top of the override.  Used by weapon-swap mechanics such as Remington's Swap
     * Weapons and Dual Wield abilities.  Max of all active effects wins.
     */
    int override_ranged_damage_min{0};
    int override_ranged_damage_max{0};
    /**
     * Percentage chance (0–100) that this entity survives a lethal hit at 1 HP instead of dying.
     * Sourced from `applies_to: "self"` passive stat grants; reset to 0 and recomputed each time
     * `refresh_passive_auras` runs.  Not serialized - always recomputed from passives.
     * If the roll succeeds and the entity was not already stunned, 1 stun stack is applied.
     */
    int survive_lethal_percent{0};
    /**
     * Permanent attack and health bonus granted to this entity each time the survive_lethal roll
     * succeeds.  Sourced from `applies_to: "self"` passive stat grants; summed (not capped) so
     * multiple passive sources stack.  Reset to 0 and recomputed by `refresh_passive_auras`.
     * Not serialized - always recomputed.
     */
    int survive_lethal_bonus_attack{0};
    int survive_lethal_bonus_health{0};
    /** Permanent armor from self passives (not a keyword). */
    int bonus_armor{0};
};

struct PassiveAbilitySpec {
    std::string key;
    std::string name;
    /** Full/advanced description (everything). */
    std::string rules_text;
    /** Normal (default) description; when empty, UI falls back to `rules_text`. */
    std::string normal_rules_text;
    /** `self` grants only to the bearer; `allied_units` is a live aura from the bearer to allied units. */
    std::string applies_to{"self"};
    /** When non-empty, only entities with at least one listed type receive this passive (OR). */
    std::vector<std::string> affects_unit_types{};
    /** Keyword attributes granted by this passive while the entity has it. */
    std::vector<PassiveAttributeGrant> granted_attributes{};
    PassiveStatGrant stat_grants{};
    /**
     * Automated action timing (e.g. `owner_turn_start`). When set with `automated_effect_key`, the passive
     * resolves automatically; oldest units on the board act first (see `passive_action_order.hpp`).
     */
    std::string trigger_timing{};
    std::string automated_effect_key{};
    std::map<std::string, int> automated_effect_payload{};
    /** String-valued auxiliary payload for automated passives (e.g. `"pool"` tag for gain_orange routing). */
    std::map<std::string, std::string> automated_string_payload{};
    std::optional<BoardTargetKind> automated_board_target_kind{};
    /** Reactive trigger (e.g. `damage_dealt_enemy_unit_survive`) with `reactive_effect_key` / payload. */
    std::string reactive_trigger{};
    std::string reactive_effect_key{};
    std::map<std::string, int> reactive_effect_payload{};
    /** String-valued auxiliary payload (e.g. `"pool"` tag for gain_orange routing). */
    std::map<std::string, std::string> reactive_string_payload{};
    /**
     * Firing order among passives on the same unit at the same trigger timing.
     * Lower = fires first. Default 0 fires before anything with a higher value.
     * Use a large value (e.g. 100) for passives that must fire last.
     */
    int sort_order{0};
    /**
     * Passive-only engine hook (not a keyword). Values: bleed_on_hit, shock_on_hit, fire_on_hit,
     * gas_on_hit, overload_on_hit, overload_resistance, overload_feed. Optional amounts live in mechanic_payload.
     */
    std::string passive_mechanic{};
    std::map<std::string, int> mechanic_payload{};
    /**
     * Chebyshev range for `applies_to: "allied_units"` auras; -1 means global (no range limit).
     * Effective range = `aura_range + temporary_stat_grants_for_entity(source).bonus_aura_range`.
     */
    int aura_range{-1};
    /** When true, owner must cast non-Focus damaging spells through this unit as a Focus caster while it is on board. */
    bool forces_damage_spell_focus_casting{false};
    /** Chebyshev range for `forces_damage_spell_focus_casting` (default 0 = disabled). */
    int forced_damage_spell_focus_range{0};
    /** True when this passive grants a net benefit to the bearer or its allies (buffs, stat boosts,
     *  energy generation, protective reactions, etc.).  Used by UI and future "remove all buffs"
     *  mechanics to categorise passives without inspecting field values. */
    bool is_positive{false};
    /** True when this passive imposes a net cost or hazard on the bearer or its allies
     *  (e.g. Macrowave Pulse - damages surrounding units each turn).  A passive can be both
     *  positive and negative when its effect is genuinely mixed. */
    bool is_negative{false};
};

struct TemporaryEntityEffect {
    std::string effect_id;
    std::string source_id;
    std::string name;
    std::string rules_text;
    /** Duration counter consumed by `expire_on`; <=0 means expired. */
    int remaining_turns{1};
    /** Currently supported: `owner_turn_start`, `owner_turn_end`, `round_start`, `never`. */
    std::string expire_on{"owner_turn_start"};
    std::vector<PassiveAttributeGrant> granted_attributes{};
    PassiveStatGrant stat_grants{};
    /**
     * Keyword attributes suppressed while this effect is active.  Checked in
     * `entity_has_attribute` before the "has" checks so a suppressed keyword is invisible
     * even if it appears in `keywords` or `granted_attributes`.
     * Used by weapon-swap mechanics to turn off base-unit keywords for a given mode.
     */
    std::vector<std::string> suppress_attributes{};
    /**
     * Ability keys on the bearer that are disabled while this effect is active.
     * Checked in `ActivateAbilityAction::validate`.  Used by Dual Wield to lock out
     * Swap Weapons once the unit commits to the dual-wield mode.
     */
    std::vector<std::string> disable_ability_keys{};
    /** Passive ability keys suppressed while this effect is active (e.g. transformation modes). */
    std::vector<std::string> suppress_passive_keys{};
};

/** A status or delayed effect currently on an entity. */
struct EntityEffectInstance {
    std::string key;
    int amount{0};
    std::string source_id;
    int remaining_turns{0};
    std::string expire_on{"never"};
};

inline bool ability_has_attribute(const AbilitySpec& a, const std::string& key)
{
    return std::find(a.keywords.begin(), a.keywords.end(), key) != a.keywords.end();
}

/** When true, activating the ability spends one attack action (see `consume_unit_attack_budget`). */
inline bool ability_consumes_attack_action(const AbilitySpec& a)
{
    return a.consumes_attack_action || ability_has_attribute(a, "attack_cost");
}

inline void add_ability_attribute(AbilitySpec& a, const std::string& key)
{
    if (!ability_has_attribute(a, key)) {
        a.keywords.push_back(key);
    }
}

inline BoardTargetKind ability_board_target_kind(const AbilitySpec& a)
{
	if (a.board_target_kind.has_value()) {
		return *a.board_target_kind;
	}
	return effect_board_target_kind(a.effect_key);
}

/** Catalog/runtime cap for this ability; 0 = unlimited uses per turn. */
inline int ability_effective_uses_per_turn(const AbilitySpec& a)
{
    if (a.uses_per_turn >= 0) {
        return a.uses_per_turn;
    }
    return a.barrage ? 0 : 1;
}

/** Lifetime match cap; 0 = unlimited uses per game. */
inline int ability_effective_uses_per_game(const AbilitySpec& a)
{
    return a.uses_per_game >= 0 ? a.uses_per_game : 0;
}

inline bool ability_has_use_cap(const AbilitySpec& a)
{
    return ability_effective_uses_per_turn(a) > 0 || ability_effective_uses_per_game(a) > 0;
}

inline bool ability_requires_board_target(const AbilitySpec& a)
{
	if (a.requires_board_target.has_value()) {
		return *a.requires_board_target;
	}
	return effect_requires_board_target(a.effect_key);
}

/** Enemy deal_damage abilities use ranged LOS/range rules unless explicitly disabled. */
inline bool ability_uses_ranged_targeting(const AbilitySpec& a)
{
    if (a.uses_ranged_targeting.has_value()) {
        return *a.uses_ranged_targeting;
    }
    return a.effect_key == "deal_damage" && ability_board_target_kind(a) == BoardTargetKind::Enemy;
}

/** Anything on the board: unit, structure, or base. */
struct Entity {
    std::string entity_id;
    /** Monotonic board entry order; lower = older. Used for passive/automated action ordering. */
    uint64_t spawn_sequence{0};
    std::string entity_type{"unit"};
    /**
     * When false, this entity is excluded from the passive action order badge/rank.
     * Set to false for layout entities (obstacles, low_cover) that have no passive behaviors.
     * Defaults to true; set explicitly in layout or factory code.
     */
    bool participates_in_passive_order{true};
    /**
     * Effect triggered when a unit confirms a move that ends on this pickup's cell.
     * Empty string = no effect (the pickup is still collected and removed).
     * The registered effect handler is called with `source_entity_id` set to the collecting unit.
     */
    std::string pickup_effect_key{};
    /** Integer payload forwarded to the pickup's effect handler (e.g. {"amount": 2}). */
    std::map<std::string, int> pickup_payload{};
    /** `card_id` of the deploying unit card; empty for tokens not from a hand card. */
    std::string source_card_id;
    std::optional<int> owner{};
    /**
     * Team id, derived from `GameState::team_of_seat(*owner)` at deployment/spawn time and kept
     * in sync by `GameState::note_entity_placed`.  In standard 1v1 matches team == seat; in team
     * games several seats share a team id.  Used by `entities_allied()` so that taunt, targeting,
     * and reactive-passive logic correctly treats same-team units as friendly even when they belong
     * to different owners (team games, future mind-control).
     *
     * Not set on entities without owners (obstacles, low-cover); `entities_allied()` falls back to
     * owner equality for those cases.
     */
    std::optional<int> team{};
    bool has_moved_this_turn{false};
    int moves_remaining_this_turn{1};
    /** Standard move action (not bonus moves); refreshed each owner turn. */
    int standard_moves_remaining_this_turn{1};
    int bonus_moves{0};
    bool has_attacked_this_turn{false};
    int attacks_remaining_this_turn{1};
    int bonus_attacks{0};
    /** Bonus attack budget for BonusAttackDeclaration phase; set at turn start, separate from base attack. */
    int bonus_attacks_remaining_this_turn{0};
    /**
     * Entity IDs of every hostile entity this unit has damaged this turn (basic attacks and
     * abilities). Used by the Coordinated keyword to grant bonus damage when an ally has already
     * attacked the same target. Cleared at the start of this unit's owner's next turn.
     */
    std::set<std::string> attacked_targets_this_turn{};
    /**
     * Active coordinated fire shots remaining this turn (set by `apply_coordinated_fire` ability;
     * decremented per inline shot; reset to 0 at the start of the owner's next turn).
     * Units with this > 0 automatically fire at any enemy that a friendly unit attacks.
     */
    int coordinated_fire_shots_remaining{0};
    /** Optional damage override for coordinated fire shots (0 = use normal ranged stats). */
    int coordinated_fire_damage_min{0};
    int coordinated_fire_damage_max{0};
    /** Remaining reactions available this turn; refreshes to 3 at the start of the unit's owner's turn. Vigilance bypasses the cap. */
    int reactions_remaining_this_turn{3};
    /** Frenzy: refreshed move/attack after a kill at most once per turn. */
    bool frenzy_triggered_this_turn{false};
    /** Core Cracker: locked until Prime Core is paid; refreshed each owner turn start. */
    bool core_cracker_shutdown{false};
    /** Core Cracker: skip shutdown on the deployment turn only. */
    bool core_cracker_deploy_turn_exempt{false};
    int base_health{10};
    int current_health{10};
    bool line_of_sight_blocked{false};
    std::optional<std::pair<int, int>> position{};
    /** Footprint relative to `position` (anchor): world cell = anchor + (dx, dy). Must be non-empty; default is a single tile. */
    std::vector<std::pair<int, int>> shape{{0, 0}};
    /** Filled by the board from `position` + `shape` while placed; used for queries. */
    std::vector<std::pair<int, int>> occupied_positions{};
    AttackType attack_type{AttackType::Melee};
    int ranged_deadzone{0};
    /** Passive keywords such as `flying`, `reach`, or `armor`. Behavior is resolved through the keyword registry. */
    std::vector<std::string> keywords{};
    /** Optional tribe-like identifiers (e.g. beast, undead). Used for spell targeting and bonuses. */
    std::vector<std::string> unit_types{};
    /** Optional numeric values for valued keywords such as `armor`. */
    std::map<std::string, int> keyword_amounts{};
    /** Mutable entity effects such as Shield, Bonus Health, Poison, Fire, and Bleed. */
    std::vector<EntityEffectInstance> entity_effects{};
    std::vector<PassiveAbilitySpec> passive_abilities{};
    /** Transient live aura grants; recomputed by GameState and never serialized as base state. */
    std::vector<std::string> aura_granted_keywords{};
    std::map<std::string, int> aura_keyword_amounts{};
    int aura_bonus_attack{0};
    int aura_bonus_health{0};
    int aura_bonus_melee_damage{0};
    int aura_bonus_ranged_damage{0};
    int aura_bonus_movement{0};
    /** Bonus flat damage added to this unit's activated ability damage rolls from nearby aura sources. */
    int aura_bonus_ability_damage{0};
    /** Armor from self/allied passive stat grants; recomputed by refresh_passive_auras. */
    int aura_bonus_armor{0};
    /**
     * Survival chance (0–100) from `applies_to: "self"` passives.  Reset and recomputed by
     * `refresh_passive_auras`; never serialized.  See `PassiveStatGrant::survive_lethal_percent`.
     */
    int survive_lethal_percent{0};
    /** Permanent stat bonuses applied each time the survive_lethal roll succeeds. Never serialized. */
    int survive_lethal_bonus_attack{0};
    int survive_lethal_bonus_health{0};
    /**
     * Set to true when this entity has already been saved by a Sentinel Veil death-shield this turn.
     * Prevents the aura from granting survive_lethal_percent again until the next turn start.
     * Serialized; reset to false at the start of each owner's turn.
     */
    bool death_shield_used_this_turn{false};
    /** Synced temporary buffs/debuffs that can grant stats, valued attributes, and keywords. */
    std::vector<TemporaryEntityEffect> temporary_effects{};
    std::vector<AbilitySpec> activated_abilities{};
    /**
     * Remaining casts this turn for capped abilities (`ability_effective_uses_per_turn` > 0).
     * Refreshed at owner turn start (and when deployment fatigue ends / surge refresh).
     */
    std::map<std::string, int> ability_uses_remaining_this_turn{};
    /** Remaining lifetime casts for `uses_per_game` abilities; initialized once, never refreshed. */
    std::map<std::string, int> ability_uses_remaining_game{};
    /**
     * For abilities with the `barrage` keyword: tracks how many times each barrage ability has been
     * cast this turn. Reset at owner turn start. Used to compute the incremental barrage cost surcharge.
     */
    std::map<std::string, int> barrage_cast_counts_this_turn{};
    virtual ~Entity() = default;
};

inline int entity_ability_uses_remaining_this_turn(const Entity& entity, const AbilitySpec& ability)
{
    const int cap = ability_effective_uses_per_turn(ability);
    if (cap <= 0) {
        return 0;
    }
    const auto it = entity.ability_uses_remaining_this_turn.find(ability.key);
    if (it == entity.ability_uses_remaining_this_turn.end()) {
        return cap;
    }
    return std::max(0, it->second);
}

inline int entity_ability_uses_remaining_game(const Entity& entity, const AbilitySpec& ability)
{
    const int cap = ability_effective_uses_per_game(ability);
    if (cap <= 0) {
        return 0;
    }
    const auto it = entity.ability_uses_remaining_game.find(ability.key);
    if (it == entity.ability_uses_remaining_game.end()) {
        return cap;
    }
    return std::max(0, it->second);
}

/** Remaining per-turn uses (0 when the ability has no per-turn cap). */
inline int entity_ability_uses_remaining(const Entity& entity, const AbilitySpec& ability)
{
    return entity_ability_uses_remaining_this_turn(entity, ability);
}

inline bool entity_can_use_ability(const Entity& entity, const AbilitySpec& ability)
{
    if (ability_effective_uses_per_game(ability) > 0
            && entity_ability_uses_remaining_game(entity, ability) <= 0) {
        return false;
    }
    if (ability_effective_uses_per_turn(ability) > 0
            && entity_ability_uses_remaining_this_turn(entity, ability) <= 0) {
        return false;
    }
    return true;
}

inline void refresh_entity_ability_uses(Entity& entity)
{
    entity.ability_uses_remaining_this_turn.clear();
    for (const AbilitySpec& ability : entity.activated_abilities) {
        const int game_cap = ability_effective_uses_per_game(ability);
        if (game_cap > 0 && !entity.ability_uses_remaining_game.contains(ability.key)) {
            entity.ability_uses_remaining_game[ability.key] = game_cap;
        }
        const int turn_cap = ability_effective_uses_per_turn(ability);
        if (turn_cap > 0) {
            entity.ability_uses_remaining_this_turn[ability.key] = turn_cap;
        }
    }
}

inline void entity_consume_ability_use(Entity& entity, const AbilitySpec& ability)
{
    const int game_cap = ability_effective_uses_per_game(ability);
    if (game_cap > 0) {
        int& game_remaining = entity.ability_uses_remaining_game[ability.key];
        if (game_remaining <= 0) {
            game_remaining = game_cap;
        }
        game_remaining = std::max(0, game_remaining - 1);
    }
    const int turn_cap = ability_effective_uses_per_turn(ability);
    if (turn_cap <= 0) {
        return;
    }
    int& remaining = entity.ability_uses_remaining_this_turn[ability.key];
    if (remaining <= 0) {
        remaining = turn_cap;
    }
    remaining = std::max(0, remaining - 1);
}

inline bool entity_owned_by(const Entity& e, int player_id) { return e.owner && *e.owner == player_id; }

/**
 * Returns true if `a` and `b` are on the same team (friendly).
 *
 * Uses `Entity::team` when available - set at deploy/spawn time by `GameState::note_entity_placed`
 * and correct for team games and mind-control.  Falls back to owner equality for entities where
 * team has not been stamped (programmatic test entities, obstacles without owners).
 *
 * Use this everywhere a friend/foe distinction is needed instead of raw owner comparison.
 */
inline bool entities_allied(const Entity& a, const Entity& b)
{
    if (a.team.has_value() && b.team.has_value()) {
        return *a.team == *b.team;
    }
    // Fallback: compare owners for entities without team info (e.g. manually created test entities).
    return a.owner.has_value() && b.owner.has_value() && *a.owner == *b.owner;
}

/**
 * Convert entity_type string to EntityKind enum for use in switch statements.
 * Prefer this over repeated string comparisons in new code.
 * Example: switch (entity_kind(e)) { case EntityKind::Unit: ... }
 */
inline EntityKind entity_kind(const Entity& e) { return entity_kind_from_string(e.entity_type); }

inline bool entity_is_building(const Entity& e) { return e.entity_type == "building"; }

inline bool entity_is_low_cover(const Entity& e) { return e.entity_type == "low_cover"; }

inline bool entity_is_breakable_obstacle(const Entity& e)
{
    return e.entity_type == "breakable_obstacle" || entity_is_low_cover(e);
}

inline bool entity_is_base(const Entity& e) { return e.entity_type == "base"; }

/**
 * Pickups are collectible board tokens. Properties:
 * - Do not block movement (units pass through freely in pathfinding).
 * - Collected (triggered + removed) when a unit ENDS its confirmed move on the pickup's cell.
 * - Destroyed by any amount of damage (1 HP, no armor).
 * - Can be directly attacked.
 * - Do not block line of sight (line_of_sight_blocked = false).
 * - Immune to all status effects (only damage matters).
 */
inline bool entity_is_pickup(const Entity& e) { return e.entity_type == "pickup"; }

/** Bases and pickups are immune to most stack effects. Bases block everything; pickups allow
 *  fire stacks and damage-dealing effects (see entity_status_allowed and stack_fizzle_if_immune). */
inline bool entity_immune_to_all_effects(const Entity& e) { return entity_is_base(e) || entity_is_pickup(e); }

inline bool entity_can_move(const Entity& e)
{
    return !entity_is_building(e) && !entity_is_breakable_obstacle(e) && !entity_is_base(e)
        && !entity_is_pickup(e) && e.entity_type != "obstacle";
}

inline bool entity_is_structure(const Entity& e) { return entity_is_building(e) || entity_is_breakable_obstacle(e) || e.entity_type == "obstacle"; }

/** Adjacent repair effects: structures and player bases are separate categories. */
inline bool entity_is_quick_repairs_target(const Entity& e) { return entity_is_structure(e) || entity_is_base(e); }

inline bool entity_is_silenced(const Entity& e)
{
    for (const EntityEffectInstance& effect : e.entity_effects) {
        if (effect.key == "silenced" && effect.amount > 0) {
            return true;
        }
    }
    return false;
}

inline bool entity_passive_is_suppressed(const Entity& e, const std::string& passive_key)
{
    if (entity_is_silenced(e)) {
        return true;
    }
    if (passive_key.empty()) {
        return false;
    }
    for (const TemporaryEntityEffect& eff : e.temporary_effects) {
        for (const auto& sp : eff.suppress_passive_keys) {
            if (sp == passive_key) {
                return true;
            }
        }
    }
    return false;
}

/** Jammed: entity cannot use activated abilities. Loses 1 stack at the end of its owner's turn. */
inline bool entity_is_jammed(const Entity& e)
{
    for (const EntityEffectInstance& effect : e.entity_effects) {
        if (effect.key == "jammed" && effect.amount > 0) {
            return true;
        }
    }
    return false;
}

/** Rooted: entity cannot move. Loses 1 stack at the end of its owner's turn, after DOT. */
inline bool entity_is_rooted(const Entity& e)
{
    for (const EntityEffectInstance& effect : e.entity_effects) {
        if (effect.key == "rooted" && effect.amount > 0) {
            return true;
        }
    }
    return false;
}

/** Stunned: entity cannot move, attack, use abilities, or cast focus spells.
 *  Loses 1 stack at the end of its owner's turn, after DOT. */
inline bool entity_is_stunned(const Entity& e)
{
    for (const EntityEffectInstance& effect : e.entity_effects) {
        if (effect.key == "stunned" && effect.amount > 0) {
            return true;
        }
    }
    return false;
}

inline bool entity_has_attribute(const Entity& e, const std::string& key);

/** Immovable: entity cannot be repositioned by any external effect.
 *  Voluntary movement actions are unaffected.
 *  Three sources (independent of each other):
 *    1. Buildings and bases - always fixed, never suppressible by silence.
 *    2. Rooted status - while rooted stacks > 0.
 *    3. "immovable" keyword - suppressed while silenced (via entity_has_attribute). */
inline bool entity_is_immovable(const Entity& e)
{
    if (entity_is_building(e) || entity_is_base(e)) return true;
    if (entity_is_rooted(e)) return true;
    return entity_has_attribute(e, "immovable");
}

/** Overload explosion triggers at end of turn when stacks >= this value. Each stack above this adds 1 to the damage. */
inline constexpr int kOverloadExplosionThreshold = 3;
/** Overload explosion base damage (at exactly threshold stacks). Each stack above threshold adds 1 more. */
inline constexpr int kOverloadExplosionDamage    = 5;

inline bool entity_has_native_keyword(const Entity& e, const std::string& key)
{
    if (entity_is_silenced(e)) {
        return false;
    }
    return std::find(e.keywords.begin(), e.keywords.end(), key) != e.keywords.end();
}

inline bool entity_has_attribute(const Entity& e, const std::string& key)
{
    if (key == "indestructible") {
        return entity_has_native_keyword(e, key);
    }
    // Single pass: check silenced inline to avoid a separate entity_effects scan
    for (const EntityEffectInstance& eff : e.entity_effects) {
        if (eff.key == "silenced" && eff.amount > 0) return false;
    }
    // A temp effect can explicitly suppress a keyword (e.g. weapon-swap modes).
    for (const TemporaryEntityEffect& eff : e.temporary_effects) {
        for (const auto& sa : eff.suppress_attributes) {
            if (sa == key) return false;
        }
    }
    for (const auto& kw : e.keywords) {
        if (kw == key) return true;
    }
    for (const auto& kw : e.aura_granted_keywords) {
        if (kw == key) return true;
    }
    for (const TemporaryEntityEffect& eff : e.temporary_effects) {
        for (const PassiveAttributeGrant& g : eff.granted_attributes) {
            if (g.key == key) return true;
        }
    }
    return false;
}

inline void add_entity_attribute(Entity& e, const std::string& key)
{
    if (key == "indestructible") {
        return;
    }
    if (std::find(e.keywords.begin(), e.keywords.end(), key) == e.keywords.end()) {
        e.keywords.push_back(key);
    }
}

inline void remove_entity_attribute(Entity& e, const std::string& key)
{
    e.keywords.erase(std::remove(e.keywords.begin(), e.keywords.end(), key), e.keywords.end());
    e.keyword_amounts.erase(key);
}

inline void set_entity_attribute_amount(Entity& e, const std::string& key, int amount)
{
    if (key == "indestructible") {
        return;
    }
    add_entity_attribute(e, key);
    // Stored armor/magic_resist values cap at 5; per-hit armor reduction is also capped at 5 in attributes.cpp.
    const int capped = (key == "armor" || key == "magic_resist") ? std::min(5, std::max(0, amount)) : std::max(0, amount);
    e.keyword_amounts[key] = capped;
}

inline int entity_attribute_amount(const Entity& e, const std::string& key, int fallback = 0)
{
    if (entity_is_silenced(e)) {
        return fallback;
    }
    const auto it = e.keyword_amounts.find(key);
    const auto aura_it = e.aura_keyword_amounts.find(key);
    int temp = 0;
    bool has_temp = false;
    for (const TemporaryEntityEffect& effect : e.temporary_effects) {
        for (const PassiveAttributeGrant& grant : effect.granted_attributes) {
            if (grant.key == key && grant.amount.has_value()) {
                temp = std::max(temp, std::max(0, *grant.amount));
                has_temp = true;
            }
        }
    }
    if (it == e.keyword_amounts.end() && aura_it == e.aura_keyword_amounts.end() && !has_temp) {
        return fallback;
    }
    const int base = it == e.keyword_amounts.end() ? 0 : it->second;
    const int aura = aura_it == e.aura_keyword_amounts.end() ? 0 : aura_it->second;
    return std::max({base, aura, temp});
}

inline int entity_effect_amount(const Entity& e, const std::string& key)
{
    int amount = 0;
    for (const EntityEffectInstance& effect : e.entity_effects) {
        if (effect.key == key) {
            amount += std::max(0, effect.amount);
        }
    }
    return amount;
}

/** Overload: stacks do not decay; at 3 stacks the entity explodes (5 damage to self + adjacent). */
inline bool entity_is_overloaded(const Entity& e) { return entity_effect_amount(e, "overload") > 0; }

inline bool entity_is_stealthed(const Entity& e) { return entity_effect_amount(e, "stealth") > 0; }

inline bool entity_has_base_or_temporary_attribute(const Entity& e, const std::string& key)
{
    if (entity_is_silenced(e)) {
        return false;
    }
    return std::find(e.keywords.begin(), e.keywords.end(), key) != e.keywords.end() ||
        std::find(e.aura_granted_keywords.begin(), e.aura_granted_keywords.end(), key) != e.aura_granted_keywords.end() ||
        std::any_of(e.temporary_effects.begin(), e.temporary_effects.end(), [&](const TemporaryEntityEffect& effect) {
            return std::any_of(effect.granted_attributes.begin(), effect.granted_attributes.end(),
                [&](const PassiveAttributeGrant& grant) { return grant.key == key; });
        });
}

inline bool entity_has_dot_immunity(const Entity& e) { return entity_has_base_or_temporary_attribute(e, "true_immunity"); }
inline bool entity_has_fire_resistance(const Entity& e) { return entity_has_dot_immunity(e) || entity_has_base_or_temporary_attribute(e, "fire_resistance"); }
inline bool entity_has_poison_resistance(const Entity& e) { return entity_has_dot_immunity(e) || entity_has_base_or_temporary_attribute(e, "poison_resistance"); }
inline bool entity_has_bleed_resistance(const Entity& e) { return entity_has_dot_immunity(e) || entity_has_base_or_temporary_attribute(e, "bleed_resistance"); }
inline int entity_poison_attack_penalty(const Entity& e) { return entity_has_poison_resistance(e) ? 0 : entity_effect_amount(e, "poison"); }

inline bool entity_status_allowed(const Entity& e, const std::string& key)
{
    // Pickups accept only fire stacks. Fire ticks at each turn end via process_end_of_turn_dot
    // (pickup-fire pass) and destroys the pickup. All other status effects are blocked.
    if (entity_is_pickup(e)) {
        return key == "fire";
    }
    if (entity_immune_to_all_effects(e)) {  // bases
        return false;
    }
    // True Immunity (key: "true_immunity") blocks poison and bleed stacks (DoT stacks whose
    // damage it also prevents), but INTENTIONALLY allows fire stacks to accumulate. Fire damage
    // is separately blocked by entity_has_fire_resistance() which returns true for true_immunity entities.
    // This design means:
    //   - A True Immunity unit can carry fire stacks and spread them (useful as a fire relay)
    //   - It takes no fire damage each turn (handled in process_end_of_turn_dot)
    //   - The stacks decay normally; they are not a wasted "stuck at max" state
    // Do NOT remove the `key != "fire"` carve-out - it is load-bearing game design.
    if (entity_has_dot_immunity(e) && key != "fire") {
        return false;
    }
    if (key == "poison" && entity_has_base_or_temporary_attribute(e, "poison_resistance")) {
        return false;
    }
    if (key == "bleed" && entity_has_base_or_temporary_attribute(e, "bleed_resistance")) {
        return false;
    }
    if (entity_is_structure(e) && (key == "poison" || key == "bleed")) {
        return false;
    }
    if (key == "stealth" && (entity_is_structure(e) || entity_is_base(e))) {
        return false;
    }
    return true;
}

inline bool add_entity_effect(Entity& e, const std::string& key, int amount, std::string source_id = {})
{
    // entity_status_allowed handles all immunity checks (bases, pickups, True Immunity/true_immunity, resistances).
    if (amount <= 0 || !entity_status_allowed(e, key)) {
        return false;
    }
    for (EntityEffectInstance& effect : e.entity_effects) {
        if (effect.key == key && effect.source_id == source_id && effect.expire_on == "never") {
            effect.amount += amount;
            return true;
        }
    }
    e.entity_effects.push_back({key, amount, std::move(source_id), 0, "never"});
    return true;
}

/** Remove Barrier stacks that expire at owner turn end (`apply_barrier_self`). */
inline void decay_barrier_owner_turn_end_stacks(Entity& e)
{
    for (auto it = e.entity_effects.begin(); it != e.entity_effects.end();) {
        if (it->key == "barrier" && it->source_id == effect_keys::kBarrierOwnerTurnEndSource) {
            it = e.entity_effects.erase(it);
        } else {
            ++it;
        }
    }
}

/** Decay timed Silenced stacks applied via `apply_silenced_owner_turn_end` (1 stack per owner turn end). */
inline void decay_silenced_owner_turn_end_stacks(Entity& e)
{
    for (auto it = e.entity_effects.begin(); it != e.entity_effects.end();) {
        if (it->key == "silenced" && it->source_id == effect_keys::kSilencedOwnerTurnEndSource) {
            it->amount -= 1;
            if (it->amount <= 0) {
                it = e.entity_effects.erase(it);
            } else {
                ++it;
            }
        } else {
            ++it;
        }
    }
}

inline void reduce_entity_effect(Entity& e, const std::string& key, int amount)
{
    if (amount <= 0) {
        return;
    }
    int remaining = amount;
    for (EntityEffectInstance& effect : e.entity_effects) {
        if (effect.key != key || remaining <= 0) {
            continue;
        }
        const int consumed = std::min(std::max(0, effect.amount), remaining);
        effect.amount -= consumed;
        remaining -= consumed;
    }
    e.entity_effects.erase(
        std::remove_if(e.entity_effects.begin(), e.entity_effects.end(), [](const EntityEffectInstance& effect) { return effect.amount <= 0; }),
        e.entity_effects.end());
}

inline int consume_first_entity_effect(Entity& e, const std::string& key)
{
    for (EntityEffectInstance& effect : e.entity_effects) {
        if (effect.key != key || effect.amount <= 0) {
            continue;
        }
        const int consumed = effect.amount;
        effect.amount = 0;
        e.entity_effects.erase(
            std::remove_if(e.entity_effects.begin(), e.entity_effects.end(), [](const EntityEffectInstance& entry) { return entry.amount <= 0; }),
            e.entity_effects.end());
        return consumed;
    }
    return 0;
}

inline int consume_all_entity_effect(Entity& e, const std::string& key)
{
    int total = 0;
    for (EntityEffectInstance& effect : e.entity_effects) {
        if (effect.key == key && effect.amount > 0) {
            total += effect.amount;
            effect.amount = 0;
        }
    }
    if (total > 0) {
        e.entity_effects.erase(
            std::remove_if(e.entity_effects.begin(), e.entity_effects.end(),
                [](const EntityEffectInstance& entry) { return entry.amount <= 0; }),
            e.entity_effects.end());
    }
    return total;
}

inline void remove_entity_effect(Entity& e, const std::string& key)
{
    e.entity_effects.erase(
        std::remove_if(e.entity_effects.begin(), e.entity_effects.end(), [&](const EntityEffectInstance& effect) { return effect.key == key; }),
        e.entity_effects.end());
}

inline PassiveStatGrant temporary_stat_grants_for_entity(const Entity& e)
{
    PassiveStatGrant out;
    if (entity_is_silenced(e)) {
        return out;
    }
    for (const TemporaryEntityEffect& effect : e.temporary_effects) {
        out.bonus_attack += effect.stat_grants.bonus_attack;
        out.bonus_health += effect.stat_grants.bonus_health;
        out.bonus_melee_damage += effect.stat_grants.bonus_melee_damage;
        out.bonus_ranged_damage += effect.stat_grants.bonus_ranged_damage;
        out.bonus_movement += effect.stat_grants.bonus_movement;
        out.bonus_moves += effect.stat_grants.bonus_moves;
        out.bonus_attacks += effect.stat_grants.bonus_attacks;
        out.bonus_ability_damage += effect.stat_grants.bonus_ability_damage;
        out.bonus_aura_range += effect.stat_grants.bonus_aura_range;
        out.bonus_aura_attack += effect.stat_grants.bonus_aura_attack;
        out.bonus_aura_health += effect.stat_grants.bonus_aura_health;
        out.bonus_aura_ability_damage += effect.stat_grants.bonus_aura_ability_damage;
        out.bonus_ranged_range += effect.stat_grants.bonus_ranged_range;
        out.bonus_ranged_deadzone += effect.stat_grants.bonus_ranged_deadzone;
        // Override fields: max-of-non-zero wins (only one mode active at a time in practice).
        if (effect.stat_grants.override_ranged_damage_min > 0)
            out.override_ranged_damage_min = std::max(out.override_ranged_damage_min,
                                                       effect.stat_grants.override_ranged_damage_min);
        if (effect.stat_grants.override_ranged_damage_max > 0)
            out.override_ranged_damage_max = std::max(out.override_ranged_damage_max,
                                                       effect.stat_grants.override_ranged_damage_max);
    }
    return out;
}

inline int entity_effective_base_health(const Entity& e)
{
    if (entity_is_silenced(e)) {
        return std::max(0, e.base_health);
    }
    const PassiveStatGrant temp = temporary_stat_grants_for_entity(e);
    return std::max(0, e.base_health + e.aura_bonus_health + temp.bonus_health);
}

/**
 * Sorted unique footprint offsets; if `e.shape` is empty, returns `{(0,0)}`.
 * `Entity::shape` is kept normalized (sorted, unique, non-empty) by `normalize_entity_shape()`
 * which is called at placement and card creation. This function trusts that invariant and avoids
 * re-sorting on every call (a hot path inside inner loops).
 */
inline std::vector<std::pair<int, int>> entity_shape_offsets(const Entity& e)
{
    if (!e.shape.empty()) return e.shape;
    return {{0, 0}};
}

/** Anchors `(ax, ay)` such that footprint `(ax+dx, ay+dy)` hits `(world_x, world_y)` for some shape offset. */
inline std::vector<std::pair<int, int>> entity_anchor_positions_covering_cell(const Entity& e, int world_x, int world_y)
{
    std::vector<std::pair<int, int>> out;
    for (const auto& [dx, dy] : entity_shape_offsets(e)) {
        out.push_back({world_x - dx, world_y - dy});
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

inline void normalize_entity_shape(Entity& e) { e.shape = entity_shape_offsets(e); }

/** Rotate footprint offsets 90° clockwise in grid space (x+ east, y+ south): (dx,dy) -> (dy, -dx). */
inline void rotate_shape_offsets_one_step_cw(std::vector<std::pair<int, int>>& offs)
{
	for (auto& p : offs) {
		const int ndx = p.second;
		const int ndy = -p.first;
		p = {ndx, ndy};
	}
}

/** Apply `quarter_turns_cw` (may be negative) modulo 4 to `offs` in place, then normalize like `Entity::shape`. */
inline void rotate_shape_offsets_n_quarters_cw(std::vector<std::pair<int, int>>& offs, int quarter_turns_cw)
{
	int n = quarter_turns_cw % 4;
	if (n < 0) {
		n += 4;
	}
	for (int i = 0; i < n; ++i) {
		rotate_shape_offsets_one_step_cw(offs);
	}
	Entity tmp;
	tmp.shape = std::move(offs);
	normalize_entity_shape(tmp);
	offs = std::move(tmp.shape);
}

/** Chebyshev distance from the closest tile of `e`'s footprint (uses `position` + `shape`) to (tx, ty). */
inline int min_chebyshev_entity_to_cell(const Entity& e, int tx, int ty)
{
	if (!e.position) {
		int best = INT_MAX;
		for (const auto& [x, y] : e.occupied_positions) {
			best = std::min(best, std::max(std::abs(x - tx), std::abs(y - ty)));
		}
		return best == INT_MAX ? 0 : best;
	}
    const auto [bx, by] = *e.position;
    const auto offs = entity_shape_offsets(e);
    int best = INT_MAX;
    for (const auto& [dx, dy] : offs) {
        const int x = bx + dx;
        const int y = by + dy;
        best = std::min(best, std::max(std::abs(x - tx), std::abs(y - ty)));
    }
    return best;
}

/** Inclusive min/max attack damage before rolling (default 3-wide band from nominal melee/ranged). */
struct DamageRange {
    int min{0};
    int max{0};
};

constexpr int kDefaultDamageRangeRadius = 1;
constexpr int kDefaultCritChancePercent = 10;

inline DamageRange damage_range_from_nominal(int nominal, int radius = kDefaultDamageRangeRadius)
{
    nominal = std::max(0, nominal);
    return {std::max(0, nominal - radius), nominal + radius};
}

class Unit : public Entity {
public:
    std::string unit_type{"Infantry"};
    bool is_ranged{false};
    int base_ranged_length{0};
    int bonus_ranged_length{0};
    int movement{5};
    int melee_range{1};
    int ranged_range{0};
    /** Nominal/center damage for display and legacy data. */
    int melee_damage{3};
    int melee_damage_min{0};
    int melee_damage_max{0};
    int ranged_damage{0};
    int ranged_damage_min{0};
    int ranged_damage_max{0};
    /** 0–100; default 10% crit chance on attack rolls. */
    int crit_chance_percent{kDefaultCritChancePercent};
};

inline void sync_unit_damage_ranges_from_nominal(Unit& u)
{
    if (u.melee_damage_max <= 0 && u.melee_damage_min <= 0) {
        const DamageRange melee = damage_range_from_nominal(u.melee_damage);
        u.melee_damage_min = melee.min;
        u.melee_damage_max = melee.max;
    } else if (u.melee_damage_max < u.melee_damage_min) {
        std::swap(u.melee_damage_min, u.melee_damage_max);
    }
    if (u.ranged_damage > 0 && u.ranged_damage_max <= 0 && u.ranged_damage_min <= 0) {
        const DamageRange ranged = damage_range_from_nominal(u.ranged_damage);
        u.ranged_damage_min = ranged.min;
        u.ranged_damage_max = ranged.max;
    } else if (u.ranged_damage_max < u.ranged_damage_min) {
        std::swap(u.ranged_damage_min, u.ranged_damage_max);
    }
}

/** Lock melee damage to an exact value (used in tests). */
inline void unit_set_fixed_melee_damage(Unit& u, int damage)
{
    damage = std::max(0, damage);
    u.melee_damage = damage;
    u.melee_damage_min = damage;
    u.melee_damage_max = damage;
}

inline void unit_set_fixed_ranged_damage(Unit& u, int damage)
{
    damage = std::max(0, damage);
    u.ranged_damage = damage;
    u.ranged_damage_min = damage;
    u.ranged_damage_max = damage;
}

inline int unit_effective_movement(const Unit& u)
{
    const PassiveStatGrant temp = temporary_stat_grants_for_entity(u);
    return std::max(0, u.movement + u.aura_bonus_movement + temp.bonus_movement);
}

inline DamageRange unit_base_melee_damage_range(const Unit& u)
{
    if (u.melee_damage_max <= 0 && u.melee_damage_min <= 0) {
        return damage_range_from_nominal(u.melee_damage);
    }
    if (u.melee_damage_max < u.melee_damage_min) {
        return {u.melee_damage_max, u.melee_damage_min};
    }
    return {u.melee_damage_min, u.melee_damage_max};
}

inline DamageRange unit_base_ranged_damage_range(const Unit& u)
{
    if (u.ranged_damage <= 0 && u.ranged_damage_max <= 0 && u.ranged_damage_min <= 0) {
        return {0, 0};
    }
    if (u.ranged_damage_max <= 0 && u.ranged_damage_min <= 0) {
        return damage_range_from_nominal(u.ranged_damage);
    }
    if (u.ranged_damage_max < u.ranged_damage_min) {
        return {u.ranged_damage_max, u.ranged_damage_min};
    }
    return {u.ranged_damage_min, u.ranged_damage_max};
}

inline DamageRange unit_effective_melee_damage_range(const Unit& u)
{
    const DamageRange base = unit_base_melee_damage_range(u);
    const PassiveStatGrant temp = temporary_stat_grants_for_entity(u);
    const int bonus = u.aura_bonus_attack + u.aura_bonus_melee_damage + temp.bonus_attack + temp.bonus_melee_damage;
    const int penalty = entity_poison_attack_penalty(u);
    const int min_d = std::max(0, base.min + bonus - penalty);
    const int max_d = std::max(min_d, base.max + bonus - penalty);
    return {min_d, max_d};
}

inline DamageRange unit_effective_ranged_damage_range(const Unit& u)
{
    const DamageRange base = unit_base_ranged_damage_range(u);
    const PassiveStatGrant temp = temporary_stat_grants_for_entity(u);
    // Weapon-mode override replaces the base range; additive bonuses still apply on top.
    const int eff_min = temp.override_ranged_damage_min > 0 ? temp.override_ranged_damage_min : base.min;
    const int eff_max = temp.override_ranged_damage_max > 0 ? temp.override_ranged_damage_max : base.max;
    const int bonus = u.aura_bonus_attack + u.aura_bonus_ranged_damage + temp.bonus_attack + temp.bonus_ranged_damage;
    const int penalty = entity_poison_attack_penalty(u);
    const int min_d = std::max(0, eff_min + bonus - penalty);
    const int max_d = std::max(min_d, eff_max + bonus - penalty);
    return {min_d, max_d};
}

inline int unit_effective_melee_damage(const Unit& u)
{
    const DamageRange range = unit_effective_melee_damage_range(u);
    return (range.min + range.max) / 2;
}

inline int unit_effective_ranged_damage(const Unit& u)
{
    const DamageRange range = unit_effective_ranged_damage_range(u);
    return (range.min + range.max) / 2;
}

inline bool entity_has_temporary_effect_id(const Entity& e, const std::string& effect_id)
{
    return std::any_of(e.temporary_effects.begin(), e.temporary_effects.end(),
        [&](const TemporaryEntityEffect& effect) { return effect.effect_id == effect_id; });
}

inline bool entity_has_defend_stance(const Entity& e)
{
    if (entity_is_silenced(e)) {
        return false;
    }
    return entity_has_temporary_effect_id(e, effect_keys::kDefendStanceEffectId);
}

inline bool entity_has_dash_movement(const Entity& e)
{
    if (entity_is_silenced(e)) {
        return false;
    }
    return entity_has_temporary_effect_id(e, effect_keys::kDashMovementEffectId);
}

inline bool entity_has_recover_stance(const Entity& e)
{
    if (entity_is_silenced(e)) {
        return false;
    }
    return entity_has_temporary_effect_id(e, effect_keys::kRecoverStanceEffectId);
}

inline bool entity_has_deployment_fatigue(const Entity& e)
{
    return entity_has_temporary_effect_id(e, effect_keys::kDeploymentFatigueEffectId);
}

inline bool entity_on_damage_applies_jammed(const Entity& e)
{
    return !entity_is_silenced(e)
        && entity_has_temporary_effect_id(e, effect_keys::kOnDamageApplyJammedEffectId);
}

inline bool entity_passive_mechanic_active(const Entity& e, const std::string& mechanic)
{
    if (entity_is_silenced(e) || mechanic.empty()) {
        return false;
    }
    for (const PassiveAbilitySpec& passive : e.passive_abilities) {
        if (passive.passive_mechanic == mechanic) {
            return true;
        }
    }
    return false;
}

inline int entity_passive_mechanic_amount(const Entity& e, const std::string& mechanic, const int fallback = 0)
{
    if (entity_is_silenced(e) || mechanic.empty()) {
        return fallback;
    }
    int best = fallback;
    for (const PassiveAbilitySpec& passive : e.passive_abilities) {
        if (passive.passive_mechanic != mechanic) {
            continue;
        }
        const auto it = passive.mechanic_payload.find("amount");
        best = std::max(best, it != passive.mechanic_payload.end() ? std::max(1, it->second) : 1);
    }
    return best;
}

inline bool entity_on_damage_applies_overload(const Entity& e)
{
    // Temporary on-hit primer (Sylvia's Overcharge) OR overload_on_hit passive mechanic.
    return (!entity_is_silenced(e)
               && entity_has_temporary_effect_id(e, effect_keys::kOnDamageApplyOverloadEffectId))
        || entity_passive_mechanic_active(e, "overload_on_hit");
}

inline bool entity_on_damage_applies_bleed(const Entity& e)
{
    return entity_has_temporary_effect_id(e, effect_keys::kOnDamageApplyBleedNextAbilityEffectId)
        || entity_passive_mechanic_active(e, "bleed_on_hit");
}

inline bool entity_on_damage_applies_movement_reduction_next_ability(const Entity& e)
{
    return entity_has_temporary_effect_id(e, effect_keys::kOnDamageApplyMovementReductionNextAbilityEffectId);
}

inline bool entity_on_damage_applies_rooted_next_ability(const Entity& e)
{
    return entity_has_temporary_effect_id(e, effect_keys::kOnDamageApplyRootedNextAbilityEffectId);
}

inline bool entity_on_damage_applies_overload_next_ability(const Entity& e)
{
    return entity_has_temporary_effect_id(e, effect_keys::kOnDamageApplyOverloadNextAbilityEffectId);
}

inline bool entity_on_damage_applies_jammed_next_ability(const Entity& e)
{
    return entity_has_temporary_effect_id(e, effect_keys::kOnDamageApplyJammedNextAbilityEffectId);
}

/** Returns the number of Vulnerable stacks on this entity (0 if none).
 *  Each stack increases incoming attack/ability damage by 1 (DoTs are unaffected). */
inline int entity_vulnerable_stacks(const Entity& e)
{
    return entity_effect_amount(e, "vulnerable");
}

/** Shock on Hit: damage this entity does applies 1 overload to each unit it damages.
 *  Fires through the on-hit primer path so cleave hits each trigger it. */
inline bool entity_has_shock_on_hit(const Entity& e)
{
    return entity_passive_mechanic_active(e, "shock_on_hit");
}

/** Gas on Hit: when this entity deals damage, place a gas cloud on the victim's tile(s). */
inline bool entity_has_gas_on_hit(const Entity& e)
{
    return entity_passive_mechanic_active(e, "gas_on_hit");
}

/** Fire on Hit: damage this entity does applies fire stacks to each unit it damages. */
inline bool entity_has_fire_on_hit(const Entity& e)
{
    return entity_passive_mechanic_active(e, "fire_on_hit");
}

/** Volley: ranged attacks restricted to cardinal directions; each attack fires a reverse cone. */
inline bool entity_has_volley(const Entity& e)
{
    return entity_has_attribute(e, "volley");
}

/** Medical Override: when this entity's deal_damage ability hits an allied entity, that ally is
 *  healed for the damage amount instead of being damaged. Enemies are still damaged normally.
 *  Expires owner_turn_end. */
inline bool entity_has_medical_override(const Entity& e)
{
    return entity_has_temporary_effect_id(e, effect_keys::kMedicalOverrideEffectId);
}

struct DeploymentFatigueRestrictions {
    bool blocks_move{false};
    bool blocks_attack{false};
    bool blocks_ranged_attack{false};
    bool blocks_abilities{false};
    bool blocks_attack_actions{false};
};

/** Charge and buildings skip deployment fatigue entirely.
 *  Buildings cannot move regardless; they should be free to attack and use abilities
 *  on the turn they are deployed. */
inline bool entity_skips_deployment_fatigue(const Entity& e)
{
    return entity_has_attribute(e, "charge") || entity_is_building(e);
}

inline DeploymentFatigueRestrictions deployment_fatigue_restrictions(const Entity& e)
{
    DeploymentFatigueRestrictions r{};
    if (!entity_has_deployment_fatigue(e) || entity_skips_deployment_fatigue(e)) {
        return r;
    }
    const bool has_haste = entity_has_attribute(e, "haste");
    const bool has_surge = entity_has_attribute(e, "surge");
    // Union each keyword's allowances: Haste grants move; Surge grants attack/abilities/actions.
    r.blocks_move = !has_haste;
    if (!has_surge) {
        r.blocks_abilities = true;
        r.blocks_attack_actions = true;
        r.blocks_ranged_attack = true;
    }
    if (has_haste && !has_surge) {
        r.blocks_attack = true;
    }
    return r;
}

inline bool deployment_fatigue_blocks_move(const Entity& e)
{
    return deployment_fatigue_restrictions(e).blocks_move;
}

inline bool deployment_fatigue_blocks_attack(const Entity& e)
{
    return deployment_fatigue_restrictions(e).blocks_attack;
}

/** Voluntary declared attacks only (`validate_attack`). Counterattacks and other inline combat reactions use `counterattack_profile_for` and are not gated by this. */
inline bool deployment_fatigue_blocks_ranged_attack(const Entity& e)
{
    return deployment_fatigue_restrictions(e).blocks_ranged_attack;
}

inline bool deployment_fatigue_blocks_abilities(const Entity& e)
{
    return deployment_fatigue_restrictions(e).blocks_abilities;
}

inline bool deployment_fatigue_blocks_attack_actions(const Entity& e)
{
    return deployment_fatigue_restrictions(e).blocks_attack_actions;
}

inline bool entity_is_core_cracker(const Entity& e)
{
    if (const auto* u = dynamic_cast<const Unit*>(&e)) {
        return u->unit_type == "core_cracker";
    }
    return false;
}

inline bool core_cracker_shutdown_blocks_actions(const Entity& e)
{
    return entity_is_core_cracker(e) && e.core_cracker_shutdown;
}

inline bool core_cracker_prime_ability_key(const std::string& ability_key)
{
    return ability_key == "core_cracker_prime_ability";
}

/** Grant extra move actions; while move-blocked by deployment fatigue, also restores move actions. */
inline void grant_unit_bonus_move_actions(Unit& u, int count = 1)
{
    if (count <= 0) {
        return;
    }
    u.bonus_moves += count;
    if (entity_has_deployment_fatigue(u) && deployment_fatigue_blocks_move(u)) {
        u.moves_remaining_this_turn += count;
    }
}

inline int entity_standard_moves_at_turn_start(const Entity& e)
{
    return entity_can_move(e) && !deployment_fatigue_blocks_move(e) ? 1 : 0;
}

inline bool unit_has_bonus_move_entitlement(const Entity& e)
{
    if (e.bonus_moves > 0) {
        return true;
    }
    return temporary_stat_grants_for_entity(e).bonus_moves > 0;
}

inline bool unit_may_move_during_bonus_attack_declaration(const Unit& u)
{
    if (u.bonus_attacks_remaining_this_turn > 0) {
        return true;
    }
    return unit_has_bonus_move_entitlement(u);
}

inline void refresh_standard_moves_remaining(Entity& e)
{
    e.standard_moves_remaining_this_turn = entity_standard_moves_at_turn_start(e);
}

/** Catalog/runtime def key for a spawned board token grave entry in purgatory (empty `source_card_id`). */
inline std::optional<std::string> try_spawned_token_purgatory_catalog_key(const Entity& e)
{
    if (!e.owner.has_value() || !e.source_card_id.empty()) {
        return std::nullopt;
    }
    if (e.entity_type != "unit" && e.entity_type != "building") {
        return std::nullopt;
    }
    static const std::pair<const char*, const char*> kSpawnedTokenPurgatoryKeys[] = {
        {"heavy_trooper_", "heavy_trooper"},
        {"conscript_", "spawned_token_conscript"},
        {"replicator_bot_", "spawned_token_replicator_bot"},
        {"shock_wire_", "spawned_token_shock_wire"},
        {"vulturous_nanite_", "spawned_token_nanite_construct"},
    };
    for (const auto& [prefix, catalog_key] : kSpawnedTokenPurgatoryKeys) {
        if (e.entity_id.rfind(prefix, 0) == 0) {
            return catalog_key;
        }
    }
    return std::nullopt;
}

inline bool entity_is_spawned_board_token(const Entity& e)
{
    return try_spawned_token_purgatory_catalog_key(e).has_value();
}

}  // namespace tactics
