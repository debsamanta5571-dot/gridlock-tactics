# Gridlock Tactics

Gridlock Tactics is a tactical card game my friends and I are working on. I’m the one writing the code. Matches happen on an 8x12 board. Take territories, spend the energy on cards and units, and try to destroy the other team’s base.

## Preview

![Gameplay](preview.gif)

## Reasoning

The real purpose of this is to hook it up to a physical smart board later. The board would register the positions of the pieces straight into the software over WebSockets.

The rules are not Unreal objects. They are a separate C++ module loaded in separately. The purpose for even having a C++ program is so you will be able to run a small server with a microcomputer. Of course you can also just launch it with Unreal and play on a 3D board.

Unreal sends commands to its own running C++ instance and draws the 3D board from that. There is a board view and a battle view right now. All of the art assets are still placeholders.

## Architecture

The match is a C++20 library (`cpp_core`). Unreal Engine 5.8 draws it. Nothing in the rules is a `UObject`. One `tactics::GameState` owns the board, energy, cards, combat, and turns. The win condition is destroying the other base.

Those same `.cpp` files compile into Unreal through one-line UBT shims (`CppCoreStub_*.cpp`). A Python script (`generate_cpp_core_stubs.py`) keeps CMake and Unreal Build Tool on the same source list, including a `--check` mode. Slate, the 3D board, Host LAN, the headless server, and the C++ join client all send the same command strings into `dispatch_master_cli_line`. The GUI does not have a second copy of the rules.

The library covers an 8x12 grid with multi-tile units, A* pathing, line of sight, territories and energy (including flux, which only pays for spells and abilities), a turn manager, and a phase batch queue. Spells and abilities are Channeled, Reflex, or Blazing. Combat is melee or ranged, with armor, magic resist, and counterattacks.

Cards, abilities, passives, and decks are JSON under `Content/TacticsData/`. Catalogs load at runtime. A legal list is 40 cards, 5 reserves, and 20 territories. Adding a card is a data change, not a new Unreal class.

**AI.** Play vs AI is Monte Carlo Tree Search on that same `GameState`. A generator lists legal deploys, moves, attacks, spells, abilities, and land uses. Attacks that fail range, line of sight, or validation never enter the tree.

Each leaf gets a score in `[-1, 1]` from the AI's seat: a dot product of features and weights, then clamped. The default weights are hand-tuned. They can be replaced at runtime from a file (`TACTICS_BOT_WEIGHTS`) without rebuilding. If the match is already over, the score is the real result (base destroyed, or sudden-death base health).

The features treat this as both a card game and a tactics game.

Base health comes first (`difference / 30`). That is the win condition, so it outweighs everything else. After that: cards in hand, float energy (it expires at end of turn), flux, how many cards are left in the deck, and how strong that remaining deck is.

Units are not counted as equals. Each piece is scored from HP, attack, movement, ranged reach, keywords, activated abilities, engine passives, and status. A large Sentinel is worth more than a 1/1 token. Spawners, energy generators, and auras are worth more still, so the AI does not trade them away cheaply. Armor and damage boosts add to the number. Stun, silence, jammed, damage-over-time, and overload subtract. Evasive adds, because half the attacks miss.

Position is in there too. Units get credit for closing on the enemy base. Enemy units within two tiles of our base are a penalty, scaled by their attack. Scanner, omni-energy, and aether tiles have hold values. Taking or contesting one scores. Stacking extra units onto a tile that is already contested does not.

When it attacks, it prefers high `piece_value` targets and pays extra for a kill rather than chip damage. Evasive targets and low cover cut the expected value in half unless the attacker has trueshot or flying. It accounts for the counterattack, including lines where our unit dies for almost nothing. The enemy base stays the best target because it never hits back.

Spells and abilities mix what happens now with what should wait. Damage and debuffs on a valuable enemy score high. Heals and buffs score when an ally actually needs them. Reflex energy is often held for the opponent's turn. Float that would expire is spent now. Wide area effects wait for two or more targets. Deploy scoring includes future engine value, so a ramp card can be worth playing before it does anything the turn it lands.

Search clones the match, runs MCTS, and takes the chosen action. There is no separate Unreal AI.

**Networking.** The host owns `GameState`. Clients talk over WebSocket (port 8788, wire version 4). They send command strings. The host replies with full snapshots, JSON-patch deltas, and a command journal. A room token, if set, is HMAC-SHA256 over seat, a rising counter, and the line, so captured frames cannot be replayed. TLS is optional if the server is built with OpenSSL. `tactics_net_server` is the headless host (P1 is the server, joiners are P2+). `tactics_net_client` joins that same socket in text. That is the path for a later microcomputer, smart board, or web UI.

**Unreal client.** UE 5.8: Slate HUD, 3D board, combat visualization, deck builder, Play vs AI, Host LAN, and Join.

**Build.** C++20, CMake, MSVC, Unreal 5.8 / UBT. The core builds and tests without the editor (`build_standalone.bat`, `aether_bot_test`).

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
