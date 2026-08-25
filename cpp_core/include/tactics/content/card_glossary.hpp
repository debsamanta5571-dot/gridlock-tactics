#pragma once

#include "tactics/cards/card_catalog.hpp"
#include "tactics/entities/entity.hpp"

#include <string>
#include <vector>

namespace tactics {

/** One sidebar glossary row for card detail UI (keyword or status). */
struct CardGlossaryEntry {
    std::string dedupe_key;
    std::string name;
    std::string body;
    /** Populated for live entity effect rows; unused for card-definition glossary. */
    bool is_negative{false};
    bool is_positive{false};
};

/** Collects keyword/status glossary rows for a card definition (hand detail panel). */
void collect_card_glossary_entries(const CardDefinition& def, std::vector<CardGlossaryEntry>& out,
    bool advanced_glossary = false);

/** Collects glossary rows referenced by `{GL:…}` / `{KW:…}` markers in rules prose. */
void collect_glossary_from_rules_text(const std::string& text, std::vector<CardGlossaryEntry>& out,
    bool advanced_glossary = false);

/** Live status stacks / primers on a deployed entity (board unit detail active-effects row). */
void collect_entity_active_glossary_entries(const Entity& entity, std::vector<CardGlossaryEntry>& out,
    bool advanced_glossary = false);

/** Glossary rows for keywords the entity has beyond its source card print (aura/temp/runtime). */
void collect_entity_gained_keyword_glossary_entries(const Entity& entity, const CardDefinition* card_def,
    std::vector<CardGlossaryEntry>& out, bool advanced_glossary = false);

/** `{KW:…}` strip for runtime-gained keywords (empty when none). For board unit detail rules text. */
std::string format_entity_gained_keywords_rules_strip(const Entity& entity, const CardDefinition* card_def);

/** Status stacks and deploy-turn state (e.g. deployment fatigue) - not gained-keyword UI. */
bool slug_is_status_state_not_gained_keyword(const std::string& key);

/** Temp effects that represent status/state rows (active-effects sidebar), not keyword grants. */
bool temporary_effect_suppresses_gained_keyword_grants(const TemporaryEntityEffect& effect);

/** True when the card grants, consumes, or references the Boost mechanic in rules or effects. */
bool card_has_boost_mechanic(const CardDefinition& def);

/** True when the card grants, consumes, or references flux energy (spell_ability tagged float). */
bool card_has_flux_energy_mechanic(const CardDefinition& def);

/** True when a passive uses phase-batch survival (source_survive_until: phase_resolution). */
bool card_has_phase_survival_mechanic(const CardDefinition& def);

/** Sidebar row for the collapsed player-base innate keyword bundle. */
void collect_player_base_innate_glossary_entry(std::vector<CardGlossaryEntry>& out, bool advanced_glossary = false);

/** `{GL:player_base_turret}` marker for player-base detail rules text. */
std::string format_player_base_innate_glossary_marker();

/** Card-detail rules prose for a selected player base (immunity blurb + innate term + abilities). */
std::string format_player_base_card_rules(bool advanced_glossary = false);

}  // namespace tactics