#include "tactics/cards/effect_definitions_io.hpp"

#include "tactics/common/types.hpp"
#include "tactics/attributes/attributes.hpp"
#include "tactics/cards/unit_types.hpp"
#include "tactics/effects/effect_registry.hpp"

#include <algorithm>
#include <optional>

namespace tactics::effect_io {
namespace {

bool read_attribute_array(const json& j, std::vector<std::string>& out, std::string& err, const std::string& path)
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
        const std::string key = v.get<std::string>();
        if (!find_attribute_spec(key)) {
            err = path + " references unknown attribute \"" + key + "\"";
            return false;
        }
        out.push_back(key);
    }
    return true;
}

}  // namespace

bool board_target_from_json_object(const json& j, std::optional<BoardTargetKind>& out, std::string& err, const std::string& path)
{
    if (!j.is_string()) {
        err = path + " must be a string";
        return false;
    }
    const auto pk = board_target_kind_parse(j.get<std::string>());
    if (!pk) {
        err = path + " is invalid";
        return false;
    }
    out = *pk;
    return true;
}

bool read_energy_cost_object(const json& j, std::map<EnergyType, int>& out, std::string& err, const std::string& path)
{
    out.clear();
    if (!j.is_object()) {
        err = path + " must be an object";
        return false;
    }
    for (auto it = j.begin(); it != j.end(); ++it) {
        const auto et = energy_type_from_string(it.key());
        if (!et) {
            err = path + " has unknown energy key \"" + it.key() + "\"";
            return false;
        }
        if (!it.value().is_number_integer() || it.value().get<int>() < 0) {
            err = path + "." + it.key() + " must be a non-negative integer";
            return false;
        }
        out[*et] = it.value().get<int>();
    }
    return true;
}

void read_string_int_map_object(const json& j, std::map<std::string, int>& out)
{
    out.clear();
    if (!j.is_object()) {
        return;
    }
    for (auto it = j.begin(); it != j.end(); ++it) {
        out[it.key()] = it.value().get<int>();
    }
}

std::vector<PassiveAttributeGrant> passive_grants_from_keyword_keys(const std::vector<std::string>& keys)
{
    std::vector<PassiveAttributeGrant> out;
    out.reserve(keys.size());
    for (const auto& key : keys) {
        if (attribute_is_non_copyable(key)) {
            continue;
        }
        out.push_back({key, std::nullopt});
    }
    return out;
}

bool read_passive_stat_grants_object(const json& j, PassiveStatGrant& out, std::string& err, const std::string& path)
{
    if (!j.is_object()) {
        err = path + " must be an object";
        return false;
    }
    auto field = [&](const char* primary, const char* alt = nullptr) {
        if (j.contains(primary)) {
            return j[primary].get<int>();
        }
        if (alt != nullptr && j.contains(alt)) {
            return j[alt].get<int>();
        }
        return 0;
    };
    out.bonus_attack = field("attack");
    out.bonus_health = field("health");
    out.bonus_melee_damage = field("melee_damage");
    out.bonus_ranged_damage = field("ranged_damage");
    out.bonus_movement = field("movement");
    out.bonus_moves = field("moves");
    out.bonus_attacks = field("attacks");
    out.on_expire_next_damage_bonus = field("on_expire_ndb");
    out.bonus_ability_damage = field("ability_damage", "bonus_ability_damage");
    out.bonus_aura_range = field("aura_range", "bonus_aura_range");
    out.bonus_aura_attack = field("bonus_aura_attack");
    out.bonus_aura_health = field("bonus_aura_health");
    out.bonus_aura_ability_damage = field("bonus_aura_ability_damage");
    out.override_ranged_damage_min = field("override_ranged_min");
    out.override_ranged_damage_max = field("override_ranged_max");
    out.survive_lethal_percent = field("survive_lethal_percent");
    out.survive_lethal_bonus_attack = field("survive_lethal_bonus_attack");
    out.survive_lethal_bonus_health = field("survive_lethal_bonus_health");
    out.bonus_armor = field("armor");
    return true;
}

bool read_ability_spec_object(const json& aj, AbilitySpec& a, std::string& err, const std::string& path)
{
    a.key = aj.value("key", std::string{});
    if (!aj.contains("name")) {
        err = path + " requires name";
        return false;
    }
    a.name = aj.at("name").get<std::string>();
    if (a.key.empty()) {
        a.key = a.name;
    }
    a.description = aj.value("description", std::string{});
    a.normal_description = aj.value("normal_description", aj.value("simple_description", std::string{}));
    const auto speed = effect_speed_from_string(aj.value("speed", std::string{"channeled"}));
    if (!speed) {
        err = path + " has invalid speed";
        return false;
    }
    a.speed = *speed;
    if (!read_energy_cost_object(aj.value("energy_cost", json::object()), a.energy_cost, err, path + ".energy_cost")) {
        return false;
    }
    if (!aj.contains("effect_key")) {
        err = path + " requires effect_key";
        return false;
    }
    a.effect_key = aj.at("effect_key").get<std::string>();
    if (!is_known_effect_key(a.effect_key)) {
        err = path + " references unknown effect \"" + a.effect_key + "\"";
        return false;
    }
    read_string_int_map_object(aj.value("effect_payload", json::object()), a.effect_payload);
    if (aj.contains("effect_string_payload") && aj.at("effect_string_payload").is_object()) {
        for (const auto& [k, v] : aj.at("effect_string_payload").items()) {
            if (v.is_string()) a.effect_string_payload[k] = v.get<std::string>();
        }
    }
    a.keywords.clear();
    if (aj.contains("keywords")) {
        if (!read_attribute_array(aj["keywords"], a.keywords, err, path + ".keywords")) {
            return false;
        }
    }
    a.requires_board_target.reset();
    if (aj.contains("requires_board_target")) {
        if (aj["requires_board_target"].is_boolean()) {
            a.requires_board_target = aj["requires_board_target"].get<bool>();
        } else if (aj["requires_board_target"].is_null()) {
            a.requires_board_target = std::nullopt;
        }
    }
    a.board_target_kind.reset();
    if (aj.contains("board_target_kind")) {
        if (!board_target_from_json_object(aj["board_target_kind"], a.board_target_kind, err, path + ".board_target_kind")) {
            return false;
        }
    }
    a.consumes_attack_action = aj.value("consumes_attack_action", false);
    if (aj.contains("attack_cost") && aj["attack_cost"].get<bool>()) {
        a.consumes_attack_action = true;
    }
    if (aj.contains("require_target_unit_types")
        && !read_unit_types_json_array(aj["require_target_unit_types"], a.require_target_unit_types, err,
            path + ".require_target_unit_types")) {
        return false;
    }
    if (aj.contains("require_target_entity_types")) {
        std::vector<std::string> entity_types;
        if (!read_unit_types_json_array(aj["require_target_entity_types"], entity_types, err,
                path + ".require_target_entity_types")) {
            return false;
        }
        for (const std::string& t : entity_types) {
            if (std::find(a.require_target_unit_types.begin(), a.require_target_unit_types.end(), t)
                    == a.require_target_unit_types.end()) {
                a.require_target_unit_types.push_back(t);
            }
        }
    }
    if (aj.contains("bonus_damage_unit_types")
        && !read_unit_types_json_array(aj["bonus_damage_unit_types"], a.bonus_damage_unit_types, err,
            path + ".bonus_damage_unit_types")) {
        return false;
    }
    if (aj.contains("bonus_damage_amount")) {
        if (!aj["bonus_damage_amount"].is_number_integer()) {
            err = path + ".bonus_damage_amount must be an integer";
            return false;
        }
        a.bonus_damage_amount = aj["bonus_damage_amount"].get<int>();
    }
    a.uses_ranged_targeting.reset();
    if (aj.contains("uses_ranged_targeting")) {
        if (!aj["uses_ranged_targeting"].is_boolean()) {
            err = path + ".uses_ranged_targeting must be a boolean";
            return false;
        }
        a.uses_ranged_targeting = aj["uses_ranged_targeting"].get<bool>();
    }
    if (aj.contains("range_max")) {
        if (!aj["range_max"].is_number_integer() || aj["range_max"].get<int>() < 0) {
            err = path + ".range_max must be a non-negative integer";
            return false;
        }
        a.range_max = aj["range_max"].get<int>();
    }
    if (aj.contains("range_min")) {
        if (!aj["range_min"].is_number_integer() || aj["range_min"].get<int>() < 0) {
            err = path + ".range_min must be a non-negative integer";
            return false;
        }
        a.range_min = aj["range_min"].get<int>();
    }
    a.barrage = aj.value("barrage", false);
    a.uses_per_turn = -1;
    if (aj.contains("uses_per_turn")) {
        if (!aj["uses_per_turn"].is_number_integer() || aj["uses_per_turn"].get<int>() < 0) {
            err = path + ".uses_per_turn must be a non-negative integer";
            return false;
        }
        a.uses_per_turn = aj["uses_per_turn"].get<int>();
    }
    a.uses_per_game = -1;
    if (aj.contains("uses_per_game")) {
        if (!aj["uses_per_game"].is_number_integer() || aj["uses_per_game"].get<int>() < 0) {
            err = path + ".uses_per_game must be a non-negative integer";
            return false;
        }
        a.uses_per_game = aj["uses_per_game"].get<int>();
    }
    if (aj.contains("barrage_cost")) {
        if (!read_energy_cost_object(aj["barrage_cost"], a.barrage_cost, err, path + ".barrage_cost")) {
            return false;
        }
    }
    a.chain = aj.value("chain", false);
    a.no_phase_batch_lock = aj.value("no_phase_batch_lock", false);
    a.x_cost_energy_type.reset();
    a.x_cost_min = 0;
    if (aj.contains("x_cost")) {
        const auto& xj = aj["x_cost"];
        if (!xj.is_object() || !xj.contains("type") || !xj["type"].is_string()) {
            err = path + ".x_cost must be an object with a \"type\" field";
            return false;
        }
        const auto et = energy_type_from_string(xj["type"].get<std::string>());
        if (!et) {
            err = path + ".x_cost.type is not a valid energy type";
            return false;
        }
        a.x_cost_energy_type = *et;
        if (xj.contains("min")) {
            if (!xj["min"].is_number_integer() || xj["min"].get<int>() < 0) {
                err = path + ".x_cost.min must be a non-negative integer";
                return false;
            }
            a.x_cost_min = xj["min"].get<int>();
        }
    }
    return true;
}

bool read_passive_spec_object(const json& v, PassiveAbilitySpec& passive, std::string& err, const std::string& path)
{
    passive.key = v.at("key").get<std::string>();
    passive.name = v.value("name", passive.key);
    passive.rules_text = v.value("rules_text", std::string{});
    passive.normal_rules_text = v.value("normal_rules_text", v.value("simple_rules_text", std::string{}));
    passive.applies_to = v.value("applies_to", std::string{"self"});
    if (passive.key.empty() || passive.name.empty()) {
        err = path + " requires non-empty key and name";
        return false;
    }
    if (passive.applies_to != "self" && passive.applies_to != "allied_units" && passive.applies_to != "allied_structures") {
        err = path + ".applies_to must be \"self\", \"allied_units\", or \"allied_structures\"";
        return false;
    }
    if (v.contains("affects_unit_types")
        && !read_unit_types_json_array(v["affects_unit_types"], passive.affects_unit_types, err, path + ".affects_unit_types")) {
        return false;
    }
    passive.granted_attributes.clear();
    if (v.contains("keywords")) {
        std::vector<std::string> keys;
        if (!read_attribute_array(v["keywords"], keys, err, path + ".keywords")) {
            return false;
        }
        passive.granted_attributes = passive_grants_from_keyword_keys(keys);
    }
    if (v.contains("stat_grants") &&
        !read_passive_stat_grants_object(v["stat_grants"], passive.stat_grants, err, path + ".stat_grants")) {
        return false;
    }
    passive.trigger_timing = v.value("trigger_timing", std::string{});
    passive.automated_effect_key = v.value("automated_effect_key", std::string{});
    if (v.contains("automated_effect_payload")) {
        read_string_int_map_object(v["automated_effect_payload"], passive.automated_effect_payload);
    }
    if (v.contains("automated_string_payload") && v["automated_string_payload"].is_object()) {
        for (const auto& [k, val] : v["automated_string_payload"].items()) {
            if (val.is_string()) passive.automated_string_payload[k] = val.get<std::string>();
        }
    }
    if (v.contains("automated_board_target_kind")) {
        if (!board_target_from_json_object(v["automated_board_target_kind"], passive.automated_board_target_kind, err,
                path + ".automated_board_target_kind")) {
            return false;
        }
    }
    passive.reactive_trigger = v.value("reactive_trigger", std::string{});
    passive.reactive_effect_key = v.value("reactive_effect_key", std::string{});
    if (v.contains("reactive_effect_payload")) {
        read_string_int_map_object(v["reactive_effect_payload"], passive.reactive_effect_payload);
    }
    if (v.contains("reactive_string_payload") && v["reactive_string_payload"].is_object()) {
        for (const auto& [k, val] : v["reactive_string_payload"].items()) {
            if (val.is_string()) passive.reactive_string_payload[k] = val.get<std::string>();
        }
    }
    passive.sort_order = v.value("sort_order", 0);
    passive.passive_mechanic = v.value("passive_mechanic", std::string{});
    passive.mechanic_payload.clear();
    if (v.contains("mechanic_payload")) {
        read_string_int_map_object(v["mechanic_payload"], passive.mechanic_payload);
    }
    passive.aura_range = v.value("aura_range", -1);
    passive.forces_damage_spell_focus_casting = v.value("forces_damage_spell_focus_casting", false);
    passive.forced_damage_spell_focus_range = v.value("forced_damage_spell_focus_range", 0);
    passive.is_positive = v.value("is_positive", false);
    passive.is_negative = v.value("is_negative", false);
    return true;
}

bool read_ability_override_patch_object(const json& j, AbilityOverridePatch& out, std::string& err, const std::string& path)
{
    if (!j.is_object()) {
        err = path + " must be an object";
        return false;
    }
    if (j.contains("name")) {
        out.name = j["name"].get<std::string>();
    }
    if (j.contains("speed")) {
        const auto speed = effect_speed_from_string(j["speed"].get<std::string>());
        if (!speed) {
            err = path + ".speed is invalid";
            return false;
        }
        out.speed = *speed;
    }
    if (j.contains("energy_cost")) {
        std::map<EnergyType, int> cost;
        if (!read_energy_cost_object(j["energy_cost"], cost, err, path + ".energy_cost")) {
            return false;
        }
        out.energy_cost = std::move(cost);
    }
    if (j.contains("effect_key")) {
        out.effect_key = j["effect_key"].get<std::string>();
    }
    if (j.contains("effect_payload")) {
        read_string_int_map_object(j["effect_payload"], out.effect_payload);
    }
    if (j.contains("keywords")) {
        if (!read_attribute_array(j["keywords"], out.add_keywords, err, path + ".keywords")) {
            return false;
        }
    }
    if (j.contains("requires_board_target")) {
        if (j["requires_board_target"].is_boolean()) {
            out.requires_board_target = j["requires_board_target"].get<bool>();
        } else if (j["requires_board_target"].is_null()) {
            out.requires_board_target = std::nullopt;
        }
    }
    if (j.contains("board_target_kind")) {
        if (!board_target_from_json_object(j["board_target_kind"], out.board_target_kind, err, path + ".board_target_kind")) {
            return false;
        }
    }
    if (j.contains("consumes_attack_action")) {
        out.consumes_attack_action = j["consumes_attack_action"].get<bool>();
    }
    if (j.contains("attack_cost") && j["attack_cost"].get<bool>()) {
        out.consumes_attack_action = true;
    }
    if (j.contains("require_target_unit_types")) {
        std::vector<std::string> types;
        if (!read_unit_types_json_array(j["require_target_unit_types"], types, err, path + ".require_target_unit_types")) {
            return false;
        }
        out.require_target_unit_types = std::move(types);
    }
    if (j.contains("bonus_damage_unit_types")) {
        std::vector<std::string> types;
        if (!read_unit_types_json_array(j["bonus_damage_unit_types"], types, err, path + ".bonus_damage_unit_types")) {
            return false;
        }
        out.bonus_damage_unit_types = std::move(types);
    }
    if (j.contains("bonus_damage_amount")) {
        if (!j["bonus_damage_amount"].is_number_integer()) {
            err = path + ".bonus_damage_amount must be an integer";
            return false;
        }
        out.bonus_damage_amount = j["bonus_damage_amount"].get<int>();
    }
    return true;
}

}  // namespace tactics::effect_io
