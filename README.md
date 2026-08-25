# Gridlock Tactics

This is a work in progress prototype for a multiplayer tactical card game me and my friends are making. Of course they wanted the comp sci major to actually program everything while they think of "ideas" nevertheless its going sort of smoothly even if the UI is kind of a mess.

## Preview

![Gameplay](preview.gif)

## Architecture

The true purpose of this is to eventually integrate it with some sort of real world smart board, where it can register game and smart pieces straight to the software with websockets(change sofware_
Technically its not actually made with c++ unreal objects but it is a separate c++ module loaded into whats effectively a GUI. The C++ standaolone is meant to be able to create small portable servers with micro ccomputers. It can also just work standalone with unreal with a 3d board gui visual. Currrently there is a board and a battle visualization however the art assets are currently all place holders.. Unreal works by effectively sending cmds lines to its own running c++ instance with the 3d board gui/

# Instructions
If you want to actually try this game, (i don't think its fully stable yet, the c++ is fine but the unreal gui still needs a lot of polishing) you will need to read the instructions its fairly complex"

## Open

Needs **Unreal Engine 5.8**. Open `TacticsGameUnreal 5.8/TacticsGameUnreal.uproject` and press Play.

- **Play vs AI** (you are seat 1, MCTS is seat 2)
- **Host LAN** / **Join** (port, default 8788)

Editor rebuild (`build_ue.bat` expects the engine at `E:\UE_5.8`; change that path if yours is different):

```
.\build_ue.bat
```

Standalone C++ host / CLI (no Unreal). These bats build the exe if it is missing and pass `--content` at `TacticsGameUnreal 5.8\Content` so the card JSON actually loads:

```
.\build_standalone.bat
.\run_net_server.bat
.\run_net_server_lan.bat
.\run_cli.bat
```

Unreal clients Join `ws://127.0.0.1:8788/`. `run_net_server_lan.bat` binds `0.0.0.0` for LAN. Extra args (`--port`, `--token`) go through.

This C++ host is the portable-server path (eventual web / smart-board client). For just playing the game, use Unreal Host LAN instead.

Packaged desktop build (stays local, not in this repo):

```
.\scripts\package_win64_desktop.ps1
```

Output: `Packaged\Win64\TacticsGameUnreal.exe`

#Made with C++ and Unreal 5.7/ 5.8
