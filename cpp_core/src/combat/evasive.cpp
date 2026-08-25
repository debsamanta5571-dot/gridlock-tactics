#include "tactics/combat/evasive.hpp"

#include <algorithm>

namespace tactics {

int evasive_stack_count(const Entity& entity)
{
    return std::max(0, entity_effect_amount(entity, "evasive"));
}

int evasive_hit_chance_percent(const int stacks)
{
    if (stacks <= 0) {
        return 100;
    }
    return kEvasiveHitChancePercent;
}

bool roll_evasive_whiff(std::mt19937& rng, const int evasive_stacks)
{
    if (evasive_stacks <= 0) {
        return false;
    }
    std::uniform_int_distribution<int> dist(1, 100);
    return dist(rng) > kEvasiveHitChancePercent;
}

}  // namespace tactics