#include "tactics/cards/card_search_index.hpp"

#include "tactics/cards/ability_catalog.hpp"
#include "tactics/cards/passive_catalog.hpp"

#include <algorithm>
#include <cctype>
#include <mutex>
#include <set>
#include <sstream>
#include <unordered_set>

namespace tactics {
namespace {

std::mutex g_card_search_mutex;
CardSearchIndex g_card_search_index;

std::string to_lower_ascii(std::string s)
{
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

std::string trim_ascii(std::string s)
{
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
        s.erase(s.begin());
    }
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
        s.pop_back();
    }
    return s;
}

std::vector<std::string> tokenize_search_text(const std::string& text)
{
    std::vector<std::string> tokens;
    std::string current;
    for (unsigned char ch : text) {
        if (std::isalnum(ch)) {
            current.push_back(static_cast<char>(std::tolower(ch)));
        } else if (!current.empty()) {
            tokens.push_back(current);
            current.clear();
        }
    }
    if (!current.empty()) {
        tokens.push_back(current);
    }
    std::sort(tokens.begin(), tokens.end());
    tokens.erase(std::unique(tokens.begin(), tokens.end()), tokens.end());
    return tokens;
}

void append_tokens_from_text(std::vector<std::string>& out, const std::string& text)
{
    for (const std::string& tok : tokenize_search_text(text)) {
        out.push_back(tok);
    }
}

void append_tokens_from_values(std::vector<std::string>& out, const std::vector<std::string>& values)
{
    for (const std::string& v : values) {
        append_tokens_from_text(out, v);
    }
}

uint32_t energy_type_bit(EnergyType type)
{
    return 1u << static_cast<uint32_t>(type);
}

int sum_energy_cost(const std::map<EnergyType, int>& cost)
{
    int total = 0;
    for (const auto& [_, amount] : cost) {
        total += std::max(0, amount);
    }
    return total;
}

bool numeric_in_range(int value, const CardSearchNumericRange& range)
{
    if (range.min && value < *range.min) {
        return false;
    }
    if (range.max && value > *range.max) {
        return false;
    }
    return true;
}

bool numeric_optional_in_range(const std::optional<int>& value, const CardSearchNumericRange& range)
{
    if (!range.min && !range.max) {
        return true;
    }
    if (!value) {
        return false;
    }
    return numeric_in_range(*value, range);
}

std::vector<std::string> collect_keywords(const CardDefinition& def)
{
    std::vector<std::string> out;
    for (const auto& kw : def.keywords) {
        if (!kw.key.empty()) {
            out.push_back(kw.key);
        }
    }
    if (def.unit) {
        for (const auto& kw : def.unit->keywords) {
            if (!kw.key.empty()) {
                out.push_back(kw.key);
            }
        }
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

std::vector<std::string> collect_effect_keys(const CardDefinition& def)
{
    std::vector<std::string> out;
    if (def.spell) {
        if (!def.spell->effect_key.empty()) {
            out.push_back(def.spell->effect_key);
        }
        if (!def.spell->effect_ref.empty()) {
            out.push_back(def.spell->effect_ref);
        }
    }
    return out;
}

std::vector<std::string> collect_passive_ids(const CardDefinition& def)
{
    std::vector<std::string> out;
    if (def.unit) {
        out = def.unit->passive_ability_ids;
        for (const auto& passive : def.unit->passive_abilities) {
            if (!passive.key.empty()) {
                out.push_back(passive.key);
            }
        }
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

void append_field_tokens(std::vector<std::string>& tokens, CardSearchTextField fields, CardSearchTextField field,
    const std::string& text)
{
    if ((fields & field) == CardSearchTextField{}) {
        return;
    }
    append_tokens_from_text(tokens, text);
}

void append_field_tokens(std::vector<std::string>& tokens, CardSearchTextField fields, CardSearchTextField field,
    const std::vector<std::string>& values)
{
    if ((fields & field) == CardSearchTextField{}) {
        return;
    }
    append_tokens_from_values(tokens, values);
}

std::vector<std::string> build_text_tokens(const CardSearchDocument& doc, CardSearchTextField fields)
{
    std::vector<std::string> tokens;
    append_field_tokens(tokens, fields, CardSearchTextField::Key, doc.key);
    append_field_tokens(tokens, fields, CardSearchTextField::Name, doc.name);
    append_field_tokens(tokens, fields, CardSearchTextField::RulesText, doc.rules_text);
    append_field_tokens(tokens, fields, CardSearchTextField::FlavorText, doc.flavor_text);
    append_field_tokens(tokens, fields, CardSearchTextField::ArtId, doc.art_id);
    append_field_tokens(tokens, fields, CardSearchTextField::SetCode, doc.set_code);
    append_field_tokens(tokens, fields, CardSearchTextField::Rarity, doc.rarity);
    append_field_tokens(tokens, fields, CardSearchTextField::Tags, doc.tags);
    append_field_tokens(tokens, fields, CardSearchTextField::UnitTypes, doc.unit_types);
    append_field_tokens(tokens, fields, CardSearchTextField::Keywords, doc.keywords);
    append_field_tokens(tokens, fields, CardSearchTextField::Abilities, doc.abilities);
    append_field_tokens(tokens, fields, CardSearchTextField::AbilityNames, doc.ability_names);
    append_field_tokens(tokens, fields, CardSearchTextField::EffectKeys, doc.effect_keys);
    append_field_tokens(tokens, fields, CardSearchTextField::PassiveAbilities, doc.passive_ability_ids);
    append_field_tokens(tokens, fields, CardSearchTextField::SearchAliases, doc.search_aliases);
    if ((fields & CardSearchTextField::CustomFacets) != CardSearchTextField{}) {
        for (const auto& [facet_id, values] : doc.custom_facets) {
            append_tokens_from_text(tokens, facet_id);
            append_tokens_from_values(tokens, values);
        }
    }
    std::sort(tokens.begin(), tokens.end());
    tokens.erase(std::unique(tokens.begin(), tokens.end()), tokens.end());
    return tokens;
}

struct InvertedIndex {
    std::unordered_map<std::string, std::unordered_map<std::string, std::vector<size_t>>> facets{};
    std::unordered_map<std::string, std::vector<size_t>> tokens{};

    void clear()
    {
        facets.clear();
        tokens.clear();
    }

    void add_posting(std::vector<size_t>& postings, size_t doc_index)
    {
        const auto it = std::lower_bound(postings.begin(), postings.end(), doc_index);
        if (it != postings.end() && *it == doc_index) {
            return;
        }
        postings.insert(it, doc_index);
    }

    void remove_posting(std::vector<size_t>& postings, size_t doc_index)
    {
        const auto it = std::lower_bound(postings.begin(), postings.end(), doc_index);
        if (it != postings.end() && *it == doc_index) {
            postings.erase(it);
        }
    }

    void add_facet(const std::string& group, const std::string& value, size_t doc_index)
    {
        if (value.empty()) {
            return;
        }
        add_posting(facets[group][to_lower_ascii(value)], doc_index);
    }

    void remove_facet(const std::string& group, const std::string& value, size_t doc_index)
    {
        if (value.empty()) {
            return;
        }
        const auto git = facets.find(group);
        if (git == facets.end()) {
            return;
        }
        const auto vit = git->second.find(to_lower_ascii(value));
        if (vit == git->second.end()) {
            return;
        }
        remove_posting(vit->second, doc_index);
        if (vit->second.empty()) {
            git->second.erase(vit);
        }
        if (git->second.empty()) {
            facets.erase(git);
        }
    }

    void add_token(const std::string& token, size_t doc_index)
    {
        if (token.empty()) {
            return;
        }
        add_posting(tokens[token], doc_index);
    }

    void remove_token(const std::string& token, size_t doc_index)
    {
        if (token.empty()) {
            return;
        }
        const auto it = tokens.find(token);
        if (it == tokens.end()) {
            return;
        }
        remove_posting(it->second, doc_index);
        if (it->second.empty()) {
            tokens.erase(it);
        }
    }

    const std::vector<size_t>* facet_postings(const std::string& group, const std::string& value) const
    {
        const auto git = facets.find(group);
        if (git == facets.end()) {
            return nullptr;
        }
        const auto vit = git->second.find(to_lower_ascii(value));
        if (vit == git->second.end()) {
            return nullptr;
        }
        return &vit->second;
    }

    const std::vector<size_t>* token_postings(const std::string& token) const
    {
        const auto it = tokens.find(token);
        if (it == tokens.end()) {
            return nullptr;
        }
        return &it->second;
    }
};

InvertedIndex g_inverted_index;

void intersect_postings(std::vector<size_t>& candidates, const std::vector<size_t>& postings)
{
    if (candidates.empty()) {
        candidates = postings;
        return;
    }
    std::vector<size_t> next;
    next.reserve(std::min(candidates.size(), postings.size()));
    std::set_intersection(candidates.begin(), candidates.end(), postings.begin(), postings.end(),
        std::back_inserter(next));
    candidates.swap(next);
}

void union_postings(std::vector<size_t>& candidates, const std::vector<size_t>& postings)
{
    if (candidates.empty()) {
        candidates = postings;
        return;
    }
    std::vector<size_t> next;
    next.reserve(candidates.size() + postings.size());
    std::set_union(candidates.begin(), candidates.end(), postings.begin(), postings.end(), std::back_inserter(next));
    candidates.swap(next);
}

void subtract_postings(std::vector<size_t>& candidates, const std::vector<size_t>& postings)
{
    if (candidates.empty() || postings.empty()) {
        return;
    }
    std::vector<size_t> next;
    next.reserve(candidates.size());
    std::set_difference(candidates.begin(), candidates.end(), postings.begin(), postings.end(),
        std::back_inserter(next));
    candidates.swap(next);
}

bool facet_any_match(const InvertedIndex& index, const std::string& group, const std::vector<std::string>& values,
    std::vector<size_t>& scratch)
{
    if (values.empty()) {
        return false;
    }
    scratch.clear();
    for (const std::string& value : values) {
        if (const std::vector<size_t>* postings = index.facet_postings(group, value)) {
            union_postings(scratch, *postings);
        }
    }
    return !scratch.empty();
}

bool facet_all_match(const InvertedIndex& index, const std::string& group, const std::vector<std::string>& values,
    std::vector<size_t>& scratch)
{
    if (values.empty()) {
        return false;
    }
    scratch.clear();
    bool first = true;
    for (const std::string& value : values) {
        const std::vector<size_t>* postings = index.facet_postings(group, value);
        if (!postings) {
            scratch.clear();
            return true;
        }
        if (first) {
            scratch = *postings;
            first = false;
        } else {
            intersect_postings(scratch, *postings);
        }
    }
    return !first;
}

bool document_matches_query(const CardSearchDocument& doc, const CardSearchQuery& query)
{
    if (!query.types_none.empty()) {
        const std::string type_lower = to_lower_ascii(doc.type);
        for (const std::string& blocked : query.types_none) {
            if (type_lower == to_lower_ascii(blocked)) {
                return false;
            }
        }
    }
    if (!query.tags_none.empty()) {
        for (const std::string& blocked : query.tags_none) {
            const std::string b = to_lower_ascii(blocked);
            for (const std::string& tag : doc.tags) {
                if (to_lower_ascii(tag) == b) {
                    return false;
                }
            }
        }
    }
    if (!query.unit_types_none.empty()) {
        for (const std::string& blocked : query.unit_types_none) {
            const std::string b = to_lower_ascii(blocked);
            for (const std::string& ut : doc.unit_types) {
                if (to_lower_ascii(ut) == b) {
                    return false;
                }
            }
            if (doc.unit_type && to_lower_ascii(*doc.unit_type) == b) {
                return false;
            }
        }
    }
    if (!query.keywords_none.empty()) {
        for (const std::string& blocked : query.keywords_none) {
            const std::string b = to_lower_ascii(blocked);
            for (const std::string& kw : doc.keywords) {
                if (to_lower_ascii(kw) == b) {
                    return false;
                }
            }
        }
    }
    if (!query.energy_types_none.empty()) {
        for (const std::string& blocked : query.energy_types_none) {
            const std::string b = to_lower_ascii(blocked);
            bool found = false;
            for (const auto& et : kEnergyBillingAllTypes) {
                if (energy_filter_matches_type(b, et) && (doc.energy_type_mask & energy_type_bit(et)) != 0) {
                    found = true;
                    break;
                }
            }
            if (found) {
                return false;
            }
        }
    }

    if (!query.tags_all.empty()) {
        for (const std::string& required : query.tags_all) {
            const std::string r = to_lower_ascii(required);
            bool found = false;
            for (const std::string& tag : doc.tags) {
                if (to_lower_ascii(tag) == r) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                return false;
            }
        }
    }
    if (!query.unit_types_all.empty()) {
        for (const std::string& required : query.unit_types_all) {
            const std::string r = to_lower_ascii(required);
            bool found = false;
            for (const std::string& ut : doc.unit_types) {
                if (to_lower_ascii(ut) == r) {
                    found = true;
                    break;
                }
            }
            if (!found && (!doc.unit_type || to_lower_ascii(*doc.unit_type) != r)) {
                return false;
            }
        }
    }
    if (!query.keywords_all.empty()) {
        for (const std::string& required : query.keywords_all) {
            const std::string r = to_lower_ascii(required);
            bool found = false;
            for (const std::string& kw : doc.keywords) {
                if (to_lower_ascii(kw) == r) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                return false;
            }
        }
    }
    if (!query.abilities_all.empty()) {
        for (const std::string& required : query.abilities_all) {
            const std::string r = to_lower_ascii(required);
            bool found = false;
            for (const std::string& ab : doc.abilities) {
                if (to_lower_ascii(ab) == r) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                return false;
            }
        }
    }
    if (!query.energy_types_all.empty()) {
        for (const std::string& required : query.energy_types_all) {
            const std::string r = to_lower_ascii(required);
            bool found = false;
            for (const auto& et : kEnergyBillingAllTypes) {
                if (energy_filter_matches_type(r, et) && (doc.energy_type_mask & energy_type_bit(et)) != 0) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                return false;
            }
        }
    }

    if (!query.custom_facets_any.empty()) {
        for (const auto& [facet_id, allowed_values] : query.custom_facets_any) {
            const auto fit = doc.custom_facets.find(facet_id);
            if (fit == doc.custom_facets.end()) {
                return false;
            }
            bool matched = false;
            for (const std::string& allowed : allowed_values) {
                const std::string a = to_lower_ascii(allowed);
                for (const std::string& value : fit->second) {
                    if (to_lower_ascii(value) == a) {
                        matched = true;
                        break;
                    }
                }
                if (matched) {
                    break;
                }
            }
            if (!matched) {
                return false;
            }
        }
    }

    if (!numeric_in_range(doc.total_energy_cost, query.total_energy_cost)) {
        return false;
    }
    if (!numeric_optional_in_range(doc.base_health, query.health)) {
        return false;
    }
    if (!numeric_optional_in_range(doc.movement, query.movement)) {
        return false;
    }
    if (!numeric_optional_in_range(doc.melee_damage, query.melee_damage)) {
        return false;
    }
    if (!numeric_optional_in_range(doc.ranged_damage, query.ranged_damage)) {
        return false;
    }
    if (!numeric_optional_in_range(doc.footprint_tiles, query.footprint_tiles)) {
        return false;
    }

    return true;
}

void index_document(const CardSearchDocument& doc, size_t doc_index)
{
    g_inverted_index.add_facet("type", doc.type, doc_index);
    for (const std::string& tag : doc.tags) {
        g_inverted_index.add_facet("tag", tag, doc_index);
    }
    for (const std::string& ut : doc.unit_types) {
        g_inverted_index.add_facet("unit_type", ut, doc_index);
    }
    if (doc.unit_type) {
        g_inverted_index.add_facet("unit_type", *doc.unit_type, doc_index);
    }
    for (const std::string& kw : doc.keywords) {
        g_inverted_index.add_facet("keyword", kw, doc_index);
    }
    for (const std::string& ab : doc.abilities) {
        g_inverted_index.add_facet("ability", ab, doc_index);
    }
    for (const std::string& ek : doc.effect_keys) {
        g_inverted_index.add_facet("effect_key", ek, doc_index);
    }
    for (const std::string& pid : doc.passive_ability_ids) {
        g_inverted_index.add_facet("passive", pid, doc_index);
    }
    for (const auto& et : kEnergyBillingAllTypes) {
        if ((doc.energy_type_mask & energy_type_bit(et)) != 0) {
            for (const std::string& key : energy_search_keys(et)) {
                g_inverted_index.add_facet("energy", key, doc_index);
            }
        }
    }
    if (doc.spell_speed) {
        g_inverted_index.add_facet("spell_speed", effect_speed_to_string(*doc.spell_speed), doc_index);
    }
    if (doc.attack_type) {
        g_inverted_index.add_facet("attack_type", attack_type_to_string(*doc.attack_type), doc_index);
    }
    if (doc.entity_type) {
        g_inverted_index.add_facet("entity_type", *doc.entity_type, doc_index);
    }
    if (!doc.set_code.empty()) {
        g_inverted_index.add_facet("set", doc.set_code, doc_index);
    }
    if (!doc.rarity.empty()) {
        g_inverted_index.add_facet("rarity", doc.rarity, doc_index);
    }
    for (const auto& [facet_id, values] : doc.custom_facets) {
        for (const std::string& value : values) {
            g_inverted_index.add_facet("custom:" + facet_id, value, doc_index);
        }
    }
    for (const std::string& token : doc.text_tokens) {
        g_inverted_index.add_token(token, doc_index);
    }
}

void unindex_document(const CardSearchDocument& doc, size_t doc_index)
{
    g_inverted_index.remove_facet("type", doc.type, doc_index);
    for (const std::string& tag : doc.tags) {
        g_inverted_index.remove_facet("tag", tag, doc_index);
    }
    for (const std::string& ut : doc.unit_types) {
        g_inverted_index.remove_facet("unit_type", ut, doc_index);
    }
    if (doc.unit_type) {
        g_inverted_index.remove_facet("unit_type", *doc.unit_type, doc_index);
    }
    for (const std::string& kw : doc.keywords) {
        g_inverted_index.remove_facet("keyword", kw, doc_index);
    }
    for (const std::string& ab : doc.abilities) {
        g_inverted_index.remove_facet("ability", ab, doc_index);
    }
    for (const std::string& ek : doc.effect_keys) {
        g_inverted_index.remove_facet("effect_key", ek, doc_index);
    }
    for (const std::string& pid : doc.passive_ability_ids) {
        g_inverted_index.remove_facet("passive", pid, doc_index);
    }
    for (const auto& et : kEnergyBillingAllTypes) {
        if ((doc.energy_type_mask & energy_type_bit(et)) != 0) {
            for (const std::string& key : energy_search_keys(et)) {
                g_inverted_index.remove_facet("energy", key, doc_index);
            }
        }
    }
    if (doc.spell_speed) {
        g_inverted_index.remove_facet("spell_speed", effect_speed_to_string(*doc.spell_speed), doc_index);
    }
    if (doc.attack_type) {
        g_inverted_index.remove_facet("attack_type", attack_type_to_string(*doc.attack_type), doc_index);
    }
    if (doc.entity_type) {
        g_inverted_index.remove_facet("entity_type", *doc.entity_type, doc_index);
    }
    if (!doc.set_code.empty()) {
        g_inverted_index.remove_facet("set", doc.set_code, doc_index);
    }
    if (!doc.rarity.empty()) {
        g_inverted_index.remove_facet("rarity", doc.rarity, doc_index);
    }
    for (const auto& [facet_id, values] : doc.custom_facets) {
        for (const std::string& value : values) {
            g_inverted_index.remove_facet("custom:" + facet_id, value, doc_index);
        }
    }
    for (const std::string& token : doc.text_tokens) {
        g_inverted_index.remove_token(token, doc_index);
    }
}

bool query_has_facet_filters(const CardSearchQuery& query)
{
    return !query.types_any.empty() || !query.tags_any.empty() || !query.unit_types_any.empty()
        || !query.keywords_any.empty() || !query.abilities_any.empty() || !query.effect_keys_any.empty()
        || !query.passive_abilities_any.empty() || !query.energy_types_any.empty() || !query.spell_speeds_any.empty()
        || !query.attack_types_any.empty() || !query.entity_types_any.empty() || !query.sets_any.empty()
        || !query.rarities_any.empty() || !query.custom_facets_any.empty() || !query.tags_all.empty()
        || !query.unit_types_all.empty() || !query.keywords_all.empty() || !query.abilities_all.empty()
        || !query.energy_types_all.empty() || !query.types_none.empty() || !query.tags_none.empty()
        || !query.unit_types_none.empty() || !query.keywords_none.empty() || !query.energy_types_none.empty()
        || query.total_energy_cost.min || query.total_energy_cost.max || query.health.min || query.health.max
        || query.movement.min || query.movement.max || query.melee_damage.min || query.melee_damage.max
        || query.ranged_damage.min || query.ranged_damage.max || query.footprint_tiles.min
        || query.footprint_tiles.max;
}

bool query_has_text_filter(const CardSearchQuery& query)
{
    return !trim_ascii(query.text.text).empty();
}

bool query_is_empty(const CardSearchQuery& query)
{
    return !query_has_facet_filters(query) && !query_has_text_filter(query);
}

void sort_results(std::vector<size_t>& indices, const CardSearchIndex& index, CardSearchSort sort)
{
    const auto cmp_name_asc = [&index](size_t a, size_t b) {
        const auto& da = index.documents[a];
        const auto& db = index.documents[b];
        if (da.name != db.name) {
            return da.name < db.name;
        }
        return da.key < db.key;
    };
    switch (sort) {
        case CardSearchSort::NameDesc:
            std::sort(indices.begin(), indices.end(), [&](size_t a, size_t b) { return cmp_name_asc(b, a); });
            break;
        case CardSearchSort::CostAsc:
            std::sort(indices.begin(), indices.end(), [&](size_t a, size_t b) {
                const auto& da = index.documents[a];
                const auto& db = index.documents[b];
                if (da.total_energy_cost != db.total_energy_cost) {
                    return da.total_energy_cost < db.total_energy_cost;
                }
                return cmp_name_asc(a, b);
            });
            break;
        case CardSearchSort::CostDesc:
            std::sort(indices.begin(), indices.end(), [&](size_t a, size_t b) {
                const auto& da = index.documents[a];
                const auto& db = index.documents[b];
                if (da.total_energy_cost != db.total_energy_cost) {
                    return da.total_energy_cost > db.total_energy_cost;
                }
                return cmp_name_asc(a, b);
            });
            break;
        case CardSearchSort::TypeThenName:
            std::sort(indices.begin(), indices.end(), [&](size_t a, size_t b) {
                const auto& da = index.documents[a];
                const auto& db = index.documents[b];
                if (da.type != db.type) {
                    return da.type < db.type;
                }
                return cmp_name_asc(a, b);
            });
            break;
        case CardSearchSort::Default:
        case CardSearchSort::NameAsc:
        default:
            std::sort(indices.begin(), indices.end(), cmp_name_asc);
            break;
    }
}

bool try_parse_numeric_range_token(const std::string& raw, CardSearchNumericRange& out)
{
    std::string s = trim_ascii(raw);
    if (s.empty()) {
        return false;
    }
    if (s.rfind("<=", 0) == 0) {
        out.max = std::stoi(s.substr(2));
        return true;
    }
    if (s.rfind(">=", 0) == 0) {
        out.min = std::stoi(s.substr(2));
        return true;
    }
    if (s.rfind("<", 0) == 0) {
        out.max = std::stoi(s.substr(1)) - 1;
        return true;
    }
    if (s.rfind(">", 0) == 0) {
        out.min = std::stoi(s.substr(1)) + 1;
        return true;
    }
    const auto dash = s.find('-');
    if (dash != std::string::npos && dash > 0) {
        out.min = std::stoi(s.substr(0, dash));
        out.max = std::stoi(s.substr(dash + 1));
        return true;
    }
    const int exact = std::stoi(s);
    out.min = exact;
    out.max = exact;
    return true;
}

void push_unique_string(std::vector<std::string>& out, const std::string& value)
{
    if (value.empty()) {
        return;
    }
    if (std::find(out.begin(), out.end(), value) == out.end()) {
        out.push_back(value);
    }
}

void apply_facet_any_filter(std::vector<size_t>& candidates, const InvertedIndex& index, const std::string& group,
    const std::vector<std::string>& values, std::vector<size_t>& scratch)
{
    if (values.empty()) {
        return;
    }
    if (!facet_any_match(index, group, values, scratch)) {
        candidates.clear();
        return;
    }
    intersect_postings(candidates, scratch);
}

void apply_facet_all_filter(std::vector<size_t>& candidates, const InvertedIndex& index, const std::string& group,
    const std::vector<std::string>& values, std::vector<size_t>& scratch)
{
    if (values.empty()) {
        return;
    }
    if (facet_all_match(index, group, values, scratch)) {
        intersect_postings(candidates, scratch);
    }
}

void apply_facet_none_filter(std::vector<size_t>& candidates, const InvertedIndex& index, const std::string& group,
    const std::vector<std::string>& values, std::vector<size_t>& scratch)
{
    if (values.empty()) {
        return;
    }
    if (facet_any_match(index, group, values, scratch)) {
        subtract_postings(candidates, scratch);
    }
}

}  // namespace

void CardSearchIndex::clear()
{
    documents.clear();
    id_to_doc_index.clear();
    catalog_generation = 0;
}

void clear_card_search_index()
{
    std::lock_guard<std::mutex> lock(g_card_search_mutex);
    g_card_search_index.clear();
    g_inverted_index.clear();
}

CardSearchDocument build_card_search_document(const CardDefinition& def)
{
    CardSearchDocument doc;
    doc.key = def.key;
    doc.name = def.name;
    doc.type = def.type;
    doc.rules_text = def.rules_text;
    doc.flavor_text = def.flavor_text;
    doc.art_id = def.art_id;
    doc.set_code = def.set_code;
    doc.rarity = def.rarity;
    doc.collector_number = def.collector_number;
    doc.tags = def.tags;
    doc.unit_types = def.unit_types;
    doc.keywords = collect_keywords(def);
    doc.abilities = def.abilities;
    doc.effect_keys = collect_effect_keys(def);
    doc.passive_ability_ids = collect_passive_ids(def);
    doc.search_aliases = def.search_aliases;
    doc.custom_facets = def.search_facets;
    doc.energy_cost = def.energy_cost;
    doc.total_energy_cost = sum_energy_cost(def.energy_cost);
    for (const auto& [type, amount] : def.energy_cost) {
        if (amount > 0) {
            doc.energy_type_mask |= energy_type_bit(type);
        }
    }

    ensure_builtin_ability_catalog_loaded();
    for (const std::string& ability_id : def.abilities) {
        AbilitySpec ability;
        if (try_get_ability_from_catalog(ability_id, ability) && !ability.name.empty()) {
            doc.ability_names.push_back(ability.name);
        }
    }

    if (def.spell) {
        doc.spell_speed = def.spell->speed;
    }
    if (def.unit) {
        doc.attack_type = def.unit->attack_type;
        doc.entity_type = def.unit->entity_type;
        doc.unit_type = def.unit->unit_type;
        doc.base_health = def.unit->base_health;
        doc.movement = def.unit->movement;
        doc.melee_damage = def.unit->melee_damage > 0 ? std::optional<int>{def.unit->melee_damage}
                                                       : std::optional<int>{def.unit->melee_damage_max};
        doc.ranged_damage = def.unit->ranged_damage > 0 ? std::optional<int>{def.unit->ranged_damage}
                                                        : std::optional<int>{def.unit->ranged_damage_max};
        doc.footprint_tiles = static_cast<int>(def.unit->shape.size());
    }

    doc.text_tokens = build_text_tokens(doc, CardSearchTextField::All);
    return doc;
}

void upsert_card_search_document(const CardDefinition& def, CardDefId id)
{
    if (def.key.empty() || !id.is_valid()) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_card_search_mutex);
    CardSearchDocument doc = build_card_search_document(def);
    doc.id = id;

    if (const auto it = g_card_search_index.id_to_doc_index.find(id); it != g_card_search_index.id_to_doc_index.end()) {
        const size_t doc_index = it->second;
        unindex_document(g_card_search_index.documents[doc_index], doc_index);
        g_card_search_index.documents[doc_index] = std::move(doc);
        index_document(g_card_search_index.documents[doc_index], doc_index);
        return;
    }

    const size_t doc_index = g_card_search_index.documents.size();
    g_card_search_index.documents.push_back(std::move(doc));
    g_card_search_index.id_to_doc_index[id] = doc_index;
    index_document(g_card_search_index.documents[doc_index], doc_index);
}

void sync_card_search_index_generation(uint64_t catalog_generation)
{
    std::lock_guard<std::mutex> lock(g_card_search_mutex);
    g_card_search_index.catalog_generation = catalog_generation;
}

void rebuild_card_search_index()
{
    std::vector<CardDefinition> defs = list_card_catalog_definitions();

    std::lock_guard<std::mutex> lock(g_card_search_mutex);
    g_card_search_index.clear();
    g_inverted_index.clear();
    g_card_search_index.catalog_generation = card_catalog_generation();
    g_card_search_index.documents.reserve(defs.size());

    for (const CardDefinition& def : defs) {
        if (def.key.empty()) {
            continue;
        }
        CardDefId id = try_card_def_id_for_key(def.key);
        if (!id.is_valid()) {
            continue;
        }
        CardSearchDocument doc = build_card_search_document(def);
        doc.id = id;
        const size_t doc_index = g_card_search_index.documents.size();
        g_card_search_index.documents.push_back(std::move(doc));
        g_card_search_index.id_to_doc_index[id] = doc_index;
    }

    for (size_t i = 0; i < g_card_search_index.documents.size(); ++i) {
        index_document(g_card_search_index.documents[i], i);
    }
}

const CardSearchIndex& card_search_index()
{
    ensure_card_search_index_current();
    std::lock_guard<std::mutex> lock(g_card_search_mutex);
    return g_card_search_index;
}

void ensure_card_search_index_current()
{
    {
        std::lock_guard<std::mutex> lock(g_card_search_mutex);
        if (!g_card_search_index.documents.empty()) {
            return;
        }
    }
    if (list_card_catalog_keys_sorted().empty()) {
        return;
    }
    rebuild_card_search_index();
}

std::vector<CardDefId> search_card_def_ids(const CardSearchQuery& query)
{
    ensure_card_search_index_current();
    std::lock_guard<std::mutex> lock(g_card_search_mutex);
    const CardSearchIndex& index = g_card_search_index;

    std::vector<size_t> candidates;
    candidates.reserve(index.documents.size());
    for (size_t i = 0; i < index.documents.size(); ++i) {
        candidates.push_back(i);
    }

    std::vector<size_t> scratch;
    apply_facet_any_filter(candidates, g_inverted_index, "type", query.types_any, scratch);
    apply_facet_any_filter(candidates, g_inverted_index, "tag", query.tags_any, scratch);
    apply_facet_any_filter(candidates, g_inverted_index, "unit_type", query.unit_types_any, scratch);
    apply_facet_any_filter(candidates, g_inverted_index, "keyword", query.keywords_any, scratch);
    apply_facet_any_filter(candidates, g_inverted_index, "ability", query.abilities_any, scratch);
    apply_facet_any_filter(candidates, g_inverted_index, "effect_key", query.effect_keys_any, scratch);
    apply_facet_any_filter(candidates, g_inverted_index, "passive", query.passive_abilities_any, scratch);
    apply_facet_any_filter(candidates, g_inverted_index, "energy", query.energy_types_any, scratch);
    apply_facet_any_filter(candidates, g_inverted_index, "spell_speed", query.spell_speeds_any, scratch);
    apply_facet_any_filter(candidates, g_inverted_index, "attack_type", query.attack_types_any, scratch);
    apply_facet_any_filter(candidates, g_inverted_index, "entity_type", query.entity_types_any, scratch);
    apply_facet_any_filter(candidates, g_inverted_index, "set", query.sets_any, scratch);
    apply_facet_any_filter(candidates, g_inverted_index, "rarity", query.rarities_any, scratch);
    for (const auto& [facet_id, values] : query.custom_facets_any) {
        apply_facet_any_filter(candidates, g_inverted_index, "custom:" + facet_id, values, scratch);
    }
    apply_facet_all_filter(candidates, g_inverted_index, "tag", query.tags_all, scratch);
    apply_facet_all_filter(candidates, g_inverted_index, "unit_type", query.unit_types_all, scratch);
    apply_facet_all_filter(candidates, g_inverted_index, "keyword", query.keywords_all, scratch);
    apply_facet_all_filter(candidates, g_inverted_index, "ability", query.abilities_all, scratch);
    apply_facet_all_filter(candidates, g_inverted_index, "energy", query.energy_types_all, scratch);

    apply_facet_none_filter(candidates, g_inverted_index, "type", query.types_none, scratch);
    apply_facet_none_filter(candidates, g_inverted_index, "tag", query.tags_none, scratch);
    apply_facet_none_filter(candidates, g_inverted_index, "unit_type", query.unit_types_none, scratch);
    apply_facet_none_filter(candidates, g_inverted_index, "keyword", query.keywords_none, scratch);
    apply_facet_none_filter(candidates, g_inverted_index, "energy", query.energy_types_none, scratch);

    if (query_has_text_filter(query)) {
        const std::vector<std::string> text_tokens = tokenize_search_text(query.text.text);
        if (!text_tokens.empty()) {
            if (query.text.match_all_tokens) {
                for (const std::string& token : text_tokens) {
                    const std::vector<size_t>* postings = g_inverted_index.token_postings(token);
                    if (!postings || postings->empty()) {
                        candidates.clear();
                        break;
                    }
                    intersect_postings(candidates, *postings);
                }
            } else {
                scratch.clear();
                for (const std::string& token : text_tokens) {
                    if (const std::vector<size_t>* postings = g_inverted_index.token_postings(token)) {
                        union_postings(scratch, *postings);
                    }
                }
                intersect_postings(candidates, scratch);
            }
        }
    }

    if (query.total_energy_cost.min || query.total_energy_cost.max || query.health.min || query.health.max
        || query.movement.min || query.movement.max || query.melee_damage.min || query.melee_damage.max
        || query.ranged_damage.min || query.ranged_damage.max || query.footprint_tiles.min
        || query.footprint_tiles.max || !query.tags_all.empty() || !query.unit_types_all.empty()
        || !query.keywords_all.empty() || !query.abilities_all.empty() || !query.energy_types_all.empty()
        || !query.types_none.empty() || !query.tags_none.empty() || !query.unit_types_none.empty()
        || !query.keywords_none.empty() || !query.energy_types_none.empty() || !query.custom_facets_any.empty()) {
        std::vector<size_t> filtered;
        filtered.reserve(candidates.size());
        for (size_t idx : candidates) {
            if (document_matches_query(index.documents[idx], query)) {
                filtered.push_back(idx);
            }
        }
        candidates.swap(filtered);
    }

    sort_results(candidates, index, query.sort);

    std::vector<CardDefId> out;
    out.reserve(candidates.size());
    for (size_t idx : candidates) {
        out.push_back(index.documents[idx].id);
    }
    return out;
}

std::vector<std::string> search_card_keys(const CardSearchQuery& query)
{
    const std::vector<CardDefId> ids = search_card_def_ids(query);
    std::vector<std::string> keys;
    keys.reserve(ids.size());
    ensure_card_search_index_current();
    std::lock_guard<std::mutex> lock(g_card_search_mutex);
    for (CardDefId id : ids) {
        const auto it = g_card_search_index.id_to_doc_index.find(id);
        if (it != g_card_search_index.id_to_doc_index.end()) {
            keys.push_back(g_card_search_index.documents[it->second].key);
        }
    }
    return keys;
}

std::vector<CardSearchFacetGroup> list_card_search_facet_groups()
{
    ensure_card_search_index_current();
    std::lock_guard<std::mutex> lock(g_card_search_mutex);
    std::vector<CardSearchFacetGroup> groups;
    for (const auto& [group_id, values] : g_inverted_index.facets) {
        CardSearchFacetGroup group;
        group.facet_id = group_id;
        group.values.reserve(values.size());
        for (const auto& [value, postings] : values) {
            group.values.push_back({value, static_cast<int>(postings.size())});
        }
        std::sort(group.values.begin(), group.values.end(),
            [](const CardSearchFacetValueCount& a, const CardSearchFacetValueCount& b) {
                if (a.count != b.count) {
                    return a.count > b.count;
                }
                return a.value < b.value;
            });
        groups.push_back(std::move(group));
    }
    std::sort(groups.begin(), groups.end(),
        [](const CardSearchFacetGroup& a, const CardSearchFacetGroup& b) { return a.facet_id < b.facet_id; });
    return groups;
}

bool try_parse_card_search_filter_string(const std::string& filter, CardSearchQuery& out, std::string& err_out)
{
    out = CardSearchQuery{};
    err_out.clear();
    std::istringstream stream(filter);
    std::string token;
    std::string free_text;
    while (stream >> token) {
        const auto colon = token.find(':');
        if (colon == std::string::npos) {
            if (!free_text.empty()) {
                free_text.push_back(' ');
            }
            free_text += token;
            continue;
        }
        const std::string key = to_lower_ascii(token.substr(0, colon));
        const std::string value = token.substr(colon + 1);
        if (value.empty()) {
            err_out = "Filter token missing value: " + token;
            return false;
        }
        try {
            if (key == "type" || key == "t") {
                push_unique_string(out.types_any, value);
            } else if (key == "-type") {
                push_unique_string(out.types_none, value);
            } else if (key == "tag") {
                push_unique_string(out.tags_any, value);
            } else if (key == "keyword" || key == "kw") {
                push_unique_string(out.keywords_any, value);
            } else if (key == "unittype" || key == "ut") {
                push_unique_string(out.unit_types_any, value);
            } else if (key == "ability" || key == "ab") {
                push_unique_string(out.abilities_any, value);
            } else if (key == "effect" || key == "fx") {
                push_unique_string(out.effect_keys_any, value);
            } else if (key == "passive") {
                push_unique_string(out.passive_abilities_any, value);
            } else if (key == "color" || key == "energy" || key == "c") {
                push_unique_string(out.energy_types_any, to_lower_ascii(value));
            } else if (key == "speed") {
                push_unique_string(out.spell_speeds_any, to_lower_ascii(value));
            } else if (key == "attack") {
                push_unique_string(out.attack_types_any, to_lower_ascii(value));
            } else if (key == "entity") {
                push_unique_string(out.entity_types_any, to_lower_ascii(value));
            } else if (key == "set") {
                push_unique_string(out.sets_any, value);
            } else if (key == "rarity" || key == "r") {
                push_unique_string(out.rarities_any, value);
            } else if (key == "cost" || key == "mv") {
                if (!try_parse_numeric_range_token(value, out.total_energy_cost)) {
                    err_out = "Invalid cost filter: " + value;
                    return false;
                }
            } else if (key == "health" || key == "hp") {
                if (!try_parse_numeric_range_token(value, out.health)) {
                    err_out = "Invalid health filter: " + value;
                    return false;
                }
            } else if (key == "move" || key == "movement") {
                if (!try_parse_numeric_range_token(value, out.movement)) {
                    err_out = "Invalid movement filter: " + value;
                    return false;
                }
            } else if (key == "melee") {
                if (!try_parse_numeric_range_token(value, out.melee_damage)) {
                    err_out = "Invalid melee filter: " + value;
                    return false;
                }
            } else if (key == "ranged") {
                if (!try_parse_numeric_range_token(value, out.ranged_damage)) {
                    err_out = "Invalid ranged filter: " + value;
                    return false;
                }
            } else if (key == "footprint" || key == "size") {
                if (!try_parse_numeric_range_token(value, out.footprint_tiles)) {
                    err_out = "Invalid footprint filter: " + value;
                    return false;
                }
            } else if (key.rfind("facet:", 0) == 0 && key.size() > 6) {
                push_unique_string(out.custom_facets_any[key.substr(6)], value);
            } else {
                if (!free_text.empty()) {
                    free_text.push_back(' ');
                }
                free_text += token;
            }
        } catch (const std::exception&) {
            err_out = "Invalid numeric filter token: " + token;
            return false;
        }
    }
    out.text.text = trim_ascii(free_text);
    out.text.match_all_tokens = true;
    out.text.fields = CardSearchTextField::All;
    return true;
}

}  // namespace tactics
