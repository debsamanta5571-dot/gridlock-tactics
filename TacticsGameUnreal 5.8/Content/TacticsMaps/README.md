# Tactics maps and navigation

Map paths are configured in **`Config/DefaultGame.ini`** → `[/Script/TacticsGameUnreal.TacticsMapSettings]` (also editable via defaults on `UTacticsMapSettings`).

## Default flow

| Action | Target map | Game mode |
|--------|------------|-----------|
| Boot / main menu | `L_MainMenu` (`/Game/TacticsMaps/L_MainMenu`) | `TacticsMenuGameMode` (no 3D board) |
| Deck builder | `L_DeckBuilder` (falls back to `L_MainMenu` if missing) | `TacticsMenuGameMode` |
| Play vs AI / Host / Join | **`L_Match`** (`/Game/TacticsMaps/L_Match`) | `Tactics3DBoardGameMode` via `?game=` on `OpenLevel` |

`MatchMapFallbacks` are tried if the primary match map is unavailable (`L_Match`, then TopDown).

## Separate maps

The game **does not** run menu and match on the same level anymore:

- **Menu** loads `L_MainMenu` at startup (`GameDefaultMap` in `DefaultEngine.ini`).
- **Play** calls `OpenLevel` to travel to **L_Match** with the 3D match game mode.
- **Back to menu** (deck builder) travels back to `L_MainMenu`.

Deck choice is applied when the match map finishes loading (`UTacticsGameInstance::OnWorldChanged`).

## Debug

The main menu status line shows deck apply results and the last **map resolve / OpenLevel** message from `GetLastMapTravelDebug()`.

## Optional dedicated levels

Duplicate `L_MainMenu` or TopDown into this folder if you want custom lighting for menu, deck builder, or a project-local match map. Point `MainMenuMap`, `DeckBuilderMap`, or `MatchMap` at them in config.
