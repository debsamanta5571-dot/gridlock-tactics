# Gridlock Tactics

This is a work in progress prototype for a multiplayer tactical card game me and my friends are making. Of course they wanted the comp sci major to actually program everything while they think of "ideas" nevertheless its going sort of smoothly even if the UI is kind of a mess.

## Preview

![Gameplay](preview.gif)

## Reasoning

The real purpose of this is to hook it up to a physical smart board later. The board would register the positions of the pieces straight into the software over WebSockets.

The rules are not Unreal objects. They are a separate C++ module loaded in separately. The purpose for even having a C++ program is so you will be able to run a small server with a microcomputer. Of course you can also just launch it with Unreal and play on a 3D board.

Unreal sends commands to its own running C++ instance and draws the 3D board from that. There is a board view and a battle view right now. All of the art assets are still placeholders.

## Architecture

Match rules live in a standalone **C++20** library (`cpp_core`). Unreal Engine 5.8 is the client (Slate HUD, 3D board, LAN). The editor does not reimplement the rules. The same `.cpp` files are compiled into Unreal through one-line UBT shims (`CppCoreStub_*.cpp`). CMake and Unreal Build Tool stay in sync via `scripts/generate_cpp_core_stubs.py`.

There is one `tactics::GameState`. Slate, the 3D board, the headless WebSocket host (`tactics_net_server`), and the C++ join client all drive it with the same command strings.

- **C++ core:** board, pathing, LOS, territories / energy, cards, combat, turn manager, phase batch queue (Channeled / Reflex / Blazing), snapshots
- **Content:** JSON catalogs for cards, abilities, passives, and decks. New cards are data, not engine objects
- **Bot:** legal-action generator + MCTS opponent (Play vs AI)
- **Unreal:** UE 5.8 GUI. Host LAN / Join on WebSocket (default port 8788). Host is authority; clients send commands and apply snapshots
- **Headless net:** `tactics_net_server` plus `tactics_net_client` so a microcomputer can host without Unreal, for a later smart-board / web client
- **Language / build:** C++20, CMake, MSVC, Unreal 5.8 / UBT

## Downloads

- [Unreal 3D client (Win64)](https://github.com/debsamanta5571-dot/gridlock-tactics/releases/latest/download/GridlockTactics-Unreal-Win64.zip)
- [Standalone C++ host + join client (Win64)](https://github.com/debsamanta5571-dot/gridlock-tactics/releases/latest/download/GridlockTactics-Cpp-Win64.zip)
- [How to play (PDF)](docs/HOW_TO_PLAY.pdf)

## Unreal build

Unzip the Unreal zip and run `TacticsGameUnreal.exe`. Keep that whole folder together (`Engine` + `TacticsGameUnreal`).

- Play vs AI
- Host LAN / Join (default port 8788)

## C++ server

I don't really recommend this for actually playing. Its not tested yet and It's meant to eventually have a web gui talk to it (and later the smart board). If you just want a match, use Host LAN in the packaged game.

Download the C++ zip, unzip it, then:

- `run_net_server.bat` - headless host on this machine
- `run_net_server_lan.bat` - same host, other PCs on the LAN can join
- `run_net_client.bat` - text join client. Start the server first, then this. Type help.

Unreal can still Join the same host at `ws://127.0.0.1:8788/`.

Made with C++ and Unreal 5.7/ 5.8
