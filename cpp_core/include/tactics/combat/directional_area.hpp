#pragma once

#include "tactics/core/game_state.hpp"
#include "tactics/entities/entity.hpp"

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace tactics {

/** Distance used for direction-picker highlights (one adjacent cell per ray). Blast depth uses payload `max_range`. */
constexpr int kDirectionalAimIndicatorRange = 1;

/** Snap vector from `from` to `to` to one of eight octilinear directions (including diagonals). */
std::pair<int, int> snap_octilinear_direction(int from_x, int from_y, int to_x, int to_y);

/** World cells in a `width`×`depth` rectangle extending from `anchor` along `dir` (dir must be octilinear). */
std::vector<std::pair<int, int>> directional_area_cells(int anchor_x, int anchor_y, int dir_x, int dir_y, int width, int depth);

/** Valid direction-picker cells on the eight octilinear rays from the caster anchor (distance 1..`max_range`).
 *  Returns the single farthest valid cell per ray - appropriate for direction-only abilities (Energy Wave, etc.). */
std::vector<std::pair<int, int>> directional_area_aim_cells(const GameState& game, const Entity& actor, int max_range, bool allow_diagonals = true);

/** Direction-picker cells for directional damage / aim abilities (Energy Wave, Beam Splitter, etc.). */
std::vector<std::pair<int, int>> directional_aim_indicator_cells(
    const GameState& game, const Entity& actor, const std::map<std::string, int>& payload);

/**
 * Valid landing cells for a movement-landing ability: all board cells per ray at distances
 * [`min_range`, `max_range`] that are passable - i.e. not occupied, or occupied only by a pickup.
 * Entities that occupy intermediate cells (enemies flown over) do NOT break the ray; only a void
 * or out-of-bounds square stops scanning in that direction.
 * Use this instead of directional_area_aim_cells when the aim cell is the exact landing tile.
 */
std::vector<std::pair<int, int>> directional_movement_landing_cells(
    const GameState& game, const Entity& actor, int min_range, int max_range, bool allow_diagonals = true);

/** Three cells in a row two steps from `anchor` along `dir` (perpendicular spread). Terra cone splash. */
std::vector<std::pair<int, int>> terra_cone_splash_cells(int anchor_x, int anchor_y, int dir_x, int dir_y);

std::vector<std::pair<int, int>> cleave_pattern_cells(const Entity& caster, int target_x, int target_y);

/** Living board entities occupying any of `cells` (each entity at most once). */
std::vector<std::shared_ptr<Entity>> entities_in_cells(const GameState& game, const std::vector<std::pair<int, int>>& cells);

/** Validate that the direction-picker cell targets a legal aim point for any directional damage ability. */
ActionResult validate_directional_area_damage_ability_target(
    const GameState& game, int controller_id, const Entity& actor, const std::map<std::string, int>& targets, int max_range,
    bool allow_diagonals = true);

/** Blast footprint for a rectangular directional area ability aimed at `aim_x`/`aim_y` (empty if invalid aim). */
std::vector<std::pair<int, int>> preview_directional_area_damage_blast_cells(
    const GameState& game, const Entity& actor, int aim_x, int aim_y, int width, int depth, int max_range);

/** Blast footprint for a helix directional damage aimed at `aim_x`/`aim_y` (empty if invalid aim).
 *  @deprecated  Use preview_directional_damage_blast_cells with shape_key="helix" instead. */
std::vector<std::pair<int, int>> preview_helix_damage_blast_cells(
    const GameState& game, const Entity& actor, int aim_x, int aim_y, int max_range);

/** Unified blast-footprint preview for any registered AoE shape.
 *  Reads width/depth/max_range from `payload`; shape_key defaults to "rectangle". */
std::vector<std::pair<int, int>> preview_directional_damage_blast_cells(
    const GameState& game, const Entity& actor, int aim_x, int aim_y,
    const std::string& shape_key,
    const std::map<std::string, int>& payload);

/**
 * Orange blast/impact preview for any `uses_directional_aim` effect (abilities and focus spells).
 * Dispatches on `effect_key` (directional_damage, piercing_shot, terra_cone_strike, movement-landing, …).
 */
std::vector<std::pair<int, int>> preview_directional_effect_blast_cells(
    const GameState& game, const Entity& actor, int aim_x, int aim_y, const std::string& effect_key,
    const std::map<std::string, int>& payload, const std::map<std::string, std::string>& string_payload = {});

/** True for all effect keys that use 8-way directional aim targeting. */
bool effect_uses_directional_aim(const std::string& effect_key);

}  // namespace tactics
