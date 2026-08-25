#include "tactics/cards/card_catalog.hpp"
#include "tactics/cards/card_search_index.hpp"

#include "tactics/cards/card_runtime.hpp"
#include "tactics/cards/cards.hpp"
#include "tactics/cards/unit_types.hpp"

#include "tactics/attributes/attributes.hpp"
#include "tactics/cards/ability_catalog.hpp"
#include "tactics/cards/effect_definitions.hpp"
#include "tactics/cards/effect_definitions_io.hpp"
#include "tactics/cards/passive_catalog.hpp"
#include "tactics/effects/effect_registry.hpp"

#include <nlohmann/json.hpp>

// std::hash
#include <functional>
#include <algorithm>
#include <mutex>
#include <sstream>
#include <unordered_set>

namespace tactics {
namespace {

using json = nlohmann::json;

std::mutex g_card_catalog_mutex;
CardCatalog g_card_catalog;
uint64_t g_card_catalog_generation{0};
std::optional<DeckListDefinition> g_starter_deck_list;
std::optional<DeckListDefinition> g_test_deck_list;
std::optional<DeckListDefinition> g_active_match_deck_list;
static const char kBuiltinCardCatalogJson[] = R"JSON({
  "schema_version": 1,
  "cards": [
    {
      "key": "basic_infantry",
      "name": "Basic Infantry",
      "type": "unit",
      "rules_text": "Stockpile 2. Melee 1–3. Quick Shot: deal 2 to an enemy (reflex).",
      "energy_cost": { "neutral": 1 },
      "keywords": [{ "key": "stockpile", "amount": 2 }],
      "abilities": ["quick_shot"],
      "unit_types": ["soldier"],
      "unit": {
        "entity_type": "unit",
        "unit_type": "Infantry",
        "attack_type": "melee",
        "base_health": 5,
        "current_health": 5,
        "melee_damage": 2,
        "melee_damage_min": 1,
        "melee_damage_max": 3,
        "movement": 3,
        "shape": [[0, 0]]
      }
    },
    {
      "key": "taunt_guard",
      "name": "Taunt Guard",
      "type": "unit",
      "rules_text": "Taunt. Enemies orthogonally adjacent cannot move away and must target this unit. Any move that ends on a tile next to this unit stops there (cannot walk past). Pathfinding cannot enter this unit's tile.",
      "energy_cost": { "neutral": 1 },
      "unit": {
        "entity_type": "unit",
        "unit_type": "Taunt Guard",
        "attack_type": "melee",
        "base_health": 6,
        "current_health": 6,
        "melee_damage": 1,
        "melee_damage_min": 1,
        "melee_damage_max": 2,
        "movement": 2,
        "keywords": [{ "key": "taunt", "amount": 1 }],
        "shape": [[0, 0]]
      }
    },
    {
      "key": "sky_banner",
      "name": "Sky Banner",
      "type": "unit",
      "rules_text": "Passive: allied units get +1/+1 and Flying while this unit is alive.",
      "energy_cost": { "neutral": 2 },
      "unit": {
        "entity_type": "unit",
        "unit_type": "Sky Banner",
        "attack_type": "melee",
        "base_health": 4,
        "current_health": 4,
        "melee_damage": 0,
        "movement": 3,
        "passives": ["sky_banner_aura"],
        "shape": [[0, 0]]
      }
    },
    {
      "key": "kill_spell",
      "name": "Kill Spell",
      "type": "spell",
      "rules_text": "Reflex. Destroy anything by dealing lethal damage to any target.",
      "energy_cost": { "neutral": 1 },
      "spell": {
        "speed": "reflex",
        "effect_key": "deal_damage",
        "effect_payload": { "amount": 999 },
        "requires_board_target": true,
        "board_target_kind": "any"
      }
    },
    {
      "key": "poison_cloud",
      "name": "Poison Cloud",
      "type": "spell",
      "rules_text": "Reflex. Apply Poison 3 to any target that can receive poison.",
      "energy_cost": { "neutral": 1 },
      "spell": {
        "speed": "reflex",
        "effect_key": "apply_poison",
        "effect_payload": { "amount": 3 },
        "requires_board_target": true,
        "board_target_kind": "any"
      }
    },
    {
      "key": "ignite",
      "name": "Ignite",
      "type": "spell",
      "rules_text": "Reflex. Apply Fire 3 to any target.",
      "energy_cost": { "neutral": 1 },
      "spell": {
        "speed": "reflex",
        "effect_key": "apply_fire",
        "effect_payload": { "amount": 3 },
        "requires_board_target": true,
        "board_target_kind": "any"
      }
    },
    {
      "key": "deep_cut",
      "name": "Deep Cut",
      "type": "spell",
      "rules_text": "Reflex. Apply Bleed 3 to any target that can receive bleed.",
      "energy_cost": { "neutral": 1 },
      "spell": {
        "speed": "reflex",
        "effect_key": "apply_bleed",
        "effect_payload": { "amount": 3 },
        "requires_board_target": true,
        "board_target_kind": "any"
      }
    },
    {
      "key": "silence",
      "name": "Silence",
      "type": "spell",
      "rules_text": "Reflex. Apply Silenced. While silenced, keywords, passives, and buffs on that unit are disabled.",
      "energy_cost": { "neutral": 1 },
      "spell": {
        "speed": "reflex",
        "effect_key": "apply_silenced",
        "effect_payload": { "amount": 1 },
        "requires_board_target": true,
        "board_target_kind": "any"
      }
    },
    {
      "key": "cleanse_silence",
      "name": "Cleanse Silence",
      "type": "spell",
      "rules_text": "Reflex. Remove Silenced from any target.",
      "energy_cost": { "neutral": 1 },
      "spell": {
        "speed": "reflex",
        "effect_key": "remove_silenced",
        "effect_payload": { "amount": 1 },
        "requires_board_target": true,
        "board_target_kind": "any"
      }
    },
    {
      "key": "stealth",
      "name": "Stealth",
      "type": "spell",
      "rules_text": "Reflex. Apply 1 Stealth to an allied unit. Stealthed units cannot be directly targeted by enemy attacks or abilities. Area effects still apply. Attacking removes all stealth. Lose 1 stack at the start of your turn.",
      "energy_cost": { "neutral": 1 },
      "spell": {
        "speed": "reflex",
        "effect_key": "apply_stealth",
        "effect_payload": { "amount": 1 },
        "requires_board_target": true,
        "board_target_kind": "ally"
      }
    },
    {
      "key": "focus_bolt",
      "name": "Focus Bolt",
      "type": "spell",
      "rules_text": "Focus. Reflex. Deal 3 damage to an enemy within range 4. Casts from a unit you control (uses that unit's keywords).",
      "energy_cost": { "neutral": 1 },
      "keywords": ["focus"],
      "spell": {
        "speed": "reflex",
        "effect_key": "deal_damage",
        "effect_payload": { "amount": 3 },
        "requires_board_target": true,
        "board_target_kind": "enemy",
        "focus_range": 4
      }
    },
    {
      "key": "test_haste_runner",
      "name": "Test Haste Runner",
      "type": "unit",
      "rules_text": "Haste. On its deploy turn, may move normally but cannot attack, defend, dash, or use Quick Shot.",
      "energy_cost": { "neutral": 1 },
      "abilities": ["quick_shot"],
      "unit_types": ["soldier"],
      "unit": {
        "entity_type": "unit",
        "unit_type": "Test Haste",
        "attack_type": "melee",
        "base_health": 5,
        "current_health": 5,
        "melee_damage": 2,
        "melee_damage_min": 1,
        "melee_damage_max": 3,
        "movement": 4,
        "keywords": ["haste"],
        "shape": [[0, 0]]
      }
    },
    {
      "key": "test_surge_striker",
      "name": "Test Surge Striker",
      "type": "unit",
      "rules_text": "Surge. On its deploy turn, may attack, defend, dash, and use Quick Shot but cannot move.",
      "energy_cost": { "neutral": 1 },
      "abilities": ["quick_shot"],
      "unit_types": ["soldier"],
      "unit": {
        "entity_type": "unit",
        "unit_type": "Test Surge",
        "attack_type": "melee",
        "base_health": 5,
        "current_health": 5,
        "melee_damage": 2,
        "melee_damage_min": 1,
        "melee_damage_max": 3,
        "movement": 3,
        "keywords": ["surge"],
        "shape": [[0, 0]]
      }
    },
    {
      "key": "test_charge_knight",
      "name": "Test Charge Knight",
      "type": "unit",
      "rules_text": "Charge. Ignores deployment fatigue on the turn it is deployed.",
      "energy_cost": { "neutral": 1 },
      "abilities": ["quick_shot"],
      "unit_types": ["soldier"],
      "unit": {
        "entity_type": "unit",
        "unit_type": "Test Charge",
        "attack_type": "melee",
        "base_health": 5,
        "current_health": 5,
        "melee_damage": 2,
        "melee_damage_min": 1,
        "melee_damage_max": 3,
        "movement": 3,
        "keywords": ["charge"],
        "shape": [[0, 0]]
      }
    },
    {
      "key": "test_haste_surge_unit",
      "name": "Test Haste Surge Unit",
      "type": "unit",
      "rules_text": "Haste. Surge. On its deploy turn, may move, attack, defend, dash, and use abilities.",
      "energy_cost": { "neutral": 1 },
      "abilities": ["quick_shot"],
      "unit_types": ["soldier"],
      "unit": {
        "entity_type": "unit",
        "unit_type": "Test Haste Surge",
        "attack_type": "melee",
        "base_health": 5,
        "current_health": 5,
        "melee_damage": 2,
        "melee_damage_min": 1,
        "melee_damage_max": 3,
        "movement": 4,
        "keywords": ["haste", "surge"],
        "shape": [[0, 0]]
      }
    },
    {
      "key": "the_macrowave",
      "name": "The Macrowave",
      "type": "spell",
      "rules_text": "Channeled. Grant target unit The Macrowave Pulse: at the end of that unit's owner's turn, deal 3 damage to all surrounding units (allies and enemies).",
      "energy_cost": { "neutral": 2, "orange": 2 },
      "spell": {
        "speed": "channeled",
        "effect_key": "grant_passive_ability",
        "effect_string_payload": { "passive_key": "the_macrowave_pulse" },
        "requires_board_target": true,
        "board_target_kind": "any"
      }
    },
    {
      "key": "power_of_the_sun",
      "name": "Power of the Sun",
      "type": "spell",
      "rules_text": "Focus. Choose X. Deal X damage to a unit surrounding your caster (range 1). Costs 2 {O} + X {N}.",
      "energy_cost": { "orange": 2 },
      "keywords": ["focus"],
      "spell": {
        "speed": "channeled",
        "effect_key": "deal_damage",
        "effect_payload": {},
        "requires_board_target": true,
        "board_target_kind": "any",
        "focus_range": 1,
        "x_cost": { "type": "neutral", "min": 1 }
      }
    },
    {
      "key": "rapid_ranged_replicator",
      "name": "Rapid Ranged Replicator",
      "type": "building",
      "rules_text": "Ranged 2–2, range 4. Replication Protocol: Whenever this structure deals damage to an enemy, spawn a Replicator Bot token on a random unoccupied adjacent tile to that enemy (if one is available) - the bot's attack and health equal the damage dealt. [Slow, 1🟠+1○] Replicate Power: permanently gain +1 damage.",
      "energy_cost": { "orange": 1, "neutral": 2 },
      "abilities": ["replicate_power"],
      "unit": {
        "unit_type": "Rapid Ranged Replicator",
        "attack_type": "ranged",
        "ranged_damage": 2,
        "ranged_damage_min": 2,
        "ranged_damage_max": 2,
        "ranged_range": 4,
        "base_health": 5,
        "current_health": 5,
        "shape": [[0, 0]],
        "passives": ["replication_protocol"]
      }
    }
  ]
})JSON";

static const char kBuiltinTestDeckJson[] = R"JSON({
  "schema_version": 1,
  "key": "test",
  "cards": [
    { "card_key": "test_haste_runner", "copies": 1 },
    { "card_key": "test_surge_striker", "copies": 1 },
    { "card_key": "test_charge_knight", "copies": 1 }
  ]
})JSON";

static const char kBuiltinStarterDeckJson[] = R"JSON({
  "schema_version": 1,
  "key": "starter",
  "cards": [
    { "card_key": "grease_monkeys", "copies": 3 },
    { "card_key": "shock_bot", "copies": 3 },
    { "card_key": "starforged_knights", "copies": 3 },
    { "card_key": "bootleg_go_go_powder", "copies": 3 },
    { "card_key": "starforged_imitation", "copies": 3 },
    { "card_key": "nurse_bot", "copies": 2 },
    { "card_key": "field_upgrade", "copies": 2 },
    { "card_key": "outdated_grenade", "copies": 2 },
    { "card_key": "beam_splitter_mk2", "copies": 2 },
    { "card_key": "shocking_stimulus", "copies": 2 },
    { "card_key": "synths_interlinked", "copies": 2 },
    { "card_key": "starforged_accumulator", "copies": 2 },
    { "card_key": "starforged_sentinel", "copies": 3 },
    { "card_key": "starforged_titan", "copies": 2 },
    { "card_key": "the_boss", "copies": 1 },
    { "card_key": "reactive_armor", "copies": 1 },
    { "card_key": "live_wire", "copies": 1 },
    { "card_key": "desperate_scan", "copies": 1 },
    { "card_key": "sunkissed_ranger", "copies": 1 },
    { "card_key": "vulturous_nanites", "copies": 1 }
  ],
  "reserves": [
    { "card_key": "starforged_titan", "copies": 1 },
    { "card_key": "starforged_sentinel", "copies": 1 },
    { "card_key": "starforged_accumulator", "copies": 1 },
    { "card_key": "starforged_knights", "copies": 1 },
    { "card_key": "shocking_stimulus", "copies": 1 }
  ],
  "zones": [
    { "zone_id": "asteria", "name": "Asteria", "basic": true, "color": "ingenuity", "copies": 7,
      "land_abilities": [{ "name": "Mine", "energy": { "ingenuity": 1 } }] },
    { "zone_id": "flux_factory", "name": "Flux Factory", "depleted": true, "copies": 2,
      "land_abilities": [{ "name": "Overclock", "energy": { "ingenuity": 2 }, "flux": true }] },
    { "zone_id": "neon_archive", "name": "The Neon Archive", "depleted": true, "copies": 2,
      "enter_effects": [{ "effect_key": "scan", "payload": { "amount": 1 } }],
      "groundwork": [{ "color": "ingenuity", "ignore_depleted": true }],
      "land_abilities": [{ "name": "Retrieve", "energy": { "ingenuity": 1 } }] },
    { "zone_id": "futuristic_forge", "name": "Futuristic Forge", "depleted": true, "copies": 2,
      "enter_effects": [{ "effect_key": "grant_permanent_stat_growth", "payload": { "attack": 1, "health": 1 },
        "requires_target": true, "board_target_kind": "any" }],
      "groundwork": [{ "color": "ingenuity", "ignore_depleted": true }],
      "land_abilities": [{ "name": "Forge", "energy": { "ingenuity": 1 } }] },
    { "zone_id": "hydroponic_farm", "name": "Hydroponic Farm", "copies": 2,
      "enter_effects": [{ "effect_key": "grant_next_damage_bonus", "payload": { "amount": 1 },
        "requires_target": true, "board_target_kind": "any" }],
      "land_abilities": [
        { "name": "Irrigate", "energy": { "neutral": 1 } },
        { "name": "Fertilize", "cost": { "orange": 1, "neutral": 2 },
          "effect": { "effect_key": "grant_next_damage_bonus", "payload": { "amount": 1 },
            "requires_target": true, "board_target_kind": "any" } }
      ] },
    { "zone_id": "unstable_laboratory", "name": "Unstable Laboratory", "depleted": true, "copies": 1,
      "groundwork": [{ "color": "ingenuity", "destroy_if_unmet": true }],
      "land_abilities": [
        { "name": "Distill", "energy": { "orange": 1 }, "flux": true },
        { "name": "Volatile Reaction", "energy": { "orange": 4 }, "flux": true, "sacrifice_self": true }] },
    { "zone_id": "weapons_workshop", "name": "Weapons Workshop", "depleted": true, "copies": 1,
      "groundwork": [{ "color": "ingenuity", "effect": { "effect_key": "draw_focus_spell_cards",
        "payload": { "amount": 1, "max_total_cost": 3 } } }],
      "land_abilities": [{ "name": "Fabricate", "energy": { "ingenuity": 1 } }] },
    { "zone_id": "downtown_bazaar", "name": "Downtown Bazaar", "depleted": true, "copies": 1,
      "enter_effects": [{ "effect_key": "optional_discard_draw" }],
      "land_abilities": [{ "name": "Haggle", "energy": { "ingenuity": 1 }, "flux": true }] },
    { "zone_id": "shockbot_printer", "name": "Shockbot Printer", "depleted": true, "copies": 1,
      "enter_effects": [{ "effect_key": "spawn_card_unit_deploy_zone", "string_payload": { "card_key": "shock_bot" } }],
      "groundwork": [{ "color": "ingenuity", "effect": { "effect_key": "spawn_card_unit_deploy_zone",
        "string_payload": { "card_key": "shock_bot" } } }],
      "land_abilities": [{ "name": "Power", "energy": { "ingenuity": 1 } }] },
    { "zone_id": "asterian_walls", "name": "The Asterian Walls", "copies": 1,
      "enter_effects": [{ "effect_key": "grant_player_base_bonus_health", "payload": { "amount": 2 } }],
      "land_abilities": [
        { "name": "Garrison", "energy": { "neutral": 1 } },
        { "name": "Reinforce", "cost": { "neutral": 2, "orange": 2 }, "sacrifice_self": true,
          "effect": { "effect_key": "grant_player_base_max_health", "payload": { "amount": 4 } } }
      ] }
  ]
})JSON";

std::string path_of(const std::string& parent, const std::string& child)
{
    return parent.empty() ? child : parent + "." + child;
}

bool read_energy_cost(const json& j, std::map<EnergyType, int>& out, std::string& err, const std::string& path)
{
    return effect_io::read_energy_cost_object(j, out, err, path);
}

// ── Conquering Territories parsers ──────────────────────────────────────────────
// A territory effect object: { "effect_key", "payload"{int}, "string_payload"{str},
// "requires_target"?, "board_target_kind"? }. Reuses the shared effect_key pipeline.
TerritoryEffect parse_territory_effect(const json& j)
{
    TerritoryEffect eff;
    if (!j.is_object()) {
        return eff;
    }
    eff.effect_key = j.value("effect_key", std::string{});
    if (const auto pit = j.find("payload"); pit != j.end() && pit->is_object()) {
        for (const auto& [k, v] : pit->items()) {
            if (v.is_number_integer()) {
                eff.payload[k] = v.get<int>();
            }
        }
    }
    if (const auto sit = j.find("string_payload"); sit != j.end() && sit->is_object()) {
        for (const auto& [k, v] : sit->items()) {
            if (v.is_string()) {
                eff.string_payload[k] = v.get<std::string>();
            }
        }
    }
    eff.requires_target = j.value("requires_target", false);
    if (const auto bit = j.find("board_target_kind"); bit != j.end() && bit->is_string()) {
        if (const auto pk = board_target_kind_parse(bit->get<std::string>())) {
            eff.board_target_kind = *pk;
        }
    }
    return eff;
}

// A "use land" ability: { "name"?, "cost"{energy}?, "energy"{produced}?, "flux"?, "effect"{} }.
TerritoryAbility parse_territory_ability(const json& j)
{
    TerritoryAbility ab;
    if (!j.is_object()) {
        return ab;
    }
    ab.name = j.value("name", std::string{});
    std::string ignore;
    if (const auto cit = j.find("cost"); cit != j.end() && cit->is_object()) {
        (void)effect_io::read_energy_cost_object(*cit, ab.cost, ignore, "land_ability.cost");
    }
    if (const auto eit = j.find("energy"); eit != j.end() && eit->is_object()) {
        (void)effect_io::read_energy_cost_object(*eit, ab.energy_produced, ignore, "land_ability.energy");
    }
    ab.produces_flux = j.value("flux", false);
    ab.sacrifice_self = j.value("sacrifice_self", false);
    if (const auto fit = j.find("effect"); fit != j.end()) {
        ab.effect = parse_territory_effect(*fit);
    }
    return ab;
}

void infer_color_search_facets(CardDefinition& def)
{
    auto color_it = def.search_facets.find("color");
    if (color_it != def.search_facets.end() && !color_it->second.empty()) {
        return;
    }
    std::vector<std::string> colors;
    for (const EnergyType et : kEnergyChromaTypes) {
        const auto cost_it = def.energy_cost.find(et);
        if (cost_it == def.energy_cost.end() || cost_it->second <= 0) {
            continue;
        }
        for (const std::string& key : energy_search_keys(et)) {
            if (std::find(colors.begin(), colors.end(), key) == colors.end()) {
                colors.push_back(key);
            }
        }
    }
    if (!colors.empty()) {
        def.search_facets["color"] = std::move(colors);
    }
}

bool read_string_array(const json& j, std::vector<std::string>& out, std::string& err, const std::string& path)
{
    out.clear();
    if (!j.is_array()) {
        err = path + " must be an array";
        return false;
    }
    for (const auto& v : j) {
        if (!v.is_string()) {
            err = path + " entries must be strings";
            return false;
        }
        out.push_back(v.get<std::string>());
    }
    return true;
}

bool read_shape(const json& j, std::vector<std::pair<int, int>>& out, std::string& err, const std::string& path)
{
    out.clear();
    if (!j.is_array() || j.empty()) {
        err = path + " must be a non-empty array";
        return false;
    }
    for (const auto& p : j) {
        if (!p.is_array() || p.size() != 2 || !p[0].is_number_integer() || !p[1].is_number_integer()) {
            err = path + " entries must be [x, y] integer pairs";
            return false;
        }
        out.push_back({p[0].get<int>(), p[1].get<int>()});
    }
    Entity tmp;
    tmp.shape = out;
    normalize_entity_shape(tmp);
    out = std::move(tmp.shape);
    return true;
}

bool read_string_int_map(const json& j, std::map<std::string, int>& out, std::string& err, const std::string& path)
{
    out.clear();
    if (!j.is_object()) {
        err = path + " must be an object";
        return false;
    }
    for (auto it = j.begin(); it != j.end(); ++it) {
        if (!it.value().is_number_integer()) {
            err = path + "." + it.key() + " must be an integer";
            return false;
        }
        out[it.key()] = it.value().get<int>();
    }
    return true;
}

bool read_card_keywords(const json& j, std::vector<CardKeywordDefinition>& out, std::string& err, const std::string& path)
{
    out.clear();
    if (!j.is_array()) {
        err = path + " must be an array";
        return false;
    }
    for (const auto& v : j) {
        CardKeywordDefinition attr;
        if (v.is_string()) {
            attr.key = v.get<std::string>();
        } else if (v.is_object()) {
            if (!v.contains("key") || !v["key"].is_string()) {
                err = path + " object entries require string key";
                return false;
            }
            attr.key = v["key"].get<std::string>();
            if (v.contains("amount")) {
                if (!v["amount"].is_number_integer() || v["amount"].get<int>() < 0) {
                    err = path + "." + attr.key + ".amount must be a non-negative integer";
                    return false;
                }
                attr.amount = v["amount"].get<int>();
            }
            if (v.contains("requirement")) {
                if (!v["requirement"].is_string() || v["requirement"].get<std::string>().empty()) {
                    err = path + "." + attr.key + ".requirement must be a non-empty string";
                    return false;
                }
                attr.requirement = v["requirement"].get<std::string>();
            }
        } else {
            err = path + " entries must be strings or objects";
            return false;
        }
        if (!find_attribute_spec(attr.key)) {
            err = path + " references unknown keyword \"" + attr.key + "\"";
            return false;
        }
        out.push_back(std::move(attr));
    }
    return true;
}

bool read_card_effects(const json& j, std::vector<CardEffectDefinition>& out, std::string& err, const std::string& path)
{
    out.clear();
    if (!j.is_array()) {
        err = path + " must be an array";
        return false;
    }
    for (const auto& v : j) {
        CardEffectDefinition effect;
        if (!v.is_object() || !v.contains("key") || !v["key"].is_string()) {
            err = path + " entries require string key";
            return false;
        }
        effect.key = v["key"].get<std::string>();
        effect.amount = v.value("amount", 1);
        if (effect.key.empty() || effect.amount < 0) {
            err = path + " entries require non-empty key and non-negative amount";
            return false;
        }
        out.push_back(std::move(effect));
    }
    return true;
}

std::vector<PassiveAttributeGrant> passive_grants_from_card_keywords(const std::vector<CardKeywordDefinition>& attrs)
{
    std::vector<PassiveAttributeGrant> out;
    out.reserve(attrs.size());
    for (const auto& attr : attrs) {
        out.push_back({attr.key, attr.amount});
    }
    return out;
}

bool read_passive_abilities(const json& j, std::vector<PassiveAbilitySpec>& out, std::string& err, const std::string& path)
{
    out.clear();
    if (!j.is_array()) {
        err = path + " must be an array";
        return false;
    }
    int idx = 0;
    for (const auto& v : j) {
        PassiveAbilitySpec passive;
        const std::string entry_path = path + "[" + std::to_string(idx) + "]";
        if (!effect_io::read_passive_spec_object(v, passive, err, entry_path)) {
            return false;
        }
        if (passive.applies_to != "self" && passive.applies_to != "allied_units") {
            err = entry_path + ".applies_to must be \"self\" or \"allied_units\" for inline unit passives";
            return false;
        }
        out.push_back(std::move(passive));
        ++idx;
    }
    return true;
}

template <typename SpecT, typename LookupFn>
bool validate_catalog_ids(const std::vector<std::string>& ids, LookupFn try_lookup, std::string& err, const std::string& path,
    const char* kind_label)
{
    for (const auto& id : ids) {
        SpecT tmp;
        if (!try_lookup(id, tmp)) {
            err = path + " references unknown " + std::string(kind_label) + " \"" + id + "\"";
            return false;
        }
    }
    return true;
}

bool validate_ability_ids(const std::vector<std::string>& ids, std::string& err, const std::string& path)
{
    ensure_builtin_ability_catalog_loaded();
    return validate_catalog_ids<AbilitySpec>(ids, try_get_ability_from_catalog, err, path, "ability");
}

bool validate_passive_ids(const std::vector<std::string>& ids, std::string& err, const std::string& path)
{
    ensure_builtin_passive_catalog_loaded();
    return validate_catalog_ids<PassiveAbilitySpec>(ids, try_get_passive_from_catalog, err, path, "passive");
}

void merge_passive_ids_unique(std::vector<std::string>& into, const std::vector<std::string>& extra)
{
    for (const std::string& id : extra) {
        if (std::find(into.begin(), into.end(), id) == into.end()) {
            into.push_back(id);
        }
    }
}

bool merge_card_level_passives(const json& card_json, UnitCardDefinition& unit, std::string& err, const std::string& path)
{
    if (!card_json.contains("passives")) {
        return true;
    }
    std::vector<std::string> card_passives;
    if (!read_string_array(card_json["passives"], card_passives, err, path_of(path, "passives"))) {
        return false;
    }
    if (!validate_passive_ids(card_passives, err, path_of(path, "passives"))) {
        return false;
    }
    merge_passive_ids_unique(unit.passive_ability_ids, card_passives);
    return true;
}

bool read_unit_definition(const json& j, UnitCardDefinition& out, std::string& err, const std::string& path)
{
    if (!j.is_object()) {
        err = path + " must be an object";
        return false;
    }
    out.entity_type = j.value("entity_type", out.entity_type);
    out.unit_type = j.value("unit_type", out.unit_type);
    if (j.contains("attack_type")) {
        const auto at = attack_type_from_string(j["attack_type"].get<std::string>());
        if (!at) {
            err = path + ".attack_type is invalid";
            return false;
        }
        out.attack_type = *at;
    }
    out.base_health = j.value("base_health", out.base_health);
    out.current_health = j.value("current_health", out.base_health);
    out.movement = j.value("movement", out.movement);
    out.melee_range = j.value("melee_range", out.melee_range);
    out.melee_damage = j.value("melee_damage", out.melee_damage);
    out.melee_damage_min = j.value("melee_damage_min", out.melee_damage_min);
    out.melee_damage_max = j.value("melee_damage_max", out.melee_damage_max);
    out.ranged_range = j.value("ranged_range", out.ranged_range);
    out.ranged_deadzone = j.value("ranged_deadzone", out.ranged_deadzone);
    out.ranged_damage = j.value("ranged_damage", out.ranged_damage);
    out.ranged_damage_min = j.value("ranged_damage_min", out.ranged_damage_min);
    out.ranged_damage_max = j.value("ranged_damage_max", out.ranged_damage_max);
    out.crit_chance_percent = j.value("crit_chance_percent", out.crit_chance_percent);
    out.line_of_sight_blocked = j.value("line_of_sight_blocked", out.line_of_sight_blocked);
    if (j.contains("shape") && !read_shape(j["shape"], out.shape, err, path_of(path, "shape"))) {
        return false;
    }
    if (j.contains("keywords")) {
        if (!read_card_keywords(j["keywords"], out.keywords, err, path_of(path, "keywords"))) {
            return false;
        }
        for (const auto& attr : out.keywords) {
            if (!find_attribute_spec(attr.key)) {
                err = path + " references unknown unit keyword \"" + attr.key + "\"";
                return false;
            }
        }
    }
    if (j.contains("initial_effects") && !read_card_effects(j["initial_effects"], out.initial_effects, err, path_of(path, "initial_effects"))) {
        return false;
    }
    if (j.contains("passives")) {
        if (!read_string_array(j["passives"], out.passive_ability_ids, err, path_of(path, "passives"))) {
            return false;
        }
        if (!validate_passive_ids(out.passive_ability_ids, err, path_of(path, "passives"))) {
            return false;
        }
    }
    if (j.contains("passive_abilities")) {
        if (!read_passive_abilities(j["passive_abilities"], out.passive_abilities, err, path_of(path, "passive_abilities"))) {
            return false;
        }
    }
    if (j.contains("abilities")) {
        if (!j["abilities"].is_array()) {
            err = path + ".abilities must be an array";
            return false;
        }
        int aidx = 0;
        for (const auto& aj : j["abilities"]) {
            const std::string ab_path = path_of(path, "abilities[" + std::to_string(aidx) + "]");
            if (aj.is_string()) {
                AbilitySpec tmpl;
                if (!try_get_ability_from_catalog(aj.get<std::string>(), tmpl)) {
                    err = ab_path + " references unknown ability \"" + aj.get<std::string>() + "\"";
                    return false;
                }
                out.activated_abilities.push_back(std::move(tmpl));
            } else if (aj.is_object()) {
                AbilitySpec spec;
                if (!effect_io::read_ability_spec_object(aj, spec, err, ab_path)) {
                    return false;
                }
                out.activated_abilities.push_back(std::move(spec));
            } else {
                err = ab_path + " must be a string id or ability object";
                return false;
            }
            ++aidx;
        }
    }
    return true;
}

bool read_spell_definition(const json& j, SpellCardDefinition& out, std::string& err, const std::string& path)
{
    if (!j.is_object()) {
        err = path + " must be an object";
        return false;
    }
    out.effect_ref = j.value("effect_ref", std::string{});
    if (j.contains("speed")) {
        const auto sp = effect_speed_from_string(j["speed"].get<std::string>());
        if (!sp) {
            err = path + ".speed is invalid";
            return false;
        }
        out.speed = *sp;
        out.explicit_speed = true;
    }
    if (j.contains("effect_payload")) {
        if (!read_string_int_map(j["effect_payload"], out.effect_payload, err, path_of(path, "effect_payload"))) {
            return false;
        }
    }
    if (j.contains("effect_string_payload") && j.at("effect_string_payload").is_object()) {
        for (const auto& [k, v] : j.at("effect_string_payload").items()) {
            if (v.is_string()) out.effect_string_payload[k] = v.get<std::string>();
        }
    }
    if (out.effect_ref.empty()) {
        if (!j.contains("effect_key")) {
            err = path + " requires effect_key or effect_ref";
            return false;
        }
        out.effect_key = j.at("effect_key").get<std::string>();
    } else if (j.contains("effect_key")) {
        out.effect_key = j.at("effect_key").get<std::string>();
    }
    if (!out.effect_ref.empty() || !out.effect_key.empty()) {
        if (!out.effect_ref.empty()) {
            ensure_builtin_ability_catalog_loaded();
            AbilitySpec tmpl;
            if (!try_get_ability_from_catalog(out.effect_ref, tmpl)) {
                err = path + ".effect_ref references unknown ability \"" + out.effect_ref + "\"";
                return false;
            }
        } else if (!is_known_effect_key(out.effect_key)) {
            err = path + ".effect_key references unknown effect \"" + out.effect_key + "\"";
            return false;
        }
    }
    if (j.contains("requires_board_target")) {
        if (!j["requires_board_target"].is_boolean()) {
            err = path + ".requires_board_target must be a boolean";
            return false;
        }
        out.requires_mandatory_board_cell = j["requires_board_target"].get<bool>();
    }
    if (j.contains("requires_mandatory_board_cell")) {
        if (!j["requires_mandatory_board_cell"].is_boolean()) {
            err = path + ".requires_mandatory_board_cell must be a boolean";
            return false;
        }
        out.requires_mandatory_board_cell = j["requires_mandatory_board_cell"].get<bool>();
    }
    if (j.contains("board_target_kind")) {
        const auto bt = board_target_kind_parse(j["board_target_kind"].get<std::string>());
        if (!bt) {
            err = path + ".board_target_kind is invalid";
            return false;
        }
        out.board_target_kind = *bt;
    }
    // Modal spells: an array of selectable modes, each a normal effect + its own targeting.
    if (j.contains("modes")) {
        if (!j["modes"].is_array()) {
            err = path + ".modes must be an array";
            return false;
        }
        for (std::size_t mi = 0; mi < j["modes"].size(); ++mi) {
            const auto& mj = j["modes"][mi];
            const std::string mpath = path + ".modes[" + std::to_string(mi) + "]";
            if (!mj.is_object()) {
                err = mpath + " must be an object";
                return false;
            }
            SpellMode mode;
            if (mj.contains("label")) {
                mode.label = mj["label"].get<std::string>();
            }
            if (mj.contains("rules_text")) {
                mode.rules_text = mj["rules_text"].get<std::string>();
            }
            if (!mj.contains("effect_key")) {
                err = mpath + " requires effect_key";
                return false;
            }
            mode.effect_key = mj["effect_key"].get<std::string>();
            if (!is_known_effect_key(mode.effect_key)) {
                err = mpath + ".effect_key references unknown effect \"" + mode.effect_key + "\"";
                return false;
            }
            if (mj.contains("effect_payload") && mj["effect_payload"].is_object()) {
                for (auto it = mj["effect_payload"].begin(); it != mj["effect_payload"].end(); ++it) {
                    if (it.value().is_number_integer()) {
                        mode.effect_payload[it.key()] = it.value().get<int>();
                    }
                }
            }
            if (mj.contains("effect_string_payload") && mj["effect_string_payload"].is_object()) {
                for (auto it = mj["effect_string_payload"].begin(); it != mj["effect_string_payload"].end(); ++it) {
                    if (it.value().is_string()) {
                        mode.effect_string_payload[it.key()] = it.value().get<std::string>();
                    }
                }
            }
            if (mj.contains("board_target_kind")) {
                const auto bt = board_target_kind_parse(mj["board_target_kind"].get<std::string>());
                if (!bt) {
                    err = mpath + ".board_target_kind is invalid";
                    return false;
                }
                mode.board_target_kind = *bt;
            }
            if (mj.contains("requires_board_target") && mj["requires_board_target"].is_boolean()) {
                mode.requires_board_target = mj["requires_board_target"].get<bool>();
            }
            out.modes.push_back(std::move(mode));
        }
    }
    if (j.contains("focus_range")) {
        if (!j["focus_range"].is_number_integer() || j["focus_range"].get<int>() < 0) {
            err = path + ".focus_range must be a non-negative integer";
            return false;
        }
        out.focus_range = j["focus_range"].get<int>();
    }
    if (j.contains("use_caster_ranged_range")) {
        if (!j["use_caster_ranged_range"].is_boolean()) {
            err = path + ".use_caster_ranged_range must be a boolean";
            return false;
        }
        out.use_caster_ranged_range = j["use_caster_ranged_range"].get<bool>();
    }
    if (j.contains("use_caster_attack_range")) {
        if (!j["use_caster_attack_range"].is_boolean()) {
            err = path + ".use_caster_attack_range must be a boolean";
            return false;
        }
        out.use_caster_attack_range = j["use_caster_attack_range"].get<bool>();
    }
    if (j.contains("require_caster_attack_types")) {
        if (!j["require_caster_attack_types"].is_array()) {
            err = path + ".require_caster_attack_types must be an array";
            return false;
        }
        for (const auto& entry : j["require_caster_attack_types"]) {
            if (!entry.is_string()) {
                err = path + ".require_caster_attack_types entries must be strings";
                return false;
            }
            out.require_caster_attack_types.push_back(entry.get<std::string>());
        }
    }
    if (j.contains("require_target_unit_types")
        && !read_unit_types_json_array(j["require_target_unit_types"], out.require_target_unit_types, err,
            path_of(path, "require_target_unit_types"))) {
        return false;
    }
    if (j.contains("bonus_damage_unit_types")
        && !read_unit_types_json_array(j["bonus_damage_unit_types"], out.bonus_damage_unit_types, err,
            path_of(path, "bonus_damage_unit_types"))) {
        return false;
    }
    if (j.contains("bonus_damage_amount")) {
        if (!j["bonus_damage_amount"].is_number_integer()) {
            err = path + ".bonus_damage_amount must be an integer";
            return false;
        }
        out.bonus_damage_amount = j["bonus_damage_amount"].get<int>();
    }
    if (j.contains("x_cost")) {
        const auto& xj = j["x_cost"];
        if (!xj.is_object() || !xj.contains("type")) {
            err = path + ".x_cost must be an object with a \"type\" field";
            return false;
        }
        const auto et = energy_type_from_string(xj["type"].get<std::string>());
        if (!et) {
            err = path + ".x_cost.type is not a valid energy type";
            return false;
        }
        out.x_cost_energy_type = *et;
        if (xj.contains("min")) {
            if (!xj["min"].is_number_integer() || xj["min"].get<int>() < 0) {
                err = path + ".x_cost.min must be a non-negative integer";
                return false;
            }
            out.x_cost_min = xj["min"].get<int>();
        }
    }
    out.chain = j.value("chain", false);
    return true;
}

bool read_card_definition(const json& j, CardDefinition& out, std::string& err, const std::string& path)
{
    if (!j.is_object()) {
        err = path + " must be an object";
        return false;
    }
    out.key = j.at("key").get<std::string>();
    out.name = j.at("name").get<std::string>();
    out.type = j.at("type").get<std::string>();
    out.rules_text = j.value("rules_text", std::string{});
    out.normal_rules_text = j.value("normal_rules_text", j.value("simple_rules_text", std::string{}));
    out.flavor_text = j.value("flavor_text", std::string{});
    out.art_id = j.value("art_id", std::string{});
    out.set_code = j.value("set", j.value("set_code", std::string{}));
    out.rarity = j.value("rarity", std::string{});
    out.collector_number = j.value("collector_number", std::string{});
    out.ignores_hand_limit = j.value("ignores_hand_limit", false);
    if (j.contains("search_aliases")
        && !read_string_array(j["search_aliases"], out.search_aliases, err, path_of(path, "search_aliases"))) {
        return false;
    }
    if (j.contains("search_facets")) {
        if (!j["search_facets"].is_object()) {
            err = path + ".search_facets must be an object";
            return false;
        }
        for (auto it = j["search_facets"].begin(); it != j["search_facets"].end(); ++it) {
            std::vector<std::string> values;
            if (!read_string_array(it.value(), values, err, path_of(path, "search_facets." + it.key()))) {
                return false;
            }
            out.search_facets[it.key()] = std::move(values);
        }
    }
    if (out.key.empty() || out.name.empty() || out.type.empty()) {
        err = path + " requires non-empty key, name, and type";
        return false;
    }
    if (out.type != "unit" && out.type != "building" && out.type != "obstacle" && out.type != "spell") {
        err = path + " has invalid type \"" + out.type + "\"";
        return false;
    }
    if (j.contains("tags") && !read_string_array(j["tags"], out.tags, err, path_of(path, "tags"))) {
        return false;
    }
    if (j.contains("unit_types") && !read_unit_types_json_array(j["unit_types"], out.unit_types, err, path_of(path, "unit_types"))) {
        return false;
    }
    if (!read_energy_cost(j.value("energy_cost", json::object()), out.energy_cost, err, path_of(path, "energy_cost"))) {
        return false;
    }
    if (j.contains("keywords") && !read_card_keywords(j["keywords"], out.keywords, err, path_of(path, "keywords"))) {
        return false;
    }
    if (j.contains("abilities")) {
        if (!read_string_array(j["abilities"], out.abilities, err, path_of(path, "abilities"))) {
            return false;
        }
        if (!validate_ability_ids(out.abilities, err, path_of(path, "abilities"))) {
            return false;
        }
    }
    if (j.contains("ability_overrides")) {
        if (!j["ability_overrides"].is_object()) {
            err = path + ".ability_overrides must be an object";
            return false;
        }
        for (auto it = j["ability_overrides"].begin(); it != j["ability_overrides"].end(); ++it) {
            AbilityOverridePatch patch;
            if (!effect_io::read_ability_override_patch_object(it.value(), patch, err, path_of(path, "ability_overrides." + it.key()))) {
                return false;
            }
            out.ability_overrides[it.key()] = std::move(patch);
        }
    }
    if (out.type == "unit" || out.type == "building" || out.type == "obstacle") {
        UnitCardDefinition unit;
        if (!read_unit_definition(j.at("unit"), unit, err, path_of(path, "unit"))) {
            return false;
        }
        if (!merge_card_level_passives(j, unit, err, path)) {
            return false;
        }
        if (out.type == "building") {
            unit.entity_type = "building";
            unit.movement = 0;
        } else if (out.type == "obstacle") {
            if (unit.entity_type.empty() || unit.entity_type == "unit") {
                unit.entity_type = "breakable_obstacle";
            }
            if (unit.entity_type == "low_cover") {
                unit.line_of_sight_blocked = false;
            } else if (!j.at("unit").contains("line_of_sight_blocked")) {
                unit.line_of_sight_blocked = true;
            }
            unit.unit_type = unit.unit_type.empty() ? "Obstacle" : unit.unit_type;
            unit.attack_type = AttackType::Utility;
            unit.movement = 0;
            unit.melee_damage = 0;
            unit.ranged_damage = 0;
        }
        out.unit = std::move(unit);
    } else {
        SpellCardDefinition spell;
        if (!read_spell_definition(j.at("spell"), spell, err, path_of(path, "spell"))) {
            return false;
        }
        if (!finalize_spell_definition(spell, err)) {
            return false;
        }
        out.spell = std::move(spell);
    }
    infer_color_search_facets(out);
    return true;
}

bool merge_card_catalog_object(const json& root, std::string& err, std::unordered_set<std::string>* cross_file_keys = nullptr,
    const char* source_label = nullptr)
{
    if (root.value("schema_version", 0) != 1) {
        err = "card catalog: unsupported or missing schema_version";
        return false;
    }
    const auto cards_it = root.find("cards");
    if (cards_it == root.end() || !cards_it->is_array()) {
        err = "card catalog: missing top-level cards array";
        return false;
    }

    std::unordered_map<std::string, CardDefinition> parsed;
    std::unordered_set<std::string> seen;
    int idx = 0;
    for (const auto& cj : *cards_it) {
        CardDefinition def;
        if (!read_card_definition(cj, def, err, "cards[" + std::to_string(idx) + "]")) {
            return false;
        }
        if (!seen.insert(def.key).second) {
            err = "card catalog: duplicate card key \"" + def.key + "\"";
            if (source_label) {
                err += " in ";
                err += source_label;
            }
            return false;
        }
        if (cross_file_keys && !cross_file_keys->insert(def.key).second) {
            err = "card catalog: duplicate card key \"" + def.key + "\"";
            if (source_label) {
                err += " in ";
                err += source_label;
            }
            err += " (already defined in an earlier catalog file)";
            return false;
        }
        parsed[def.key] = std::move(def);
        ++idx;
    }

    // Optional top-level faction metadata: set, set_name, colors (works even when cards[] is empty).
    const std::string top_set = root.value("set", root.value("set_code", std::string{}));

    std::lock_guard<std::mutex> lock(g_card_catalog_mutex);
    for (auto& [key, def] : parsed) {
        (void)key;
        // Cards without an explicit "set" inherit the shard's top-level set (sandbox faction decks).
        if (def.set_code.empty() && !top_set.empty()) {
            def.set_code = top_set;
        }
        g_card_catalog.upsert(std::move(def));
    }
    {
        const std::string top_name = root.value("set_name", std::string{});
        if (!top_set.empty()) {
            g_card_catalog.registered_set_codes.insert(top_set);
            if (!top_name.empty()) {
                g_card_catalog.set_display_names[top_set] = top_name;
            }
            if (root.contains("colors")) {
                std::vector<std::string> colors;
                if (!read_string_array(root["colors"], colors, err, "colors")) {
                    return false;
                }
                g_card_catalog.set_colors[top_set] = std::move(colors);
            }
        }
    }
    ++g_card_catalog_generation;
    sync_card_search_index_generation(g_card_catalog_generation);
    return true;
}

std::string join_catalog_path(const std::string& catalog_dir, const std::string& entry)
{
    if (catalog_dir.empty()) {
        return entry;
    }
    if (catalog_dir.back() == '/' || catalog_dir.back() == '\\') {
        return catalog_dir + entry;
    }
    return catalog_dir + "/" + entry;
}

}  // namespace

void CardCatalog::clear()
{
    definitions.clear();
    key_to_id.clear();
    set_display_names.clear();
    registered_set_codes.clear();
    set_colors.clear();
}

void CardCatalog::upsert(CardDefinition def)
{
    if (def.key.empty()) {
        return;
    }
    const std::string key = def.key;
    CardDefId id;
    if (const auto it = key_to_id.find(key); it != key_to_id.end()) {
        id = it->second;
        definitions[id.value] = std::move(def);
    } else {
        id = CardDefId{static_cast<uint32_t>(definitions.size())};
        definitions.push_back(std::move(def));
        key_to_id[key] = id;
    }
    upsert_card_search_document(definitions[id.value], id);
}

void clear_card_catalog()
{
    {
        std::lock_guard<std::mutex> lock(g_card_catalog_mutex);
        g_card_catalog.clear();
        g_starter_deck_list.reset();
        g_test_deck_list.reset();
        g_active_match_deck_list.reset();
        ++g_card_catalog_generation;
    }
    clear_card_search_index();
}

bool load_card_catalog_from_json_utf8(const std::string& utf8, std::string& err_out)
{
    try {
        const json root = json::parse(utf8);
        return merge_card_catalog_object(root, err_out);
    } catch (const std::exception& ex) {
        err_out = ex.what();
        return false;
    }
}

bool load_card_catalog_manifest_from_json_utf8(const std::string& manifest_utf8, const std::string& catalog_dir,
    const CardCatalogFileReader& read_file, std::string& err_out)
{
    try {
        const json root = json::parse(manifest_utf8);
        if (root.value("schema_version", 0) != 1) {
            err_out = "card catalog manifest: unsupported or missing schema_version";
            return false;
        }
        const auto catalogs_it = root.find("catalogs");
        if (catalogs_it == root.end() || !catalogs_it->is_array() || catalogs_it->empty()) {
            err_out = "card catalog manifest: missing non-empty catalogs array";
            return false;
        }
        std::unordered_set<std::string> cross_file_keys;
        int idx = 0;
        int loaded_count = 0;
        std::string warnings;
        for (const auto& entry : *catalogs_it) {
            if (!entry.is_string()) {
                err_out = "card catalog manifest: catalogs[" + std::to_string(idx) + "] must be a string path";
                return false;
            }
            const std::string rel = entry.get<std::string>();
            if (rel.empty()) {
                err_out = "card catalog manifest: catalogs[" + std::to_string(idx) + "] is empty";
                return false;
            }
            const std::string path = join_catalog_path(catalog_dir, rel);
            std::string catalog_utf8;
            if (!read_file(path, catalog_utf8, err_out)) {
                if (err_out.empty()) {
                    err_out = "card catalog manifest: failed to read " + path;
                }
                warnings += err_out + "; ";
                err_out.clear();
                ++idx;
                continue;
            }
            json catalog_root;
            try {
                catalog_root = json::parse(catalog_utf8);
            } catch (const std::exception& ex) {
                warnings += std::string("card catalog manifest: failed to parse ") + rel + ": " + ex.what() + "; ";
                ++idx;
                continue;
            }
            if (!merge_card_catalog_object(catalog_root, err_out, &cross_file_keys, rel.c_str())) {
                // Cross-file duplicate keys are a hard error - abort the whole load so callers
                // can detect and report conflicts. Other parse errors are also hard: a shard that
                // fails to parse must not silently disappear and leave the catalog incomplete.
                return false;
            }
            ++loaded_count;
            ++idx;
        }
        if (loaded_count == 0) {
            err_out = warnings.empty() ? "card catalog manifest: no catalogs loaded" : warnings;
            return false;
        }
        err_out = warnings;
        return true;
    } catch (const std::exception& ex) {
        err_out = ex.what();
        return false;
    }
}

bool load_project_card_catalogs(const CardCatalogFileReader& read_file, std::string& err_out)
{
    constexpr const char kManifestPath[] = "TacticsData/cards/card_catalog_manifest.json";
    constexpr const char kLegacyPath[] = "TacticsData/cards/card_catalog.json";
    constexpr const char kCatalogDir[] = "TacticsData/cards/";

    std::string manifest_utf8;
    if (read_file(kManifestPath, manifest_utf8, err_out)) {
        return load_card_catalog_manifest_from_json_utf8(manifest_utf8, kCatalogDir, read_file, err_out);
    }
    err_out.clear();
    std::string legacy_utf8;
    if (read_file(kLegacyPath, legacy_utf8, err_out)) {
        return load_card_catalog_from_json_utf8(legacy_utf8, err_out);
    }
    err_out.clear();
    return true;
}

bool load_deck_list_from_json_utf8(const std::string& utf8, DeckListDefinition& out, std::string& err_out)
{
    try {
        const json root = json::parse(utf8);
        if (root.value("schema_version", 0) != 1) {
            err_out = "deck list: unsupported or missing schema_version";
            return false;
        }
        out.key = root.value("key", std::string{"starter"});
        out.entries.clear();
        const auto cards_it = root.find("cards");
        if (cards_it == root.end() || !cards_it->is_array()) {
            err_out = "deck list: missing top-level cards array";
            return false;
        }
        for (const auto& cj : *cards_it) {
            if (!cj.is_object() || !cj.contains("card_key") || !cj["card_key"].is_string()) {
                err_out = "deck list: entries require string card_key";
                return false;
            }
            const int copies = cj.value("copies", 0);
            if (copies < 0) {
                err_out = "deck list: copies must be non-negative";
                return false;
            }
            out.entries.push_back({cj["card_key"].get<std::string>(), copies});
        }
        out.reserves.clear();
        const auto reserves_it = root.find("reserves");
        if (reserves_it != root.end()) {
            if (!reserves_it->is_array()) {
                err_out = "deck list: reserves must be an array";
                return false;
            }
            int reserve_total = 0;
            for (const auto& cj : *reserves_it) {
                if (!cj.is_object() || !cj.contains("card_key") || !cj["card_key"].is_string()) {
                    err_out = "deck list: reserve entries require string card_key";
                    return false;
                }
                const int copies = cj.value("copies", 0);
                if (copies < 0) {
                    err_out = "deck list: reserve copies must be non-negative";
                    return false;
                }
                reserve_total += copies;
                out.reserves.push_back({cj["card_key"].get<std::string>(), copies});
            }
            if (reserve_total > kMaxReservesSize) {
                err_out = "deck list: reserves cannot exceed " + std::to_string(kMaxReservesSize) + " cards";
                return false;
            }
        }
        out.zones.clear();
        const auto zones_it = root.find("zones");
        if (zones_it != root.end()) {
            if (!zones_it->is_array()) {
                err_out = "deck list: zones must be an array";
                return false;
            }
            int zone_total = 0;
            for (const auto& zj : *zones_it) {
                if (!zj.is_object()) {
                    err_out = "deck list: zone entries must be objects";
                    return false;
                }
                ZoneListEntry ze;
                ze.zone_id = zj.value("zone_id", std::string{});
                ze.name = zj.value("name", std::string{"Zone"});
                ze.art_id = zj.value("art_id", std::string{});
                if (ze.art_id.empty()) {
                    ze.art_id = "territories/" + ze.zone_id;
                }
                ze.copies = zj.value("copies", 1);
                if (ze.zone_id.empty()) {
                    err_out = "deck list: zone entries require string zone_id";
                    return false;
                }
                if (ze.copies < 1) {
                    err_out = "deck list: zone copies must be at least 1";
                    return false;
                }
                // ── Conquering Territories fields (all optional) ──
                if (const auto cj = zj.find("color"); cj != zj.end() && cj->is_string()) {
                    ze.color = energy_type_from_string(cj->get<std::string>());
                }
                ze.is_basic = zj.value("basic", false);
                ze.enters_depleted = zj.value("depleted", false);
                if (const auto eeit = zj.find("enter_effects"); eeit != zj.end() && eeit->is_array()) {
                    for (const auto& eff : *eeit) {
                        ze.enter_effects.push_back(parse_territory_effect(eff));
                    }
                }
                if (const auto gwit = zj.find("groundwork"); gwit != zj.end() && gwit->is_array()) {
                    for (const auto& gj : *gwit) {
                        if (!gj.is_object()) {
                            continue;
                        }
                        GroundworkTrigger gw;
                        if (const auto gcj = gj.find("color"); gcj != gj.end() && gcj->is_string()) {
                            if (const auto c = energy_type_from_string(gcj->get<std::string>())) {
                                gw.color = *c;
                            }
                        }
                        gw.ignore_depleted = gj.value("ignore_depleted", false);
                        gw.destroy_if_unmet = gj.value("destroy_if_unmet", false);
                        if (const auto gej = gj.find("effect"); gej != gj.end()) {
                            gw.effect = parse_territory_effect(*gej);
                        }
                        ze.groundwork.push_back(std::move(gw));
                    }
                }
                if (const auto laj = zj.find("land_abilities"); laj != zj.end() && laj->is_array()) {
                    for (const auto& aj : *laj) {
                        ze.land_abilities.push_back(parse_territory_ability(aj));
                    }
                }

                // Passive `energy` is optional now: a territory may generate via `land_abilities`
                // instead. Only require it when the territory has no other way to matter.
                const auto energy_it = zj.find("energy");
                const bool has_energy_obj = energy_it != zj.end() && energy_it->is_object();
                if (has_energy_obj && !read_energy_cost(*energy_it, ze.energy_produced, err_out, "zone.energy")) {
                    return false;
                }
                if (ze.energy_produced.empty() && ze.land_abilities.empty() && ze.enter_effects.empty()) {
                    err_out = "deck list: zone must produce energy or define land_abilities/enter_effects";
                    return false;
                }
                zone_total += ze.copies;
                out.zones.push_back(std::move(ze));
            }
            if (zone_total > kMaxZoneDeckSize) {
                err_out = "deck list: zones cannot exceed " + std::to_string(kMaxZoneDeckSize) + " cards";
                return false;
            }
        }
        return true;
    } catch (const std::exception& ex) {
        err_out = ex.what();
        return false;
    }
}

namespace {

int total_copies(const std::vector<DeckListEntry>& entries)
{
    int n = 0;
    for (const auto& e : entries) {
        n += e.copies;
    }
    return n;
}

int copies_of_key(const std::vector<DeckListEntry>& entries, const std::string& key)
{
    int n = 0;
    for (const auto& e : entries) {
        if (e.card_key == key) {
            n += e.copies;
        }
    }
    return n;
}

}  // namespace

bool validate_deck_list(const DeckListDefinition& deck, std::string& err_out)
{
    const int main_total = total_copies(deck.entries);
    if (main_total != kMaxMainDeckCards) {
        err_out = "deck must contain exactly " + std::to_string(kMaxMainDeckCards) + " main-deck cards (has " +
                  std::to_string(main_total) + ")";
        return false;
    }
    std::unordered_map<std::string, int> main_by_key;
    for (const auto& e : deck.entries) {
        if (e.copies <= 0) {
            err_out = "deck entries need positive copies";
            return false;
        }
        CardDefinition def;
        if (!try_get_card_definition(e.card_key, def)) {
            err_out = "unknown card \"" + e.card_key + "\"";
            return false;
        }
        main_by_key[e.card_key] += e.copies;
        if (main_by_key[e.card_key] > kMaxCopiesPerCard) {
            err_out = "card \"" + e.card_key + "\" exceeds " + std::to_string(kMaxCopiesPerCard) + " copies in main deck";
            return false;
        }
    }
    const int reserve_total = total_copies(deck.reserves);
    if (reserve_total != kMaxReservesSize) {
        err_out = "reserves must contain exactly " + std::to_string(kMaxReservesSize) + " cards (has " +
                  std::to_string(reserve_total) + ")";
        return false;
    }
    std::unordered_map<std::string, int> reserve_by_key;
    for (const auto& e : deck.reserves) {
        if (e.copies <= 0) {
            err_out = "reserve entries need positive copies";
            return false;
        }
        CardDefinition def;
        if (!try_get_card_definition(e.card_key, def)) {
            err_out = "unknown reserve card \"" + e.card_key + "\"";
            return false;
        }
        reserve_by_key[e.card_key] += e.copies;
        if (reserve_by_key[e.card_key] > kMaxCopiesPerCard) {
            err_out = "card \"" + e.card_key + "\" exceeds " + std::to_string(kMaxCopiesPerCard) +
                      " copies in reserves";
            return false;
        }
    }
    int zone_total = 0;
    for (const auto& z : deck.zones) {
        if (z.copies < 1) {
            err_out = "zone entries need positive copies";
            return false;
        }
        if (z.zone_id.empty()) {
            err_out = "zone entries need zone_id";
            return false;
        }
        if (z.energy_produced.empty() && z.land_abilities.empty() && z.enter_effects.empty()) {
            err_out = "zone \"" + z.zone_id + "\" must produce energy or define land_abilities/enter_effects";
            return false;
        }
        if (!z.is_basic && z.copies > kMaxCopiesPerUniqueTerritory) {
            err_out = "unique territory \"" + z.zone_id + "\" exceeds " +
                      std::to_string(kMaxCopiesPerUniqueTerritory) + " copies";
            return false;
        }
        zone_total += z.copies;
    }
    if (zone_total != kMaxZoneDeckSize) {
        err_out = "zones must contain exactly " + std::to_string(kMaxZoneDeckSize) + " cards (has " +
                  std::to_string(zone_total) + ")";
        return false;
    }
    return true;
}

bool save_deck_list_to_json_utf8(const DeckListDefinition& deck, std::string& out_utf8, std::string& err_out)
{
    if (!validate_deck_list(deck, err_out)) {
        return false;
    }
    json root;
    root["schema_version"] = 1;
    root["key"] = deck.key.empty() ? "custom" : deck.key;
    json cards = json::array();
    for (const auto& e : deck.entries) {
        cards.push_back({{"card_key", e.card_key}, {"copies", e.copies}});
    }
    root["cards"] = std::move(cards);
    if (!deck.reserves.empty()) {
        json reserves = json::array();
        for (const auto& e : deck.reserves) {
            reserves.push_back({{"card_key", e.card_key}, {"copies", e.copies}});
        }
        root["reserves"] = std::move(reserves);
    }
    if (!deck.zones.empty()) {
        json zones = json::array();
        for (const auto& z : deck.zones) {
            json energy = json::object();
            for (const auto& [et, amount] : z.energy_produced) {
                energy[to_string(et)] = amount;
            }
            zones.push_back({{"zone_id", z.zone_id},
                             {"name", z.name},
                             {"energy", std::move(energy)},
                             {"copies", z.copies}});
        }
        root["zones"] = std::move(zones);
    }
    out_utf8 = root.dump(2);
    return true;
}

bool load_builtin_deck_list_from_json_utf8(const std::string& utf8, BuiltinDeckListSlot slot, std::string& err_out)
{
    DeckListDefinition parsed;
    if (!load_deck_list_from_json_utf8(utf8, parsed, err_out)) {
        return false;
    }
    std::lock_guard<std::mutex> lock(g_card_catalog_mutex);
    if (slot == BuiltinDeckListSlot::Starter) {
        g_starter_deck_list = std::move(parsed);
    } else {
        g_test_deck_list = std::move(parsed);
    }
    return true;
}

bool load_starter_deck_list_from_json_utf8(const std::string& utf8, std::string& err_out)
{
    return load_builtin_deck_list_from_json_utf8(utf8, BuiltinDeckListSlot::Starter, err_out);
}

bool load_test_deck_list_from_json_utf8(const std::string& utf8, std::string& err_out)
{
    return load_builtin_deck_list_from_json_utf8(utf8, BuiltinDeckListSlot::Test, err_out);
}

void ensure_builtin_card_catalog_loaded()
{
    // Catalogs load from JSON at runtime (load_project_card_catalogs). Callers still hit this.
}

std::vector<std::string> list_card_catalog_keys_sorted()
{
    std::lock_guard<std::mutex> lock(g_card_catalog_mutex);
    std::vector<std::string> keys;
    keys.reserve(g_card_catalog.key_to_id.size());
    for (const auto& [key, _] : g_card_catalog.key_to_id) {
        keys.push_back(key);
    }
    std::sort(keys.begin(), keys.end());
    return keys;
}

std::vector<CardDefinition> list_card_catalog_definitions()
{
    std::lock_guard<std::mutex> lock(g_card_catalog_mutex);
    std::vector<CardDefinition> out;
    out.reserve(g_card_catalog.definitions.size());
    for (const CardDefinition& def : g_card_catalog.definitions) {
        if (!def.key.empty()) {
            out.push_back(def);
        }
    }
    std::sort(out.begin(), out.end(), [](const CardDefinition& a, const CardDefinition& b) {
        return a.key < b.key;
    });
    return out;
}

std::vector<std::string> list_catalog_set_codes()
{
    std::unordered_set<std::string> seen;
    {
        std::lock_guard<std::mutex> lock(g_card_catalog_mutex);
        seen = g_card_catalog.registered_set_codes;
        for (const CardDefinition& def : g_card_catalog.definitions) {
            if (!def.set_code.empty()) {
                seen.insert(def.set_code);
            }
        }
    }
    std::vector<std::string> result(seen.begin(), seen.end());
    std::sort(result.begin(), result.end());
    return result;
}

std::vector<std::string> list_playable_set_codes()
{
    std::unordered_map<std::string, int> counts;
    {
        std::lock_guard<std::mutex> lock(g_card_catalog_mutex);
        for (const CardDefinition& def : g_card_catalog.definitions) {
            if (!def.key.empty() && !def.set_code.empty()) {
                ++counts[def.set_code];
            }
        }
    }
    std::vector<std::string> result;
    result.reserve(counts.size());
    for (const auto& [code, card_count] : counts) {
        if (card_count > 0) {
            result.push_back(code);
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

std::vector<std::string> list_set_colors(const std::string& set_code)
{
    std::lock_guard<std::mutex> lock(g_card_catalog_mutex);
    const auto it = g_card_catalog.set_colors.find(set_code);
    if (it != g_card_catalog.set_colors.end()) {
        return it->second;
    }
    return {};
}

std::string set_code_display_name(const std::string& set_code)
{
    // Check the registered display-name map first (populated when shards declare "set_name").
    {
        std::lock_guard<std::mutex> lock(g_card_catalog_mutex);
        const auto it = g_card_catalog.set_display_names.find(set_code);
        if (it != g_card_catalog.set_display_names.end() && !it->second.empty()) {
            return it->second;
        }
    }
    // Fallback: replace underscores with spaces and title-case each word.
    std::string s = set_code;
    bool cap_next = true;
    for (char& c : s) {
        if (c == '_') {
            c = ' ';
            cap_next = true;
        } else if (cap_next) {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            cap_next = false;
        }
    }
    return s;
}

uint64_t card_catalog_generation()
{
    std::lock_guard<std::mutex> lock(g_card_catalog_mutex);
    return g_card_catalog_generation;
}

std::string card_catalog_fingerprint_utf8()
{
    const std::vector<std::string> keys = list_card_catalog_keys_sorted();
    std::string blob;
    for (const std::string& k : keys) {
        blob += k;
        blob.push_back('\n');
    }
    return std::to_string(std::hash<std::string>{}(blob));
}

void set_active_match_deck_list(DeckListDefinition deck)
{
    std::lock_guard<std::mutex> lock(g_card_catalog_mutex);
    g_active_match_deck_list = std::move(deck);
}

std::optional<DeckListDefinition> get_active_match_deck_list()
{
    std::lock_guard<std::mutex> lock(g_card_catalog_mutex);
    return g_active_match_deck_list;
}

void clear_active_match_deck_list()
{
    std::lock_guard<std::mutex> lock(g_card_catalog_mutex);
    g_active_match_deck_list.reset();
}

std::optional<DeckListDefinition> try_get_starter_deck_list()
{
    std::lock_guard<std::mutex> lock(g_card_catalog_mutex);
    return g_starter_deck_list;
}

void register_runtime_card_definition(CardDefinition def)
{
    std::lock_guard<std::mutex> lock(g_card_catalog_mutex);
    g_card_catalog.upsert(std::move(def));
    ++g_card_catalog_generation;
    sync_card_search_index_generation(g_card_catalog_generation);
}

bool try_get_card_definition(const std::string& key, CardDefinition& out)
{
    std::lock_guard<std::mutex> lock(g_card_catalog_mutex);
    const auto it = g_card_catalog.key_to_id.find(key);
    if (it == g_card_catalog.key_to_id.end()) {
        return false;
    }
    out = g_card_catalog.definitions[it->second.value];
    return true;
}

bool try_get_card_definition(const CardDefId id, CardDefinition& out)
{
    std::lock_guard<std::mutex> lock(g_card_catalog_mutex);
    if (!id.is_valid() || id.value >= g_card_catalog.definitions.size()) {
        return false;
    }
    out = g_card_catalog.definitions[id.value];
    return true;
}

CardDefId try_card_def_id_for_key(const std::string& key)
{
    std::lock_guard<std::mutex> lock(g_card_catalog_mutex);
    const auto it = g_card_catalog.key_to_id.find(key);
    if (it == g_card_catalog.key_to_id.end()) {
        return kInvalidCardDefId;
    }
    return it->second;
}

const CardDefinition* try_get_card_definition_ptr(const std::string& key)
{
    std::lock_guard<std::mutex> lock(g_card_catalog_mutex);
    const auto it = g_card_catalog.key_to_id.find(key);
    if (it == g_card_catalog.key_to_id.end()) {
        return nullptr;
    }
    return &g_card_catalog.definitions[it->second.value];
}

const CardDefinition* try_get_card_definition_ptr(const CardDefId id)
{
    std::lock_guard<std::mutex> lock(g_card_catalog_mutex);
    if (!id.is_valid() || id.value >= g_card_catalog.definitions.size()) {
        return nullptr;
    }
    return &g_card_catalog.definitions[id.value];
}

Deck create_deck_from_deck_list(const DeckListDefinition& deck_list, std::mt19937& rng, bool shuffle, std::string* err_out)
{
    Deck d;
    int global_copy_index = 0;
    for (const auto& entry : deck_list.entries) {
        const CardDefId def_id = try_card_def_id_for_key(entry.card_key);
        if (!def_id.is_valid()) {
            if (err_out) {
                *err_out = "deck list references unknown card \"" + entry.card_key + "\"";
            }
            return {};
        }
        for (int i = 0; i < entry.copies; ++i) {
            d.add_card(deck_allocate_instance(d, def_id, global_copy_index++));
        }
    }
    int reserve_total = 0;
    for (const auto& entry : deck_list.reserves) {
        const CardDefId def_id = try_card_def_id_for_key(entry.card_key);
        if (!def_id.is_valid()) {
            if (err_out) {
                *err_out = "deck list reserves reference unknown card \"" + entry.card_key + "\"";
            }
            return {};
        }
        for (int i = 0; i < entry.copies; ++i) {
            d.reserves.push_back(deck_allocate_instance(d, def_id, global_copy_index++));
            ++reserve_total;
        }
    }
    if (reserve_total > kMaxReservesSize) {
        if (err_out) {
            *err_out = "deck list reserves cannot exceed " + std::to_string(kMaxReservesSize) + " cards";
        }
        return {};
    }
    if (shuffle) {
        d.shuffle(rng);
    }
    return d;
}

Deck create_deck_from_builtin_slot(BuiltinDeckListSlot slot, std::mt19937& rng)
{
    DeckListDefinition deck_list;
    {
        std::lock_guard<std::mutex> lock(g_card_catalog_mutex);
        const std::optional<DeckListDefinition>& cached =
            slot == BuiltinDeckListSlot::Starter ? g_starter_deck_list : g_test_deck_list;
        if (cached.has_value()) {
            deck_list = *cached;
        }
    }
    return create_deck_from_deck_list(deck_list, rng, true, nullptr);
}

Deck create_starter_deck_from_catalog(std::mt19937& rng)
{
    return create_deck_from_builtin_slot(BuiltinDeckListSlot::Starter, rng);
}

Deck create_test_deck_from_catalog(std::mt19937& rng)
{
    return create_deck_from_builtin_slot(BuiltinDeckListSlot::Test, rng);
}

CardPtr create_card_from_definition(const CardDefinition& def, int copy_index)
{
    CardPtr out;
    if (def.type == "spell") {
        auto sp = std::make_shared<SpellCard>();
        if (def.spell) {
            sp->speed = def.spell->speed;
            sp->effect_key = def.spell->effect_key;
            sp->effect_payload = def.spell->effect_payload;
            sp->board_target_kind = def.spell->board_target_kind;
            sp->requires_mandatory_board_cell = def.spell->requires_mandatory_board_cell;
            sp->focus_range = def.spell->focus_range;
            sp->require_target_unit_types = def.spell->require_target_unit_types;
            sp->bonus_damage_unit_types = def.spell->bonus_damage_unit_types;
            sp->bonus_damage_amount = def.spell->bonus_damage_amount;
        }
        out = sp;
    } else {
        auto uc = std::make_shared<UnitCard>();
        if (def.unit) {
            const auto& ud = *def.unit;
            Unit& u = uc->template_unit;
            u.entity_type = ud.entity_type;
            u.unit_type = ud.unit_type;
            u.attack_type = ud.attack_type;
            u.base_health = ud.base_health;
            u.current_health = ud.current_health;
            u.movement = ud.movement;
            u.melee_range = ud.melee_range;
            u.melee_damage = ud.melee_damage;
            u.melee_damage_min = ud.melee_damage_min;
            u.melee_damage_max = ud.melee_damage_max;
            u.ranged_range = ud.ranged_range;
            u.ranged_deadzone = ud.ranged_deadzone;
            u.ranged_damage = ud.ranged_damage;
            u.ranged_damage_min = ud.ranged_damage_min;
            u.ranged_damage_max = ud.ranged_damage_max;
            u.crit_chance_percent = ud.crit_chance_percent;
            sync_unit_damage_ranges_from_nominal(u);
            u.line_of_sight_blocked = ud.line_of_sight_blocked;
            u.shape = ud.shape;
            for (const auto& attr : ud.keywords) {
                if (attr.amount.has_value()) {
                    set_entity_attribute_amount(u, attr.key, *attr.amount);
                } else {
                    add_entity_attribute(u, attr.key);
                }
            }
            for (const auto& effect : ud.initial_effects) {
                add_entity_effect(u, effect.key, effect.amount);
            }
            {
                std::string resolve_err;
                if (!resolve_passive_abilities_for_unit(ud.passive_ability_ids, ud.passive_abilities, u.passive_abilities, resolve_err)) {
                    static_cast<void>(resolve_err);
                }
            }
            {
                std::string resolve_err;
                if (!resolve_activated_abilities_for_card(def.abilities, def.ability_overrides, u.activated_abilities, resolve_err)) {
                    static_cast<void>(resolve_err);
                } else if (!ud.activated_abilities.empty()) {
                    u.activated_abilities.insert(u.activated_abilities.end(),
                        ud.activated_abilities.begin(), ud.activated_abilities.end());
                }
            }
            u.unit_types = def.unit_types;
            normalize_entity_shape(u);
        }
        out = uc;
    }

    out->definition_key = def.key;
    out->card_id = def.key + "_" + std::to_string(copy_index);
    out->name = def.name;
    out->card_type = def.type;
    out->rules_text = def.rules_text;
    out->art_id = def.art_id;
    out->tags = def.tags;
    out->unit_types = def.unit_types;
    out->energy_cost = def.energy_cost;
    for (const auto& attr : def.keywords) {
        if (attr.key == "stockpile" && attr.amount.has_value()) {
            set_card_stockpile(*out, *attr.amount);
        } else {
            add_card_attribute(*out, attr.key);
        }
    }
    return out;
}

}  // namespace tactics
