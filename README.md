# Gridlock Tactics

This is a work in progress prototype for a multiplayer tactical card game me and my friends are making. Of course they wanted the comp sci major to actually program everything while they think of "ideas" nevertheless its going sort of smoothly even if the UI is kind of a mess.

## Preview

![Gameplay](preview.gif)

## Architecture

The real purpose of this is to hook it up to a physical smart board later. The board would register the positions of the pieces straight into the software over WebSockets.

The rules are not Unreal objects. They are a separate C++ module loaded in separately. The purpose for even having a C++ program is so you will be able to run a small server with a microcomputer. Of course you can also just launch it with Unreal and play on a 3D board.

Unreal sends commands to its own running C++ instance and draws the 3D board from that. There is a board view and a battle view right now. All of the art assets are still placeholders.

## Downloads

- [Unreal 3D client (Win64)](https://github.com/debsamanta5571-dot/gridlock-tactics/releases/latest/download/GridlockTactics-Unreal-Win64.zip)
- [Standalone C++ host + CLI (Win64)](https://github.com/debsamanta5571-dot/gridlock-tactics/releases/latest/download/GridlockTactics-Cpp-Win64.zip)
- [How to play (PDF)](docs/HOW_TO_PLAY.pdf)

## Unreal build

Unzip the Unreal zip and run `TacticsGameUnreal.exe`. Keep that whole folder together (`Engine` + `TacticsGameUnreal`).

- Play vs AI
- Host LAN / Join (default port 8788)

## C++ server

I don't really recommend this for actually playing. Its not tested yet and It's meant to eventually have a web gui talk to it (and later the smart board). If you just want a match, use Host LAN in the packaged game.

Download the C++ zip, unzip it, then double-click one of these:

- `run_net_server.bat` — headless host on this machine. In the Unreal client, Join `ws://127.0.0.1:8788/`
- `run_net_server_lan.bat` — same thing, but other PCs on the LAN can join
- `run_cli.bat` — text match, no Unreal at all

Made with C++ and Unreal 5.7/ 5.8
