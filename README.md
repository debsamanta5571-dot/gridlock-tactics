# Gridlock Tactics

This is a work in progress prototype for a multiplayer tactical card game me and my friends are making. Of course they wanted the comp sci major to actually program everything while they think of "ideas" nevertheless its going sort of smoothly even if the UI is kind of a mess.

## Preview

![Gameplay](preview.gif)

## Reasoning

The real purpose of this is to hook it up to a physical smart board later. The board would register the positions of the pieces straight into the software over WebSockets.

The rules are not Unreal objects. They are a separate C++ module loaded in separately. The purpose for even having a C++ program is so you will be able to run a small server with a microcomputer. Of course you can also just launch it with Unreal and play on a 3D board.

Unreal sends commands to its own running C++ instance and draws the 3D board from that. There is a board view and a battle view right now. All of the art assets are still placeholders.

## Architecture

This is built as a **rules engine plus a client**, not as an Unreal Blueprint game.

**C++20 core (`cpp_core`).** All match logic lives in a standalone library with no `UObject` types: grid and multi-tile footprints, A* pathing, line of sight, territories and energy (including tagged flux pools), card instances, combat (melee / ranged, armor, magic resist, counterattack), status effects, a turn manager, and a phase batch queue. Speeds are Channeled, Reflex, and Blazing. There is one `tactics::GameState`. Win condition is destroying the enemy base.

**Same sources in Unreal.** Those `.cpp` files are compiled into UE 5.8 through one-line UBT shims (`TacticsCore/Private/CppCoreStub_*.cpp`). CMake and Unreal Build Tool stay aligned with `scripts/generate_cpp_core_stubs.py` (including a `--check` mode). Slate, the 3D board, Host LAN, the headless server, and the C++ join client all call `dispatch_master_cli_line`. The GUI does not reimplement the rules.

**Data-driven content.** Units, spells, abilities, passives, and decks are JSON under `Content/TacticsData/`. Catalogs load at runtime. A constructed list is 40 main cards, 5 reserves, 20 territories. New cards are content, not engine classes.

**AI.** Play vs AI is Monte Carlo Tree Search over the same `GameState` a human uses. Legal moves are enumerated by a generator (deploy, move, attack, spells, abilities, lands). Only attacks that already pass range, LOS, and validation are considered.

Leaf positions are scored with a linear model: `value = clamp(dot(features, weights), -1, 1)` from the AI's seat. Default weights are the hand-tuned heuristic; they can be swapped at runtime from a file (`TACTICS_BOT_WEIGHTS`) without a recompile. Terminal positions use the real match outcome (base destroyed, sudden-death base-HP tiebreak).

The scorer is built around two axes, because this game is a card economy and a tactics board at the same time:

- **Win condition first.** Base HP difference `/ 30` is the dominant term. The AI is trying to kill the enemy base, not farm a unit count.
- **Card / tempo.** Hand difference, spendable float (it expires at end of turn), flux, remaining deck count, and remaining deck quality. Idle energy is wasted tempo.
- **Piece value, not headcount.** Each unit or structure is `HP + attack + movement*0.5 + ranged range*0.6 + keyword premia + ability optionality + engine passives + status`. A 22/22 Sentinel is not scored like a 1/1 token. Value engines (spawners, energy generators, auras) are weighted higher so they are not traded for junk. Status is in the same number: armor and boosts add; stun / silence / jammed / DoTs / overload subtract; evasive adds.
- **Position.** Advancement toward the enemy base (Chebyshev, normalized). Penalty if enemy units sit within 2 tiles of our base, weighted by their attack. Objective tiles (scanner, omni-energy, aether) have hold values; capturing or contesting them is scored, piling extra bodies onto an already-contested tile is not.
- **Attack policy.** Target `piece_value` (prefer the Sentinel). Lethality bonus for a kill vs chip. Expected-hit fraction for evasive and low cover (unless trueshot/flying). Counterattack risk, including not dying for a token. The base stays the top target (it does not counter).
- **Spells and abilities.** Immediate effect (damage/debuff on enemies scaled by target value, heals/buffs on allies that need them) vs future value (hold Reflex mana for the opponent's turn, spend expiring float now, wait for 2+ AoE targets). Deploy scoring includes `card_engine_future_value` so ramp cards are worth playing before they do anything the turn they land.

Search clones the match, rolls MCTS, and picks from that. No separate Unreal AI.

**Networking.** Host-authoritative WebSocket (default port 8788, wire version 4). The host owns `GameState`. Clients send command strings; the host broadcasts snapshots and JSON-patch deltas, plus a command journal. Optional room-token auth is HMAC-SHA256 over seat, counter, and line (replay-protected). Optional in-process TLS if built with OpenSSL. `tactics_net_server` is a headless host (P1 is server authority, remotes are P2+). `tactics_net_client` is a text join client for the same socket. That path is for a later microcomputer / smart-board / web GUI.

**Unreal client.** UE 5.8: Slate HUD, 3D board actors, combat visualization, deck builder, Play vs AI, Host LAN / Join.

**Build.** C++20, CMake, MSVC, Unreal 5.8 / UBT. Headless core builds and tests without the editor (`build_standalone.bat`, `aether_bot_test`).

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
