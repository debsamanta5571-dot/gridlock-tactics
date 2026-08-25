#pragma once

#include "tactics/core/stack.hpp"

#include <string>
#include <utility>
#include <vector>

namespace tactics {

struct AbilityResolveVizHit {
    std::string entity_id;
    int grid_x{0};
    int grid_y{0};
    int amount{0};
    bool is_heal{false};
    /** Card/ability display name shown in floating resolve popups (e.g. "Reactive Armor"). */
    std::string event_label;
};

struct AbilityResolveVizBlastCell {
    int grid_x{0};
    int grid_y{0};
    /** Ring index for synchronized tile flash and damage-popup timing (0 = first band). */
    int wave_depth{0};
};

struct AbilityResolveVizPreview {
    std::string source_entity_id;
    std::string ability_name;
    std::string effect_key;
    /** Blast tiles in wave order (directional: from caster; lobbed AoE: from impact center). */
    std::vector<AbilityResolveVizBlastCell> blast_cells;
    /** True when wave_depth is measured from caster along aim (directional abilities). */
    bool directional_wave{false};
    /** Green tile pulse (ally heal/buff); false = red damage/hostile pulse. */
    bool friendly_effect{false};
};

/** True when Unreal should pause before resolving this ability on the stack. */
bool should_pause_for_ability_resolve_viz(const GameState& game, const StackItem& item);

/** Read-only preview of blast tiles and per-unit damage/heal (before state mutation). */
AbilityResolveVizPreview build_ability_resolve_viz_preview(GameState& game, const StackItem& item);

/** When no damage/heal popup was logged, emit label-only popups at preview blast entities. */
void emit_ability_resolve_result_label_popups_if_needed(GameState& game);

}  // namespace tactics
