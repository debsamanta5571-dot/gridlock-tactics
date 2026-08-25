#pragma once

#include "tactics/board/board_layout.hpp"
#include "tactics/cards/ability_catalog.hpp"
#include "tactics/entities/entity.hpp"

#include <memory>
#include <string>
#include <vector>

namespace tactics {

inline constexpr int kPlayerBaseHealth = 25;
/** Glossary term slug (`{GL:…}`) for all innate base turret traits in UI copy. */
inline constexpr const char* kPlayerBaseInnateGlossaryTerm = "player_base_turret";

inline bool is_player_base_innate_keyword(const std::string& slug)
{
    return slug == "crit_immunity" || slug == "trueshot" || slug == "shadowstrike";
}

inline void attach_activated_abilities_from_catalog(Entity& entity, const std::vector<std::string>& keys)
{
    for (const std::string& key : keys) {
        AbilitySpec spec;
        if (try_get_ability_from_catalog(key, spec)) {
            entity.activated_abilities.push_back(std::move(spec));
        }
    }
}

inline std::shared_ptr<Unit> make_player_base(int owner)
{
    auto base = std::make_shared<Unit>();
    base->entity_id = "base_p" + std::to_string(owner);
    base->entity_type = "base";
    base->unit_type = "Base";
    base->unit_types = {"base"};
    base->owner = owner;
    base->attack_type = AttackType::Ranged;
    base->base_health = kPlayerBaseHealth;
    base->current_health = kPlayerBaseHealth;
    base->melee_damage = 0;
    base->ranged_damage = 3;
    base->ranged_damage_min = 2;
    base->ranged_damage_max = 4;
    base->ranged_range = 2;
    base->ranged_deadzone = 0;
    base->movement = 0;
    base->shape = {
        {0, 0}, {1, 0}, {2, 0}, {3, 0},
        {0, 1}, {1, 1}, {2, 1}, {3, 1},
    };
    base->keywords = {"crit_immunity", "trueshot", "shadowstrike"};
    attach_activated_abilities_from_catalog(*base, {"base_overcharge", "base_extend_range"});
    normalize_entity_shape(*base);
    return base;
}

}  // namespace tactics