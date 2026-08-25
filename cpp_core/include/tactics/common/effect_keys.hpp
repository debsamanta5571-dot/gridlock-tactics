#pragma once

namespace tactics::effect_keys {

/** Stack / spell `targets` map keys for a single grid cell (0-based game coordinates). */
inline constexpr const char kCellX[] = "x";
inline constexpr const char kCellY[] = "y";
/** Push direction aim cell (paired with kCellX/kCellY entity target). */
inline constexpr const char kAimX[] = "aim_x";
inline constexpr const char kAimY[] = "aim_y";
/** Optional focus spell caster anchor cell (friendly unit footprint). */
inline constexpr const char kFocusCasterX[] = "focus_caster_x";
inline constexpr const char kFocusCasterY[] = "focus_caster_y";
inline constexpr const char kStackItemId[] = "stack_item_id";
/** Target seat id (1-based player id) for player-target spells and effects. */
inline constexpr const char kTargetPlayerSeat[] = "target_player";
inline constexpr const char kAttackPreferRanged[] = "attack_prefer_ranged";

/** Common effect payload keys. */
inline constexpr const char kPayloadAmount[] = "amount";
/** Optional repair payload: heal amount when the target is a player base (`repair_structure_adjacent`). */
inline constexpr const char kPayloadBaseAmount[] = "base_amount";
/** Optional stack payload: 0=physical, 1=magic, 2=pure (see `DamageType`). */
inline constexpr const char kDamageType[] = "damage_type";
inline constexpr const char kPayloadPoison[] = "poison";
inline constexpr const char kPayloadFire[] = "fire";
inline constexpr const char kPayloadBleed[] = "bleed";
/** Source tag on timed Silenced stacks that expire at the silenced unit owner's turn end. */
inline constexpr const char kSilencedOwnerTurnEndSource[] = "owner_turn_end";

/** Optional second board cell for Soul Steal: which allied base to heal (when multiple exist). */
inline constexpr const char kHealBaseX[] = "heal_base_x";
inline constexpr const char kHealBaseY[] = "heal_base_y";

/** Temporary effect ids for attack-action alternatives. */
inline constexpr const char kDefendStanceEffectId[] = "defend_stance";
/** Legacy stack handler for defend stance (burst Defend uses apply_defend_stance directly). */
inline constexpr const char kUnitDefendStanceActionKey[] = "unit_defend_stance";
inline constexpr const char kDashMovementEffectId[] = "dash_movement";
inline constexpr const char kRecoverStanceEffectId[] = "recover_stance";
/** Set on a unit when it takes HP damage while recover_stance is active. */
inline constexpr const char kRecoverStanceDamageTakenKey[] = "recover_stance_damage_taken";
inline constexpr const char kDeploymentFatigueEffectId[] = "deployment_fatigue";
/** Sylvia Overcharge: bearer applies overload to victims of their damage until owner turn end. */
inline constexpr const char kOnDamageApplyOverloadEffectId[] = "on_damage_apply_overload";
/** Sylvia Jamming Array: bearer applies jammed to victims of their damage until owner turn end. */
inline constexpr const char kOnDamageApplyJammedEffectId[] = "on_damage_apply_jammed";
/** Medical Override: when bearer's deal_damage ability hits an allied entity, it heals that ally
 *  for the damage amount instead of damaging them. Enemies are still damaged normally.
 *  Granted by grant_medical_override; expires owner_turn_end. */
inline constexpr const char kMedicalOverrideEffectId[] = "medical_override";
/** Next damaging ability only: bearer reduces movement of each unit hit until that unit's owner's turn end. */
inline constexpr const char kOnDamageApplyMovementReductionNextAbilityEffectId[] =
    "on_damage_apply_movement_reduction_next_ability";
/** Next damaging ability only: bearer roots each unit hit. */
inline constexpr const char kOnDamageApplyRootedNextAbilityEffectId[] = "on_damage_apply_rooted_next_ability";
/** Sir Garrick Valiant Guard: bearer intercepts the next attack on a surrounding 1×1 ally. */
inline constexpr const char kValiantGuardEffectId[] = "valiant_guard";
/** Barrier stacks that expire at the bearer's owner's turn end. */
inline constexpr const char kBarrierOwnerTurnEndSource[] = "owner_turn_end";
/** Mana Pylon charge stacks consumed to grant per-cast Conduit on allied damaging spells. */
inline constexpr const char kManaPylonChargeEffectId[] = "mana_pylon_charge";
/** Disregarded Rangers Serrated Bolt: bearer applies bleed to victims of their next attack or ability. */
inline constexpr const char kOnDamageApplyBleedNextAbilityEffectId[] = "on_damage_apply_bleed_next_ability";
/** Sylvia Static Charge (boost): bearer applies 1 overload to victims of their next attack or ability. */
inline constexpr const char kOnDamageApplyOverloadNextAbilityEffectId[] = "on_damage_apply_overload_next_ability";
/** Sylvia Jamming Array (boost): bearer applies 1 jammed to victims of their next attack or ability. */
inline constexpr const char kOnDamageApplyJammedNextAbilityEffectId[] = "on_damage_apply_jammed_next_ability";

}  // namespace tactics::effect_keys
