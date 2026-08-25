#pragma once

#include "tactics/cards/cards.hpp"
#include "tactics/common/types.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace tactics {

struct Entity;
class GameState;
class Unit;

struct BoardTargetHighlightCells {
    std::vector<std::pair<int, int>> enemy_cells;
    std::vector<std::pair<int, int>> other_cells;
};

/** Optional legality filter for directional target cells (ability/spell validate). */
using DirectionalTargetCellProbe = std::function<bool(int wx, int wy)>;

using LobbedAoeCenterCellProbe = std::function<bool(int wx, int wy)>;

/**
 * Blue highlight cells for any `uses_directional_aim` effect from `actor`.
 * Direction-indicator effects use adjacent cells per ray; movement-landing lists passable tiles in range.
 */
BoardTargetHighlightCells gather_directional_effect_board_target_cells(
    GameState& game, const std::shared_ptr<Unit>& actor, const std::string& effect_key,
    const std::map<std::string, int>& payload, const std::map<std::string, std::string>& string_payload,
    DirectionalTargetCellProbe probe = nullptr);

/** Orange blast footprint for a lobbed AoE centered on `(center_x, center_y)`. */
std::vector<std::pair<int, int>> preview_lobbed_aoe_blast_cells(
    int center_x, int center_y, const std::map<std::string, int>& payload);

/**
 * Unified orange blast preview for any armed ability or spell.
 * Dispatches in priority order: directional (shape registry) → lobbed center → tile-centered square.
 * `actor` is required for directional effects; optional for lobbed/hand tile placement.
 */
std::vector<std::pair<int, int>> preview_effect_aoe_blast_cells(
    const GameState& game, const Entity* actor, int aim_x, int aim_y, const std::string& effect_key,
    const std::map<std::string, int>& payload, const std::map<std::string, std::string>& string_payload);

/** Blue cells: any board tile in Chebyshev `max_range` with LOS that may be the AoE center. */
BoardTargetHighlightCells gather_lobbed_aoe_center_cells(
    const GameState& game, const Entity& caster, int max_range, const std::map<std::string, int>& payload,
    LobbedAoeCenterCellProbe probe = nullptr);

/** Collect valid board target cells for an armed ability (cpp_core authority for UI highlights). */
BoardTargetHighlightCells gather_ability_board_target_cells(GameState& game, const std::shared_ptr<Unit>& actor, int player_id,
                                                              const std::string& ability_key_utf8);

/**
 * Collect valid board target cells for an armed spell (focus or hand-cast).
 * Focus spells use `focus_range` + LOS from `focus_caster`; tile-centered AoE may target empty cells.
 * Hand spells without a caster highlight any on-board cell when the effect does not pin a unit.
 */
BoardTargetHighlightCells gather_spell_board_target_cells(
    GameState& game, const std::shared_ptr<Unit>& focus_caster, int player_id, const std::string& effect_key, int focus_range,
    BoardTargetKind board_target_kind, const std::map<std::string, int>& payload,
    const std::map<std::string, std::string>& string_payload, LobbedAoeCenterCellProbe probe = nullptr);

/** Legality probe for armed spell board-target highlights (energy + full cast validate). */
bool spell_probe_valid(GameState& game, CardInstanceId card_id, int player_id, const std::shared_ptr<Entity>& focus_caster,
    CardPlayZone zone, int wx, int wy);

/** Footprint highlights for friendly units that can legally cast an armed focus spell. */
BoardTargetHighlightCells gather_focus_caster_highlight_cells(
    GameState& game, int player_id, const std::string& effect_key, int focus_range, BoardTargetKind board_target_kind,
    const std::map<std::string, int>& payload, const std::map<std::string, std::string>& string_payload);

/** Footprint highlights for friendly units with forced damage-spell focus casting (e.g. Valgar). */
BoardTargetHighlightCells gather_forced_damage_spell_focus_caster_cells(GameState& game, int player_id);

/** Phase-1 entity targets for a push-direction spell (any legal unit footprint). */
BoardTargetHighlightCells gather_push_direction_spell_entity_cells(
    GameState& game, int player_id, const std::string& effect_key, BoardTargetKind board_target_kind,
    const std::vector<std::string>& require_target_unit_types);

/** Phase-2 adjacent direction picker cells around the chosen push target. */
BoardTargetHighlightCells gather_push_direction_indicator_cells_for_target(
    const GameState& game, int target_x, int target_y, const std::map<std::string, int>& payload);

/** Validates unit target + `aim_x`/`aim_y` for push-direction spells. */
ActionResult validate_push_direction_spell_target(
    GameState& game, int player_id, const std::string& effect_key, BoardTargetKind board_target_kind,
    const std::vector<std::string>& require_target_unit_types, const std::map<std::string, int>& payload,
    const std::map<std::string, int>& targets);

/** Orange path preview for push-direction spell hover (tiles the unit would traverse). */
std::vector<std::pair<int, int>> preview_push_direction_spell_path_cells(
    const GameState& game, int target_x, int target_y, int aim_x, int aim_y, const std::map<std::string, int>& payload);

/** True when the actor's footprint overlaps a legal target cell for this ability (engine authority for {_self} icons). */
bool ability_board_target_includes_caster_self(
    GameState& game, const std::shared_ptr<Unit>& actor, int player_id, const std::string& ability_key_utf8);

}  // namespace tactics