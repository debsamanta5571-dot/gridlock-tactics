#include "tactics/bot/bot_game_clone.hpp"

#include <nlohmann/json.hpp>

namespace tactics::bot {

std::unique_ptr<GameState> clone_game_from_snapshot_utf8(const std::string& snapshot_utf8)
{
    std::unique_ptr<GameState> cloned;
    std::string err;
    if (!load_game_from_snapshot_utf8(cloned, snapshot_utf8, err) || !cloned) {
        return nullptr;
    }
    cloned->set_combat_visualization_enabled(false);
    return cloned;
}

std::unique_ptr<GameState> clone_game_from_snapshot_json(const nlohmann::json& snapshot_root)
{
    std::unique_ptr<GameState> cloned;
    std::string err;
    if (!load_game_from_snapshot_json(cloned, snapshot_root, err) || !cloned) {
        return nullptr;
    }
    cloned->set_combat_visualization_enabled(false);
    return cloned;
}

std::unique_ptr<GameState> clone_game_for_search(const GameState& src)
{
    return clone_game_from_snapshot_utf8(src.build_match_snapshot_utf8());
}

}  // namespace tactics::bot
