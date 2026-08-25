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

inline constexpr const char* kScannerModifierName = "scanner";

/** Map-authored scanner footprint; each tile resolves control and scan independently. */
struct ScannerClusterSpec {
    std::string cluster_id;
    /** Seat whose base this scanner sits beside (enemy scanner for hostile teams). */
    int home_seat{0};
    std::vector<std::pair<int, int>> cells;
};

/** Runtime state for one scanner tile (synced in match snapshots). */
struct ScannerClusterState {
    std::string cluster_id;
    int home_seat{0};
    std::vector<std::pair<int, int>> cells;
    std::unordered_set<int> teams_fired_this_round;
};

/** P1 far left - two rows toward center from first deploy row (bottom base rows 0-1). */
inline ScannerClusterSpec make_standard_duel_p1_scanner_cluster()
{
    return ScannerClusterSpec{
        "p1_scanner",
        1,
        {{0, 4}},
    };
}

/** P2 far left - two rows toward center from top deploy strip (top base rows 10-11). */
inline ScannerClusterSpec make_standard_duel_p2_scanner_cluster()
{
    return ScannerClusterSpec{
        "p2_scanner",
        2,
        {{0, 7}},
    };
}

bool square_has_scanner(const GridSquare& sq);
bool is_scanner_world_cell(const GameState& game, int wx, int wy);

bool place_scanner_on_square(std::shared_ptr<GridSquare> sq);
bool seed_scanner_cluster(GameState& game, const ScannerClusterSpec& spec);
void seed_standard_duel_scanner_tiles(GameState& game);

const ScannerClusterState* find_scanner_cluster_at_cell(const GameState& game, int wx, int wy);
const ScannerClusterState* find_scanner_cluster_by_id(const GameState& game, const std::string& cluster_id);

bool entity_on_scanner_tile(const GameState& game, const Entity& e);

/** Start-of-turn hook: uncontested enemy scanner control grants scan 1 before the turn draw. */
void process_scanner_tiles_at_turn_start(GameState& game, int active_player_id);

void clear_scanner_teams_fired_for_new_round(GameState& game);

}  // namespace tactics