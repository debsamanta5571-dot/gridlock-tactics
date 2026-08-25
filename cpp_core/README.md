# Tactics C++ core

Engine-agnostic C++20 match rules. No Unreal types. Unreal compiles these same sources through `TacticsCore` shims.

- Grid, board, pathing, LOS
- Territories / energy zones
- Cards, decks, JSON catalogs
- Phase batch queue and effect registry
- Combat, aether objective, passives
- Turn manager and `GameState`
- Legal-action generator + MCTS bot
- Snapshots for host-authoritative LAN

## Layout

- `include/tactics/` public headers (`core/game_state.hpp` is the match object)
- `src/` implementation, mirroring the header tree
- `tests/aether_bot_test.cpp` aether kill tracking and bot "do not die on objective" checks
- `src/apps/bot_match.cpp` headless two-bot match
- `src/master_cli.cpp` command CLI

`include/tactics/core.hpp` is the umbrella header.

Board targeting for the Unreal HUD is computed here (`gather_ability_board_target_cells`, `gather_spell_board_target_cells`, `preview_effect_aoe_blast_cells`). The client only draws the cells.

## Build

```
cmake -S cpp_core -B cpp_core/build
cmake --build cpp_core/build --config Release
```

```
./cpp_core/build/Release/tactics_core_cli
./cpp_core/build/Release/tactics_master_cli
./cpp_core/build/Release/tactics_net_server
./cpp_core/build/Release/bot_match
./cpp_core/build/Release/aether_bot_test
```

From the repo root on Windows, `build_standalone.bat` builds those targets. `run_net_server.bat` launches the WebSocket host with `--content` pointed at `TacticsGameUnreal 5.8\Content`. `run_cli.bat` launches `tactics_master_cli`.
