#pragma once

#include "tactics/core.hpp"

namespace tactics::apps {

// Opens a native Win32 board window and blocks until closed. Uses merged `board_cell_bounds()` (fallback: main module size).
void plot_board_native(const GameState& game);

}  // namespace tactics::apps
