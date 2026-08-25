#pragma once

#include "tactics/common/types.hpp"
#include "tactics/effects/effect_traits.hpp"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace tactics {

class GameState;

enum class TargetDomain { None, BoardEntityCell, StackItem, PlayerSeat };
enum class TargetRequirement { None, Optional, Required };

struct TargetDefinition {
    TargetDomain domain{TargetDomain::None};
    TargetRequirement requirement{TargetRequirement::None};
    BoardTargetKind board_target_kind{BoardTargetKind::Enemy};
    /** When true, stealth does not block this effect (area damage / non-direct application). */
    bool area_effect{false};
    /** Empty means any stack source type; otherwise stack items must match one of these ids (spell, ability, attack, ...). */
    std::vector<std::string> allowed_stack_source_types{};
    /** Board target must have at least one of these unit types (empty = no restriction). */
    std::vector<std::string> require_target_unit_types{};
};

struct EffectDefinition {
    std::string key;
    std::string display_name;
    std::string rules_text;
    TargetDefinition target{};
    std::vector<std::string> payload_keys{};
    /**
     * E7: Default board-target kind used when `target.domain != BoardEntityCell`
     * (e.g., no-target or stack-item effects that may still be invoked as automated passives).
     * Eliminates the need to add new effect keys to the hardcoded fallback in types.hpp.
     * Defaults to Enemy; set to Own for self/player effects, Ally for healing-type effects, etc.
     */
    BoardTargetKind default_board_target_kind{BoardTargetKind::Enemy};
    /**
     * Canonical runtime traits, declared per-effect at registration rather than re-listed in a
     * hardcoded key table elsewhere. `deals_damage` drives pierce / lifesteal / soul-steal grants and
     * the damage-target LOS path; `uses_directional_aim` routes targeting through the octilinear
     * direction-cell picker (and, with it, the AoE "ignores line of sight" rule). Adding a new
     * damaging or directional effect is therefore a one-line flag here. Mirrored into EffectTraits by
     * effect_traits.cpp.
     */
    bool deals_damage{false};
    bool uses_directional_aim{false};
    /** Spell/ability push: pick a board entity, then an adjacent direction cell (see push_displacement.hpp). */
    bool uses_push_direction_aim{false};
    /** When true, the caster's `aura_bonus_ability_damage` is propagated into `StackItem::ability_damage_bonus` even though this effect does not deal damage. */
    bool scales_with_ability_damage{false};
    /**
     * When true, the aim cell is the exact landing tile for the moving unit (not just a direction
     * indicator). `gather_ability_board_target_cells` uses `directional_movement_landing_cells`
     * instead of `directional_area_aim_cells`, exposing every passable cell per ray.
     */
    bool movement_landing{false};
    /**
     * When true, this effect targets an empty board cell rather than a board entity.
     * `gather_ability_board_target_cells` scans cells in range; `ActivateAbilityAction::validate`
     * checks range + emptiness without requiring an entity at the target position.
     */
    bool targets_empty_cell{false};
};

void clear_effect_registry();
void ensure_builtin_effect_registry_loaded();
bool register_effect_definition(EffectDefinition def);
bool try_get_effect_definition(const std::string& key, EffectDefinition& out);
bool is_known_effect_key(const std::string& key);
std::vector<std::string> all_registered_effect_keys();
/** Debug/tests: every metadata effect key must have a stack handler (and vice versa for handler keys in metadata). */
void verify_effect_stack_handler_parity(const class StackManager& stack);

TargetDefinition target_definition_for_effect_key(const std::string& effect_key);
bool effect_requires_board_target(const std::string& effect_key);
BoardTargetKind effect_board_target_kind(const std::string& effect_key);
ActionResult validate_effect_targets(const GameState& game, int controller_id, const std::string& effect_key, const std::map<std::string, int>& targets,
    const std::string& stack_item_target_id = {});
ActionResult validate_targets_against_definition(
    const GameState& game, int controller_id, const TargetDefinition& target, const std::map<std::string, int>& targets,
    const std::string& stack_item_target_id = {}, const std::string& effect_key = {});

/** Metadata + runtime traits for one effect key (single lookup for UI/rules). */
inline EffectTraits unified_effect_traits_for_key(const std::string& effect_key) { return effect_traits_for_key(effect_key); }

}  // namespace tactics
