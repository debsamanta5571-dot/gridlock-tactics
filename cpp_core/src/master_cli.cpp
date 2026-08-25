#include "tactics/apps/master_cli_dispatch.hpp"
#include "tactics/core.hpp"  // GameState

#include "apps/native_plot_win.hpp"

#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

constexpr int kDefaultCliDemoBoard = 8;

}  // namespace

int main(int argc, char** argv) {
    bool footprint_test_deck = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--footprint-test") == 0) {
            footprint_test_deck = true;
        }
    }
#ifdef _WIN32
    wchar_t module[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, module, MAX_PATH);
    if (n > 0 && n < MAX_PATH) {
        std::error_code ec;
        std::filesystem::current_path(std::filesystem::path(module).parent_path(), ec);
    }
#endif
    const std::string gid = footprint_test_deck ? "master_cli_footprint_test" : "master_cli";
    tactics::GameState game(gid, kDefaultCliDemoBoard, kDefaultCliDemoBoard);
    for (int i = 1; i <= tactics::kDefaultDemoSeatCount; ++i) {
        game.add_player(i, "Player " + std::to_string(i));
    }
    tactics::master_cli_seed_demo_state(game);
    game.start_game();

    int controlled = 1;
    std::shared_ptr<tactics::Unit> selected{};

    tactics::MasterCliPlotSink plot_sink;
#ifdef _WIN32
    plot_sink.plot_board = [](const tactics::GameState& g) { tactics::apps::plot_board_native(g); };
#endif

    std::cout << "Master CLI (engine agnostic)";
    if (footprint_test_deck) {
        std::cout << "  [--footprint-test: six multi-tile unit cards for deploy/rotate testing]";
    }
    std::cout << "\n";
    tactics::master_cli_print_help(game, std::cout);
    tactics::master_cli_write_board_ppm(game, std::cout);
    std::cout << "Board snapshot written (see path above)\n";

    for (std::string line; std::cout << "\n> ", std::getline(std::cin, line);) {
        if (tactics::dispatch_master_cli_line(game, controlled, selected, line, std::cout, plot_sink)) break;
    }

    return 0;
}
