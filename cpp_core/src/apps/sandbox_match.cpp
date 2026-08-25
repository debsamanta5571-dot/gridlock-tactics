#include "tactics/apps/sandbox_match.hpp"

#include "tactics/board/board_layout.hpp"
#include "tactics/cards/card_catalog.hpp"
#include "tactics/cards/card_runtime.hpp"
#include "tactics/core.hpp"
#include "tactics/energy/energy_zone.hpp"
#include "tactics/entities/entity.hpp"

#include <algorithm>
#include <cctype>
#include <functional>
#include <optional>

namespace tactics {
namespace {

bool sandbox_skip_catalog_key(const std::string& key)
{
    // Skip internal test fixture cards - neither ftest_ nor test_ should appear in a playable hand.
    return key.rfind("ftest_", 0) == 0 || key.rfind("test_", 0) == 0;
}

std::shared_ptr<Unit> sandbox_make_building(int owner, const std::string& entity_id, const std::string& display_name, int hp)
{
    auto building = std::make_shared<Unit>();
    building->entity_id = entity_id;
    building->owner = owner;
    building->entity_type = "building";
    building->unit_type = display_name;
    building->attack_type = AttackType::Utility;
    building->base_health = hp;
    building->current_health = hp;
    building->movement = 0;
    building->shape = {{0, 0}};
    normalize_entity_shape(*building);
    return building;
}

bool sandbox_place_building(GameState& game, const std::shared_ptr<Unit>& building, int anchor_x, int anchor_y)
{
    if (!building || !game.board.place_entity(building, anchor_x, anchor_y)) {
        return false;
    }
    game.note_entity_placed(building);
    return true;
}

bool sandbox_place_catalog_unit(GameState& game, int owner, const std::string& card_key, int anchor_x, int anchor_y, const std::string& entity_id)
{
    const CardDefinition* def = try_get_card_definition_ptr(card_key);
    if (!def || !definition_is_unit(*def)) {
        return false;
    }
    CardInstance inst;
    const CardDefId def_id = try_card_def_id_for_key(card_key);
    if (!def_id.is_valid()) {
        return false;
    }
    inst.definition_id = def_id;
    inst.public_id = "sandbox_" + card_key;
    auto unit = create_unit_from_definition(*def, inst, owner, entity_id);
    if (unit) {
        unit->source_card_id = card_key;
    }
    if (!unit || !game.board.place_entity(unit, anchor_x, anchor_y)) {
        return false;
    }
    game.note_entity_placed(unit);
    return true;
}

bool sandbox_place_low_cover(GameState& game, const std::string& entity_id, int anchor_x, int anchor_y, int hp)
{
    auto cover = std::make_shared<Unit>();
    cover->entity_id = entity_id;
    cover->entity_type = "low_cover";
    cover->unit_type = "Low Cover";
    cover->attack_type = AttackType::Utility;
    cover->base_health = hp;
    cover->current_health = hp;
    cover->movement = 0;
    cover->line_of_sight_blocked = false;
    cover->shape = {{0, 0}};
    normalize_entity_shape(*cover);
    if (!game.board.place_entity(cover, anchor_x, anchor_y)) {
        return false;
    }
    game.note_entity_placed(cover);
    return true;
}

void sandbox_upgrade_to_ranged(Unit& u, int range, int damage)
{
    u.attack_type = AttackType::Ranged;
    u.ranged_deadzone = 0;
    u.ranged_range = range;
    unit_set_fixed_ranged_damage(u, damage);
}

void sandbox_move_deck_to_hand(GameState& game, int player_id)
{
    auto deck_it = game.players_decks.find(player_id);
    if (deck_it == game.players_decks.end()) {
        return;
    }
    Deck& deck = deck_it->second;
    deck.hand.insert(deck.hand.end(), deck.deck.begin(), deck.deck.end());
    deck.deck.clear();
    deck.sort_hand_by_energy_cost();
}

}  // namespace

bool game_id_is_sandbox(const std::string& game_id) { return game_id.find("sandbox") != std::string::npos; }

// ---------------------------------------------------------------------------
// Sandbox deck filters: faction (set_code) vs color (energy pool)
// ---------------------------------------------------------------------------
namespace {

std::string normalize_key(std::string k)
{
    std::transform(k.begin(), k.end(), k.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return k;
}

/** Legacy CLI shortcuts: color display name → primary faction for that color. */
std::optional<std::string> legacy_color_name_faction_shortcut(const std::string& k)
{
    if (k == "gallantry") return "99th_dieselheart_company";
    if (k == "ingenuity") return "asterian_civilian_militia";
    if (k == "mythology") return "the_lost_kingdom";
    return std::nullopt;
}

/**
 * Returns the canonical set_code for a faction key (not a color chroma alias).
 * Unknown → "__unknown__"; sentinels "__all__" / "__core__".
 */
std::string resolve_faction_set_code(const std::string& faction_key)
{
    const std::string k = normalize_key(faction_key);

    if (k.empty() || k == "all") {
        return "__all__";
    }
    if (k == "core") {
        return "__core__";
    }

    if (k.find("dieselheart") != std::string::npos) {
        return "99th_dieselheart_company";
    }
    if (k.find("asterian") != std::string::npos) {
        return "asterian_civilian_militia";
    }
    if (k == "lost_kingdom" || k == "the_lost_kingdom") {
        return "the_lost_kingdom";
    }

    const std::vector<std::string> known_codes = list_catalog_set_codes();
    if (std::find(known_codes.begin(), known_codes.end(), k) != known_codes.end()) {
        return k;
    }

    return "__unknown__";
}

bool card_matches_color(const CardDefinition& def, EnergyType color)
{
    const auto keys = energy_search_keys(color);
    if (const auto it = def.search_facets.find("color"); it != def.search_facets.end()) {
        for (const std::string& value : it->second) {
            const std::string lower = normalize_key(value);
            for (const std::string& key : keys) {
                if (lower == key) {
                    return true;
                }
            }
        }
    }
    const auto cost_it = def.energy_cost.find(color);
    return cost_it != def.energy_cost.end() && cost_it->second > 0;
}

Deck build_sandbox_deck_filter(const std::function<bool(const CardDefinition&)>& include)
{
    Deck deck;
    int copy_index = 0;
    for (const CardDefinition& def : list_card_catalog_definitions()) {
        if (def.key.empty() || sandbox_skip_catalog_key(def.key)) {
            continue;
        }
        // Deployable cards: units, structures (type "building"), and spells.
        if (!definition_is_unit(def) && !definition_is_spell(def)) {
            continue;
        }
        if (!try_card_def_id_for_key(def.key).is_valid()) {
            continue;
        }
        if (!include(def)) {
            continue;
        }
        deck.add_card(deck_allocate_instance(deck, def.key, copy_index++));
    }
    return deck;
}

}  // namespace

Deck create_sandbox_deck_from_catalog()
{
    return build_sandbox_deck_filter([](const CardDefinition&) { return true; });
}

Deck create_sandbox_deck_for_color(const std::string& color_key)
{
    const auto color = energy_type_from_string(color_key);
    if (!color || !is_named_faction_color(*color)) {
        return {};
    }
    const EnergyType pool = *color;
    return build_sandbox_deck_filter([pool](const CardDefinition& def) { return card_matches_color(def, pool); });
}

Deck create_sandbox_deck_for_faction(const std::string& faction_key)
{
    const std::string k = normalize_key(faction_key);

    if (const auto legacy = legacy_color_name_faction_shortcut(k)) {
        return create_sandbox_deck_for_faction(*legacy);
    }

    // Chroma aliases filter by color across factions.
    if (k == "green" || k == "orange" || k == "turquoise") {
        return create_sandbox_deck_for_color(k);
    }

    const std::string set_code = resolve_faction_set_code(faction_key);
    if (set_code == "__unknown__") {
        return {};
    }
    if (set_code == "__all__") {
        return create_sandbox_deck_from_catalog();
    }

    return build_sandbox_deck_filter([set_code](const CardDefinition& def) {
        return (set_code == "__core__") ? def.set_code.empty() : (def.set_code == set_code);
    });
}

bool apply_sandbox_faction_deck_to_player(GameState& game, const int player_id, const std::string& faction_key,
    std::string* err_out)
{
    std::string k = faction_key;
    std::transform(k.begin(), k.end(), k.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (k.empty() || k == "all") {
        if (err_out) {
            *err_out = "faction key must name a specific faction (not 'all')";
        }
        return false;
    }

    Deck new_deck = create_sandbox_deck_for_faction(faction_key);
    if (new_deck.hand.empty() && new_deck.deck.empty()) {
        if (err_out) {
            *err_out = "unknown faction '" + faction_key + "'";
        }
        return false;
    }
    new_deck.hand.insert(new_deck.hand.end(), new_deck.deck.begin(), new_deck.deck.end());
    new_deck.deck.clear();
    new_deck.sort_hand_by_energy_cost();

    auto deck_it = game.players_decks.find(player_id);
    if (deck_it == game.players_decks.end()) {
        if (err_out) {
            *err_out = "no deck for player " + std::to_string(player_id);
        }
        return false;
    }

    // Replace the full deck object so hand instance ids resolve against the matching pool.
    deck_it->second = std::move(new_deck);
    return true;
}

bool apply_sandbox_faction_deck_to_all_players(GameState& game, const std::string& faction_key, std::string* err_out)
{
    if (game.players_decks.empty()) {
        if (err_out) {
            *err_out = "no players in match";
        }
        return false;
    }
    bool any_ok = false;
    std::string last_err;
    for (const auto& [player_id, _] : game.players_decks) {
        std::string err;
        if (apply_sandbox_faction_deck_to_player(game, player_id, faction_key, &err)) {
            any_ok = true;
        } else if (!err.empty()) {
            last_err = std::move(err);
        }
    }
    if (!any_ok && err_out) {
        *err_out = last_err.empty() ? "failed to apply sandbox faction deck" : last_err;
    }
    return any_ok;
}

namespace {

std::vector<ZoneListEntry> sandbox_acm_zone_templates()
{
    if (const std::optional<DeckListDefinition> starter = try_get_starter_deck_list()) {
        if (!starter->zones.empty()) {
            return starter->zones;
        }
    }
    return {};
}

std::vector<EnergyZone> build_sandbox_territory_row(const std::vector<ZoneListEntry>& templates, const int player_id,
    const int total_count, const int unique_copies)
{
    if (templates.empty() || total_count <= 0) {
        return {};
    }

    int unique_total = 0;
    bool has_basic = false;
    for (const ZoneListEntry& entry : templates) {
        if (entry.is_basic) {
            has_basic = true;
        } else {
            unique_total += unique_copies;
        }
    }

    const int basic_count = has_basic ? std::max(0, total_count - unique_total) : 0;
    std::vector<EnergyZone> zones;
    zones.reserve(static_cast<std::size_t>(total_count));

    int instance = 0;
    for (const ZoneListEntry& entry : templates) {
        const int copies = entry.is_basic ? basic_count : unique_copies;
        for (int i = 0; i < copies; ++i) {
            EnergyZone zone = energy_zone_from_list_entry(
                entry, "p" + std::to_string(player_id) + "_" + entry.zone_id + "_" + std::to_string(instance++));
            zone.refresh_land_use();
            zones.push_back(std::move(zone));
        }
    }
    return zones;
}

}  // namespace

void master_cli_seed_sandbox_territories(GameState& game, const int player_id, const int territory_count)
{
    const int count = std::max(0, territory_count);
    const std::vector<ZoneListEntry> templates = sandbox_acm_zone_templates();
    std::vector<EnergyZone> zones = build_sandbox_territory_row(
        templates, player_id, count, kSandboxUniqueTerritoryCopies);
    if (zones.empty()) {
        zones.reserve(static_cast<std::size_t>(count));
        for (int i = 0; i < count; ++i) {
            EnergyZone zone;
            zone.zone_id = "p" + std::to_string(player_id) + "_asteria_" + std::to_string(i);
            zone.name = "Asteria";
            zone.art_id = "territories/asteria";
            zone.color = EnergyType::Orange;
            zone.is_basic = true;
            TerritoryAbility mine;
            mine.name = "Mine";
            mine.energy_produced = {{EnergyType::Orange, 1}};
            zone.land_abilities.push_back(std::move(mine));
            zones.push_back(std::move(zone));
        }
    }
    game.players_energy_zones[player_id] = std::move(zones);
}

void master_cli_seed_sandbox_state(GameState& game)
{
    if (!game_id_is_sandbox(game.game_id())) {
        return;
    }
    for (const int player_id : game.turn_manager.players) {
        master_cli_seed_sandbox_territories(game, player_id, kSandboxTerritoriesPerPlayer);
        sandbox_move_deck_to_hand(game, player_id);
    }

    // Board starts empty - players deploy all units from hand.
    game.refresh_passive_auras();
}

}  // namespace tactics