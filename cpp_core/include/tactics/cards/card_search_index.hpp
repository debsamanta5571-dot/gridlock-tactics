#pragma once

#include "tactics/cards/card_instances.hpp"
#include "tactics/cards/card_catalog.hpp"
#include "tactics/common/types.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace tactics {

/** Bitmask of which textual fields a free-text query searches. */
enum class CardSearchTextField : uint32_t {
    Key = 1u << 0,
    Name = 1u << 1,
    RulesText = 1u << 2,
    FlavorText = 1u << 3,
    Tags = 1u << 4,
    UnitTypes = 1u << 5,
    Keywords = 1u << 6,
    Abilities = 1u << 7,
    AbilityNames = 1u << 8,
    EffectKeys = 1u << 9,
    PassiveAbilities = 1u << 10,
    SetCode = 1u << 11,
    Rarity = 1u << 12,
    SearchAliases = 1u << 13,
    ArtId = 1u << 14,
    CustomFacets = 1u << 15,
    All = 0xFFFFFFFFu,
};

inline constexpr CardSearchTextField operator|(CardSearchTextField A, CardSearchTextField B)
{
    return static_cast<CardSearchTextField>(static_cast<uint32_t>(A) | static_cast<uint32_t>(B));
}

inline constexpr CardSearchTextField operator&(CardSearchTextField A, CardSearchTextField B)
{
    return static_cast<CardSearchTextField>(static_cast<uint32_t>(A) & static_cast<uint32_t>(B));
}

struct CardSearchTextQuery {
    std::string text;
    /** When true, every whitespace-separated token must match; otherwise any token may match. */
    bool match_all_tokens{true};
    CardSearchTextField fields{CardSearchTextField::All};
};

struct CardSearchNumericRange {
    std::optional<int> min{};
    std::optional<int> max{};
};

enum class CardSearchSort {
    Default,
    NameAsc,
    NameDesc,
    CostAsc,
    CostDesc,
    TypeThenName,
};

/**
 * Structured card-library query (MTG Arena / Scryfall-style facets + free text).
 * Empty facet vectors are ignored. `_any` = OR within the list, `_all` = AND across the list, `_none` = exclude.
 */
struct CardSearchQuery {
    CardSearchTextQuery text{};

    std::vector<std::string> types_any{};
    std::vector<std::string> types_none{};

    std::vector<std::string> tags_any{};
    std::vector<std::string> tags_all{};
    std::vector<std::string> tags_none{};

    std::vector<std::string> unit_types_any{};
    std::vector<std::string> unit_types_all{};
    std::vector<std::string> unit_types_none{};

    std::vector<std::string> keywords_any{};
    std::vector<std::string> keywords_all{};
    std::vector<std::string> keywords_none{};

    std::vector<std::string> abilities_any{};
    std::vector<std::string> abilities_all{};

    std::vector<std::string> effect_keys_any{};

    std::vector<std::string> passive_abilities_any{};

    /** Energy colors present in the card's cost (e.g. "red", "neutral", "omni"). */
    std::vector<std::string> energy_types_any{};
    std::vector<std::string> energy_types_all{};
    std::vector<std::string> energy_types_none{};

    std::vector<std::string> spell_speeds_any{};
    std::vector<std::string> attack_types_any{};
    std::vector<std::string> entity_types_any{};

    std::vector<std::string> sets_any{};
    std::vector<std::string> rarities_any{};

    /** Custom catalog facets: facet_id -> allowed values (OR within each facet group). */
    std::map<std::string, std::vector<std::string>> custom_facets_any{};

    CardSearchNumericRange total_energy_cost{};
    CardSearchNumericRange health{};
    CardSearchNumericRange movement{};
    CardSearchNumericRange melee_damage{};
    CardSearchNumericRange ranged_damage{};
    CardSearchNumericRange footprint_tiles{};

    CardSearchSort sort{CardSearchSort::Default};
};

/** Flattened searchable record for one catalog card (built from CardDefinition + resolved catalogs). */
struct CardSearchDocument {
    CardDefId id{};
    std::string key;
    std::string name;
    std::string type;
    std::string rules_text;
    std::string flavor_text;
    std::string art_id;
    std::string set_code;
    std::string rarity;
    std::string collector_number;

    std::vector<std::string> tags{};
    std::vector<std::string> unit_types{};
    std::vector<std::string> keywords{};
    std::vector<std::string> abilities{};
    std::vector<std::string> ability_names{};
    std::vector<std::string> effect_keys{};
    std::vector<std::string> passive_ability_ids{};
    std::vector<std::string> search_aliases{};
    std::map<std::string, std::vector<std::string>> custom_facets{};

    std::map<EnergyType, int> energy_cost{};
    int total_energy_cost{0};
    uint32_t energy_type_mask{0};

    std::optional<EffectSpeed> spell_speed{};
    std::optional<AttackType> attack_type{};
    std::optional<std::string> entity_type{};
    std::optional<std::string> unit_type{};

    std::optional<int> base_health{};
    std::optional<int> movement{};
    std::optional<int> melee_damage{};
    std::optional<int> ranged_damage{};
    std::optional<int> footprint_tiles{};

    /** Lowercase tokens derived from selected text fields (for inverted index). */
    std::vector<std::string> text_tokens{};
};

struct CardSearchFacetValueCount {
    std::string value;
    int count{0};
};

struct CardSearchFacetGroup {
    std::string facet_id;
    std::vector<CardSearchFacetValueCount> values{};
};

struct CardSearchIndex {
    std::vector<CardSearchDocument> documents{};
    std::unordered_map<CardDefId, size_t> id_to_doc_index{};
    uint64_t catalog_generation{0};

    void clear();
};

void clear_card_search_index();
/** Incrementally create or refresh the search document for one catalog card. */
void upsert_card_search_document(const CardDefinition& def, CardDefId id);
void sync_card_search_index_generation(uint64_t catalog_generation);
void rebuild_card_search_index();
const CardSearchIndex& card_search_index();
void ensure_card_search_index_current();

CardSearchDocument build_card_search_document(const CardDefinition& def);

/** Returns matching catalog keys in sort order. Empty query returns all cards. */
std::vector<std::string> search_card_keys(const CardSearchQuery& query);
std::vector<CardDefId> search_card_def_ids(const CardSearchQuery& query);

/** Facet buckets for filter UI chips (counts reflect full catalog, not a filtered subset). */
std::vector<CardSearchFacetGroup> list_card_search_facet_groups();

/**
 * Parses a compact filter string into a structured query.
 * Examples: `type:unit keyword:haste cost:<=3 red bolt`, `tag:starter set:core`
 */
bool try_parse_card_search_filter_string(const std::string& filter, CardSearchQuery& out, std::string& err_out);

}  // namespace tactics
