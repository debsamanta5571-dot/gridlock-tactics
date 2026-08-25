#include "tactics/content/card_glossary.hpp"

#include "tactics/attributes/attributes.hpp"
#include "tactics/cards/ability_catalog.hpp"
#include "tactics/cards/card_runtime.hpp"
#include "tactics/cards/passive_catalog.hpp"
#include "tactics/content/glossary_copy.hpp"
#include "tactics/effects/status_effect_catalog.hpp"
#include "tactics/entities/player_base.hpp"

#include <algorithm>
#include <cctype>
#include <optional>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace tactics {
namespace {

// First sentence for simple-mode tooltip copy. Splits on the first sentence terminator (. ! ?)
// that is followed by whitespace or end-of-string, so embedded decimals ("Deals 2.5 damage.")
// and version-style numbers don't truncate the blurb early.
std::string first_sentence(const std::string& full)
{
    for (std::size_t i = 0; i < full.size(); ++i) {
        const char c = full[i];
        if (c == '.' || c == '!' || c == '?') {
            if (i + 1 >= full.size() || std::isspace(static_cast<unsigned char>(full[i + 1]))) {
                return full.substr(0, i + 1);
            }
        }
    }
    return full;
}

std::string keyword_glossary_body_or_attribute(const std::string& key, bool advanced_glossary)
{
    const std::string body = keyword_glossary_body(key, advanced_glossary);
    if (!body.empty()) {
        return body;
    }
    const std::string full = attribute_rules_text(key);
    if (full.empty()) {
        return {};
    }
    if (advanced_glossary) {
        return full;
    }
    return first_sentence(full);
}

void try_append_entry(std::vector<CardGlossaryEntry>& out, const std::string& dedupe_key, const std::string& name,
    const std::string& body)
{
    if (dedupe_key.empty() || name.empty() || body.empty()) {
        return;
    }
    if (std::any_of(out.begin(), out.end(), [&](const CardGlossaryEntry& e) { return e.dedupe_key == dedupe_key; })) {
        return;
    }
    out.push_back({dedupe_key, name, body});
}

std::string glossary_keyword_display_name(const std::string& key)
{
    const std::string from_attr = attribute_display_name(key);
    return from_attr != key ? from_attr : key;
}

std::string glossary_term_display_name(const std::string& key)
{
    if (key == kPlayerBaseInnateGlossaryTerm) {
        return "Base Turret";
    }
    if (key == "flux_energy") {
        return "Flux Energy";
    }
    if (key == "armor") {
        return "Armor";
    }
    std::string label = key;
    for (char& ch : label) {
        if (ch == '_') {
            ch = ' ';
        }
    }
    bool capitalize_next = true;
    for (char& ch : label) {
        if (capitalize_next && std::isalpha(static_cast<unsigned char>(ch))) {
            ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            capitalize_next = false;
        } else if (ch == ' ') {
            capitalize_next = true;
        }
    }
    return label;
}

void append_term_glossary(std::vector<CardGlossaryEntry>& out, const std::string& key, bool advanced_glossary)
{
    const std::string body = term_glossary_body(key, advanced_glossary);
    if (body.empty()) {
        return;
    }
    const std::string label = glossary_term_display_name(key);
    try_append_entry(out, "gl:" + key, label, body);
}

void append_keyword_glossary(std::vector<CardGlossaryEntry>& out, const std::string& key, bool advanced_glossary)
{
    const std::string body = keyword_glossary_body_or_attribute(key, advanced_glossary);
    if (body.empty()) {
        return;
    }
    const std::string label = glossary_keyword_display_name(key);
    try_append_entry(out, "kw:" + key, label, body);
}

void append_status_glossary(std::vector<CardGlossaryEntry>& out, const std::string& key, int amount,
    bool advanced_glossary)
{
    StatusEffectSpec spec;
    if (!try_get_status_effect_spec(key, spec)) {
        return;
    }
    const std::string body = status_glossary_body(spec, advanced_glossary);
    std::string label = spec.display_name.empty() ? key : spec.display_name;
    if (amount > 0) {
        label += " " + std::to_string(amount);
    }
    try_append_entry(out, "fx:" + key, label, body);
}

std::optional<std::string> status_applied_by_on_hit_mechanic(const std::string& mechanic_key)
{
    static const std::unordered_map<std::string, std::string> k_on_hit_to_status = {
        {"overload_on_hit", "overload"},
        {"shock_on_hit", "overload"},
        {"bleed_on_hit", "bleed"},
        {"fire_on_hit", "fire"},
        {"gas_on_hit", "poison"},
    };
    if (const auto it = k_on_hit_to_status.find(mechanic_key); it != k_on_hit_to_status.end()) {
        return it->second;
    }
    return std::nullopt;
}

void append_keyword_glossary_with_granted_status(std::vector<CardGlossaryEntry>& out, const std::string& key,
    bool advanced_glossary)
{
    append_keyword_glossary(out, key, advanced_glossary);
    if (const std::optional<std::string> status_key = status_applied_by_on_hit_mechanic(key)) {
        append_status_glossary(out, *status_key, 0, advanced_glossary);
    }
}

void append_passive_mechanic_status_glossary(std::vector<CardGlossaryEntry>& out, const PassiveAbilitySpec& passive,
    bool advanced_glossary)
{
    if (passive.passive_mechanic.empty()) {
        return;
    }
    if (const std::optional<std::string> status_key = status_applied_by_on_hit_mechanic(passive.passive_mechanic)) {
        append_status_glossary(out, *status_key, 0, advanced_glossary);
    }
}

void trim_ascii_whitespace(std::string& s)
{
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
        s.erase(s.begin());
    }
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
        s.pop_back();
    }
}

void append_glossary_from_rules_markup(std::vector<CardGlossaryEntry>& out, const std::string& text,
    bool advanced_glossary)
{
    for (std::size_t i = 0; i < text.size();) {
        if (text[i] != '{') {
            ++i;
            continue;
        }
        const std::size_t close = text.find('}', i + 1);
        if (close == std::string::npos) {
            break;
        }
        std::string token = text.substr(i + 1, close - i - 1);
        i = close + 1;
        const std::size_t colon = token.find(':');
        if (colon == std::string::npos) {
            continue;
        }
        std::string kind = token.substr(0, colon);
        std::transform(kind.begin(), kind.end(), kind.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        std::string slug = token.substr(colon + 1);
        if (const std::size_t pipe = slug.find('|'); pipe != std::string::npos) {
            slug = slug.substr(0, pipe);
        }
        trim_ascii_whitespace(slug);
        if (slug.empty()) {
            continue;
        }
        if (kind == "kw") {
            if (find_attribute_spec(slug) != nullptr) {
                append_keyword_glossary_with_granted_status(out, slug, advanced_glossary);
            }
        } else if (kind == "gl") {
            append_term_glossary(out, slug, advanced_glossary);
        } else if (kind == "fx") {
            append_status_glossary(out, slug, 0, advanced_glossary);
        }
    }
}

std::optional<std::string> status_key_for_effect_key(const std::string& effect_key)
{
    static const std::unordered_map<std::string, std::string> k_effect_to_status = {
        {"apply_poison", "poison"},
        {"apply_fire", "fire"},
        {"apply_bleed", "bleed"},
        {"apply_silenced", "silenced"},
        {"apply_silenced_owner_turn_end", "silenced"},
        {"apply_stealth", "stealth"},
        {"apply_jammed", "jammed"},
        {"apply_vulnerable", "vulnerable"},
        {"apply_rooted", "rooted"},
        {"apply_stunned", "stunned"},
        {"grant_next_damage_bonus", "next_damage_bonus"},
        {"grant_next_damage_bonus_adjacent", "next_damage_bonus"},
        {"grant_next_damage_bonus_self", "next_damage_bonus"},
        {"grant_delayed_next_damage_bonus", "delayed_next_damage_bonus"},
        {"grant_doubled_next_ability", "next_ability_doubled"},
        {"grant_on_damage_apply_overload_adjacent", "on_damage_apply_overload_next_ability"},
        {"grant_on_damage_apply_jammed_adjacent", "on_damage_apply_jammed_next_ability"},
        {"grant_next_ability_movement_reduction_ally", "on_damage_apply_movement_reduction_next_ability"},
        {"grant_next_ability_movement_reduction_self", "on_damage_apply_movement_reduction_next_ability"},
        {"grant_next_ability_rooted_ally", "on_damage_apply_rooted_next_ability"},
        {"grant_next_ability_rooted_self", "on_damage_apply_rooted_next_ability"},
        {"grant_next_bleed_self", "on_damage_apply_bleed_next_ability"},
        {"grant_medical_override", "medical_override"},
        {"grant_reactive_armor", "reactive_armor_grant"},
        {"grant_bonus_move", "bonus_move_grant"},
        {"liquid_data", "volatile_surge_buff"},
        {"shocking_stimulus_aoe", "shocking_stimulus_movement"},
        {"extend_aura_range", "aura_range_boost"},
        {"amplify_aura_stats", "aura_stats_boost"},
        {"apply_covering_fire", "covering_fire"},
        {"apply_covering_fire_range_buff", "covering_fire"},
        {"apply_covering_fire_stacks", "covering_fire"},
        {"artillery_mode", "artillery_mode_buff"},
        {"grant_cleave_self", "grant_cleave_self_buff"},
        {"whirlwind_spray", "whirlwind_spray_buff"},
        {"grant_first_strike_self", "grant_first_strike_self_buff"},
        {"apply_valiant_guard_self", "valiant_guard"},
        {"grant_relentless_aura", "relentless_aura_grant"},
        {"grant_movement_aura", "movement_aura_grant"},
        {"grant_damage_aura", "damage_aura_grant"},
        {"grant_multistrike_ally", "multistrike_ally_grant"},
        {"grant_stealth_self", "stealth"},
        {"grant_keyword_mirror_passive", "hyperactive_scanning"},
    };
    if (const auto it = k_effect_to_status.find(effect_key); it != k_effect_to_status.end()) {
        return it->second;
    }
    StatusEffectSpec spec;
    if (try_get_status_effect_spec(effect_key, spec)) {
        return effect_key;
    }
    return std::nullopt;
}

bool status_key_is_boost(const std::string& key)
{
    static const std::unordered_set<std::string> k_boost_status_keys = {
        "next_damage_bonus",
        "delayed_next_damage_bonus",
        "next_ability_doubled",
        "on_damage_apply_overload_next_ability",
        "on_damage_apply_jammed_next_ability",
        "on_damage_apply_bleed_next_ability",
        "on_damage_apply_movement_reduction_next_ability",
        "on_damage_apply_rooted_next_ability",
        "medical_override",
    };
    return k_boost_status_keys.count(key) > 0;
}

std::string boost_active_effect_short_name(const std::string& key, int amount, const std::string& custom_name,
    const StatusEffectSpec& spec)
{
    if (!custom_name.empty()) {
        return custom_name;
    }
    if (key == "next_damage_bonus") {
        return amount > 0 ? "+" + std::to_string(amount) + " Damage Boost" : "Damage Boost";
    }
    if (key == "delayed_next_damage_bonus") {
        return "Delayed Damage Boost";
    }
    if (key == "next_ability_doubled") {
        return "Doubled Ability";
    }
    if (key == "on_damage_apply_overload_next_ability") {
        return "Overload on Hit";
    }
    if (key == "on_damage_apply_jammed_next_ability") {
        return "Jam on Hit";
    }
    if (key == "on_damage_apply_bleed_next_ability") {
        return "Bleed on Hit";
    }
    if (key == "on_damage_apply_movement_reduction_next_ability") {
        return "Slow on Hit";
    }
    if (key == "on_damage_apply_rooted_next_ability") {
        return "Root on Hit";
    }
    if (key == "medical_override") {
        return "Medical Override";
    }
    return spec.display_name.empty() ? key : spec.display_name;
}

std::string boost_active_effect_tooltip(const std::string& key, int amount, const StatusEffectSpec& spec,
    const std::string& custom_rules, bool advanced_glossary)
{
    if (!custom_rules.empty()) {
        return custom_rules;
    }
    const std::string base = status_glossary_body(spec, advanced_glossary);
    if (key == "next_damage_bonus" && amount > 0) {
        return "+" + std::to_string(amount) + " damage on your next attack or ability. " + base;
    }
    if (key == "delayed_next_damage_bonus" && amount > 0) {
        return "+" + std::to_string(amount)
            + " damage on your next attack or ability at the start of your owner's next turn. " + base;
    }
    return base;
}

bool effect_key_grants_boost(const std::string& effect_key)
{
    static const std::unordered_set<std::string> k_boost_grant_keys = {
        "grant_next_damage_bonus",
        "grant_next_damage_bonus_adjacent",
        "grant_next_damage_bonus_self",
        "grant_delayed_next_damage_bonus",
        "grant_doubled_next_ability",
        "grant_on_damage_apply_overload_adjacent",
        "grant_on_damage_apply_jammed_adjacent",
        "grant_next_ability_movement_reduction_ally",
        "grant_next_ability_movement_reduction_self",
        "grant_next_ability_rooted_ally",
        "grant_next_ability_rooted_self",
        "grant_next_bleed_self",
        "grant_medical_override",
        "heal_boosted",
    };
    return k_boost_grant_keys.count(effect_key) > 0;
}

bool rules_text_mentions_boost(const std::string& text)
{
    std::string lower = text;
    std::transform(lower.begin(), lower.end(), lower.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower.find("boost") != std::string::npos;
}

bool ability_id_grants_boost(const std::string& ability_id)
{
    static const std::unordered_set<std::string> k_known_boost_abilities = {
        "tune_up",
        "overcharge_burst",
        "sylvia_static_charge",
        "sylvia_jamming_array",
        "debilitator_drag_line",
        "debilitator_anchor_line",
        "mending_shot",
        "doublecast",
    };
    if (k_known_boost_abilities.count(ability_id) > 0) {
        return true;
    }
    AbilitySpec ability;
    if (try_get_ability_from_catalog(ability_id, ability) && !ability.effect_key.empty()) {
        return effect_key_grants_boost(ability.effect_key);
    }
    return false;
}

void append_boost_keyword_glossary(std::vector<CardGlossaryEntry>& out, bool advanced_glossary)
{
    append_keyword_glossary(out, "boost", advanced_glossary);
}

bool passive_string_payload_is_spell_ability_pool(const std::map<std::string, std::string>& payload)
{
    const auto it = payload.find("pool");
    return it != payload.end() && it->second == "spell_ability";
}

bool passive_spec_uses_flux_energy(const PassiveAbilitySpec& passive)
{
    if (passive.automated_effect_key == "consume_spell_orange_for_growth") {
        return true;
    }
    if (passive.reactive_effect_key == "release_stored_energy_spell_turquoise") {
        return true;
    }
    return passive_string_payload_is_spell_ability_pool(passive.reactive_string_payload)
        || passive_string_payload_is_spell_ability_pool(passive.automated_string_payload);
}

bool passive_spec_requires_phase_survival(const PassiveAbilitySpec& passive)
{
    const auto it = passive.reactive_string_payload.find("source_survive_until");
    return it != passive.reactive_string_payload.end() && it->second == "phase_resolution";
}

bool rules_text_mentions_flux_energy(const std::string& text)
{
    std::string lower = text;
    std::transform(lower.begin(), lower.end(), lower.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower.find("flux energy") != std::string::npos || lower.find("gl:flux_energy") != std::string::npos
        || lower.find("kw:flux_energy") != std::string::npos || lower.find("spells and abilities only") != std::string::npos
        || lower.find("spells/abilities only") != std::string::npos;
}

void append_flux_energy_term_glossary(std::vector<CardGlossaryEntry>& out, bool advanced_glossary)
{
    append_term_glossary(out, "flux_energy", advanced_glossary);
}

void append_survives_term_glossary(std::vector<CardGlossaryEntry>& out, bool advanced_glossary)
{
    append_term_glossary(out, "survives", advanced_glossary);
}

bool card_passive_uses_flux_energy(const CardDefinition& def)
{
    if (definition_is_unit(def)) {
        const UnitCardDefinition& unit = definition_unit(def);
        for (const PassiveAbilitySpec& passive : unit.passive_abilities) {
            if (passive_spec_uses_flux_energy(passive)) {
                return true;
            }
        }
        for (const std::string& passive_id : unit.passive_ability_ids) {
            PassiveAbilitySpec passive;
            if (try_get_passive_from_catalog(passive_id, passive) && passive_spec_uses_flux_energy(passive)) {
                return true;
            }
        }
    }
    return false;
}

bool card_passive_requires_phase_survival(const CardDefinition& def)
{
    if (definition_is_unit(def)) {
        const UnitCardDefinition& unit = definition_unit(def);
        for (const PassiveAbilitySpec& passive : unit.passive_abilities) {
            if (passive_spec_requires_phase_survival(passive)) {
                return true;
            }
        }
        for (const std::string& passive_id : unit.passive_ability_ids) {
            PassiveAbilitySpec passive;
            if (try_get_passive_from_catalog(passive_id, passive) && passive_spec_requires_phase_survival(passive)) {
                return true;
            }
        }
    }
    return false;
}

bool skip_status_glossary_for_boost_grant(const std::string& effect_key, const std::string& status_key)
{
    return effect_key_grants_boost(effect_key) && status_key == "next_damage_bonus";
}

void append_glossary_from_effect_key(std::vector<CardGlossaryEntry>& out, const std::string& effect_key, int amount,
    bool advanced_glossary)
{
    if (effect_key_grants_boost(effect_key)) {
        append_boost_keyword_glossary(out, advanced_glossary);
    }
    if (const std::optional<std::string> status_key = status_key_for_effect_key(effect_key)) {
        if (!skip_status_glossary_for_boost_grant(effect_key, *status_key)) {
            append_status_glossary(out, *status_key, amount, advanced_glossary);
        }
    }
}

}  // namespace

bool card_has_flux_energy_mechanic(const CardDefinition& def)
{
    if (rules_text_mentions_flux_energy(def.rules_text) || rules_text_mentions_flux_energy(def.normal_rules_text)) {
        return true;
    }
    if (card_passive_uses_flux_energy(def)) {
        return true;
    }
    if (definition_is_spell(def)) {
        const SpellCardDefinition& spell = definition_spell(def);
        if (spell.effect_key == "sacrifice_ally_gain_mana") {
            return true;
        }
    }
    for (const std::string& ability_id : def.abilities) {
        AbilitySpec ability;
        if (try_get_ability_from_catalog(ability_id, ability)
            && ability.effect_key == "consume_spell_orange_for_growth") {
            return true;
        }
    }
    return false;
}

bool card_has_phase_survival_mechanic(const CardDefinition& def)
{
    if (card_passive_requires_phase_survival(def)) {
        return true;
    }
    const auto mentions_survives = [](const std::string& text) {
        std::string lower = text;
        std::transform(lower.begin(), lower.end(), lower.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return lower.find("gl:survives") != std::string::npos;
    };
    return mentions_survives(def.rules_text) || mentions_survives(def.normal_rules_text);
}

bool card_has_boost_mechanic(const CardDefinition& def)
{
    if (rules_text_mentions_boost(def.rules_text) || rules_text_mentions_boost(def.normal_rules_text)) {
        return true;
    }
    for (const std::string& ability_id : def.abilities) {
        if (ability_id_grants_boost(ability_id)) {
            return true;
        }
    }
    if (definition_is_spell(def)) {
        const SpellCardDefinition& spell = definition_spell(def);
        std::string effect_key = spell.effect_key;
        if (!spell.effect_ref.empty()) {
            AbilitySpec ability;
            if (try_get_ability_from_catalog(spell.effect_ref, ability) && !ability.effect_key.empty()) {
                effect_key = ability.effect_key;
            }
        }
        if (effect_key_grants_boost(effect_key)) {
            return true;
        }
    }
    return false;
}

int active_effect_sort_rank(const StatusEffectSpec& spec, const std::string& key)
{
    if (spec.blocks_activated_abilities || key == "silenced" || key == "rooted" || key == "stunned") {
        return 0;
    }
    if (spec.is_negative) {
        return 1;
    }
    if (spec.is_positive) {
        return 2;
    }
    return 3;
}

struct RankedActiveEffectEntry {
    int rank{3};
    CardGlossaryEntry entry;
};

bool status_label_omits_stack_count(const std::string& key)
{
    return key == "deployment_fatigue" || key == "hyperactive_scanning";
}

std::string passive_glossary_body(const PassiveAbilitySpec& passive, bool advanced_glossary)
{
    if (advanced_glossary) {
        if (!passive.rules_text.empty()) {
            return passive.rules_text;
        }
        return passive.normal_rules_text;
    }
    if (!passive.normal_rules_text.empty()) {
        return passive.normal_rules_text;
    }
    return passive.rules_text;
}

bool append_live_status_entry(std::vector<RankedActiveEffectEntry>& pending, const std::string& key, int amount,
    const std::string& dedupe_key, bool advanced_glossary, const std::string& custom_name = {},
    const std::string& custom_rules = {})
{
    if (amount <= 0) {
        return false;
    }
    StatusEffectSpec spec;
    if (!try_get_status_effect_spec(key, spec)) {
        return false;
    }
    std::string body;
    std::string label;
    if (status_key_is_boost(key)) {
        label = boost_active_effect_short_name(key, amount, custom_name, spec);
        body = boost_active_effect_tooltip(key, amount, spec, custom_rules, advanced_glossary);
    } else {
        body = status_glossary_body(spec, advanced_glossary);
        label = spec.display_name.empty() ? key : spec.display_name;
        if (!status_label_omits_stack_count(key)) {
            label += " " + std::to_string(amount);
        }
    }
    if (body.empty() || label.empty()) {
        return false;
    }
    CardGlossaryEntry entry;
    entry.dedupe_key = dedupe_key;
    entry.name = std::move(label);
    entry.body = std::move(body);
    entry.is_negative = spec.is_negative;
    entry.is_positive = spec.is_positive;
    pending.push_back({active_effect_sort_rank(spec, key), std::move(entry)});
    return true;
}

void collect_entity_active_glossary_entries(const Entity& entity, std::vector<CardGlossaryEntry>& out,
    bool advanced_glossary)
{
    out.clear();
    const bool silenced = entity_is_silenced(entity);
    std::unordered_set<std::string> seen;
    std::vector<RankedActiveEffectEntry> pending;
    pending.reserve(entity.entity_effects.size() + entity.temporary_effects.size());

    for (const EntityEffectInstance& effect : entity.entity_effects) {
        if (effect.amount <= 0) {
            continue;
        }
        StatusEffectSpec spec;
        if (!try_get_status_effect_spec(effect.key, spec)) {
            continue;
        }
        if (silenced && spec.is_positive) {
            continue;
        }
        const std::string dedupe = "live:" + effect.key;
        if (!seen.insert(dedupe).second) {
            continue;
        }
        append_live_status_entry(pending, effect.key, effect.amount, dedupe, advanced_glossary);
    }

    for (const TemporaryEntityEffect& temp : entity.temporary_effects) {
        if (temp.expire_on != "never" && temp.remaining_turns <= 0) {
            continue;
        }
        StatusEffectSpec spec;
        if (try_get_status_effect_spec(temp.effect_id, spec)) {
            if (silenced && spec.is_positive) {
                continue;
            }
            const std::string dedupe = "live:temp:" + temp.effect_id;
            if (!seen.insert(dedupe).second) {
                continue;
            }
            const int amount = temp.stat_grants.on_expire_next_damage_bonus > 0
                ? temp.stat_grants.on_expire_next_damage_bonus
                : 1;
            if (status_key_is_boost(temp.effect_id)) {
                append_live_status_entry(pending, temp.effect_id, amount, dedupe, advanced_glossary, temp.name,
                    temp.rules_text);
            } else {
                append_live_status_entry(pending, temp.effect_id, 1, dedupe, advanced_glossary);
            }
            continue;
        }
        if (temp.name.empty() || temp.rules_text.empty()) {
            continue;
        }
        const std::string dedupe = "live:temp:" + temp.effect_id;
        if (!seen.insert(dedupe).second) {
            continue;
        }
        CardGlossaryEntry entry;
        entry.dedupe_key = dedupe;
        entry.name = temp.name;
        entry.body = temp.rules_text;
        pending.push_back({3, std::move(entry)});
    }

    for (const PassiveAbilitySpec& passive : entity.passive_abilities) {
        if (silenced && passive.is_positive) {
            continue;
        }
        const std::string dedupe = "live:passive:" + passive.key;
        if (!seen.insert(dedupe).second) {
            continue;
        }
        PassiveAbilitySpec catalog = passive;
        if (passive_glossary_body(catalog, advanced_glossary).empty()) {
            if (!try_get_passive_from_catalog(passive.key, catalog)) {
                continue;
            }
        }
        const std::string body = passive_glossary_body(catalog, advanced_glossary);
        if (body.empty()) {
            continue;
        }
        CardGlossaryEntry entry;
        entry.dedupe_key = dedupe;
        entry.name = catalog.name.empty() ? catalog.key : catalog.name;
        entry.body = body;
        entry.is_positive = catalog.is_positive;
        entry.is_negative = catalog.is_negative;
        const int rank = catalog.is_negative ? 1 : catalog.is_positive ? 2 : 3;
        pending.push_back({rank, std::move(entry)});
    }

    std::sort(pending.begin(), pending.end(), [](const RankedActiveEffectEntry& a, const RankedActiveEffectEntry& b) {
        if (a.rank != b.rank) {
            return a.rank < b.rank;
        }
        return a.entry.name < b.entry.name;
    });
    out.reserve(pending.size());
    for (RankedActiveEffectEntry& item : pending) {
        out.push_back(std::move(item.entry));
    }
}

void collect_glossary_from_rules_text(const std::string& text, std::vector<CardGlossaryEntry>& out,
    bool advanced_glossary)
{
    append_glossary_from_rules_markup(out, text, advanced_glossary);
}

void collect_card_glossary_entries(const CardDefinition& def, std::vector<CardGlossaryEntry>& out,
    bool advanced_glossary)
{
    for (const CardKeywordDefinition& kw : def.keywords) {
        append_keyword_glossary_with_granted_status(out, kw.key, advanced_glossary);
    }
    if (definition_is_unit(def)) {
        const UnitCardDefinition& unit = definition_unit(def);
        for (const CardKeywordDefinition& kw : unit.keywords) {
            append_keyword_glossary_with_granted_status(out, kw.key, advanced_glossary);
        }
        for (const CardEffectDefinition& effect : unit.initial_effects) {
            append_status_glossary(out, effect.key, effect.amount, advanced_glossary);
        }
        for (const std::string& passive_id : unit.passive_ability_ids) {
            PassiveAbilitySpec passive;
            if (try_get_passive_from_catalog(passive_id, passive)) {
                append_passive_mechanic_status_glossary(out, passive, advanced_glossary);
            }
        }
        for (const PassiveAbilitySpec& passive : unit.passive_abilities) {
            append_passive_mechanic_status_glossary(out, passive, advanced_glossary);
        }
    } else if (definition_is_spell(def)) {
        const SpellCardDefinition& spell = definition_spell(def);
        std::string effect_key = spell.effect_key;
        if (!spell.effect_ref.empty()) {
            AbilitySpec ability;
            if (try_get_ability_from_catalog(spell.effect_ref, ability) && !ability.effect_key.empty()) {
                effect_key = ability.effect_key;
            }
        }
        int amount = 0;
        if (const auto it = spell.effect_payload.find("amount"); it != spell.effect_payload.end()) {
            amount = it->second;
        }
        append_glossary_from_effect_key(out, effect_key, amount, advanced_glossary);
    }

    if (card_has_boost_mechanic(def)) {
        append_boost_keyword_glossary(out, advanced_glossary);
    }

    if (card_has_flux_energy_mechanic(def)) {
        append_flux_energy_term_glossary(out, advanced_glossary);
    }

    if (card_has_phase_survival_mechanic(def)) {
        append_survives_term_glossary(out, advanced_glossary);
    }

    for (const std::string& ability_id : def.abilities) {
        AbilitySpec ability;
        if (!try_get_ability_from_catalog(ability_id, ability) || ability.effect_key.empty()) {
            continue;
        }
        int amount = 0;
        if (const auto it = ability.effect_payload.find("amount"); it != ability.effect_payload.end()) {
            amount = it->second;
        }
        append_glossary_from_effect_key(out, ability.effect_key, amount, advanced_glossary);
    }

    append_glossary_from_rules_markup(out, def.normal_rules_text, advanced_glossary);
    append_glossary_from_rules_markup(out, def.rules_text, advanced_glossary);
}

namespace {

std::unordered_set<std::string> card_printed_keyword_slugs(const CardDefinition* card_def)
{
    std::unordered_set<std::string> printed;
    if (!card_def) {
        return printed;
    }
    for (const CardKeywordDefinition& kw : card_def->keywords) {
        if (!kw.key.empty()) {
            printed.insert(kw.key);
        }
    }
    if (definition_is_unit(*card_def)) {
        for (const CardKeywordDefinition& kw : definition_unit(*card_def).keywords) {
            if (!kw.key.empty()) {
                printed.insert(kw.key);
            }
        }
    }
    return printed;
}

void collect_effective_keyword_slugs(const Entity& entity, std::vector<std::string>& out)
{
    auto add = [&](const std::string& key) {
        if (key.empty() || attribute_is_non_copyable(key) || slug_is_status_state_not_gained_keyword(key)) {
            return;
        }
        if (std::find(out.begin(), out.end(), key) == out.end()) {
            out.push_back(key);
        }
    };
    for (const std::string& kw : entity.keywords) {
        add(kw);
    }
    for (const std::string& kw : entity.aura_granted_keywords) {
        add(kw);
    }
    for (const TemporaryEntityEffect& effect : entity.temporary_effects) {
        if (temporary_effect_suppresses_gained_keyword_grants(effect)) {
            continue;
        }
        for (const PassiveAttributeGrant& grant : effect.granted_attributes) {
            add(grant.key);
        }
    }
}

std::vector<std::string> entity_gained_keyword_slugs(const Entity& entity, const CardDefinition* card_def)
{
    if (entity_is_silenced(entity)) {
        return {};
    }
    const std::unordered_set<std::string> printed = card_printed_keyword_slugs(card_def);
    std::vector<std::string> effective;
    collect_effective_keyword_slugs(entity, effective);
    std::vector<std::string> gained;
    gained.reserve(effective.size());
    for (const std::string& slug : effective) {
        if (!printed.count(slug)) {
            if (entity_is_base(entity) && is_player_base_innate_keyword(slug)) {
                continue;
            }
            gained.push_back(slug);
        }
    }
    return gained;
}

}  // namespace

bool slug_is_status_state_not_gained_keyword(const std::string& key)
{
    StatusEffectSpec spec;
    return try_get_status_effect_spec(key, spec);
}

bool temporary_effect_suppresses_gained_keyword_grants(const TemporaryEntityEffect& effect)
{
    if (effect.effect_id.empty()) {
        return false;
    }
    return slug_is_status_state_not_gained_keyword(effect.effect_id);
}

void collect_entity_gained_keyword_glossary_entries(const Entity& entity, const CardDefinition* card_def,
    std::vector<CardGlossaryEntry>& out, bool advanced_glossary)
{
    for (const std::string& slug : entity_gained_keyword_slugs(entity, card_def)) {
        append_keyword_glossary_with_granted_status(out, slug, advanced_glossary);
    }
}

std::string format_entity_gained_keywords_rules_strip(const Entity& entity, const CardDefinition* card_def)
{
    const std::vector<std::string> gained = entity_gained_keyword_slugs(entity, card_def);
    if (gained.empty()) {
        return {};
    }
    std::ostringstream oss;
    bool first = true;
    for (const std::string& slug : gained) {
        if (!first) {
            oss << ' ';
        }
        first = false;
        oss << "{KW:" << slug << "}";
        const int amount = entity_attribute_amount(entity, slug, -1);
        if (amount >= 0) {
            oss << ' ' << amount;
        }
    }
    return oss.str();
}

void collect_player_base_innate_glossary_entry(std::vector<CardGlossaryEntry>& out, const bool advanced_glossary)
{
    append_term_glossary(out, kPlayerBaseInnateGlossaryTerm, advanced_glossary);
}

std::string format_player_base_innate_glossary_marker()
{
    return std::string("{GL:") + kPlayerBaseInnateGlossaryTerm + "}";
}

namespace {

std::string effect_speed_brace_token(const EffectSpeed speed)
{
    switch (speed) {
    case EffectSpeed::Reflex:
        return "{REFLEX}";
    case EffectSpeed::Blazing:
        return "{BLAZING}";
    case EffectSpeed::Channeled:
    default:
        return "{CHANNELED}";
    }
}

std::string ability_rules_blurb(const AbilitySpec& spec, const bool advanced_glossary)
{
    if (!advanced_glossary && !spec.normal_description.empty()) {
        return spec.normal_description;
    }
    if (!spec.description.empty()) {
        return spec.description;
    }
    return spec.normal_description;
}

void append_player_base_ability_strip(std::ostringstream& oss, const char* ability_id, const bool advanced_glossary)
{
    AbilitySpec spec;
    if (!try_get_ability_from_catalog(ability_id, spec)) {
        return;
    }
    const std::string desc = ability_rules_blurb(spec, advanced_glossary);
    if (desc.empty()) {
        return;
    }
    const std::string name = spec.name.empty() ? spec.key : spec.name;
    oss << "\n\n" << effect_speed_brace_token(spec.speed) << ", " << name << ": " << desc;
}

}  // namespace

std::string format_player_base_card_rules(const bool advanced_glossary)
{
    std::ostringstream oss;
    oss << "Immune to spells and buffs (combat damage still applies). " << format_player_base_innate_glossary_marker();
    append_player_base_ability_strip(oss, "base_overcharge", advanced_glossary);
    append_player_base_ability_strip(oss, "base_extend_range", advanced_glossary);
    return oss.str();
}

}  // namespace tactics