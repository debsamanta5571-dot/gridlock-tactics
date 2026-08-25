# Gridlock Tactics

Unreal Engine 5.8 tactical card game. Two players (or you vs the bot) fight on an 8x12 board: play territories for energy, deploy units, resolve combat, destroy the enemy base.

The match rules are a standalone C++20 library. Unreal is the client (3D board, Slate HUD, LAN). There is one `tactics::GameState`. The editor does not reimplement the rules.

## Preview

![Gameplay](preview.gif)

Asteria pick, deploy a unit, move a unit, Shocking Stimulus green resolve, Sentinel CRIT. About 12 seconds, looping.

## Architecture

```
Slate HUD / 3D board actors
WebSocket host-join client      -->  UTacticsMatchSubsystem  -->  tactics::GameState
Headless CLI / bot_match.exe
```

`cpp_core/` is a normal C++ library (CMake). Unreal compiles the **same** `.cpp` files through one-line shims in `TacticsCore` (`CppCoreStub_*.cpp`). CMake and Unreal Build Tool stay in sync via `scripts/generate_cpp_core_stubs.py`.

```
cpp_core/src/core/game_state.cpp
        ^
        |  #include "core/game_state.cpp"
        |
TacticsCore/Private/CppCoreStub_game_state.cpp   (UBT translation unit)
```

## Open

Needs **Unreal Engine 5.8**. Open `TacticsGameUnreal 5.8/TacticsGameUnreal.uproject` and press Play.

- **Play vs AI** (you are seat 1, MCTS is seat 2)
- **Host LAN** / **Join** (port, default 8788)

Or build the editor target (`build_ue.bat` expects the engine at `E:\UE_5.8`; change that path if yours is different):

```
.\build_ue.bat
```

Headless rules core:

```
cmake -S cpp_core -B cpp_core/build
cmake --build cpp_core/build --config Release
.\cpp_core\build\Release\bot_match.exe
.\cpp_core\build\Release\aether_bot_test.exe
```

Stub sync check (no Unreal):

```
python scripts/generate_cpp_core_stubs.py --check
```

## Layout

```
cpp_core/                 C++20 rules: board, cards, combat, bot, snapshots
TacticsGameUnreal 5.8/    UE 5.8 client: Slate, 3D board, host/join
scripts/                  stub generator, catalog helpers, Win64 package
```
