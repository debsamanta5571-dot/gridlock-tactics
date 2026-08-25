#pragma once

#include <string>

namespace tactics {

struct EffectTraits {
    bool deals_damage{false};
    bool uses_directional_aim{false};
    bool uses_push_direction_aim{false};
    bool area_effect{false};
    /**
     * When true, the ability_damage_bonus from the caster's aura is applied to this effect's
     * primary amount even though it does not deal damage (e.g. a heal that scales with ability boosts).
     */
    bool scales_with_ability_damage{false};
    /**
     * When true, the directional aim cell is the exact tile where the unit lands (not merely a
     * direction indicator). Targeting highlights every passable cell (empty or pickup-occupied)
     * at distances [aim_min_range, max_range] per direction instead of only the farthest cell.
     */
    bool movement_landing{false};
    /**
     * When true, this effect targets an empty board cell (no entity required at the destination).
     * Targeting scans cells in range instead of iterating entities; validation checks emptiness
     * and range directly rather than looking up an entity at the target coordinates.
     */
    bool targets_empty_cell{false};
};

EffectTraits effect_traits_for_key(const std::string& effect_key);
bool effect_key_deals_damage(const std::string& effect_key);
bool effect_key_uses_directional_aim(const std::string& effect_key);
bool effect_key_uses_push_direction_aim(const std::string& effect_key);
bool effect_key_scales_with_ability_damage(const std::string& effect_key);
bool effect_key_is_movement_landing(const std::string& effect_key);
bool effect_key_targets_empty_cell(const std::string& effect_key);
/** Lobbed AoE: pick any board cell in range as blast center (grenades, nukes). */
bool effect_key_uses_lobbed_aoe_center(const std::string& effect_key);
/** Direct unit-target effects require a living entity on the picked cell; ground/AoE centers do not. */
bool effect_requires_entity_at_target_cell(const std::string& effect_key);
/** True when hover should show an orange AoE blast footprint (directional, lobbed, or tile-centered). */
bool effect_supports_aoe_blast_preview(const std::string& effect_key);

}  // namespace tactics
