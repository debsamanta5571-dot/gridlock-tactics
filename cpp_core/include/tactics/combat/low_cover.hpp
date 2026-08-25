#pragma once



#include "tactics/entities/entity.hpp"



#include <memory>

#include <optional>

#include <random>

#include <utility>

#include <vector>



namespace tactics {



class GameState;
struct AbilitySpec;
struct StackItem;

struct DamagePacket;



/** Ranged attacks vs units behind adjacent low cover on the LOS path. */

inline constexpr int kLowCoverRangedEvasionPercent = 50;



/**

 * All living low-cover obstacles on a clear LOS lane that are adjacent to `defender` and between shooter and target.

 * Empty when no qualifying cover exists.

 */

std::vector<std::shared_ptr<Entity>> find_all_low_cover_for_ranged_evasion(const GameState& game, const Entity& attacker,

    std::pair<int, int> target_cell, const Entity& defender);



/** First qualifying cover from `find_all_low_cover_for_ranged_evasion`, or null. */

std::shared_ptr<Entity> find_low_cover_for_ranged_evasion(const GameState& game, const Entity& attacker,

    std::pair<int, int> target_cell, const Entity& defender);



bool roll_low_cover_evasion(std::mt19937& rng, int chance_percent = kLowCoverRangedEvasionPercent);



/** On success, retargets `packet` to a random qualifying cover and returns true. */

bool maybe_redirect_ranged_attack_to_low_cover(GameState& game, const Entity& attacker, std::pair<int, int> target_cell,

    const Entity& intended_target, DamagePacket& packet, const AbilitySpec* ability = nullptr);

/** When true, `ability_out` holds the activated ability spec for ability-sourced damage. */

bool stack_item_uses_ranged_low_cover_evasion(const StackItem& item, const Entity& source, std::optional<AbilitySpec>& ability_out);



}  // namespace tactics

