#pragma once

#include "tactics/entities/entity.hpp"

#include <memory>

namespace tactics::bot {

/** Mirrors master CLI session state (`controlled_player`, `selected_unit`). */
struct BotSession {
    int controlled_player{1};
    std::shared_ptr<Unit> selected_unit;
};

}  // namespace tactics::bot