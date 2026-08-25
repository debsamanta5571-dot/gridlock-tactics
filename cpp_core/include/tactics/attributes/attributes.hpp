#pragma once

#include "tactics/common/types.hpp"
#include "tactics/entities/entity.hpp"

#include <string>
#include <vector>

namespace tactics {

struct AttributeSpec {
    std::string key;
    std::string display_name;
    std::string rules_text;
    bool ignores_terrain_movement_cost{false};
    bool moves_over_blockers{false};
    int minimum_melee_range{0};
    bool card_retains_until_charges_depleted{false};
    bool card_once_per_turn{false};
    bool does_not_block_line_of_sight{false};
    bool does_not_block_movement{false};
    bool unlimited_reactions{false};
    bool can_counterattack_with_ranged{false};
    bool attacks_skip_reactions{false};
    bool reduces_incoming_damage{false};
    bool grants_bonus_health{false};
    bool blocks_next_damage{false};
    bool pierces_damage_prevention{false};
    bool heals_on_damage_dealt{false};
    /** Damage dealt also heals an allied base (see Soul Steal targeting). */
    bool heals_allied_base_on_damage_dealt{false};
    bool ignores_attack_line_of_sight{false};
    /** Ranged attacks from this entity ignore enemy low-cover evasion. */
    bool ignores_low_cover_evasion{false};
    /** Diagonal movement costs no extra (used by large_unit). */
    bool ignores_diagonal_movement_cost{false};
    /** Uses the policy void_fall_threshold (50%) for landing rather than blocking any void cell. */
    bool uses_void_threshold_for_landing{false};
    /** Incoming crits deal normal (non-multiplied) damage instead. */
    bool immune_to_crits{false};
    /** Basic attacks deal bonus damage against base entities. Amount stored in keyword_amounts. */
    bool bonus_damage_vs_base{false};
    /** Allows friendly units with total energy cost ≤ Command value to deploy adjacent to this unit. */
    bool grants_forward_deploy{false};
    /** Deal bonus damage when another ally has already attacked the same target this turn. Amount stored in keyword_amounts. */
    bool bonus_damage_when_coordinated{false};
    /** Deal bonus damage while unit has not moved this turn. Amount stored in keyword_amounts. */
    bool bonus_damage_when_stationary{false};
    /** Always deal the maximum value in the attack's damage range (basic attacks and abilities). */
    bool uses_max_damage{false};
    /** Deal bonus damage while below half maximum HP. Amount stored in keyword_amounts. */
    bool bonus_damage_when_low_hp{false};
    /** All reactions (counterattack, return fire, covering fire) deal bonus damage. Amount stored in keyword_amounts. */
    bool bonus_damage_on_reactions{false};
    /** Structure may only be deployed within this Chebyshev range of an enemy base. Amount stored in keyword_amounts. */
    bool restricts_deploy_to_enemy_base_range{false};
    /** When an attack kills an enemy unit, that defender does not counterattack. */
    bool suppresses_counterattack_on_kill{false};
};

const AttributeSpec* find_attribute_spec(const std::string& key);
std::vector<AttributeSpec> all_attribute_specs();

std::string attribute_display_name(const std::string& key);
std::string attribute_rules_text(const std::string& key);
std::string format_attribute_names(const std::vector<std::string>& keys);

/** Keywords that may only exist on a unit's native card keywords - never aura/temp/mirror grants. */
bool attribute_is_non_copyable(const std::string& key);

/** True when the unit was printed with Indestructible (native keywords only; silenced = false). */
bool entity_is_indestructible(const Entity& e);

bool has_large_unit(const Entity& e);
bool entity_ignores_diagonal_movement_cost(const Entity& e);
bool entity_uses_void_threshold_for_landing(const Entity& e);
bool has_flying(const Entity& e);
bool has_reach(const Entity& e);
bool ignores_terrain_movement_cost(const Entity& e);
bool moves_over_blockers(const Entity& e);
int minimum_melee_range_from_attributes(const Entity& e);
bool does_not_block_line_of_sight(const Entity& e);
bool entity_blocks_line_of_sight(const Entity& e);
bool does_not_block_movement(const Entity& e);
bool has_vigilance(const Entity& e);
bool has_crit_immunity(const Entity& e);
bool has_return_fire(const Entity& e);
bool has_shadowstrike(const Entity& e);
/** First Strike: killing blow suppresses the defender's counterattack (0 when silenced). */
bool has_first_strike(const Entity& e);
bool has_pierce(const Entity& e);
bool has_lifesteal(const Entity& e);
bool has_soul_steal(const Entity& e);
struct Card;
bool card_has_soul_steal(const Card& c);
bool ignores_attack_line_of_sight(const Entity& e);
bool ignores_low_cover_evasion(const Entity& e);
struct AbilitySpec;
bool ability_has_trueshot(const AbilitySpec& ability);
/** Unit keywords and/or ability `trueshot` keyword. */
bool damage_source_ignores_attack_line_of_sight(const Entity& source, const AbilitySpec* ability);
bool damage_source_ignores_low_cover_evasion(const Entity& source, const AbilitySpec* ability);
bool ability_has_pierce(const AbilitySpec& a);
bool ability_has_lifesteal(const AbilitySpec& a);
bool ability_has_soul_steal(const AbilitySpec& a);
/** True when `source` has lifesteal and/or the stack item was cast from an ability with lifesteal (single heal, not stacked). */
bool damage_source_has_lifesteal(const Entity* source, bool ability_grants_lifesteal);
bool damage_source_has_soul_steal(const Entity* source, bool ability_grants_soul_steal);
/** Base-Breaker bonus damage on basic attacks vs. base entities (0 when silenced or no keyword). */
int base_breaker_bonus(const Entity& e);
/** Relentless extra-attack count (0 when silenced or no keyword). */
int relentless_value(const Entity& e);
/** Multistrike extra-strike count within a single attack (0 when silenced or no keyword).
 *  Each strike re-applies damage and on-hit effects; no new counterattack per strike. */
int multistrike_value(const Entity& e);
/** Command value for forward-deploy (0 when silenced or no keyword). */
int command_value(const Entity& e);
/** Coordinated bonus damage when an ally has already attacked the same target this turn (0 when silenced). */
int coordinated_value(const Entity& e);
/** Entrenched bonus damage while the unit has not moved this turn (0 when silenced or has moved). */
int entrenched_value(const Entity& e);
/** True if unit always deals maximum attack damage (suppressed while silenced). */
bool has_precise(const Entity& e);
/** Berserk bonus damage while below half maximum HP (0 when silenced or HP not low). */
int berserk_value(const Entity& e);
/** Defender bonus damage on all reactions - counterattack, return fire, covering fire (0 when silenced). */
int defender_value(const Entity& e);
/** Spearhead deploy range (0 when silenced or no keyword). Structures with this keyword may only be deployed within this Chebyshev distance of an enemy base. */
int spearhead_value(const Entity& e);
int armor_value(const Entity& e);
int reduce_damage_by_armor(const Entity& e, int raw_damage, int terrain_armor_bonus = 0);
int magic_resist_value(const Entity& e);
int reduce_damage_by_magic_resist(const Entity& e, int raw_damage);
/** Regen amount from keywords/auras/temporary grants (0 when silenced). */
int regen_value(const Entity& e);
/** Thorns retaliation amount (0 when silenced). */
int thorns_value(const Entity& e);
/** Conduit keyword value on this entity (0 when silenced or absent). */
int conduit_value(const Entity& e);
int bonus_health_value(const Entity& e);
bool has_shield(const Entity& e);
bool has_barrier(const Entity& e);
/** Returns HP/bonus-health damage actually applied after mitigation (not raw packet size). */
int apply_incoming_damage(Entity& e, int raw_damage, bool pierce = false, DamageType damage_type = DamageType::Physical,
    int terrain_armor_bonus = 0);
/** Post-mitigation damage before HP overkill clamp (combat viz floating numbers). */
int incoming_damage_display_amount(const Entity& e, int raw_damage, bool pierce = false,
    DamageType damage_type = DamageType::Physical, int terrain_armor_bonus = 0);

}  // namespace tactics
