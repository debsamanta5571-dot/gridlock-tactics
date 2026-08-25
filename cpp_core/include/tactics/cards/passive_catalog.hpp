#pragma once

#include "tactics/cards/cards.hpp"

#include <string>

namespace tactics {

void clear_passive_catalog();

/** Parses `{ "passives": { "<id>": { ...PassiveAbilitySpec fields... }, ... } }`. */
bool load_passive_catalog_from_json_utf8(const std::string& utf8, std::string& err_out);

void ensure_builtin_passive_catalog_loaded();

bool try_get_passive_from_catalog(const std::string& id, PassiveAbilitySpec& out);

}  // namespace tactics
