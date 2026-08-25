#pragma once

#include "tactics/board/board.hpp"
#include "tactics/board/grid.hpp"

#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace tactics {

/** How forced movement treats cells with no board tile (off-map). Default: impassable wall. */
enum class OffBoardTileTreatment { Wall, Void };

inline constexpr int kPlayerBaseWidth = 4;
inline constexpr int kPlayerBaseDepth = 2;
inline constexpr int kStandardBoardWidth = 8;
inline constexpr int kStandardBoardHeight = 12;
/** 2v2 map (12 wide x 12 tall): 4 bases - two along the top (team A: seats 1,3), two along the bottom
 *  (team B: seats 2,4), spaced apart. Scanner objectives sit on the left edge, omni on the right edge,
 *  aether in the center. */
inline constexpr int k2v2BoardWidth = 12;
inline constexpr int k2v2BoardHeight = 12;
/** Bumped when tile geometry changes; snapshots with a different id must rebuild the board. */
inline constexpr const char* kDefaultBoardLayoutId = "duel_8x12_bases_v3";
inline constexpr const char* k2v2BoardLayoutId = "team_12x12_4bases_v1";

/** World-space anchor for a seated player's 4x2 base footprint. */
struct PlayerBaseZone {
    int anchor_x{0};
    int anchor_y{0};
};

/** Axis-aligned world rectangle (e.g. 4x2 deployment strip). */
struct BoardRectZone {
    int anchor_x{0};
    int anchor_y{0};
    int width{kPlayerBaseWidth};
    int height{kPlayerBaseDepth};
};

/** Axis-aligned rectangle merged into the board at a world offset. */
struct BoardRectModule {
    std::string id{"rect"};
    int width{1};
    int height{1};
    int offset_x{0};
    int offset_y{0};
};

/** Arbitrary tile set (jigsaw piece) in world coordinates. */
struct BoardCellModule {
    std::string id{"cells"};
    std::vector<std::pair<int, int>> cells;
};

/** Describes how to assemble a playable map and where bases belong. */
struct BoardLayoutSpec {
    std::vector<BoardRectModule> rects;
    std::vector<BoardCellModule> cell_modules;
    std::optional<PlayerBaseZone> base_zone_p1;
    std::optional<PlayerBaseZone> base_zone_p2;
    /** Seats 3 & 4 - only set on multi-base maps (e.g. the 2v2 map). */
    std::optional<PlayerBaseZone> base_zone_p3;
    std::optional<PlayerBaseZone> base_zone_p4;
    /** Deployment strip in front of each base where that seat may deploy units. */
    std::optional<BoardRectZone> deploy_zone_p1;
    std::optional<BoardRectZone> deploy_zone_p2;
    std::optional<BoardRectZone> deploy_zone_p3;
    std::optional<BoardRectZone> deploy_zone_p4;
    int nominal_width{0};
    int nominal_height{0};
    std::string layout_id;
    /** Treatment for off-map cells not listed in `off_board_cells`. */
    OffBoardTileTreatment off_board_default{OffBoardTileTreatment::Wall};
    /** Per-coordinate treatment for cells with no board tile (sparse; overrides `off_board_default`). */
    std::map<long long, OffBoardTileTreatment> off_board_cells;
};

/** Treatment for world cell `(x,y)` when it has no board tile; unspecified cells use `off_board_default`. */
inline OffBoardTileTreatment off_board_treatment_at(const BoardLayoutSpec& spec, int x, int y)
{
    const auto it = spec.off_board_cells.find(board_cell_key(x, y));
    if (it != spec.off_board_cells.end()) {
        return it->second;
    }
    return spec.off_board_default;
}

inline void set_off_board_cell(BoardLayoutSpec& spec, int x, int y, OffBoardTileTreatment treatment)
{
    spec.off_board_cells[board_cell_key(x, y)] = treatment;
}

inline int player_base_anchor_x(int board_width) { return (board_width - kPlayerBaseWidth) / 2; }

inline int player_base_anchor_y(int board_height, int player_id)
{
    return player_id == 1 ? 0 : board_height - kPlayerBaseDepth;
}

inline bool rect_zone_contains(const BoardRectZone& zone, int x, int y)
{
    return x >= zone.anchor_x && x < zone.anchor_x + zone.width && y >= zone.anchor_y && y < zone.anchor_y + zone.height;
}

inline std::optional<BoardRectZone> deploy_zone_for_player(const BoardLayoutSpec& spec, int player_id)
{
    switch (player_id) {
    case 1: return spec.deploy_zone_p1;
    case 2: return spec.deploy_zone_p2;
    case 3: return spec.deploy_zone_p3;
    case 4: return spec.deploy_zone_p4;
    default: return std::nullopt;
    }
}

/** World base-zone anchor for a seat (any of the up-to-4 seats), or nullopt. */
inline std::optional<PlayerBaseZone> base_zone_for_player(const BoardLayoutSpec& spec, int player_id)
{
    switch (player_id) {
    case 1: return spec.base_zone_p1;
    case 2: return spec.base_zone_p2;
    case 3: return spec.base_zone_p3;
    case 4: return spec.base_zone_p4;
    default: return std::nullopt;
    }
}

/** 4x2 deployment strip between base pad and arena (standard 8x12 duel only). */
inline std::optional<BoardRectZone> default_deploy_zone_for_player(int board_width, int board_height, int player_id)
{
    if (board_width != kStandardBoardWidth || board_height != kStandardBoardHeight) {
        return std::nullopt;
    }
    const int ax = player_base_anchor_x(board_width);
    if (player_id == 1) {
        return BoardRectZone{ax, kPlayerBaseDepth, kPlayerBaseWidth, kPlayerBaseDepth};
    }
    if (player_id == 2) {
        return BoardRectZone{ax, board_height - kPlayerBaseDepth - kPlayerBaseDepth, kPlayerBaseWidth, kPlayerBaseDepth};
    }
    return std::nullopt;
}

/** Single solid rectangle; no player bases. */
BoardLayoutSpec make_rect_board_layout(int width, int height);

/**
 * Default match map (8 wide x 12 tall):
 * - Center: 8x8 arena (rows 2-9)
 * - Bottom: 4x2 base pad centered (cols 2-5, rows 0-1) - not full-width rows
 * - Top: 4x2 base pad centered (cols 2-5, rows 10-11)
 */
BoardLayoutSpec make_default_map_layout();

/** Uses `make_default_map_layout()` for 8x12; otherwise a plain rectangle without bases. */
BoardLayoutSpec make_standard_duel_layout(int width, int height);

/**
 * 2v2 team map (16 wide x 8 tall): full rectangle with four 4x2 base pads - two along the top edge
 * (seats 1 & 3, spaced apart) and two along the bottom edge (seats 2 & 4). Deploy strips sit one row
 * in front of each base, leaving a central objective strip (rows 3-4) between the teams.
 */
BoardLayoutSpec make_2v2_map_layout();

/** Sandbox / test map: same 8x12 duel geometry with player bases and deploy strips. */
BoardLayoutSpec make_sandbox_map_layout();

/** Merge all modules from `spec` into `board`. Returns false on overlap conflicts. */
bool apply_board_layout(GameBoard& board, const BoardLayoutSpec& spec);

class GameState;

/** Terrain modifiers and LOS-blocking obstacles on the standard 8x12 duel map (no-op on other layouts). */
void seed_standard_duel_map_features(GameState& game);

/**
 * Seed the 2v2 map's objective tiles in the central strip (rows 3-4) - farther into the map than the
 * duel map's edge tiles. Per-type toggles let the pre-match settings enable/disable each objective.
 * No-op unless the game is on the 2v2 layout.
 */
void seed_2v2_map_objectives(GameState& game, bool scanner, bool omni, bool aether);

/** Seed the standard 8x12 duel map's objective tiles, per-type toggles. No-op off the duel layout. */
void seed_standard_duel_objectives(GameState& game, bool scanner, bool omni, bool aether);

/** Idempotently seeds permanent duel-map tiles (aether, scanner) when clusters are absent. */
void ensure_standard_duel_permanent_map_tiles(GameState& game);

/**
 * Place a pickup token at (wx, wy).  Fails silently if the cell is void, has damaging terrain,
 * or is already occupied.  `effect_key` is the registered effect handler fired when a unit
 * collects the pickup; empty = no effect.  `payload` is forwarded to the handler.
 * Returns true if the pickup was placed.
 */
bool place_map_pickup(GameState& game, int wx, int wy,
    const std::string& effect_key = {},
    const std::map<std::string, int>& payload = {});

}  // namespace tactics
