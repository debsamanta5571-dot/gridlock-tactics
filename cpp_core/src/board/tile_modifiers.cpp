#include "tactics/board/aether.hpp"
#include "tactics/board/scanner.hpp"
#include "tactics/board/omni_energy_tile.hpp"
#include "tactics/board/tile_modifiers.hpp"

#include <algorithm>

namespace tactics {

const SquareModifier* square_terrain_modifier(const GridSquare& sq)
{
    for (const auto& m : sq.modifiers) {
        if (m.layer == TileLayer::Terrain) return &m;
    }
    return nullptr;
}

const SquareModifier* square_overlay_modifier(const GridSquare& sq)
{
    for (const auto& m : sq.modifiers) {
        if (m.layer == TileLayer::Overlay) return &m;
    }
    return nullptr;
}

bool set_terrain_modifier(GridSquare& sq, SquareModifier mod)
{
    mod.layer = TileLayer::Terrain;
    for (auto& m : sq.modifiers) {
        if (m.layer == TileLayer::Terrain) {
            if (m.name == kAetherModifierName || m.name == kScannerModifierName || m.name == kOmniEnergyModifierName) {
                return false;
            }
            m = std::move(mod);
            return true;  // replaced
        }
    }
    sq.modifiers.push_back(std::move(mod));
    return false;  // fresh add
}

bool set_overlay_modifier(GridSquare& sq, SquareModifier mod)
{
    mod.layer = TileLayer::Overlay;
    for (auto& m : sq.modifiers) {
        if (m.layer == TileLayer::Overlay) {
            m = std::move(mod);
            return true;  // replaced
        }
    }
    sq.modifiers.push_back(std::move(mod));
    return false;  // fresh add
}

bool clear_overlay_modifier(GridSquare& sq)
{
    const auto it = std::find_if(sq.modifiers.begin(), sq.modifiers.end(),
        [](const SquareModifier& m) { return m.layer == TileLayer::Overlay; });
    if (it == sq.modifiers.end()) return false;
    sq.modifiers.erase(it);
    return true;
}

bool clear_terrain_modifier(GridSquare& sq)
{
    const auto it = std::find_if(sq.modifiers.begin(), sq.modifiers.end(),
        [](const SquareModifier& m) { return m.layer == TileLayer::Terrain; });
    if (it == sq.modifiers.end()) return false;
    if (it->name == kAetherModifierName || it->name == kScannerModifierName || it->name == kOmniEnergyModifierName) {
        return false;
    }
    sq.modifiers.erase(it);
    return true;
}


bool merge_overlay_modifier(GridSquare& sq, SquareModifier mod)
{
    mod.layer = TileLayer::Overlay;
    // nullopt means infinite duration - always "longer" than any finite value.
    auto lasts_longer = [](const std::optional<int>& a, const std::optional<int>& b) -> bool {
        if (!a.has_value()) return true;   // infinite always lasts longer
        if (!b.has_value()) return false;  // finite < infinite
        return *a > *b;
    };
    for (auto& m : sq.modifiers) {
        if (m.layer == TileLayer::Overlay) {
            if (m.name == mod.name) {
                // Same overlay type: keep whichever duration is longer.
                if (lasts_longer(mod.duration, m.duration)) {
                    m.duration = mod.duration;
                    m.owner_seat = mod.owner_seat;
                    m.movement_cost = mod.movement_cost;
                }
                return true;
            }
            // Different overlay type: replace entirely.
            m = std::move(mod);
            return true;
        }
    }
    sq.modifiers.push_back(std::move(mod));
    return false;
}

}  // namespace tactics
