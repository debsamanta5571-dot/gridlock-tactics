#pragma once

#include "tactics/core/game_state.hpp"

#include <nlohmann/json.hpp>
#include <string>

namespace tactics {

/** Accept absent wire version; reject present-but-mismatched `v` on network frames. */
inline bool wire_version_ok(const nlohmann::json& root, std::string& err)
{
    if (!root.contains("v")) {
        return true;
    }
    const int wire_v = root["v"].is_number_integer() ? root["v"].get<int>() : static_cast<int>(root["v"].get<double>());
    if (wire_v != kNetworkWireVersion) {
        err = "unsupported wire version " + std::to_string(wire_v);
        return false;
    }
    return true;
}

}  // namespace tactics