#pragma once

#include <cstddef>

#include <optional>
#include <string>
#include <utility>

namespace tactics {

struct BoardCellBounds;

/** Distinct fill RGB for entity/unit plots by seat id (wraps for ids beyond palette length). Neutral when seat_id < 1. */
struct PlayerSeatRgb {
    unsigned char r{};
    unsigned char g{};
    unsigned char b{};
};

inline PlayerSeatRgb rgb_for_player_seat(int seat_id) {
    static constexpr unsigned char palette[][3] = {
        {231, 76, 60},   // P1
        {52, 152, 219},  // P2
        {46, 204, 113},  // P3
        {241, 196, 15},  // P4
        {155, 89, 182},
        {230, 126, 34},
        {26, 188, 156},
        {211, 84, 0},
    };
    static constexpr unsigned char neutral[] = {44, 62, 80};
    if (seat_id < 1) return {neutral[0], neutral[1], neutral[2]};
    const size_t n = sizeof(palette) / sizeof(palette[0]);
    const size_t i = static_cast<size_t>(seat_id - 1) % n;
    return {palette[i][0], palette[i][1], palette[i][2]};
}

/** Inset from cell edge to drawn entity rectangle (pixels), each side. */
inline constexpr int kBoardEntityPixelInset = 2;
/** Win32 plot and PPM both use inner size `cell_px - kBoardEntityInnerExtentDelta`. */
inline constexpr int kBoardEntityInnerExtentDelta = 4;

inline int cell_fill_extent_px(int cell_px) { return cell_px - kBoardEntityInnerExtentDelta; }

/**
 * Parse 1-based user coordinates (column then row, e.g. CLI "1".."width") into 0-based game grid indices.
 * Row 1 is the bottom band in game space (game_y == 0); this only validates numeric range, not occupancy.
 */
std::optional<std::pair<int, int>> parse_grid_cell_1based(int width, int height, const std::string& col_token,
                                                          const std::string& row_token);

/**
 * Like `parse_grid_cell_1based` over `bb.span_x()` × `bb.span_y()`, but returns **world** `(x,y)` with origin at `bb.min_*`.
 * Row 1 is the lowest world `y` within the bounds (south / bottom of the drawn rectangle).
 */
std::optional<std::pair<int, int>> parse_grid_cell_1based_world(const BoardCellBounds& bb, const std::string& col_token,
                                                                 const std::string& row_token);

/**
 * 1-based list slot (e.g. hand index shown as "1." in CLI) -> 0-based index in [0, max_slot).
 */
std::optional<int> parse_cli_index_1based(int max_slot, const std::string& token);

/** Screen row index measured from the top of the board (0 = top row of cells). Game y=0 is bottom. */
inline int game_y_to_screen_row_top_origin(int height_cells, int game_y) { return height_cells - 1 - game_y; }

inline int cell_left_pixels(int margin_px, int cell_px, int game_x, int inset = kBoardEntityPixelInset) {
    return margin_px + game_x * cell_px + inset;
}

inline int cell_top_pixels(int margin_px, int cell_px, int height_cells, int game_y, int inset = kBoardEntityPixelInset) {
    return margin_px + game_y_to_screen_row_top_origin(height_cells, game_y) * cell_px + inset;
}

/** Log / print: 0-based game cell -> 1-based user coordinates. */
inline std::pair<int, int> game_cell_to_display_1based(int game_x, int game_y) { return {game_x + 1, game_y + 1}; }

}  // namespace tactics
