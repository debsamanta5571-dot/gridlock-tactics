#pragma once

#include <array>
#include <cctype>
#include <cmath>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace tactics {

enum class AttackType { Melee, Ranged, Hybrid, Utility };
enum class EnergyType { Neutral, Omni, Red, Turquoise, Orange, Purple, Green };

/** How incoming damage interacts with shield, armor, magic resist, and bonus health. */
enum class DamageType { Physical, Magic, Pure };

/** All pool keys touched by payment / end-turn reset / player init (single source of truth for ordering). */
inline constexpr std::array<EnergyType, 7> kEnergyBillingAllTypes = {{
    EnergyType::Neutral,
    EnergyType::Omni,
    EnergyType::Red,
    EnergyType::Turquoise,
    EnergyType::Orange,
    EnergyType::Purple,
    EnergyType::Green,
}};

/** Strict chroma (no neutral / omni): colored cost loop and neutral-zone chroma preference. */
inline constexpr std::array<EnergyType, 5> kEnergyChromaTypes = {{
    EnergyType::Red,
    EnergyType::Turquoise,
    EnergyType::Orange,
    EnergyType::Purple,
    EnergyType::Green,
}};

/** When paying generic neutral from float pool, try types in this order (matches legacy planner). */
inline constexpr std::array<EnergyType, 7> kEnergyNeutralSpendPoolPriority = {{
    EnergyType::Neutral,
    EnergyType::Red,
    EnergyType::Turquoise,
    EnergyType::Orange,
    EnergyType::Purple,
    EnergyType::Green,
    EnergyType::Omni,
}};
/** `Spell` = cast from hand. `Special` = classification only; turn gating uses `GameAction::requires_active_player()`. */
enum class ActionType { Deploy, Move, Combat, Ability, Spell, Special };
/**
 * Turn phase sequence:
 *   Energy -> Main -> SpellWindow* -> AttackDeclaration -> Defense -> SecondMain -> SecondSpellWindow* -> (end turn)
 *   (* = only opens if spells/abilities were queued in the preceding Main/SecondMain)
 *
 * Main / SecondMain: deploy and move freely; channeled/reflex spells and abilities are batched into
 *   pending_spell_declarations_. `end_main` commits the batch and opens SpellWindow/SecondSpellWindow
 *   (skipped if nothing queued), then auto-advances to AttackDeclaration (from Main) or
 *   end-of-turn (from SecondMain).
 *
 * SpellWindow / SecondSpellWindow: reaction window for the queued spell batch.
 *   ALL players (including active) may play reflex/blazing responses in round-robin cycles.
 *   Window closes when forfeited_count >= total_players - 1.
 *   Stack resolves LIFO-of-groups / FIFO-within-group.
 *
 * AttackDeclaration: attacks, moves, and reflex/blazing spells are interleaved freely in declaration
 *   order. `attack_commit` locks the queue and opens Defense (or advances to SecondMain if empty).
 *
 * Defense: same reaction-window rules as SpellWindow. After closing, SecondMain begins.
 *
 * BonusAttackDeclaration: only opens after SecondMain/SecondSpellWindow if the active player has
 *   any units with bonus_attacks_remaining_this_turn > 0. Only those units may declare attacks.
 *   Only units with bonus attacks or bonus-move entitlement may move; movement still costs a normal
 *   or bonus move action (bonus attacks alone do not grant movement). `attack_commit` opens BonusDefense.
 *
 * BonusDefense: same reaction-window rules as Defense. After closing, turn ends.
 */
enum class TurnPhase { Energy, Main, SpellWindow, AttackDeclaration, Defense, SecondMain, SecondSpellWindow, BonusAttackDeclaration, BonusDefense };
enum class EffectSpeed { Channeled, Reflex, Blazing };

inline std::optional<AttackType> attack_type_from_string(const std::string& s)
{
    std::string lower = s;
    for (char& c : lower) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (lower == "melee") return AttackType::Melee;
    if (lower == "ranged") return AttackType::Ranged;
    if (lower == "hybrid") return AttackType::Hybrid;
    if (lower == "utility") return AttackType::Utility;
    return std::nullopt;
}

inline std::string attack_type_to_string(AttackType t)
{
    switch (t) {
        case AttackType::Melee: return "melee";
        case AttackType::Ranged: return "ranged";
        case AttackType::Hybrid: return "hybrid";
        case AttackType::Utility: return "utility";
    }
    return "melee";
}

inline std::optional<EffectSpeed> effect_speed_from_string(const std::string& s)
{
    std::string lower = s;
    for (char& c : lower) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (lower == "channeled") return EffectSpeed::Channeled;
    if (lower == "reflex") return EffectSpeed::Reflex;
    if (lower == "blazing") return EffectSpeed::Blazing;
    return std::nullopt;
}

inline std::string effect_speed_to_string(EffectSpeed s)
{
    switch (s) {
        case EffectSpeed::Channeled: return "channeled";
        case EffectSpeed::Reflex: return "reflex";
        case EffectSpeed::Blazing: return "blazing";
    }
    return "channeled";
}

inline const char* turn_phase_to_string(TurnPhase p)
{
    switch (p) {
    case TurnPhase::Energy:              return "energy";
    case TurnPhase::Main:               return "main";
    case TurnPhase::SpellWindow:        return "spell_window";
    case TurnPhase::AttackDeclaration:  return "attack_declaration";
    case TurnPhase::Defense:            return "defense";
    case TurnPhase::SecondMain:              return "second_main";
    case TurnPhase::SecondSpellWindow:       return "second_spell_window";
    case TurnPhase::BonusAttackDeclaration:  return "bonus_attack_declaration";
    case TurnPhase::BonusDefense:            return "bonus_defense";
    }
    return "main";
}

inline TurnPhase turn_phase_from_string(const std::string& s)
{
    if (s == "energy")               return TurnPhase::Energy;
    if (s == "spell_window")         return TurnPhase::SpellWindow;
    if (s == "attack_declaration")   return TurnPhase::AttackDeclaration;
    if (s == "defense")              return TurnPhase::Defense;
    if (s == "second_main")               return TurnPhase::SecondMain;
    if (s == "second_spell_window")       return TurnPhase::SecondSpellWindow;
    if (s == "bonus_attack_declaration")  return TurnPhase::BonusAttackDeclaration;
    if (s == "bonus_defense")             return TurnPhase::BonusDefense;
    return TurnPhase::Main;
}


/** Who may be targeted on the board for spells / activated abilities with a cell target. */
enum class BoardTargetKind { Any, Enemy, Ally, Own, NonSelf };

inline std::string board_target_kind_to_string(BoardTargetKind k)
{
	switch (k) {
	case BoardTargetKind::Any: return "any";
	case BoardTargetKind::Enemy: return "enemy";
	case BoardTargetKind::Ally: return "ally";
	case BoardTargetKind::Own: return "own";
	case BoardTargetKind::NonSelf: return "non_self";
	}
	return "enemy";
}

inline std::optional<BoardTargetKind> board_target_kind_parse(const std::string& s)
{
	std::string lower = s;
	for (char& c : lower) {
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	}
	if (lower == "any") return BoardTargetKind::Any;
	if (lower == "enemy") return BoardTargetKind::Enemy;
	if (lower == "ally") return BoardTargetKind::Ally;
	if (lower == "own") return BoardTargetKind::Own;
	if (lower == "non_self") return BoardTargetKind::NonSelf;
	return std::nullopt;
}

/**
 * Fallback when an effect key is not in the registry.
 * Built-in keys belong in `ensure_builtin_effect_registry_loaded()`.
 */
inline BoardTargetKind default_board_target_kind_for_effect_key(const std::string& effect_key)
{
	if (effect_key == "heal") return BoardTargetKind::Ally;
	if (effect_key == "gain_neutral" || effect_key == "gain_orange" || effect_key == "gain_turquoise"
	    || effect_key == "gain_turquoise_if_enemy_within" || effect_key == "draw_cards"
	    || effect_key == "draw_unit_cards" || effect_key == "draw_spell_cards" || effect_key == "scan") {
		return BoardTargetKind::Own;
	}
	return BoardTargetKind::Enemy;
}

/** Strongly-typed entity category. Use entity_kind() rather than comparing entity_type strings directly. */
enum class EntityKind {
    Unit,              ///< "unit"
    Base,              ///< "base" (player base; immune to effects)
    Building,          ///< "building" (stationary allied structure)
    Obstacle,          ///< "obstacle" (impassable terrain, no owner)
    LowCover,          ///< "low_cover" (passable terrain, provides cover)
    BreakableObstacle, ///< "breakable_obstacle" (can be destroyed; also includes low_cover)
    Pickup,            ///< "pickup" (collectible token; units pass through freely, collected on landing)
    Unknown,           ///< Any unrecognized entity_type string
};

inline EntityKind entity_kind_from_string(const std::string& s)
{
    if (s == "unit")                return EntityKind::Unit;
    if (s == "base")                return EntityKind::Base;
    if (s == "building")            return EntityKind::Building;
    if (s == "obstacle")            return EntityKind::Obstacle;
    if (s == "low_cover")           return EntityKind::LowCover;
    if (s == "breakable_obstacle")  return EntityKind::BreakableObstacle;
    if (s == "pickup")              return EntityKind::Pickup;
    return EntityKind::Unknown;
}

inline const char* entity_kind_to_string(EntityKind k)
{
    switch (k) {
    case EntityKind::Unit:              return "unit";
    case EntityKind::Base:              return "base";
    case EntityKind::Building:          return "building";
    case EntityKind::Obstacle:          return "obstacle";
    case EntityKind::LowCover:          return "low_cover";
    case EntityKind::BreakableObstacle: return "breakable_obstacle";
    case EntityKind::Pickup:            return "pickup";
    case EntityKind::Unknown:           break;
    }
    return "unknown";
}

inline std::string to_string(EnergyType e) {
    switch (e) {
        case EnergyType::Neutral: return "neutral";
        case EnergyType::Omni: return "omni";
        case EnergyType::Red: return "red";
        case EnergyType::Turquoise: return "turquoise";
        case EnergyType::Orange: return "ingenuity";
        case EnergyType::Purple: return "purple";
        case EnergyType::Green: return "gallantry";
    }
    return "unknown";
}

inline std::optional<EnergyType> energy_type_from_string(const std::string& s)
{
    std::string lower = s;
    for (char& c : lower) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (lower == "neutral") return EnergyType::Neutral;
    if (lower == "omni")    return EnergyType::Omni;
    if (lower == "red")     return EnergyType::Red;
    if (lower == "turquoise" || lower == "mythology" || lower == "blue" || lower == "yellow") {
        return EnergyType::Turquoise;
    }
    if (lower == "orange" || lower == "ingenuity") return EnergyType::Orange;
    if (lower == "purple")  return EnergyType::Purple;
    if (lower == "green" || lower == "gallantry") return EnergyType::Green;
    return std::nullopt;
}

/** All lowercase search/filter tokens for an energy type (color name + chroma aliases). */
inline std::vector<std::string> energy_search_keys(EnergyType e)
{
    switch (e) {
        case EnergyType::Neutral: return {"neutral"};
        case EnergyType::Omni: return {"omni"};
        case EnergyType::Red: return {"red"};
        case EnergyType::Turquoise: return {"mythology", "turquoise"};
        case EnergyType::Orange: return {"ingenuity", "orange"};
        case EnergyType::Purple: return {"purple"};
        case EnergyType::Green: return {"gallantry", "green"};
    }
    return {"unknown"};
}

/** True when `token_lower` matches any alias for `e` (token must already be lowercased). */
inline bool energy_filter_matches_type(const std::string& token_lower, EnergyType e)
{
    for (const std::string& key : energy_search_keys(e)) {
        if (token_lower == key) {
            return true;
        }
    }
    return false;
}

/** True when `e` is a named faction color pool (Gallantry / Ingenuity / Mythology). */
inline bool is_named_faction_color(EnergyType e)
{
    return e == EnergyType::Green || e == EnergyType::Orange || e == EnergyType::Turquoise;
}

struct ActionResult {
    bool ok{false};
    std::string message;
    std::map<std::string, std::string> data;
};

/**
 * Integer rounding policy: any gameplay amount that would be fractional rounds down.
 * Use these helpers (not raw `/ 2`, `ceil`, or `static_cast<int>(x * mult)`) for new code.
 * Expects non-negative amounts - damage, HP, stacks, and energy pips are never negative.
 */
inline int round_down_half(int value)
{
    return value / 2;
}

inline int divide_rounded_down(int dividend, int divisor)
{
    if (divisor <= 0) {
        return 0;
    }
    return dividend / divisor;
}

inline int multiply_rounded_down(int value, double multiplier)
{
    if (value <= 0 || multiplier <= 0.0) {
        return 0;
    }
    return static_cast<int>(std::floor(static_cast<double>(value) * multiplier));
}

}  // namespace tactics
