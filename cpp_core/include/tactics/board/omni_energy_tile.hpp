#pragma once

#include "tactics/board/grid.hpp"
#include "tactics/common/types.hpp"

#include <memory>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace tactics {

class GameState;

inline constexpr const char* kOmniEnergyModifierName = "omni_energy";

/** Map-authored omni-energy footprint; each tile resolves control and payout independently. */
struct OmniEnergyClusterSpec {
    std::string cluster_id;
    /** Seat whose base this tile sits beside (enemy tile for hostile teams). */
    int home_seat{0};
    std::vector<std::pair<int, int>> cells;
};

/** Runtime state for one omni-energy tile (synced in match snapshots). */
struct OmniEnergyClusterState {
    std::string cluster_id;
    int home_seat{0};
    std::vector<std::pair<int, int>> cells;
    std::unordered_set<int> teams_fired_this_round;
};

/** P1 far right - two rows toward center from first deploy row (bottom base rows 0-1). */
inline OmniEnergyClusterSpec make_standard_duel_p1_omni_energy_cluster()
{
    return OmniEnergyClusterSpec{
        "p1_omni_energy",
        1,
        {{7, 4}},
    };
}

/** P2 far right - two rows toward center from top deploy strip (top base rows 10-11). */
inline OmniEnergyClusterSpec make_standard_duel_p2_omni_energy_cluster()
{
    return OmniEnergyClusterSpec{
        "p2_omni_energy",
        2,
        {{7, 7}},
    };
}

bool square_has_omni_energy(const GridSquare& sq);
bool is_omni_energy_world_cell(const GameState& game, int wx, int wy);

bool place_omni_energy_on_square(std::shared_ptr<GridSquare> sq);
bool seed_omni_energy_cluster(GameState& game, const OmniEnergyClusterSpec& spec);
void seed_standard_duel_omni_energy_tiles(GameState& game);

const OmniEnergyClusterState* find_omni_energy_cluster_at_cell(const GameState& game, int wx, int wy);
const OmniEnergyClusterState* find_omni_energy_cluster_by_id(const GameState& game, const std::string& cluster_id);

bool entity_on_omni_energy_tile(const GameState& game, const Entity& e);

/** Start-of-turn hook: uncontested enemy omni-energy control grants 1 floating omni energy. */
void process_omni_energy_tiles_at_turn_start(GameState& game, int active_player_id);

void clear_omni_energy_teams_fired_for_new_round(GameState& game);

}  // namespace tactics