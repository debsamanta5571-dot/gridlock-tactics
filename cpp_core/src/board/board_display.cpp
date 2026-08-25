#include "tactics/board/board_display.hpp"

#include "tactics/board/board.hpp"

#include <stdexcept>

namespace tactics {

std::optional<std::pair<int, int>> parse_grid_cell_1based(int width, int height, const std::string& col_token,
                                                          const std::string& row_token) {
    if (width < 1 || height < 1) return std::nullopt;
    try {
        const int col1 = std::stoi(col_token);
        const int row1 = std::stoi(row_token);
        if (col1 < 1 || col1 > width || row1 < 1 || row1 > height) return std::nullopt;
        return std::pair{col1 - 1, row1 - 1};
    } catch (const std::logic_error&) {
        return std::nullopt;
    }
}

std::optional<std::pair<int, int>> parse_grid_cell_1based_world(const BoardCellBounds& bb, const std::string& col_token,
                                                                 const std::string& row_token) {
    if (bb.empty()) return std::nullopt;
    const int w = bb.span_x();
    const int h = bb.span_y();
    auto local = parse_grid_cell_1based(w, h, col_token, row_token);
    if (!local) return std::nullopt;
    return std::pair{bb.min_x + local->first, bb.min_y + local->second};
}

std::optional<int> parse_cli_index_1based(int max_slot, const std::string& token) {
    if (max_slot < 1) return std::nullopt;
    try {
        const int one_based = std::stoi(token);
        if (one_based < 1 || one_based > max_slot) return std::nullopt;
        return one_based - 1;
    } catch (const std::logic_error&) {
        return std::nullopt;
    }
}

}  // namespace tactics
