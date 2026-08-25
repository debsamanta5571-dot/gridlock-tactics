#pragma once

#include "tactics/effects/status_effect_catalog.hpp"

#include <string>

namespace tactics {

/** Clears loaded glossary copy (tests / hot reload). */
void clear_glossary_copy();

/**
 * Parses `{ "schema_version": 1, "keywords": { ... }, "terms": { ... }, "speeds": { ... }, "icons": { ... },
 * "status_effects": { ... } }`.
 * Merges into the global glossary maps (later keys override earlier).
 * @return false and err_out on parse failure.
 */
bool load_glossary_copy_from_json_utf8(const std::string& utf8, std::string& err_out);

/** Loads built-in glossary JSON if maps are still empty (safe to call repeatedly). */
void ensure_builtin_glossary_copy_loaded();

/** @return Sidebar keyword body, or empty if unknown (caller may fall back to attribute_rules_text). */
std::string keyword_glossary_body(const std::string& key, bool advanced = false);

/** @return Sidebar/hover body for a glossary term (not a unit keyword), or empty if unknown. */
std::string term_glossary_body(const std::string& key, bool advanced = false);

/** @return Hover tooltip for spell/ability speed (channeled / reflex / blazing), or empty if unknown. */
std::string speed_glossary_body(const std::string& speed_id, bool advanced = false);

/** @return Hover tooltip for an inline UI icon key (e.g. energy_orange, stats_life), or empty if unknown. */
std::string icon_glossary_body(const std::string& icon_id, bool advanced = false);

/** Sidebar status body from catalog key + generic fallbacks from StatusEffectSpec flags. */
std::string status_glossary_body(const StatusEffectSpec& spec, bool advanced = false);

}  // namespace tactics