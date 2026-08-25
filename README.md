# Gridlock Tactics

This is a work in progress prototype for a multiplayer tactical card game me and my friends are making. Of course they wanted the comp sci major to actually program everything while they think of "ideas" nevertheless its going sort of smoothly even if the UI is kind of a mess.

## Preview

![Gameplay](preview.gif)

## Architecture

## Architecture

The real purpose of this is to hook it up to a physical smart board later. The board would register the positions of the pieces straight into the software over WebSockets.

The rules are not Unreal objects. They are a separate C++ module loaded in separately. The purpose for even having a C++ program is so you will be able to run a small server with a microcomputer. Of course you can also just launch it with Unreal and play on a 3D board.

Unreal sends commands to its own running C++ instance and draws the 3D board from that. There is a board view and a battle view right now. All of the art assets are still placeholders.

# Instructions

If you want to actually try this game, (i don't think its fully stable yet, the c++ is fine but the unreal gui still needs a lot of polishing) you do not need Unreal Engine installed.

## Unreal build

Run `Packaged\Win64\TacticsGameUnreal.exe`. No editor, no engine install.

- Play vs AI
- Host LAN / Join (default port 8788)

## C++ server

I don't really recommend this for actually playing. It's meant to eventually have a web gui talk to it (and later the smart board). If you just want a match, use Host LAN in the packaged game.

If you still want the headless host:

```
.\build_standalone.bat
.\run_net_server.bat
```

That builds `tactics_net_server` if it isn't there yet and loads cards from `TacticsGameUnreal 5.8\Content`. From the packaged game, Join `ws://127.0.0.1:8788/`.

LAN bind (0.0.0.0):

```
.\run_net_server_lan.bat
```

Extra args go through (`--port 9000`, `--token secret`).

Interactive C++ match with no Unreal at all:

```
.\run_cli.bat
```

Made with C++ and Unreal 5.7/ 5.8
