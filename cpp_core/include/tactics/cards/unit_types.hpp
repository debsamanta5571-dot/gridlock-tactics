#pragma once

#include "tactics/entities/entity.hpp"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace tactics {

struct Card;

/** Normalize tribe id: lowercase trim; empty strings are dropped. */
std::string normalize_unit_type_id(const std::string& raw);

/**
 * True when `required` is empty, or the entity has at least one listed type (OR).
 * Matches explicit `entity.unit_types` plus implied board categories:
 * `unit`, `structure` (buildings/breakable obstacles/obstacles - not bases), `base`, `building`.
 */
bool entity_satisfies_unit_type_filter(const Entity& entity, const std::vector<std::string>& required);

/** Bonus damage when the target matches any listed type (OR). */
int unit_type_bonus_damage_for_target(const Entity& target, const std::vector<std::string>& bonus_types, int bonus_amount);

bool card_has_any_unit_type(const Card& card, const std::string& type_id);

/** Parses a JSON string array of unit type ids (normalized, deduped). */
bool read_unit_types_json_array(const nlohmann::json& j, std::vector<std::string>& out, std::string& err, const std::string& path);

}  // namespace tactics
