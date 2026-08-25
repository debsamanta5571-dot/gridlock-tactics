#pragma once

#include "tactics/entities/entity.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace tactics {

class GameBoard;
class GameState;
struct PassiveAbilitySpec;

/** Timings for automated / triggered passive actions (matches TemporaryEntityEffect expire_on style). */
inline constexpr const char* kPassiveActionTimingOwnerTurnStart = "owner_turn_start";
inline constexpr const char* kPassiveActionTimingOwnerTurnEnd = "owner_turn_end";
inline constexpr const char* kPassiveActionTimingRoundStart = "round_start";

/** True when `a` entered the board before `b` (lower spawn_sequence wins; tie-break entity_id). */
bool entity_spawn_older(const Entity& a, const Entity& b);

/** Units / buildings in passive-action and turn-order UI (excludes bases and map obstacles). */
bool entity_participates_in_passive_action_order(const Entity& e);

void sort_entities_oldest_first(std::vector<std::shared_ptr<Entity>>& entities);

/** Living entities for one seat, oldest first. */
std::vector<std::shared_ptr<Entity>> living_entities_for_owner_oldest_first(const GameBoard& board, int owner_id);

/** All living board entities, oldest first. */
std::vector<std::shared_ptr<Entity>> all_living_entities_oldest_first(const GameBoard& board);

/** 1-based passive-action rank (1 = oldest). Returns 0 when `entity_id` is absent or dead. */
int passive_action_order_rank(const GameBoard& board, const std::string& entity_id);

/** All living entities mapped to 1-based rank (oldest first). */
std::unordered_map<std::string, int> compute_passive_action_order_ranks(const GameBoard& board);

/** True when turn-order badges should render on this footprint cell (one badge per entity). */
bool entity_turn_order_badge_at_cell(const Entity& entity, int world_x, int world_y);

/** Resolve one automated passive on `source` (damage/heal/etc.). Returns false if fizzled. */
bool resolve_automated_passive_action(GameState& game, const std::shared_ptr<Entity>& source, const PassiveAbilitySpec& passive);

/**
 * Fires all automated passives for `player_id` at `timing`.
 * Sources are processed oldest-unit-first; passives on each unit run in catalog order.
 */
void process_automated_passive_actions(GameState& game, int player_id, const std::string& timing);

}  // namespace tactics
