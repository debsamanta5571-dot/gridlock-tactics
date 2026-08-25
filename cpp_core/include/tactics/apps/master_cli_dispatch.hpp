#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <ostream>
#include <string>

namespace tactics {

class GameState;
class Unit;

/** Optional Win32 native plot hook (master_cli supplies this; Unreal leaves it empty). */
struct MasterCliPlotSink {
    std::function<void(const GameState&)> plot_board{};
};

/** Replace P<id> energy zone row with the standard demo layout (untapped). Safe when adding a player mid-match. */
void master_cli_seed_zones_for_player(GameState& game, int player_id);

/** Static movement modifiers on the demo board (roads are fast, rough is slow). */
void master_cli_seed_demo_terrain(GameState& game);

/** Static terrain cubes on the demo board (blocks movement / deploy). */
void master_cli_seed_demo_obstacles(GameState& game);

void master_cli_seed_demo_state(GameState& game);
void master_cli_print_help(const GameState& game, std::ostream& out);
void master_cli_write_board_ppm(const GameState& game, std::ostream& out, const std::string& path = "board_snapshot.ppm");

/**
 * One line of the master CLI (same tokens as the standalone `tactics_master_cli` tool).
 * @return true if the line was `quit` (caller should stop); false otherwise.
 * @param out_move_performed_ok If non-null, set only for `move_confirm`: true when the confirmed move succeeded.
 */
bool dispatch_master_cli_line(GameState& game, int& controlled_player, std::shared_ptr<Unit>& selected_unit, const std::string& line, std::ostream& out,
                              const MasterCliPlotSink& plot = {}, std::optional<bool>* out_move_performed_ok = nullptr);

}  // namespace tactics
