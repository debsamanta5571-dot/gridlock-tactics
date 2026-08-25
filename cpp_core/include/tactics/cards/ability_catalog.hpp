#pragma once

#include "tactics/cards/cards.hpp"

#include <initializer_list>
#include <string>

namespace tactics {

/** Clears the global ability catalog (tests / hot reload). */
void clear_ability_catalog();

/**
 * Parses `{ "abilities": { "<id>": { ...AbilitySpec fields... }, ... } }`.
 * Merges into the global catalog (later keys override earlier).
 * @return false and err_out on parse failure.
 */
bool load_ability_catalog_from_json_utf8(const std::string& utf8, std::string& err_out);

/** Loads built-in catalog JSON if the catalog is still empty (safe to call repeatedly). */
void ensure_builtin_ability_catalog_loaded();

/**
 * F3: Override the embedded builtin ability catalog JSON with caller-supplied UTF-8 content.
 * Must be called before `ensure_builtin_ability_catalog_loaded()` fires, or after `clear_ability_catalog()`.
 * Passing an empty string reverts to the compiled-in `kBuiltinAbilityCatalogJson`.
 */
void set_builtin_ability_catalog_json_override(std::string utf8_json);

/** @return false if id is unknown. */
bool try_get_ability_from_catalog(const std::string& id, AbilitySpec& out);

/** Appends abilities from catalog ids in order; skips unknown ids. */
void append_abilities_from_catalog_ids(UnitCard& card, std::initializer_list<const char*> ids);

/** Stable fingerprint of loaded ability ids (for multiplayer catalog parity). */
std::string ability_catalog_fingerprint_utf8();

}  // namespace tactics
