#pragma once

#include "tactics/entities/entity.hpp"

#include <random>

namespace tactics {

/** Flat 50% hit chance while any Evasive stacks remain (stacks are duration, not potency). */
inline constexpr int kEvasiveHitChancePercent = 50;

/** Evasive stacks on `entity` (entity effect `evasive`). Each stack lasts one owner turn. */
int evasive_stack_count(const Entity& entity);

/** 50% hit while stacks > 0; 100% once expired. Stack count does not change miss rate. */
int evasive_hit_chance_percent(int stacks);

/**
 * Returns true when the attack completely misses due to Evasive (no damage, no body-block redirect).
 * Rolled before body-block / low-cover redirects.
 */
bool roll_evasive_whiff(std::mt19937& rng, int evasive_stacks);

}  // namespace tactics