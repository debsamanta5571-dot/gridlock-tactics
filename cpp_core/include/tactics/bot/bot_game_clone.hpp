#pragma once

#include "tactics/core/game_state.hpp"

#include <nlohmann/json_fwd.hpp>

#include <memory>

namespace tactics::bot {

/** Deep-clone via authoritative match snapshot (same path as multiplayer restore). */
std::unique_ptr<GameState> clone_game_for_search(const GameState& src);

/** Restore from a snapshot captured once per search (avoids re-serializing each sim). */
std::unique_ptr<GameState> clone_game_from_snapshot_utf8(const std::string& snapshot_utf8);

/** Restore from a pre-parsed snapshot DOM - parse the root snapshot once per decision,
 *  then clone per simulation without re-lexing the JSON. */
std::unique_ptr<GameState> clone_game_from_snapshot_json(const nlohmann::json& snapshot_root);

}  // namespace tactics::bot
