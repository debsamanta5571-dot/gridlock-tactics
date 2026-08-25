#pragma once

#include "tactics/board/grid.hpp"
#include "tactics/common/types.hpp"

#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace tactics {

class GameState;

inline constexpr const char* kAetherModifierName = "aether";
/** First `round_number` at which objective payouts fire at turn start (aether, scanner, omni).
 *  Rounds 1–3: no payouts. Round 3 is the first full round where every player has taken a turn
 *  (contest only). Earliest payout is round 4. All three objectives share this gate. */
inline constexpr int kObjectiveActivationRound = 4;

/** Map-authored aether footprint; each cluster resolves control and damage independently. */
struct AetherClusterSpec {
    std::string cluster_id;
    std::vector<std::pair<int, int>> cells;
};

/** Runtime state for one aether cluster (synced in match snapshots). */
struct AetherClusterState {
    std::string cluster_id;
    std::vector<std::pair<int, int>> cells;
    int damage_next{1};
    std::optional<int> last_sole_control_team;
    std::unordered_set<int> teams_fired_this_round;
    /** Board units/structures killed by this cluster's bursts (not bases). */
    int units_killed{0};
};

/** Standard duel map: 2×2 center cluster (cols 3–4, rows 5–6). */
inline AetherClusterSpec make_standard_duel_center_aether_cluster()
{
    return AetherClusterSpec{
        "center",
        {{3, 5}, {4, 5}, {3, 6}, {4, 6}},
    };
}

/** True if this square has the aether terrain modifier. */
bool square_has_aether(const GridSquare& sq);
/** True if world cell (wx, wy) belongs to any aether cluster. */
bool is_aether_world_cell(const GameState& game, int wx, int wy);

/** Place permanent aether terrain on a cell (no-op if the cell is missing). */
bool place_aether_on_square(std::shared_ptr<GridSquare> sq);

/** Register cluster state and paint its cells with aether terrain. */
bool seed_aether_cluster(GameState& game, const AetherClusterSpec& spec);

/** Seed all standard 8×12 duel aether clusters (currently the center 2×2). */
void seed_standard_duel_aether_tiles(GameState& game);

/** Cluster covering (wx, wy), or null if the cell is not aether. */
const AetherClusterState* find_aether_cluster_at_cell(const GameState& game, int wx, int wy);
/** Cluster with this id, or null if it is not registered. */
const AetherClusterState* find_aether_cluster_by_id(const GameState& game, const std::string& cluster_id);

/**
 * True when the entity can contest or establish aether control.
 * Large units and bases never count; 1×1 structures count like units.
 */
bool entity_counts_for_aether_control(const Entity& e);

/** True when any footprint cell overlaps a cell in `cluster_cells`. */
bool entity_on_cluster_cells(const Entity& e, const std::vector<std::pair<int, int>>& cluster_cells);

/** True when any footprint cell overlaps any registered aether cluster cell. */
bool entity_on_aether_tile(const GameState& game, const Entity& e);

/** Next burst that would hit a `seat`-controlled occupant of (x,y). 0 if the cell is not aether. */
int aether_predicted_burst_damage(const GameState& game, int seat, int x, int y);

/** True when standing on (x,y) would kill `e` on the next uncontested aether burst for `seat`. */
bool aether_would_kill_entity(const GameState& game, int seat, const Entity& e, int x, int y);

/** Sum of `AetherClusterState::units_killed` across every cluster. */
int aether_units_killed_total(const GameState& game);

/** Start-of-turn hook for the active seat (round `kObjectiveActivationRound`+); each cluster
 *  resolves independently. */
void process_aether_tiles_at_turn_start(GameState& game, int active_player_id);

/** Clears per-round team fire tracking on every cluster when `round_number` advances. */
void clear_aether_teams_fired_for_new_round(GameState& game);

}  // namespace tactics