#pragma once

#include "tactics/cards/card_catalog.hpp"
#include "tactics/cards/card_runtime.hpp"

#include <map>
#include <memory>
#include <optional>
#include <string>

namespace tactics {

class GameState;
struct Entity;
struct StackItem;

inline bool spell_is_focus(const CardDefinition& def) { return definition_has_keyword(def, "focus"); }
inline int spell_focus_range(const SpellCardDefinition& spell) { return spell.focus_range; }
inline bool spell_requires_focus_caster(const CardDefinition& def) { return spell_is_focus(def); }

bool spell_subject_to_forced_damage_spell_focus(const CardDefinition& def);
bool spell_requires_forced_damage_spell_focus_caster(const GameState& game, int player_id, const CardDefinition& def);
bool cast_uses_forced_damage_spell_focus_caster(const GameState& game, const CardDefinition& def, const std::shared_ptr<Entity>& caster);
bool player_has_forced_damage_spell_focus_caster(const GameState& game, int player_id);
std::optional<int> entity_forced_damage_spell_focus_range(const Entity& entity);

std::shared_ptr<Entity> resolve_focus_caster_from_targets(
    const GameState& game, int player_id, const std::map<std::string, int>& targets, const std::shared_ptr<Entity>& explicit_caster);

ActionResult validate_focus_spell_cast(GameState& game, int player_id, const CardDefinition& def, const SpellCardDefinition& spell,
    const Entity& caster, const std::map<std::string, int>& targets, const std::string& stack_target_id,
    std::optional<int> focus_range_override = std::nullopt);

bool focus_spell_pierces_damage_prevention(const CardDefinition& def, const SpellCardDefinition& spell, const Entity& caster);
bool focus_spell_grants_lifesteal(const CardDefinition& def, const SpellCardDefinition& spell, const Entity& caster);
bool focus_spell_grants_soul_steal(const CardDefinition& def, const SpellCardDefinition& spell, const Entity& caster);

bool queued_focus_spell_still_valid(GameState& game, const StackItem& item);

}  // namespace tactics