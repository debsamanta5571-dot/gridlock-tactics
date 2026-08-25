#pragma once

#include <string>

namespace tactics {

class GameState;
struct Deck;

/** True when `game_id` contains the substring `sandbox` (case-sensitive). */
bool game_id_is_sandbox(const std::string& game_id);

/** One copy of every catalog unit/spell card (skips footprint-test runtime keys). */
Deck create_sandbox_deck_from_catalog();

/**
 * One copy of every catalog unit/spell card whose colored energy cost includes `color_key`.
 * Accepts color name or chroma alias (e.g. gallantry, green, ingenuity, orange).
 * Returns an empty deck when the key is not a recognized color.
 */
Deck create_sandbox_deck_for_color(const std::string& color_key);

/**
 * One copy of every catalog unit/spell card belonging to a faction (`set_code`).
 * Faction keys (case-insensitive):
 *   "99th_dieselheart_company" / "dieselheart"  → 99th Dieselheart Company
 *   "asterian_civilian_militia" / "asterian"    → Asterian Civilian Militia
 *   "the_lost_kingdom" / "lost_kingdom"         → The Lost Kingdom
 *   "core"       → core set (no set_code)
 *   ""  / "all"  → all cards (same as create_sandbox_deck_from_catalog)
 *
 * Legacy color-name shortcuts still map to their primary faction deck for CLI compat:
 *   gallantry → 99th, ingenuity → ACM, mythology → Lost Kingdom.
 * Use `create_sandbox_deck_for_color` for cross-faction color pools (e.g. green).
 */
Deck create_sandbox_deck_for_faction(const std::string& faction_key);

/**
 * Replaces a sandbox player's entire deck (pool + zones) with one faction's cards, all in hand.
 * Returns false when `faction_key` is empty/"all" or unrecognised.
 */
bool apply_sandbox_faction_deck_to_player(GameState& game, int player_id, const std::string& faction_key,
    std::string* err_out = nullptr);

/** Applies the same faction deck to every seated player (separate card instances per player). */
bool apply_sandbox_faction_deck_to_all_players(GameState& game, const std::string& faction_key,
    std::string* err_out = nullptr);

/** Pre-placed territories per sandbox seat (ACM mix: unlimited basics + 3 of each unique). */
inline constexpr int kSandboxTerritoriesPerPlayer = 40;
/** Copies of each non-basic ACM territory in sandbox (basics fill the remainder). */
inline constexpr int kSandboxUniqueTerritoryCopies = 3;

/** Pre-placed ACM territory row for one player (default 40 total). */
void master_cli_seed_sandbox_territories(GameState& game, int player_id,
    int territory_count = kSandboxTerritoriesPerPlayer);

/**
 * Seeds the sandbox board: ACM territories per player, all catalog cards moved to hand,
 * and three target dummies (10 HP, 1 melee damage) in the arena center for practice.
 */
void master_cli_seed_sandbox_state(GameState& game);

}  // namespace tactics