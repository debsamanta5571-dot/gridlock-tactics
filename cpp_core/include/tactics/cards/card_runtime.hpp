#pragma once

#include "tactics/cards/card_catalog.hpp"
#include "tactics/cards/card_instances.hpp"
#include "tactics/cards/cards.hpp"
#include "tactics/entities/entity.hpp"

#include <map>
#include <memory>
#include <optional>
#include <string>

namespace tactics {

class GameState;

/** Read-only helpers: resolve static card data from the catalog. */
bool definition_is_unit(const CardDefinition& def);
bool definition_is_spell(const CardDefinition& def);
const UnitCardDefinition& definition_unit(const CardDefinition& def);
const SpellCardDefinition& definition_spell(const CardDefinition& def);
const CardDefinition& definition_for_instance(const CardInstancePool& pool, CardInstanceId id);
const CardDefinition& definition_for_instance(const CardInstancePool& pool, const CardInstance& inst);

std::map<EnergyType, int> definition_energy_cost(const CardDefinition& def);
/** Sum of all energy pip values - used for Command keyword deployment checks. */
int definition_total_energy_cost(const CardDefinition& def);
/** Deploy cost of the source card for a board entity (`source_card_id` / token key). Empty when unknown. */
std::optional<int> entity_source_deploy_cost(const Entity& entity);
/** True for deployable units (not structures) whose source card total deploy cost is <= max_cost. */
bool entity_satisfies_max_deploy_cost(const Entity& entity, int max_cost);
int batched_spell_total_cost_for_cast(const CardDefinition& def, int x_amount);
/** True when the spell block declares variable X energy at cast time. */
bool definition_spell_has_x_cost(const CardDefinition& def);
int definition_spell_x_cost_min(const CardDefinition& def);
/** Highest X >= min the player can pay for this spell (ActionType::Spell), or min-1 when unaffordable. */
int max_affordable_spell_x_amount(const GameState& game, int player_id, const CardDefinition& def);
/** Highest X >= min the player can pay for this activated ability, or min-1 when unaffordable. */
int max_affordable_ability_x_amount(const GameState& game, int player_id, const AbilitySpec& spec);
std::string definition_name(const CardDefinition& def);
bool definition_has_keyword(const CardDefinition& def, const std::string& key);
std::optional<CardKeywordDefinition> definition_exalted_keyword(const CardDefinition& def);
bool exalted_requirement_met(const GameState& game, int player_id, const CardDefinition& def,
                             std::string* reason = nullptr);
int definition_stockpile_amount(const CardDefinition& def);
/** Multicast X on spells: number of separate stack entries (and Spellbound triggers) per cast. Default 1. */
int definition_multicast_amount(const CardDefinition& def);
/** True when Multicast > 1 and the player must pick a separate target for each copy. */
bool definition_spell_multicast_requires_per_copy_targets(const CardDefinition& def);

BoardTargetKind definition_spell_board_target_kind(const CardDefinition& def);
bool definition_spell_requires_mandatory_board_cell(const CardDefinition& def);
bool definition_spell_requires_stack_target(const CardDefinition& def);
bool definition_spell_requires_player_seat_target(const CardDefinition& def);

/** Modal spells (`spell.modes` non-empty): choose-one at cast time. */
bool definition_spell_is_modal(const CardDefinition& def);
int definition_spell_modal_mode_count(const CardDefinition& def);
const SpellMode* try_definition_spell_mode(const CardDefinition& def, int mode_index);
std::string definition_spell_mode_effect_key(const CardDefinition& def, int mode_index);
bool definition_spell_mode_requires_board_cell(const CardDefinition& def, int mode_index);
BoardTargetKind definition_spell_mode_board_target_kind(const CardDefinition& def, int mode_index);

bool definition_has_soul_steal(const CardDefinition& def);

/** Spell speed at cast time (passive overrides such as Seraphina's Scorching Acceleration). */
EffectSpeed effective_spell_cast_speed(const GameState& game, int player_id, const CardDefinition& def);

/** Build a unit on the board from catalog data (not from a fat runtime card object). */
std::shared_ptr<Unit> create_unit_from_definition(
    const CardDefinition& def, const CardInstance& inst, int owner, const std::string& entity_id);

/** Allocate a new instance in `deck.pool` from a catalog key. */
CardInstanceId deck_allocate_instance(Deck& deck, CardDefId def_id, int copy_index);
CardInstanceId deck_allocate_instance(Deck& deck, const std::string& def_key, int copy_index);

/** Legacy snapshot import: hydrate pool from a fully serialized v1 card blob. */
CardInstanceId deck_import_legacy_card(Deck& deck, const Card& legacy_card);

}  // namespace tactics
