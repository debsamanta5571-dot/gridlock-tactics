#pragma once

#include "tactics/cards/card_instances.hpp"
#include "tactics/cards/effect_definitions.hpp"
#include "tactics/common/types.hpp"
#include "tactics/energy/energy_zone.hpp"

#include <functional>
#include <map>
#include <optional>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace tactics {

struct Deck;

struct CardKeywordDefinition {
    std::string key;
    std::optional<int> amount{};
    /** Exalted play gate, e.g. `"flux_generated"`. Only used when `key == "exalted"`. */
    std::optional<std::string> requirement{};
};

struct CardEffectDefinition {
    std::string key;
    int amount{0};
};

struct UnitCardDefinition {
    std::string entity_type{"unit"};
    std::string unit_type{"Infantry"};
    AttackType attack_type{AttackType::Melee};
    int base_health{10};
    int current_health{10};
    int movement{5};
    int melee_range{1};
    int melee_damage{3};
    int melee_damage_min{0};
    int melee_damage_max{0};
    int ranged_range{0};
    int ranged_deadzone{0};
    int ranged_damage{0};
    int ranged_damage_min{0};
    int ranged_damage_max{0};
    int crit_chance_percent{kDefaultCritChancePercent};
    bool line_of_sight_blocked{false};
    std::vector<std::pair<int, int>> shape{{0, 0}};
    std::vector<CardKeywordDefinition> keywords{};
    std::vector<CardEffectDefinition> initial_effects{};
    /** Inline bespoke passives (merged after catalog `passives` ids). */
    std::vector<PassiveAbilitySpec> passive_abilities{};
    /** References into passive_catalog.json. */
    std::vector<std::string> passive_ability_ids{};
    /** Inline activated abilities (merged after top-level catalog `abilities` ids). */
    std::vector<AbilitySpec> activated_abilities{};
};

/** One selectable mode of a modal spell (the extensible "choose one of N" system). Each mode
 *  is a normal effect + its own targeting, chosen at cast time; the chosen mode's fields
 *  override the spell's top-level effect_key/payload/target_kind. A spell with a non-empty
 *  `modes` list is modal. */
struct SpellMode {
    std::string label;       // short button text, e.g. "Deal 5 damage"
    std::string rules_text;  // per-mode description for the tooltip
    std::string effect_key;
    std::map<std::string, int> effect_payload{};
    std::map<std::string, std::string> effect_string_payload{};
    std::optional<BoardTargetKind> board_target_kind{};
    bool requires_board_target{false};
};

struct SpellCardDefinition {
    EffectSpeed speed{EffectSpeed::Channeled};
    /** When set, pulls effect_key/payload/targeting from ability catalog (card `speed` still overrides if set in JSON). */
    std::string effect_ref;
    std::string effect_key{"generic_effect"};
    std::map<std::string, int> effect_payload{};
    /** Non-empty ⇒ this is a modal spell (choose one mode at cast). Empty ⇒ single-effect. */
    std::vector<SpellMode> modes{};
    /** String-valued payload fields (e.g. "shape" for directional_damage). */
    std::map<std::string, std::string> effect_string_payload{};
    std::optional<BoardTargetKind> board_target_kind{};
    std::optional<bool> requires_mandatory_board_cell{};
    /** Focus spells: max Chebyshev distance from caster to effect target (0 = unlimited). */
    int focus_range{0};
    /** When true, focus range/LOS uses the caster's effective ranged range (ranged/hybrid casters only). */
    bool use_caster_ranged_range{false};
    /** When true, focus targeting uses the caster's normal attack range/LOS (melee/ranged/hybrid). */
    bool use_caster_attack_range{false};
    bool explicit_speed{false};
    /** Focus caster must have one of these attack types (e.g. "ranged", "hybrid"). Empty = any. */
    std::vector<std::string> require_caster_attack_types{};
    std::vector<std::string> require_target_unit_types{};
    std::vector<std::string> bonus_damage_unit_types{};
    int bonus_damage_amount{0};
    /** X-cost spell: player chooses X at cast time; pay x_cost_amount extra of this energy type;
     *  X is injected into effect_payload["amount"]. nullopt = no variable cost. */
    std::optional<EnergyType> x_cost_energy_type{};
    int x_cost_min{0};
    /**
     * Chain: after the primary effect resolves, BFS flood-fill through connected entities
     * that satisfy `board_target_kind` and apply the same effect to each.
     * See `StackItem::chain` for the full runtime contract.
     */
    bool chain{false};
};

struct CardDefinition {
    std::string key;
    std::string name;
    std::string type;
    /** Full/advanced description: everything (numbers, range, conditions). Shown when Advanced view is on. */
    std::string rules_text;
    /** Normal (default) description: complete self-contained rules. When empty, UI falls back to `rules_text`. */
    std::string normal_rules_text;
    std::string flavor_text;
    std::string art_id;
    /** Optional collection metadata for deck-library filters. */
    std::string set_code;
    std::string rarity;
    std::string collector_number;
    /** Signature/token cards that do NOT count toward the hand limit and are never
     *  auto-discarded at end of turn (e.g. turn-order Field Requisition). */
    bool ignores_hand_limit{false};
    /** Extra names/phrases that should match text search (not shown on the card). */
    std::vector<std::string> search_aliases{};
    /** Open-ended facet buckets, e.g. `"role": ["control"]`, `"format": ["standard"]`. */
    std::map<std::string, std::vector<std::string>> search_facets{};
    std::vector<std::string> tags{};
    /** Optional tribe-like identifiers on any card type. */
    std::vector<std::string> unit_types{};
    std::map<EnergyType, int> energy_cost{};
    std::vector<CardKeywordDefinition> keywords{};
    /** Activated ability catalog ids (see ability_catalog.json). */
    std::vector<std::string> abilities{};
    /** Per-id patches applied after catalog lookup (must reference ids in `abilities`). */
    std::map<std::string, AbilityOverridePatch> ability_overrides{};
    std::optional<UnitCardDefinition> unit{};
    std::optional<SpellCardDefinition> spell{};
};

struct DeckListEntry {
    std::string card_key;
    int copies{0};
};

struct ZoneListEntry {
    std::string zone_id;
    std::string name;
    /** Card-art path slug, e.g. `territories/asteria` → `card_art/territories/asteria.png`. */
    std::string art_id;
    std::map<EnergyType, int> energy_produced{};
    int copies{1};
    // ── Conquering Territories definition (copied into the runtime EnergyZone) ──
    std::optional<EnergyType> color{};
    bool is_basic{false};
    bool enters_depleted{false};
    std::vector<TerritoryEffect> enter_effects{};
    std::vector<GroundworkTrigger> groundwork{};
    std::vector<TerritoryAbility> land_abilities{};
};

struct DeckListDefinition {
    std::string key{"starter"};
    std::vector<DeckListEntry> entries{};
    std::vector<DeckListEntry> reserves{};
    std::vector<ZoneListEntry> zones{};
};

struct CardCatalog {
    std::vector<CardDefinition> definitions;
    std::unordered_map<std::string, CardDefId> key_to_id;
    /**
     * Optional human-readable display names for set_codes, populated from "set_name"
     * in shard JSON top-level.  e.g. "99th_dieselheart_company" → "99th Dieselheart Company".
     * Used by set_code_display_name(); falls back to auto-generated name if absent.
     */
    std::unordered_map<std::string, std::string> set_display_names;
    /** Set codes declared in shard headers (includes empty shards before any cards load). */
    std::unordered_set<std::string> registered_set_codes;
    /** Optional shard-level color tags per faction, from top-level "colors" array. */
    std::unordered_map<std::string, std::vector<std::string>> set_colors;

    void clear();
    /** Insert or replace by `CardDefinition::key`; assigns stable `CardDefId`. */
    void upsert(CardDefinition def);
};

void clear_card_catalog();
bool load_card_catalog_from_json_utf8(const std::string& utf8, std::string& err_out);

/** Reads `catalogs/*.json` paths relative to `catalog_dir` (e.g. `TacticsData/cards/`). */
using CardCatalogFileReader = std::function<bool(const std::string& relative_path, std::string& out_utf8, std::string& err_out)>;
bool load_card_catalog_manifest_from_json_utf8(const std::string& manifest_utf8, const std::string& catalog_dir,
    const CardCatalogFileReader& read_file, std::string& err_out);
/** Loads manifest when present, otherwise a single legacy `card_catalog.json`. */
bool load_project_card_catalogs(const CardCatalogFileReader& read_file, std::string& err_out);
bool load_deck_list_from_json_utf8(const std::string& utf8, DeckListDefinition& out, std::string& err_out);
bool save_deck_list_to_json_utf8(const DeckListDefinition& deck, std::string& out_utf8, std::string& err_out);
bool validate_deck_list(const DeckListDefinition& deck, std::string& err_out);
enum class BuiltinDeckListSlot { Starter, Test };
bool load_builtin_deck_list_from_json_utf8(const std::string& utf8, BuiltinDeckListSlot slot, std::string& err_out);
bool load_starter_deck_list_from_json_utf8(const std::string& utf8, std::string& err_out);
bool load_test_deck_list_from_json_utf8(const std::string& utf8, std::string& err_out);
/** No-op: all card catalog content is now loaded from JSON shards at runtime.
 *  Kept for call-site compatibility. Will be removed in a future cleanup. */
void ensure_builtin_card_catalog_loaded();
bool try_get_card_definition(const std::string& key, CardDefinition& out);
bool try_get_card_definition(CardDefId id, CardDefinition& out);
CardDefId try_card_def_id_for_key(const std::string& key);
const CardDefinition* try_get_card_definition_ptr(const std::string& key);
const CardDefinition* try_get_card_definition_ptr(CardDefId id);
std::vector<std::string> list_card_catalog_keys_sorted();
/** Snapshot of all loaded definitions (sorted by key). */
std::vector<CardDefinition> list_card_catalog_definitions();
/** Monotonic generation counter; bumps when catalog contents change. */
uint64_t card_catalog_generation();

/**
 * Returns every unique non-empty set_code present in the loaded catalog, sorted.
 * Call after load_project_card_catalogs to include faction/set data from JSON shards.
 * Useful for dynamically building faction pickers without hardcoding faction names.
 */
std::vector<std::string> list_catalog_set_codes();
/** Set codes with at least one loaded card (excludes empty registered shards). */
std::vector<std::string> list_playable_set_codes();

/**
 * Best-effort human-readable display name for a set_code.
 * Converts "99th_dieselheart_company" → "99Th Dieselheart Company".
 * Override display names are loaded from set_metadata.json if present.
 */
std::string set_code_display_name(const std::string& set_code);

/** Color tags declared for a faction shard (empty when unset). */
std::vector<std::string> list_set_colors(const std::string& set_code);

/** Stable fingerprint of loaded card ids (for multiplayer catalog parity). */
std::string card_catalog_fingerprint_utf8();

void set_active_match_deck_list(DeckListDefinition deck);
std::optional<DeckListDefinition> get_active_match_deck_list();
void clear_active_match_deck_list();
/** Loaded `starter_deck.json` (empty when project content was not loaded). */
std::optional<DeckListDefinition> try_get_starter_deck_list();

/** Test / legacy snapshot: add or replace a definition at runtime. */
void register_runtime_card_definition(CardDefinition def);

Deck create_deck_from_deck_list(const DeckListDefinition& deck_list, std::mt19937& rng, bool shuffle = true, std::string* err_out = nullptr);
Deck create_starter_deck_from_catalog(std::mt19937& rng);
Deck create_test_deck_from_catalog(std::mt19937& rng);

/** Legacy/test helper: materializes a full card object (prefer catalog instances in match code). */
CardPtr create_card_from_definition(const CardDefinition& def, int copy_index);

}  // namespace tactics
