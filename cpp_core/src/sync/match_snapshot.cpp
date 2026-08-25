#include "tactics/board/aether.hpp"
#include "tactics/board/scanner.hpp"
#include "tactics/board/omni_energy_tile.hpp"
#include "tactics/core/game_state.hpp"

#include "tactics/sync/match_sync.hpp"
#include "tactics/sync/wire_version.hpp"
#include "tactics/cards/card_catalog.hpp"
#include "tactics/cards/card_runtime.hpp"
#include "tactics/cards/cards.hpp"
#include "tactics/cards/effect_definitions_io.hpp"
#include "tactics/attributes/attributes.hpp"
#include "tactics/common/types.hpp"
#include "tactics/entities/entity.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace tactics {
namespace {

using json = nlohmann::json;

// E6: parse_energy and energy_key removed - replaced by canonical energy_type_from_string()
// and to_string(EnergyType) from tactics/common/types.hpp throughout this file.

json aether_cluster_json(const AetherClusterState& cluster)
{
	json cj = json::object();
	cj["cluster_id"] = cluster.cluster_id;
	json cells = json::array();
	for (const auto& [wx, wy] : cluster.cells) {
		json cell = json::object();
		cell["x"] = wx;
		cell["y"] = wy;
		cells.push_back(std::move(cell));
	}
	cj["cells"] = std::move(cells);
	cj["damage_next"] = cluster.damage_next;
	cj["units_killed"] = cluster.units_killed;
	if (cluster.last_sole_control_team.has_value()) {
		cj["last_sole_control_team"] = *cluster.last_sole_control_team;
	} else {
		cj["last_sole_control_team"] = nullptr;
	}
	json fired = json::array();
	for (const int team_id : cluster.teams_fired_this_round) {
		fired.push_back(team_id);
	}
	cj["teams_fired_this_round"] = std::move(fired);
	return cj;
}

json scanner_cluster_json(const ScannerClusterState& cluster)
{
	json cj = json::object();
	cj["cluster_id"] = cluster.cluster_id;
	cj["home_seat"] = cluster.home_seat;
	json cells = json::array();
	for (const auto& [wx, wy] : cluster.cells) {
		json cell = json::object();
		cell["x"] = wx;
		cell["y"] = wy;
		cells.push_back(std::move(cell));
	}
	cj["cells"] = std::move(cells);
	json fired = json::array();
	for (const int team_id : cluster.teams_fired_this_round) {
		fired.push_back(team_id);
	}
	cj["teams_fired_this_round"] = std::move(fired);
	return cj;
}

ScannerClusterState scanner_cluster_from_json(const json& cj)
{
	ScannerClusterState cluster;
	cluster.cluster_id = cj.at("cluster_id").get<std::string>();
	cluster.home_seat = cj.value("home_seat", 0);
	if (cj.contains("teams_fired_this_round") && cj["teams_fired_this_round"].is_array()) {
		for (const auto& team_j : cj["teams_fired_this_round"]) {
			cluster.teams_fired_this_round.insert(team_j.get<int>());
		}
	}
	if (cj.contains("cells") && cj["cells"].is_array()) {
		for (const auto& cell : cj["cells"]) {
			cluster.cells.emplace_back(cell.at("x").get<int>(), cell.at("y").get<int>());
		}
	}
	return cluster;
}

json omni_energy_cluster_json(const OmniEnergyClusterState& cluster)
{
	json cj = json::object();
	cj["cluster_id"] = cluster.cluster_id;
	cj["home_seat"] = cluster.home_seat;
	json cells = json::array();
	for (const auto& [wx, wy] : cluster.cells) {
		json cell = json::object();
		cell["x"] = wx;
		cell["y"] = wy;
		cells.push_back(std::move(cell));
	}
	cj["cells"] = std::move(cells);
	json fired = json::array();
	for (const int team_id : cluster.teams_fired_this_round) {
		fired.push_back(team_id);
	}
	cj["teams_fired_this_round"] = std::move(fired);
	return cj;
}

OmniEnergyClusterState omni_energy_cluster_from_json(const json& cj)
{
	OmniEnergyClusterState cluster;
	cluster.cluster_id = cj.at("cluster_id").get<std::string>();
	cluster.home_seat = cj.value("home_seat", 0);
	if (cj.contains("teams_fired_this_round") && cj["teams_fired_this_round"].is_array()) {
		for (const auto& team_j : cj["teams_fired_this_round"]) {
			cluster.teams_fired_this_round.insert(team_j.get<int>());
		}
	}
	if (cj.contains("cells") && cj["cells"].is_array()) {
		for (const auto& cell : cj["cells"]) {
			cluster.cells.emplace_back(cell.at("x").get<int>(), cell.at("y").get<int>());
		}
	}
	return cluster;
}

AetherClusterState aether_cluster_from_json(const json& cj)
{
	AetherClusterState cluster;
	cluster.cluster_id = cj.at("cluster_id").get<std::string>();
	cluster.damage_next = cj.value("damage_next", 1);
	cluster.units_killed = cj.value("units_killed", 0);
	if (cj.contains("last_sole_control_team") && !cj["last_sole_control_team"].is_null()) {
		cluster.last_sole_control_team = cj["last_sole_control_team"].get<int>();
	}
	if (cj.contains("teams_fired_this_round") && cj["teams_fired_this_round"].is_array()) {
		for (const auto& team_j : cj["teams_fired_this_round"]) {
			cluster.teams_fired_this_round.insert(team_j.get<int>());
		}
	}
	if (cj.contains("cells") && cj["cells"].is_array()) {
		for (const auto& cell_j : cj["cells"]) {
			cluster.cells.emplace_back(cell_j.at("x").get<int>(), cell_j.at("y").get<int>());
		}
	}
	return cluster;
}

void read_energy_pool_json(const json& j, std::map<EnergyType, int>& pool)
{
	for (EnergyType et : kEnergyBillingAllTypes) {
		pool[et] = 0;
	}
	if (!j.is_object()) {
		return;
	}
	for (auto it = j.begin(); it != j.end(); ++it) {
		const auto et = energy_type_from_string(it.key());
		if (!et || !it.value().is_number_integer()) {
			continue;
		}
		pool[*et] = it.value().get<int>();
	}
}

json map_energy_cost_json(const std::map<EnergyType, int>& m)
{
	json j = json::object();
	for (const auto& [et, amt] : m) {
		j[to_string(et)] = amt;
	}
	return j;
}

void read_energy_cost_json(const json& j, std::map<EnergyType, int>& m)
{
	std::string err;
	if (!effect_io::read_energy_cost_object(j, m, err, "energy_cost")) {
		throw std::runtime_error(err);
	}
}

json map_string_int_json(const std::map<std::string, int>& mp)
{
	json j = json::object();
	for (const auto& [k, v] : mp) j[k] = v;
	return j;
}

void read_string_int_json(const json& j, std::map<std::string, int>& mp)
{
	effect_io::read_string_int_map_object(j, mp);
}

const char* phase_str(TurnPhase p)
{
	return turn_phase_to_string(p);
}

TurnPhase parse_phase(const std::string& s)
{
	return turn_phase_from_string(s);
}

AttackType parse_atk(const std::string& s)
{
	const auto parsed = attack_type_from_string(s);
	if (!parsed) {
		throw std::runtime_error("unknown attack type: " + s);
	}
	return *parsed;
}

EffectSpeed parse_speed(const std::string& s)
{
	const auto parsed = effect_speed_from_string(s);
	if (!parsed) {
		throw std::runtime_error("unknown effect speed: " + s);
	}
	return *parsed;
}

json abilities_json(const std::vector<AbilitySpec>& abs)
{
	json arr = json::array();
	for (const auto& a : abs) {
		json aj = json::object();
		aj["key"] = a.key;
		aj["name"] = a.name;
		aj["speed"] = effect_speed_to_string(a.speed);
		aj["energy_cost"] = map_energy_cost_json(a.energy_cost);
		aj["effect_key"] = a.effect_key;
		aj["effect_payload"] = map_string_int_json(a.effect_payload);
		if (!a.effect_string_payload.empty()) {
			json esp = json::object();
			for (const auto& [k, v] : a.effect_string_payload) { esp[k] = v; }
			aj["effect_string_payload"] = std::move(esp);
		}
		aj["keywords"] = a.keywords;
		if (a.requires_board_target.has_value()) {
			aj["requires_board_target"] = *a.requires_board_target;
		}
		if (a.board_target_kind.has_value()) {
			aj["board_target_kind"] = board_target_kind_to_string(*a.board_target_kind);
		}
		aj["consumes_attack_action"] = a.consumes_attack_action;
		if (!a.require_target_unit_types.empty()) {
			aj["require_target_unit_types"] = a.require_target_unit_types;
		}
		if (!a.bonus_damage_unit_types.empty()) {
			aj["bonus_damage_unit_types"] = a.bonus_damage_unit_types;
		}
		if (a.bonus_damage_amount != 0) {
			aj["bonus_damage_amount"] = a.bonus_damage_amount;
		}
		// Range overrides affect targeting; omit them and restore falls back to wrong defaults.
		if (a.range_max != 0) aj["range_max"] = a.range_max;
		if (a.range_min != 0) aj["range_min"] = a.range_min;
		// Explicit ranged/melee targeting mode.
		if (a.uses_ranged_targeting.has_value()) {
			aj["uses_ranged_targeting"] = *a.uses_ranged_targeting;
		}
		if (a.barrage) {
			aj["barrage"] = true;
			if (!a.barrage_cost.empty()) {
				aj["barrage_cost"] = map_energy_cost_json(a.barrage_cost);
			}
		}
		if (a.uses_per_turn >= 0) {
			aj["uses_per_turn"] = a.uses_per_turn;
		}
		if (a.uses_per_game >= 0) {
			aj["uses_per_game"] = a.uses_per_game;
		}
		arr.push_back(aj);
	}
	return arr;
}

void read_abilities_json(const json& j, std::vector<AbilitySpec>& abs)
{
	abs.clear();
	if (!j.is_array()) return;
	for (const auto& aj : j) {
		AbilitySpec a;
		a.key = aj.at("key").get<std::string>();
		a.name = aj.at("name").get<std::string>();
		a.speed = parse_speed(aj.value("speed", std::string{"channeled"}));
		read_energy_cost_json(aj.at("energy_cost"), a.energy_cost);
		a.effect_key = aj.at("effect_key").get<std::string>();
		read_string_int_json(aj.at("effect_payload"), a.effect_payload);
		if (aj.contains("effect_string_payload") && aj.at("effect_string_payload").is_object()) {
			for (const auto& [k, v] : aj.at("effect_string_payload").items()) {
				if (v.is_string()) a.effect_string_payload[k] = v.get<std::string>();
			}
		}
		a.keywords.clear();
		if (aj.contains("keywords") && aj["keywords"].is_array()) {
			for (const auto& attr : aj["keywords"]) {
				if (attr.is_string()) {
					add_ability_attribute(a, attr.get<std::string>());
				}
			}
		}
		if (aj.contains("requires_board_target")) {
			if (aj["requires_board_target"].is_boolean()) {
				a.requires_board_target = aj["requires_board_target"].get<bool>();
			} else if (aj["requires_board_target"].is_null()) {
				a.requires_board_target = std::nullopt;
			}
		}
		if (aj.contains("board_target_kind") && aj["board_target_kind"].is_string()) {
			if (const auto pk = board_target_kind_parse(aj["board_target_kind"].get<std::string>())) {
				a.board_target_kind = *pk;
			}
		}
		a.consumes_attack_action = aj.value("consumes_attack_action", false);
		a.require_target_unit_types = aj.value("require_target_unit_types", std::vector<std::string>{});
		a.bonus_damage_unit_types = aj.value("bonus_damage_unit_types", std::vector<std::string>{});
		a.bonus_damage_amount = aj.value("bonus_damage_amount", 0);
		// Restore range overrides.
		a.range_max = aj.value("range_max", 0);
		a.range_min = aj.value("range_min", 0);
		// Restore optional ranged-targeting mode.
		if (aj.contains("uses_ranged_targeting") && aj["uses_ranged_targeting"].is_boolean()) {
			a.uses_ranged_targeting = aj["uses_ranged_targeting"].get<bool>();
		} else {
			a.uses_ranged_targeting = std::nullopt;
		}
		a.barrage = aj.value("barrage", false);
		a.barrage_cost.clear();
		if (aj.contains("barrage_cost")) {
			read_energy_cost_json(aj["barrage_cost"], a.barrage_cost);
		}
		a.uses_per_turn = -1;
		if (aj.contains("uses_per_turn") && aj["uses_per_turn"].is_number_integer()) {
			a.uses_per_turn = aj["uses_per_turn"].get<int>();
		}
		a.uses_per_game = -1;
		if (aj.contains("uses_per_game") && aj["uses_per_game"].is_number_integer()) {
			a.uses_per_game = aj["uses_per_game"].get<int>();
		}
		abs.push_back(std::move(a));
	}
}

json passive_attribute_grants_json(const std::vector<PassiveAttributeGrant>& grants)
{
	json arr = json::array();
	for (const auto& grant : grants) {
		json gj = json::object();
		gj["key"] = grant.key;
		if (grant.amount.has_value()) {
			gj["amount"] = *grant.amount;
		}
		arr.push_back(std::move(gj));
	}
	return arr;
}

json passive_stat_grants_json(const PassiveStatGrant& stats)
{
	json j = json::object();
	j["attack"] = stats.bonus_attack;
	j["health"] = stats.bonus_health;
	j["melee_damage"] = stats.bonus_melee_damage;
	j["ranged_damage"] = stats.bonus_ranged_damage;
	j["movement"] = stats.bonus_movement;
	j["moves"] = stats.bonus_moves;
	j["attacks"] = stats.bonus_attacks;
	if (stats.on_expire_next_damage_bonus != 0) {
		j["on_expire_ndb"] = stats.on_expire_next_damage_bonus;
	}
	if (stats.override_ranged_damage_min != 0) {
		j["override_ranged_min"] = stats.override_ranged_damage_min;
	}
	if (stats.override_ranged_damage_max != 0) {
		j["override_ranged_max"] = stats.override_ranged_damage_max;
	}
	if (stats.bonus_ability_damage != 0)       { j["bonus_ability_damage"]      = stats.bonus_ability_damage; }
	if (stats.bonus_aura_range != 0)           { j["bonus_aura_range"]          = stats.bonus_aura_range; }
	if (stats.bonus_aura_attack != 0)          { j["bonus_aura_attack"]         = stats.bonus_aura_attack; }
	if (stats.bonus_aura_health != 0)          { j["bonus_aura_health"]         = stats.bonus_aura_health; }
	if (stats.bonus_aura_ability_damage != 0)  { j["bonus_aura_ability_damage"] = stats.bonus_aura_ability_damage; }
	if (stats.bonus_armor != 0)                { j["armor"] = stats.bonus_armor; }
	return j;
}

PassiveStatGrant passive_stat_grants_from_json(const json& j)
{
	PassiveStatGrant stats;
	if (!j.is_object()) {
		return stats;
	}
	std::string err;
	if (!effect_io::read_passive_stat_grants_object(j, stats, err, "passive_stat_grants")) {
		throw std::runtime_error(err);
	}
	return stats;
}

void read_passive_attribute_grants_json(const json& j, std::vector<PassiveAttributeGrant>& grants)
{
	grants.clear();
	if (!j.is_array()) return;
	for (const auto& gj : j) {
		PassiveAttributeGrant grant;
		if (gj.is_string()) {
			grant.key = gj.get<std::string>();
		} else if (gj.is_object()) {
			grant.key = gj.at("key").get<std::string>();
			if (gj.contains("amount") && gj["amount"].is_number_integer()) {
				grant.amount = gj["amount"].get<int>();
			}
		}
		if (!grant.key.empty()) {
			grants.push_back(std::move(grant));
		}
	}
}

json passive_abilities_json(const std::vector<PassiveAbilitySpec>& passives)
{
	json arr = json::array();
	for (const auto& passive : passives) {
		json pj = json::object();
		pj["key"] = passive.key;
		pj["name"] = passive.name;
		pj["rules_text"] = passive.rules_text;
		pj["applies_to"] = passive.applies_to;
		if (!passive.affects_unit_types.empty()) {
			pj["affects_unit_types"] = passive.affects_unit_types;
		}
		pj["keywords"] = passive_attribute_grants_json(passive.granted_attributes);
		pj["stat_grants"] = passive_stat_grants_json(passive.stat_grants);
		if (!passive.trigger_timing.empty()) {
			pj["trigger_timing"] = passive.trigger_timing;
		}
		if (!passive.automated_effect_key.empty()) {
			pj["automated_effect_key"] = passive.automated_effect_key;
		}
		if (!passive.automated_effect_payload.empty()) {
			pj["automated_effect_payload"] = map_string_int_json(passive.automated_effect_payload);
		}
		if (!passive.automated_string_payload.empty()) {
			json asp = json::object();
			for (const auto& [k, v] : passive.automated_string_payload) asp[k] = v;
			pj["automated_string_payload"] = asp;
		}
		if (passive.automated_board_target_kind.has_value()) {
			pj["automated_board_target_kind"] = board_target_kind_to_string(*passive.automated_board_target_kind);
		}
		if (!passive.reactive_trigger.empty()) {
			pj["reactive_trigger"] = passive.reactive_trigger;
		}
		if (!passive.reactive_effect_key.empty()) {
			pj["reactive_effect_key"] = passive.reactive_effect_key;
		}
		if (!passive.reactive_effect_payload.empty()) {
			pj["reactive_effect_payload"] = map_string_int_json(passive.reactive_effect_payload);
		}
		if (!passive.reactive_string_payload.empty()) {
			json rsp = json::object();
			for (const auto& [k, v] : passive.reactive_string_payload) rsp[k] = v;
			pj["reactive_string_payload"] = rsp;
		}
		if (passive.sort_order != 0) {
			pj["sort_order"] = passive.sort_order;
		}
		if (!passive.passive_mechanic.empty()) {
			pj["passive_mechanic"] = passive.passive_mechanic;
		}
		if (!passive.mechanic_payload.empty()) {
			pj["mechanic_payload"] = map_string_int_json(passive.mechanic_payload);
		}
		arr.push_back(std::move(pj));
	}
	return arr;
}

void read_passive_abilities_json(const json& j, std::vector<PassiveAbilitySpec>& passives)
{
	passives.clear();
	if (!j.is_array()) return;
	for (const auto& pj : j) {
		PassiveAbilitySpec passive;
		passive.key = pj.at("key").get<std::string>();
		passive.name = pj.value("name", passive.key);
		passive.rules_text = pj.value("rules_text", std::string{});
		passive.applies_to = pj.value("applies_to", std::string{"self"});
		passive.affects_unit_types = pj.value("affects_unit_types", std::vector<std::string>{});
		if (pj.contains("keywords")) {
			read_passive_attribute_grants_json(pj["keywords"], passive.granted_attributes);
		}
		if (pj.contains("stat_grants")) {
			passive.stat_grants = passive_stat_grants_from_json(pj["stat_grants"]);
		}
		passive.trigger_timing = pj.value("trigger_timing", std::string{});
		passive.automated_effect_key = pj.value("automated_effect_key", std::string{});
		if (pj.contains("automated_effect_payload")) {
			read_string_int_json(pj["automated_effect_payload"], passive.automated_effect_payload);
		}
		if (pj.contains("automated_string_payload") && pj["automated_string_payload"].is_object()) {
			for (const auto& [k, val] : pj["automated_string_payload"].items()) {
				if (val.is_string()) passive.automated_string_payload[k] = val.get<std::string>();
			}
		}
		if (pj.contains("automated_board_target_kind") && pj["automated_board_target_kind"].is_string()) {
			if (const auto pk = board_target_kind_parse(pj["automated_board_target_kind"].get<std::string>())) {
				passive.automated_board_target_kind = *pk;
			}
		}
		passive.reactive_trigger = pj.value("reactive_trigger", std::string{});
		passive.reactive_effect_key = pj.value("reactive_effect_key", std::string{});
		if (pj.contains("reactive_effect_payload")) {
			read_string_int_json(pj["reactive_effect_payload"], passive.reactive_effect_payload);
		}
		if (pj.contains("reactive_string_payload") && pj["reactive_string_payload"].is_object()) {
			for (const auto& [k, val] : pj["reactive_string_payload"].items()) {
				if (val.is_string()) passive.reactive_string_payload[k] = val.get<std::string>();
			}
		}
		passive.sort_order = pj.value("sort_order", 0);
		passive.passive_mechanic = pj.value("passive_mechanic", std::string{});
		passive.mechanic_payload.clear();
		if (pj.contains("mechanic_payload")) {
			read_string_int_json(pj["mechanic_payload"], passive.mechanic_payload);
		}
		passives.push_back(std::move(passive));
	}
}

json temporary_effects_json(const std::vector<TemporaryEntityEffect>& effects)
{
	json arr = json::array();
	for (const auto& effect : effects) {
		json ej = json::object();
		ej["effect_id"] = effect.effect_id;
		ej["source_id"] = effect.source_id;
		ej["name"] = effect.name;
		ej["rules_text"] = effect.rules_text;
		ej["remaining_turns"] = effect.remaining_turns;
		ej["expire_on"] = effect.expire_on;
		ej["keywords"] = passive_attribute_grants_json(effect.granted_attributes);
		ej["stat_grants"] = passive_stat_grants_json(effect.stat_grants);
		if (!effect.suppress_attributes.empty()) {
			ej["suppress_attributes"] = effect.suppress_attributes;
		}
		if (!effect.disable_ability_keys.empty()) {
			ej["disable_ability_keys"] = effect.disable_ability_keys;
		}
		if (!effect.suppress_passive_keys.empty()) {
			ej["suppress_passive_keys"] = effect.suppress_passive_keys;
		}
		arr.push_back(std::move(ej));
	}
	return arr;
}

json entity_effects_json(const std::vector<EntityEffectInstance>& effects)
{
	json arr = json::array();
	for (const auto& effect : effects) {
		json ej = json::object();
		ej["key"] = effect.key;
		ej["amount"] = effect.amount;
		if (!effect.source_id.empty()) {
			ej["source_id"] = effect.source_id;
		}
		ej["remaining_turns"] = effect.remaining_turns;
		ej["expire_on"] = effect.expire_on;
		arr.push_back(std::move(ej));
	}
	return arr;
}

void read_entity_effects_json(const json& j, Entity& e)
{
	e.entity_effects.clear();
	if (!j.is_array()) return;
	for (const auto& ej : j) {
		EntityEffectInstance effect;
		effect.key = ej.value("key", std::string{});
		effect.amount = ej.value("amount", 0);
		effect.source_id = ej.value("source_id", std::string{});
		effect.remaining_turns = ej.value("remaining_turns", 0);
		effect.expire_on = ej.value("expire_on", std::string{"never"});
		if (!effect.key.empty() && effect.amount > 0 && entity_status_allowed(e, effect.key)) {
			e.entity_effects.push_back(std::move(effect));
		}
	}
}

void read_temporary_effects_json(const json& j, std::vector<TemporaryEntityEffect>& effects)
{
	effects.clear();
	if (!j.is_array()) return;
	for (const auto& ej : j) {
		TemporaryEntityEffect effect;
		effect.effect_id = ej.value("effect_id", std::string{});
		effect.source_id = ej.value("source_id", std::string{});
		effect.name = ej.value("name", effect.effect_id);
		effect.rules_text = ej.value("rules_text", std::string{});
		effect.remaining_turns = ej.value("remaining_turns", 1);
		effect.expire_on = ej.value("expire_on", std::string{"owner_turn_start"});
		if (ej.contains("keywords")) {
			read_passive_attribute_grants_json(ej["keywords"], effect.granted_attributes);
		}
		if (ej.contains("stat_grants")) {
			effect.stat_grants = passive_stat_grants_from_json(ej["stat_grants"]);
		}
		if (ej.contains("suppress_attributes") && ej["suppress_attributes"].is_array()) {
			for (const auto& sa : ej["suppress_attributes"]) {
				if (sa.is_string()) effect.suppress_attributes.push_back(sa.get<std::string>());
			}
		}
		if (ej.contains("disable_ability_keys") && ej["disable_ability_keys"].is_array()) {
			for (const auto& dk : ej["disable_ability_keys"]) {
				if (dk.is_string()) effect.disable_ability_keys.push_back(dk.get<std::string>());
			}
		}
		if (ej.contains("suppress_passive_keys") && ej["suppress_passive_keys"].is_array()) {
			for (const auto& sp : ej["suppress_passive_keys"]) {
				if (sp.is_string()) effect.suppress_passive_keys.push_back(sp.get<std::string>());
			}
		}
		effects.push_back(std::move(effect));
	}
}

json shape_json(const std::vector<std::pair<int, int>>& sh)
{
	json arr = json::array();
	for (const auto& [x, y] : sh) {
		arr.push_back(json::array({x, y}));
	}
	return arr;
}

void read_shape_json(const json& j, std::vector<std::pair<int, int>>& sh)
{
	sh.clear();
	if (!j.is_array()) return;
	for (const auto& p : j) {
		sh.push_back({p.at(0).get<int>(), p.at(1).get<int>()});
	}
}

json square_modifier_json(const SquareModifier& m)
{
	json j = json::object();
	j["name"] = m.name;
	j["layer"] = (m.layer == TileLayer::Overlay) ? "overlay" : "terrain";
	j["movement_cost"] = m.movement_cost;
	j["blocks_line_of_sight"] = m.blocks_line_of_sight;
	j["damage_on_enter"] = m.damage_on_enter;
	j["is_void"] = m.is_void;
	if (m.owner_seat.has_value()) j["owner_seat"] = *m.owner_seat;
	if (m.duration.has_value())   j["duration"]   = *m.duration;
	return j;
}

SquareModifier square_modifier_from_json(const json& j)
{
	SquareModifier m;
	m.name = j.value("name", std::string{});
	const std::string layer_str = j.value("layer", std::string{"terrain"});
	m.layer = (layer_str == "overlay") ? TileLayer::Overlay : TileLayer::Terrain;
	m.movement_cost = j.value("movement_cost", 0.0f);
	m.blocks_line_of_sight = j.value("blocks_line_of_sight", false);
	m.damage_on_enter = j.value("damage_on_enter", 0);
	m.is_void = j.value("is_void", false);
	if (j.contains("owner_seat")) m.owner_seat = j["owner_seat"].get<int>();
	if (j.contains("duration"))   m.duration   = j["duration"].get<int>();
	return m;
}

json entity_core_json(const Entity& e)
{
	json ej = json::object();
	ej["entity_id"] = e.entity_id;
	ej["spawn_sequence"] = e.spawn_sequence;
	ej["source_card_id"] = e.source_card_id;
	ej["entity_type"] = e.entity_type;
	if (e.owner) ej["owner"] = *e.owner;
	else ej["owner"] = nullptr;
	ej["has_moved_this_turn"] = e.has_moved_this_turn;
	ej["moves_remaining_this_turn"] = e.moves_remaining_this_turn;
	ej["standard_moves_remaining_this_turn"] = e.standard_moves_remaining_this_turn;
	ej["bonus_moves"] = e.bonus_moves;
	ej["has_attacked_this_turn"] = e.has_attacked_this_turn;
	ej["attacks_remaining_this_turn"] = e.attacks_remaining_this_turn;
	ej["bonus_attacks"] = e.bonus_attacks;
	if (e.bonus_attacks_remaining_this_turn != 0) ej["bonus_attacks_remaining_this_turn"] = e.bonus_attacks_remaining_this_turn;
	ej["coordinated_fire_shots_remaining"] = e.coordinated_fire_shots_remaining;
	if (e.coordinated_fire_damage_min > 0) ej["coordinated_fire_damage_min"] = e.coordinated_fire_damage_min;
	if (e.coordinated_fire_damage_max > 0) ej["coordinated_fire_damage_max"] = e.coordinated_fire_damage_max;
	ej["reactions_remaining_this_turn"] = e.reactions_remaining_this_turn;
	ej["core_cracker_shutdown"] = e.core_cracker_shutdown;
	ej["core_cracker_deploy_turn_exempt"] = e.core_cracker_deploy_turn_exempt;
	ej["frenzy_triggered_this_turn"] = e.frenzy_triggered_this_turn;
	ej["base_health"] = e.base_health;
	ej["current_health"] = e.current_health;
	ej["line_of_sight_blocked"] = e.line_of_sight_blocked;
	ej["attack_type"] = attack_type_to_string(e.attack_type);
	ej["ranged_deadzone"] = e.ranged_deadzone;
	ej["shape"] = shape_json(e.shape);
	ej["occupied_positions"] = shape_json(e.occupied_positions);
	ej["keywords"] = e.keywords;
	ej["unit_types"] = e.unit_types;
	ej["keyword_amounts"] = map_string_int_json(e.keyword_amounts);
	ej["entity_effects"] = entity_effects_json(e.entity_effects);
	ej["passive_abilities"] = passive_abilities_json(e.passive_abilities);
	ej["temporary_effects"] = temporary_effects_json(e.temporary_effects);
	ej["activated_abilities"] = abilities_json(e.activated_abilities);
	if (!e.ability_uses_remaining_this_turn.empty()) {
		json uses = json::object();
		for (const auto& [k, v] : e.ability_uses_remaining_this_turn) {
			uses[k] = v;
		}
		ej["ability_uses_remaining_this_turn"] = std::move(uses);
	}
	if (!e.ability_uses_remaining_game.empty()) {
		json uses = json::object();
		for (const auto& [k, v] : e.ability_uses_remaining_game) {
			uses[k] = v;
		}
		ej["ability_uses_remaining_game"] = std::move(uses);
	}
	if (!e.barrage_cast_counts_this_turn.empty()) {
		json bcounts = json::object();
		for (const auto& [k, v] : e.barrage_cast_counts_this_turn) {
			bcounts[k] = v;
		}
		ej["barrage_cast_counts_this_turn"] = std::move(bcounts);
	}
	if (!e.attacked_targets_this_turn.empty()) {
		json atk_targets = json::array();
		for (const auto& id : e.attacked_targets_this_turn) {
			atk_targets.push_back(id);
		}
		ej["attacked_targets_this_turn"] = std::move(atk_targets);
	}
	if (e.death_shield_used_this_turn) {
		ej["death_shield_used_this_turn"] = true;
	}
	// Layout entities set this false; preserve it so clients do not show wrong passive badges.
	ej["participates_in_passive_order"] = e.participates_in_passive_order;
	if (!e.pickup_effect_key.empty()) {
		ej["pickup_effect_key"] = e.pickup_effect_key;
		if (!e.pickup_payload.empty()) {
			ej["pickup_payload"] = map_string_int_json(e.pickup_payload);
		}
	}
	return ej;
}

void read_entity_core_json(const json& ej, Entity& e)
{
	e.entity_id = ej.at("entity_id").get<std::string>();
	e.spawn_sequence = ej.value("spawn_sequence", uint64_t{0});
	e.source_card_id = ej.value("source_card_id", std::string{});
	e.entity_type = ej.value("entity_type", std::string{"unit"});
	if (ej["owner"].is_null()) e.owner.reset();
	else e.owner = ej["owner"].get<int>();
	e.has_moved_this_turn = ej.value("has_moved_this_turn", false);
	e.moves_remaining_this_turn = ej.value("moves_remaining_this_turn", 1);
	e.standard_moves_remaining_this_turn = ej.value("standard_moves_remaining_this_turn", 1);
	e.bonus_moves = ej.value("bonus_moves", 0);
	e.has_attacked_this_turn = ej.value("has_attacked_this_turn", false);
	e.attacks_remaining_this_turn = ej.value("attacks_remaining_this_turn", 1);
	e.bonus_attacks = ej.value("bonus_attacks", 0);
	e.bonus_attacks_remaining_this_turn = ej.value("bonus_attacks_remaining_this_turn", 0);
	e.coordinated_fire_shots_remaining = ej.value("coordinated_fire_shots_remaining", 0);
	e.coordinated_fire_damage_min = ej.value("coordinated_fire_damage_min", 0);
	e.coordinated_fire_damage_max = ej.value("coordinated_fire_damage_max", 0);
	e.reactions_remaining_this_turn = ej.value("reactions_remaining_this_turn", 3);
	e.core_cracker_shutdown = ej.value("core_cracker_shutdown", false);
	e.core_cracker_deploy_turn_exempt = ej.value("core_cracker_deploy_turn_exempt", false);
	e.frenzy_triggered_this_turn = ej.value("frenzy_triggered_this_turn", false);
	e.base_health = ej.at("base_health").get<int>();
	e.current_health = ej.at("current_health").get<int>();
	e.line_of_sight_blocked = ej.value("line_of_sight_blocked", false);
	e.attack_type = parse_atk(ej.value("attack_type", std::string{"melee"}));
	e.ranged_deadzone = ej.value("ranged_deadzone", 0);
	if (ej.contains("shape") && ej["shape"].is_array()) {
		read_shape_json(ej["shape"], e.shape);
	} else {
		e.shape = {{0, 0}};
	}
	if (ej.contains("occupied_positions") && ej["occupied_positions"].is_array()) {
		read_shape_json(ej["occupied_positions"], e.occupied_positions);
	} else {
		e.occupied_positions.clear();
	}
	e.keywords.clear();
	e.unit_types.clear();
	e.keyword_amounts.clear();
	e.entity_effects.clear();
	e.aura_granted_keywords.clear();
	e.aura_keyword_amounts.clear();
	e.aura_bonus_attack = 0;
	e.aura_bonus_health = 0;
	e.aura_bonus_melee_damage = 0;
	e.aura_bonus_ranged_damage = 0;
	e.aura_bonus_movement = 0;
	e.temporary_effects.clear();
	if (ej.contains("keywords") && ej["keywords"].is_array()) {
		for (const auto& attr : ej["keywords"]) {
			if (attr.is_string()) {
				add_entity_attribute(e, attr.get<std::string>());
			}
		}
	}
	if (ej.contains("unit_types") && ej["unit_types"].is_array()) {
		for (const auto& v : ej["unit_types"]) {
			if (v.is_string()) {
				e.unit_types.push_back(v.get<std::string>());
			}
		}
	}
	if (ej.contains("keyword_amounts") && ej["keyword_amounts"].is_object()) {
		std::map<std::string, int> amounts;
		read_string_int_json(ej["keyword_amounts"], amounts);
		for (const auto& [key, amount] : amounts) {
			set_entity_attribute_amount(e, key, amount);
		}
	}
	if (ej.contains("entity_effects")) {
		read_entity_effects_json(ej["entity_effects"], e);
	}
	normalize_entity_shape(e);
	if (ej.contains("passive_abilities")) {
		read_passive_abilities_json(ej["passive_abilities"], e.passive_abilities);
	} else {
		e.passive_abilities.clear();
	}
	if (ej.contains("temporary_effects")) {
		read_temporary_effects_json(ej["temporary_effects"], e.temporary_effects);
	}
	read_abilities_json(ej.at("activated_abilities"), e.activated_abilities);
	e.ability_uses_remaining_this_turn.clear();
	if (ej.contains("ability_uses_remaining_this_turn") && ej["ability_uses_remaining_this_turn"].is_object()) {
		for (const auto& [k, v] : ej["ability_uses_remaining_this_turn"].items()) {
			if (v.is_number_integer()) {
				e.ability_uses_remaining_this_turn[k] = v.get<int>();
			}
		}
	}
	e.ability_uses_remaining_game.clear();
	if (ej.contains("ability_uses_remaining_game") && ej["ability_uses_remaining_game"].is_object()) {
		for (const auto& [k, v] : ej["ability_uses_remaining_game"].items()) {
			if (v.is_number_integer()) {
				e.ability_uses_remaining_game[k] = v.get<int>();
			}
		}
	}
	if (!ej.contains("ability_uses_remaining_this_turn")) {
		std::vector<std::string> legacy_used;
		if (ej.contains("abilities_used_this_turn") && ej["abilities_used_this_turn"].is_array()) {
			for (const auto& k : ej["abilities_used_this_turn"]) {
				legacy_used.push_back(k.get<std::string>());
			}
		}
		refresh_entity_ability_uses(e);
		for (const auto& used_key : legacy_used) {
			e.ability_uses_remaining_this_turn[used_key] = 0;
		}
	}
	e.barrage_cast_counts_this_turn.clear();
	if (ej.contains("barrage_cast_counts_this_turn") && ej["barrage_cast_counts_this_turn"].is_object()) {
		for (const auto& [k, v] : ej["barrage_cast_counts_this_turn"].items()) {
			if (v.is_number_integer()) {
				e.barrage_cast_counts_this_turn[k] = v.get<int>();
			}
		}
	}
	e.attacked_targets_this_turn.clear();
	if (ej.contains("attacked_targets_this_turn") && ej["attacked_targets_this_turn"].is_array()) {
		for (const auto& v : ej["attacked_targets_this_turn"]) {
			if (v.is_string()) {
				e.attacked_targets_this_turn.insert(v.get<std::string>());
			}
		}
	}
	e.death_shield_used_this_turn = ej.value("death_shield_used_this_turn", false);
	// Restore opt-out so obstacles/low_cover do not show passive action badges on the client.
	e.participates_in_passive_order = ej.value("participates_in_passive_order", true);
	e.pickup_effect_key = ej.value("pickup_effect_key", std::string{});
	e.pickup_payload.clear();
	if (ej.contains("pickup_payload") && ej["pickup_payload"].is_object()) {
		read_string_int_json(ej["pickup_payload"], e.pickup_payload);
	}
	e.position.reset();
}

json unit_extra_json(const Unit& u)
{
	json j = json::object();
	j["unit_type"] = u.unit_type;
	j["is_ranged"] = u.is_ranged;
	j["base_ranged_length"] = u.base_ranged_length;
	j["bonus_ranged_length"] = u.bonus_ranged_length;
	j["movement"] = u.movement;
	j["melee_range"] = u.melee_range;
	j["ranged_range"] = u.ranged_range;
	j["melee_damage"] = u.melee_damage;
	j["melee_damage_min"] = u.melee_damage_min;
	j["melee_damage_max"] = u.melee_damage_max;
	j["ranged_damage"] = u.ranged_damage;
	j["ranged_damage_min"] = u.ranged_damage_min;
	j["ranged_damage_max"] = u.ranged_damage_max;
	j["crit_chance_percent"] = u.crit_chance_percent;
	return j;
}

void read_unit_extra_json(const json& j, Unit& u)
{
	u.unit_type = j.at("unit_type").get<std::string>();
	u.is_ranged = j.value("is_ranged", false);
	u.base_ranged_length = j.value("base_ranged_length", 0);
	u.bonus_ranged_length = j.value("bonus_ranged_length", 0);
	u.movement = j.at("movement").get<int>();
	u.melee_range = j.at("melee_range").get<int>();
	u.ranged_range = j.at("ranged_range").get<int>();
	u.melee_damage = j.at("melee_damage").get<int>();
	u.melee_damage_min = j.value("melee_damage_min", 0);
	u.melee_damage_max = j.value("melee_damage_max", 0);
	u.ranged_damage = j.at("ranged_damage").get<int>();
	u.ranged_damage_min = j.value("ranged_damage_min", 0);
	u.ranged_damage_max = j.value("ranged_damage_max", 0);
	u.crit_chance_percent = j.value("crit_chance_percent", kDefaultCritChancePercent);
	sync_unit_damage_ranges_from_nominal(u);
}

json card_json(const CardPtr& c)
{
	if (!c) return nullptr;
	json j = json::object();
	j["definition_key"] = c->definition_key;
	j["card_id"] = c->card_id;
	j["name"] = c->name;
	j["card_type"] = c->card_type;
	j["rules_text"] = c->rules_text;
	j["art_id"] = c->art_id;
	j["tags"] = c->tags;
	j["energy_cost"] = map_energy_cost_json(c->energy_cost);
	j["keywords"] = c->keywords;
	j["unit_types"] = c->unit_types;
	j["stockpile_amount"] = c->stockpile_amount;
	j["stockpile_remaining"] = c->stockpile_remaining;
	j["stockpile_used_this_turn"] = c->stockpile_used_this_turn;
	if (auto uc = std::dynamic_pointer_cast<UnitCard>(c)) {
		j["kind"] = "unit_card";
		json tu = entity_core_json(static_cast<const Entity&>(uc->template_unit));
		tu.merge_patch(unit_extra_json(uc->template_unit));
		j["template_unit"] = tu;
	} else if (auto sp = std::dynamic_pointer_cast<SpellCard>(c)) {
		j["kind"] = "spell_card";
		j["speed"] = effect_speed_to_string(sp->speed);
		j["effect_key"] = sp->effect_key;
		j["effect_payload"] = map_string_int_json(sp->effect_payload);
		if (sp->board_target_kind.has_value()) {
			j["board_target_kind"] = board_target_kind_to_string(*sp->board_target_kind);
		}
		if (sp->requires_mandatory_board_cell.has_value()) {
			j["requires_mandatory_board_cell"] = *sp->requires_mandatory_board_cell;
		}
		j["focus_range"] = sp->focus_range;
		j["require_target_unit_types"] = sp->require_target_unit_types;
		j["bonus_damage_unit_types"] = sp->bonus_damage_unit_types;
		j["bonus_damage_amount"] = sp->bonus_damage_amount;
	} else {
		j["kind"] = "card";
	}
	return j;
}

CardPtr card_from_json(const json& j)
{
	const auto read_card_core = [&j](Card& c) {
		c.definition_key = j.value("definition_key", std::string{});
		c.card_id = j.at("card_id").get<std::string>();
		c.name = j.at("name").get<std::string>();
		c.card_type = j.at("card_type").get<std::string>();
		c.rules_text = j.value("rules_text", std::string{});
		c.art_id = j.value("art_id", std::string{});
		c.tags.clear();
		if (j.contains("tags") && j["tags"].is_array()) {
			for (const auto& tag : j["tags"]) {
				if (tag.is_string()) {
					c.tags.push_back(tag.get<std::string>());
				}
			}
		}
		read_energy_cost_json(j.at("energy_cost"), c.energy_cost);
		c.keywords.clear();
		if (j.contains("keywords") && j["keywords"].is_array()) {
			for (const auto& attr : j["keywords"]) {
				if (attr.is_string()) {
					add_card_attribute(c, attr.get<std::string>());
				}
			}
		}
		c.stockpile_amount = j.value("stockpile_amount", 0);
		c.stockpile_remaining = j.value("stockpile_remaining", c.stockpile_amount);
		c.stockpile_used_this_turn = j.value("stockpile_used_this_turn", false);
		if (c.stockpile_amount > 0) {
			add_card_attribute(c, "stockpile");
		}
		c.unit_types.clear();
		if (j.contains("unit_types") && j["unit_types"].is_array()) {
			for (const auto& v : j["unit_types"]) {
				if (v.is_string()) {
					c.unit_types.push_back(v.get<std::string>());
				}
			}
		}
	};
	const std::string kind = j.value("kind", std::string{"card"});
	if (kind == "unit_card") {
		auto uc = std::make_shared<UnitCard>();
		read_card_core(*uc);
		const json& tu = j.at("template_unit");
		read_entity_core_json(tu, uc->template_unit);
		read_unit_extra_json(tu, uc->template_unit);
		return uc;
	}
	if (kind == "spell_card") {
		auto sp = std::make_shared<SpellCard>();
		read_card_core(*sp);
		sp->speed = parse_speed(j.value("speed", std::string{"channeled"}));
		sp->effect_key = j.at("effect_key").get<std::string>();
		read_string_int_json(j.at("effect_payload"), sp->effect_payload);
		if (j.contains("board_target_kind") && j["board_target_kind"].is_string()) {
			if (const auto pk = board_target_kind_parse(j["board_target_kind"].get<std::string>())) {
				sp->board_target_kind = *pk;
			}
		}
		if (j.contains("requires_mandatory_board_cell") && j["requires_mandatory_board_cell"].is_boolean()) {
			sp->requires_mandatory_board_cell = j["requires_mandatory_board_cell"].get<bool>();
		}
		sp->focus_range = j.value("focus_range", 0);
		sp->require_target_unit_types = j.value("require_target_unit_types", std::vector<std::string>{});
		sp->bonus_damage_unit_types = j.value("bonus_damage_unit_types", std::vector<std::string>{});
		sp->bonus_damage_amount = j.value("bonus_damage_amount", 0);
		return sp;
	}
	auto c = std::make_shared<Card>();
	read_card_core(*c);
	return c;
}

json card_instance_json(const CardInstance& inst)
{
	json j = json::object();
	j["id"] = inst.id.value;
	if (const CardDefinition* def = try_get_card_definition_ptr(inst.definition_id)) {
		j["def_key"] = def->key;
	} else {
		j["def_key"] = nullptr;
	}
	j["public_id"] = inst.public_id;
	j["stockpile_amount"] = inst.stockpile_amount;
	j["stockpile_remaining"] = inst.stockpile_remaining;
	j["stockpile_used_this_turn"] = inst.stockpile_used_this_turn;
	j["stockpile_double_play_used_this_turn"] = inst.stockpile_double_play_used_this_turn;
	j["hand_expires_after_owner_turn_ends"] = inst.hand_expires_after_owner_turn_ends;
	return j;
}

CardInstance card_instance_from_json(const json& j)
{
	CardInstance inst;
	inst.id = CardInstanceId{j.at("id").get<uint32_t>()};
	// def_key is null when the definition is missing; do not call get<string> on it.
	const auto& dk = j.at("def_key");
	inst.definition_id = dk.is_string() ? try_card_def_id_for_key(dk.get<std::string>()) : CardDefId{};
	inst.public_id = j.at("public_id").get<std::string>();
	inst.stockpile_amount = j.value("stockpile_amount", 0);
	inst.stockpile_remaining = j.value("stockpile_remaining", inst.stockpile_amount);
	inst.stockpile_used_this_turn = j.value("stockpile_used_this_turn", false);
	inst.stockpile_double_play_used_this_turn = j.value("stockpile_double_play_used_this_turn", false);
	inst.hand_expires_after_owner_turn_ends = j.value("hand_expires_after_owner_turn_ends", 0);
	return inst;
}

json zone_ids_json(const std::vector<CardInstanceId>& zone)
{
	json arr = json::array();
	for (const CardInstanceId id : zone) {
		arr.push_back(id.value);
	}
	return arr;
}

std::vector<CardInstanceId> zone_ids_from_json(const json& arr)
{
	std::vector<CardInstanceId> out;
	for (const auto& v : arr) {
		out.push_back(CardInstanceId{v.get<uint32_t>()});
	}
	return out;
}

void load_deck_v1(Deck& d, const json& dj)
{
	for (const auto& cj : dj.at("deck")) {
		if (CardPtr legacy = card_from_json(cj)) {
			d.deck.push_back(deck_import_legacy_card(d, *legacy));
		}
	}
	for (const auto& cj : dj.at("hand")) {
		if (CardPtr legacy = card_from_json(cj)) {
			d.hand.push_back(deck_import_legacy_card(d, *legacy));
		}
	}
	if (dj.contains("discard") && dj.at("discard").is_array()) {
		for (const auto& cj : dj.at("discard")) {
			if (CardPtr legacy = card_from_json(cj)) {
				d.discard.push_back(deck_import_legacy_card(d, *legacy));
			}
		}
	}
	if (dj.contains("graveyard") && dj.at("graveyard").is_array()) {
		for (const auto& cj : dj.at("graveyard")) {
			if (CardPtr legacy = card_from_json(cj)) {
				d.send_to_discard_pile(deck_import_legacy_card(d, *legacy));
			}
		}
	}
	if (dj.contains("in_play") && dj.at("in_play").is_array()) {
		for (const auto& cj : dj.at("in_play")) {
			if (CardPtr legacy = card_from_json(cj)) {
				d.in_play.push_back(deck_import_legacy_card(d, *legacy));
			}
		}
	}
	if (dj.contains("reserves") && dj.at("reserves").is_array()) {
		for (const auto& cj : dj.at("reserves")) {
			if (CardPtr legacy = card_from_json(cj)) {
				d.reserves.push_back(deck_import_legacy_card(d, *legacy));
			}
		}
	}
}

void load_deck_v2(Deck& d, const json& dj)
{
	d.pool.clear();
	for (const auto& ij : dj.at("instances")) {
		d.pool.import_instance(card_instance_from_json(ij));
	}
	d.deck = zone_ids_from_json(dj.at("deck"));
	d.hand = zone_ids_from_json(dj.at("hand"));
	d.discard = dj.contains("discard") ? zone_ids_from_json(dj.at("discard")) : std::vector<CardInstanceId>{};
	d.purgatory = dj.contains("purgatory") ? zone_ids_from_json(dj.at("purgatory")) : std::vector<CardInstanceId>{};
	d.in_play = dj.contains("in_play") ? zone_ids_from_json(dj.at("in_play")) : std::vector<CardInstanceId>{};
	d.reserves = dj.contains("reserves") ? zone_ids_from_json(dj.at("reserves")) : std::vector<CardInstanceId>{};
	d.stockpile_double_play_active = dj.value("stockpile_double_play_active", false);
}

// ── Conquering Territories serialization ────────────────────────────────────────
json territory_effect_json(const TerritoryEffect& e)
{
	json j = json::object();
	j["effect_key"] = e.effect_key;
	if (!e.payload.empty()) {
		j["payload"] = map_string_int_json(e.payload);
	}
	if (!e.string_payload.empty()) {
		json sp = json::object();
		for (const auto& [k, v] : e.string_payload) {
			sp[k] = v;
		}
		j["string_payload"] = std::move(sp);
	}
	if (e.requires_target) {
		j["requires_target"] = true;
	}
	j["board_target_kind"] = board_target_kind_to_string(e.board_target_kind);
	return j;
}

TerritoryEffect territory_effect_from_json(const json& j)
{
	TerritoryEffect e;
	e.effect_key = j.value("effect_key", std::string{});
	if (const auto p = j.find("payload"); p != j.end() && p->is_object()) {
		for (const auto& [k, v] : p->items()) {
			if (v.is_number_integer()) {
				e.payload[k] = v.get<int>();
			}
		}
	}
	if (const auto s = j.find("string_payload"); s != j.end() && s->is_object()) {
		for (const auto& [k, v] : s->items()) {
			if (v.is_string()) {
				e.string_payload[k] = v.get<std::string>();
			}
		}
	}
	e.requires_target = j.value("requires_target", false);
	if (const auto b = j.find("board_target_kind"); b != j.end() && b->is_string()) {
		if (const auto pk = board_target_kind_parse(b->get<std::string>())) {
			e.board_target_kind = *pk;
		}
	}
	return e;
}

json territory_ability_json(const TerritoryAbility& a)
{
	json j = json::object();
	j["name"] = a.name;
	if (!a.cost.empty()) {
		j["cost"] = map_energy_cost_json(a.cost);
	}
	if (!a.energy_produced.empty()) {
		j["energy_produced"] = map_energy_cost_json(a.energy_produced);
	}
	if (a.produces_flux) {
		j["produces_flux"] = true;
	}
	if (a.sacrifice_self) {
		j["sacrifice_self"] = true;
	}
	if (!a.effect.effect_key.empty()) {
		j["effect"] = territory_effect_json(a.effect);
	}
	return j;
}

TerritoryAbility territory_ability_from_json(const json& j)
{
	TerritoryAbility a;
	a.name = j.value("name", std::string{});
	if (const auto c = j.find("cost"); c != j.end() && c->is_object()) {
		read_energy_cost_json(*c, a.cost);
	}
	if (const auto e = j.find("energy_produced"); e != j.end() && e->is_object()) {
		read_energy_cost_json(*e, a.energy_produced);
	}
	a.produces_flux = j.value("produces_flux", j.value("flux", false));
	a.sacrifice_self = j.value("sacrifice_self", false);
	if (const auto en = j.find("energy"); en != j.end() && en->is_object()) {
		read_energy_cost_json(*en, a.energy_produced);
	}
	if (const auto ef = j.find("effect"); ef != j.end() && ef->is_object()) {
		a.effect = territory_effect_from_json(*ef);
	}
	return a;
}

json zone_json(const EnergyZone& z)
{
	json j = json::object();
	j["zone_id"] = z.zone_id;
	j["name"] = z.name;
	if (!z.art_id.empty()) {
		j["art_id"] = z.art_id;
	}
	j["energy_produced"] = map_energy_cost_json(z.energy_produced);
	j["is_tapped"] = z.is_tapped;
	// Conquering Territories runtime + definitional state (omitted when a plain zone).
	if (z.color) {
		j["color"] = to_string(*z.color);
	}
	if (z.is_basic) {
		j["is_basic"] = true;
	}
	if (z.enters_depleted) {
		j["enters_depleted"] = true;
	}
	if (z.depleted) {
		j["depleted"] = true;
	}
	if (z.land_use_available != 0) {
		j["land_use_available"] = z.land_use_available;
	}
	if (!z.enter_effects.empty()) {
		json arr = json::array();
		for (const TerritoryEffect& e : z.enter_effects) {
			arr.push_back(territory_effect_json(e));
		}
		j["enter_effects"] = std::move(arr);
	}
	if (!z.groundwork.empty()) {
		json arr = json::array();
		for (const GroundworkTrigger& g : z.groundwork) {
			json gj = json::object();
			gj["color"] = to_string(g.color);
			if (g.ignore_depleted) {
				gj["ignore_depleted"] = true;
			}
			if (g.destroy_if_unmet) {
				gj["destroy_if_unmet"] = true;
			}
			if (g.effect && !g.effect->effect_key.empty()) {
				gj["effect"] = territory_effect_json(*g.effect);
			}
			arr.push_back(std::move(gj));
		}
		j["groundwork"] = std::move(arr);
	}
	if (!z.land_abilities.empty()) {
		json arr = json::array();
		for (const TerritoryAbility& a : z.land_abilities) {
			arr.push_back(territory_ability_json(a));
		}
		j["land_abilities"] = std::move(arr);
	}
	return j;
}

EnergyZone zone_from_json(const json& j)
{
	EnergyZone z;
	z.zone_id = j.at("zone_id").get<std::string>();
	z.name = j.at("name").get<std::string>();
	z.art_id = j.value("art_id", std::string{});
	if (z.art_id.empty()) {
		const std::string& zid = z.zone_id;
		const std::size_t us = zid.rfind('_');
		const std::string base = (us != std::string::npos && us + 1 < zid.size()
			&& std::all_of(zid.begin() + static_cast<std::ptrdiff_t>(us + 1), zid.end(),
				[](const unsigned char c) { return std::isdigit(c) != 0; }))
			? zid.substr(0, us)
			: zid;
		const std::size_t slash = base.rfind('_');
		const std::string slug = (slash != std::string::npos && slash > 0 && base[0] == 'p' && std::isdigit(static_cast<unsigned char>(base[1])))
			? base.substr(slash + 1)
			: base;
		z.art_id = "territories/" + slug;
	}
	read_energy_cost_json(j.at("energy_produced"), z.energy_produced);
	z.is_tapped = j.value("is_tapped", false);
	if (const auto c = j.find("color"); c != j.end() && c->is_string()) {
		z.color = energy_type_from_string(c->get<std::string>());
	}
	z.is_basic = j.value("is_basic", false);
	z.enters_depleted = j.value("enters_depleted", false);
	z.depleted = j.value("depleted", false);
	z.land_use_available = j.value("land_use_available", 0);
	if (const auto ee = j.find("enter_effects"); ee != j.end() && ee->is_array()) {
		for (const auto& e : *ee) {
			z.enter_effects.push_back(territory_effect_from_json(e));
		}
	}
	if (const auto gw = j.find("groundwork"); gw != j.end() && gw->is_array()) {
		for (const auto& gj : *gw) {
			GroundworkTrigger g;
			if (const auto gc = gj.find("color"); gc != gj.end() && gc->is_string()) {
				if (const auto c = energy_type_from_string(gc->get<std::string>())) {
					g.color = *c;
				}
			}
			g.ignore_depleted = gj.value("ignore_depleted", false);
			g.destroy_if_unmet = gj.value("destroy_if_unmet", false);
			if (const auto ef = gj.find("effect"); ef != gj.end() && ef->is_object()) {
				g.effect = territory_effect_from_json(*ef);
			}
			z.groundwork.push_back(std::move(g));
		}
	}
	if (const auto la = j.find("land_abilities"); la != j.end() && la->is_array()) {
		for (const auto& a : *la) {
			z.land_abilities.push_back(territory_ability_from_json(a));
		}
	}
	return z;
}

json stack_item_json(const StackItem& it)
{
	json j = json::object();
	j["item_id"] = it.item_id;
	j["source_type"] = it.source_type;
	j["source_name"] = it.source_name;
	j["source_entity_id"] = it.source_entity_id;
	if (!it.source_ability_key.empty()) {
		j["source_ability_key"] = it.source_ability_key;
	}
	j["controller_id"] = it.controller_id;
	j["effect_key"] = it.effect_key;
	j["speed"] = effect_speed_to_string(it.speed);
	j["payload"] = map_string_int_json(it.payload);
	if (!it.string_payload.empty()) {
		json sp = json::object();
		for (const auto& [k, v] : it.string_payload) { sp[k] = v; }
		j["string_payload"] = std::move(sp);
	}
	j["targets"] = map_string_int_json(it.targets);
	j["target_entity_id"] = it.target_entity_id;
	j["target_stack_item_id"] = it.target_stack_item_id;
	j["board_target_kind"] = board_target_kind_to_string(it.board_target_kind);
	j["pierces_damage_prevention"] = it.pierces_damage_prevention;
	j["heals_on_damage_dealt"] = it.heals_on_damage_dealt;
	j["heals_allied_base_on_damage_dealt"] = it.heals_allied_base_on_damage_dealt;
	j["soul_steal_heal_base_entity_id"] = it.soul_steal_heal_base_entity_id;
	j["consumes_attack_action"] = it.consumes_attack_action;
	j["require_target_unit_types"] = it.require_target_unit_types;
	j["bonus_damage_unit_types"] = it.bonus_damage_unit_types;
	j["bonus_damage_amount"] = it.bonus_damage_amount;
	j["played_from_reserves"] = it.played_from_reserves;
	if (it.chain) {
		j["chain"] = true;
	}
	if (it.no_phase_batch_lock) {
		j["no_phase_batch_lock"] = true;
	}
	if (it.batched_spell_total_cost > 0) {
		j["batched_spell_total_cost"] = it.batched_spell_total_cost;
	}
	return j;
}

StackItem stack_item_from_json(const json& j)
{
	StackItem it;
	it.item_id = j.at("item_id").get<std::string>();
	it.source_type = j.at("source_type").get<std::string>();
	it.source_name = j.at("source_name").get<std::string>();
	it.source_entity_id = j.value("source_entity_id", std::string{});
	it.source_ability_key = j.value("source_ability_key", std::string{});
	it.controller_id = j.at("controller_id").get<int>();
	it.effect_key = j.at("effect_key").get<std::string>();
	it.speed = parse_speed(j.value("speed", std::string{"channeled"}));
	read_string_int_json(j.at("payload"), it.payload);
	read_string_int_json(j.at("targets"), it.targets);
	it.target_entity_id = j.value("target_entity_id", std::string{});
	it.target_stack_item_id = j.value("target_stack_item_id", std::string{});
	it.pierces_damage_prevention = j.value("pierces_damage_prevention", false);
	it.heals_on_damage_dealt = j.value("heals_on_damage_dealt", false);
	it.heals_allied_base_on_damage_dealt = j.value("heals_allied_base_on_damage_dealt", false);
	it.soul_steal_heal_base_entity_id = j.value("soul_steal_heal_base_entity_id", std::string{});
	it.consumes_attack_action = j.value("consumes_attack_action", false);
	it.require_target_unit_types = j.value("require_target_unit_types", std::vector<std::string>{});
	it.bonus_damage_unit_types = j.value("bonus_damage_unit_types", std::vector<std::string>{});
	it.bonus_damage_amount = j.value("bonus_damage_amount", 0);
	it.played_from_reserves = j.value("played_from_reserves", false);
	it.chain = j.value("chain", false);
	it.no_phase_batch_lock = j.value("no_phase_batch_lock", false);
	it.batched_spell_total_cost = j.value("batched_spell_total_cost", 0);
	if (j.contains("board_target_kind") && j["board_target_kind"].is_string()) {
		if (const auto pk = board_target_kind_parse(j["board_target_kind"].get<std::string>())) {
			it.board_target_kind = *pk;
		} else {
			throw std::runtime_error("unknown board target kind: " + j["board_target_kind"].get<std::string>());
		}
	}
	if (j.contains("string_payload") && j["string_payload"].is_object()) {
		for (const auto& [k, v] : j["string_payload"].items()) {
			if (v.is_string()) {
				it.string_payload[k] = v.get<std::string>();
			}
		}
	}
	return it;
}

void clear_board(GameBoard& board)
{
	auto ents = board.all_entities();
	for (const auto& e : ents) {
		board.remove_entity(e);
	}
	const BoardCellBounds b = board.cell_bounds();
	for (int y = b.min_y; y <= b.max_y; ++y) {
		for (int x = b.min_x; x <= b.max_x; ++x) {
			if (auto sq = board.get_square(x, y)) {
				sq->modifiers.clear();
			}
		}
	}
}

}  // namespace

std::string GameState::build_match_snapshot_utf8() const
{
	json j;
	j["version"] = kMatchSnapshotVersion;
	j["network_snap_seq"] = network_snap_seq_;
	j["match_command_seq"] = match_command_seq();
	json journal = json::array();
	for (const MatchCommandEntry& entry : command_journal_) {
		json ej = json::object();
		ej["seq"] = entry.seq;
		ej["seat"] = entry.seat;
		ej["line"] = entry.line_utf8;
		journal.push_back(std::move(ej));
	}
	j["command_journal"] = std::move(journal);
	j["game_id"] = game_id_;
	j["game_mode"] = game_mode_to_string(game_mode_);
	j["match_settings"] = json{{"allow_deployment_undo", match_settings.allow_deployment_undo}};
	json aether_clusters = json::array();
	for (const AetherClusterState& cluster : aether_clusters_) {
		aether_clusters.push_back(aether_cluster_json(cluster));
	}
	j["aether_clusters"] = std::move(aether_clusters);
	json scanner_clusters = json::array();
	for (const ScannerClusterState& cluster : scanner_clusters_) {
		scanner_clusters.push_back(scanner_cluster_json(cluster));
	}
	j["scanner_clusters"] = std::move(scanner_clusters);
	json omni_energy_clusters = json::array();
	for (const OmniEnergyClusterState& cluster : omni_energy_clusters_) {
		omni_energy_clusters.push_back(omni_energy_cluster_json(cluster));
	}
	j["omni_energy_clusters"] = std::move(omni_energy_clusters);
	j["board_width"] = board_width_;
	j["board_height"] = board_height_;
	j["board_layout_id"] = layout_spec_.layout_id;
	j["off_board_default"] =
	    layout_spec_.off_board_default == OffBoardTileTreatment::Void ? "void" : "wall";
	json off_board_cells = json::array();
	for (const auto& [key, treatment] : layout_spec_.off_board_cells) {
		json cell = json::object();
		cell["x"] = static_cast<int>(key >> 32);
		cell["y"] = static_cast<int>(key & 0xffffffffLL);
		cell["treatment"] = treatment == OffBoardTileTreatment::Void ? "void" : "wall";
		off_board_cells.push_back(std::move(cell));
	}
	j["off_board_cells"] = std::move(off_board_cells);
	j["board_cell_count"] = board.cell_count();
	{
		std::ostringstream oss;
		oss << rng_;
		j["rng_state"] = oss.str();
	}
	if (pending_discard_player_) {
		j["pending_discard_player"] = *pending_discard_player_;
	} else {
		j["pending_discard_player"] = nullptr;
	}
	if (pending_scan_) {
		json ps = json::object();
		ps["player_id"] = pending_scan_->player_id;
		ps["peeked"] = zone_ids_json(pending_scan_->peeked);
		j["pending_scan"] = std::move(ps);
	} else {
		j["pending_scan"] = nullptr;
	}
	// Conquering Territories: pending enter/groundwork target effects (optional).
	if (pending_territory_target_) {
		json pt = json::object();
		pt["player_id"] = pending_territory_target_->player_id;
		json effs = json::array();
		for (const TerritoryEffect& e : pending_territory_target_->effects) {
			effs.push_back(territory_effect_json(e));
		}
		pt["effects"] = std::move(effs);
		j["pending_territory_target"] = std::move(pt);
	}
	if (pending_territory_loot_) {
		json pl = json::object();
		pl["player_id"] = pending_territory_loot_->player_id;
		j["pending_territory_loot"] = std::move(pl);
	} else {
		j["pending_territory_loot"] = nullptr;
	}

	if (pending_moves_.empty()) {
		j["pending_moves"] = json::object();
		j["pending_move"] = nullptr;
	} else {
		json by_seat = json::object();
		for (const auto& [pid, pm] : pending_moves_) {
			json pmj = json::object();
			pmj["player_id"] = pm.player_id;
			pmj["unit_entity_id"] = pm.unit_entity_id;
			pmj["goal_x"] = pm.goal_x;
			pmj["goal_y"] = pm.goal_y;
			pmj["resolved_ax"] = pm.resolved_ax;
			pmj["resolved_ay"] = pm.resolved_ay;
			pmj["quarter_turns_cw"] = pm.quarter_turns_cw;
			by_seat[std::to_string(pid)] = std::move(pmj);
		}
		j["pending_moves"] = by_seat;
		if (pending_moves_.size() == 1) {
			j["pending_move"] = *j["pending_moves"].begin();
		} else {
			j["pending_move"] = nullptr;
		}
	}

	json terrain = json::array();
	const BoardCellBounds tb = board.cell_bounds();
	for (int y = tb.min_y; y <= tb.max_y; ++y) {
		for (int x = tb.min_x; x <= tb.max_x; ++x) {
			const auto sq = board.get_square(x, y);
			if (!sq || sq->modifiers.empty()) {
				continue;
			}
			json cell = json::object();
			cell["world_x"] = x;
			cell["world_y"] = y;
			json mods = json::array();
			for (const SquareModifier& m : sq->modifiers) {
				mods.push_back(square_modifier_json(m));
			}
			cell["modifiers"] = std::move(mods);
			terrain.push_back(std::move(cell));
		}
	}
	j["terrain"] = std::move(terrain);

	json tm = json::object();
	tm["players"] = turn_manager.players;
	tm["current_player_index"] = turn_manager.current_player_index;
	tm["round_number"] = turn_manager.round_number;
	tm["current_phase"] = phase_str(turn_manager.current_phase);
	tm["skip_first_turn_draw"] = turn_manager.skip_first_turn_draw_for_snapshot();
	if (turn_manager.deferred_turn_draw_seat_for_snapshot()) {
		tm["deferred_turn_draw_seat"] = *turn_manager.deferred_turn_draw_seat_for_snapshot();
	} else {
		tm["deferred_turn_draw_seat"] = nullptr;
	}
	json pe = json::object();
	for (const auto& [pid, pool] : turn_manager.player_energy) {
		json poolj = json::object();
		for (EnergyType et : kEnergyBillingAllTypes) {
			auto it = pool.find(et);
			poolj[to_string(et)] = it != pool.end() ? it->second : 0;
		}
		pe[std::to_string(pid)] = poolj;
	}
	tm["player_energy"] = pe;
	// Tagged float pools: { tag: { player_id: { energy_type: amount } } }
	json ptf = json::object();
	for (const auto& [tag, per_player] : turn_manager.player_tagged_float) {
		json tag_obj = json::object();
		for (const auto& [pid, pool] : per_player) {
			json poolj = json::object();
			for (EnergyType et : kEnergyBillingAllTypes) {
				auto it = pool.find(et);
				poolj[to_string(et)] = it != pool.end() ? it->second : 0;
			}
			tag_obj[std::to_string(pid)] = poolj;
		}
		ptf[tag] = tag_obj;
	}
	tm["player_tagged_float"] = ptf;
	json flux_gen = json::object();
	for (const auto& [pid, total] : turn_manager.player_flux_energy_generated_total) {
		flux_gen[std::to_string(pid)] = total;
	}
	tm["player_flux_energy_generated_total"] = flux_gen;
	json overload_applied = json::object();
	for (const auto& [pid, total] : turn_manager.player_overload_applied_total) {
		overload_applied[std::to_string(pid)] = total;
	}
	tm["player_overload_applied_total"] = overload_applied;
	json ability_damage = json::object();
	for (const auto& [pid, total] : turn_manager.player_ability_damage_dealt_total) {
		ability_damage[std::to_string(pid)] = total;
	}
	tm["player_ability_damage_dealt_total"] = ability_damage;
	json pec = json::object();
	for (const auto& [pid, zones] : turn_manager.pending_energy_choices) {
		json arr = json::array();
		for (const auto& z : zones) arr.push_back(zone_json(z));
		pec[std::to_string(pid)] = arr;
	}
	tm["pending_energy_choices"] = pec;
	// Per-player deploy discount (Mobilize and similar effects).
	json ddisc = json::object();
	for (const auto& [pid, amt] : turn_manager.deploy_discount_per_unit) {
		ddisc[std::to_string(pid)] = amt;
	}
	tm["deploy_discount_per_unit"] = ddisc;
	j["turn_manager"] = tm;

	std::vector<StackItem> stk;
	std::optional<int> pri;
	int passes = 0;
	int counter = 0;
	stack_manager.capture_network_snapshot(stk, pri, passes, counter);
	json sm = json::object();
	json sarr = json::array();
	for (const auto& it : stk) sarr.push_back(stack_item_json(it));
	sm["stack"] = sarr;
	if (pri) sm["priority_player"] = *pri;
	else sm["priority_player"] = nullptr;
	sm["passes"] = passes;
	sm["counter"] = counter;
	j["stack_manager"] = sm;

	// ── Spell / Attack / Defense phase state ──────────────────────────────────
	{

		// Queued spell/ability declarations (Main Phase -> SpellWindow).
		json sdecls = json::array();
		for (const auto& item : pending_spell_declarations()) sdecls.push_back(stack_item_json(item));
		j["pending_spell_declarations"] = sdecls;

		// Unified phase batch queue (full queue + group boundaries).
		json adecls = json::array();
		for (const auto& e : phase_action_queue()) {
			json dj = json::object();
			if (e.is_attack) {
				dj["kind"] = "attack";
				dj["attacker_id"] = e.attack.attacker_id;
				dj["target_x"] = e.attack.target_x;
				dj["target_y"] = e.attack.target_y;
				dj["ranged"] = e.attack.ranged;
				dj["consumed_move_on_declare"] = e.attack.consumed_move_on_declare;
				dj["undo_moves_remaining"] = e.attack.undo_moves_remaining;
				dj["undo_standard_moves_remaining"] = e.attack.undo_standard_moves_remaining;
			} else {
				dj["kind"] = "spell";
				dj["spell_item"] = stack_item_json(e.spell_item);
			}
			adecls.push_back(dj);
		}
		j["phase_action_queue"] = adecls;
		json boundaries = json::array();
		for (const size_t b : phase_action_group_boundaries()) boundaries.push_back(b);
		j["phase_action_group_boundaries"] = boundaries;
		// Legacy keys for older clients.
		j["attack_phase_queue"] = adecls;

		// Shared reaction-window state (used for Defense).
		json forfeited = json::array();
		for (int seat : reaction_window_forfeited()) forfeited.push_back(seat);
		j["reaction_window_forfeited"] = forfeited;
		if (reaction_window_priority_player().has_value())
			j["reaction_window_priority_player"] = *reaction_window_priority_player();
		else
			j["reaction_window_priority_player"] = nullptr;
		if (bonus_attack_phase_used_this_turn_)
			j["bonus_attack_phase_used_this_turn"] = true;
	}

	json decks = json::object();
	for (const auto& [pid, deck] : players_decks) {
		json dj = json::object();
		std::unordered_set<uint32_t> seen_ids;
		const auto mark_zone = [&](const std::vector<CardInstanceId>& zone) {
			for (const CardInstanceId id : zone) {
				if (id.is_valid()) {
					seen_ids.insert(id.value);
				}
			}
		};
		mark_zone(deck.deck);
		mark_zone(deck.hand);
		mark_zone(deck.discard);
		mark_zone(deck.purgatory);
		mark_zone(deck.in_play);
		mark_zone(deck.reserves);
		json instances = json::array();
		for (const uint32_t raw_id : seen_ids) {
			instances.push_back(card_instance_json(deck.pool.at(CardInstanceId{raw_id})));
		}
		dj["instances"] = instances;
		dj["deck"] = zone_ids_json(deck.deck);
		dj["hand"] = zone_ids_json(deck.hand);
		dj["discard"] = zone_ids_json(deck.discard);
		dj["purgatory"] = zone_ids_json(deck.purgatory);
		dj["in_play"] = zone_ids_json(deck.in_play);
		dj["reserves"] = zone_ids_json(deck.reserves);
		dj["stockpile_double_play_active"] = deck.stockpile_double_play_active;
		decks[std::to_string(pid)] = dj;
	}
	j["players_decks"] = decks;

	json ez = json::object();
	for (const auto& [pid, zones] : players_energy_zones) {
		json arr = json::array();
		for (const auto& z : zones) arr.push_back(zone_json(z));
		ez[std::to_string(pid)] = arr;
	}
	j["players_energy_zones"] = ez;

	json ezd = json::object();
	for (const auto& [pid, ezv] : players_energy_zones_decks) {
		json arr = json::array();
		for (const auto& z : ezv.deck) arr.push_back(zone_json(z));
		json wrap = json::object();
		wrap["deck"] = arr;
		ezd[std::to_string(pid)] = wrap;
	}
	j["players_energy_zones_decks"] = ezd;

	// Conquering Territories: per-player last-conquered memory (drives groundwork).
	json lct = json::object();
	for (const auto& [pid, mem] : last_conquered_territory) {
		if (!mem.has_value) {
			continue;
		}
		json mj = json::object();
		mj["was_basic"] = mem.was_basic;
		mj["color"] = to_string(mem.color);
		lct[std::to_string(pid)] = std::move(mj);
	}
	j["last_conquered_territory"] = std::move(lct);

	json teamj = json::object();
	for (const auto& [s, tid] : seat_team_id) {
		teamj[std::to_string(s)] = tid;
	}
	for (int s : turn_manager.players) {
		const std::string sk = std::to_string(s);
		if (!teamj.contains(sk)) {
			teamj[sk] = s;
		}
	}
	j["seat_team"] = teamj;

	json ents = json::array();
	for (const auto& [id, ent] : board.all_entities_map) {
		static_cast<void>(id);
		if (!ent->position) continue;
		json ej = json::object();
		ej["world_x"] = ent->position->first;
		ej["world_y"] = ent->position->second;
		if (auto u = std::dynamic_pointer_cast<Unit>(ent)) {
			if (entity_is_base(*u)) {
				ej["kind"] = "base";
			} else if (entity_is_building(*u)) {
				ej["kind"] = "building";
			} else if (entity_is_structure(*u)) {
				ej["kind"] = "structure";
			} else {
				ej["kind"] = "unit";
			}
			json ec = entity_core_json(static_cast<const Entity&>(*u));
			ec.merge_patch(unit_extra_json(*u));
			ej["data"] = ec;
		} else {
			ej["kind"] = "entity";
			ej["data"] = entity_core_json(*ent);
		}
		ents.push_back(ej);
	}
	j["entities"] = ents;

	return j.dump();
}

bool GameState::apply_match_snapshot_utf8(const std::string& utf8, std::string& err)
{
	try {
		const json root = json::parse(utf8);
		return apply_match_snapshot_json(root, err);
	} catch (const std::exception& ex) {
		err = ex.what();
		return false;
	}
}

bool GameState::apply_match_snapshot_json(const json& root, std::string& err)
{
	try {
		const int ver = root.at("version").get<int>();
		if (ver < 1 || ver > kMatchSnapshotVersion) {
			err = "unsupported snapshot version";
			return false;
		}
		const int bw = root.at("board_width").get<int>();
		const int bh = root.at("board_height").get<int>();
		const std::string snap_layout_id =
		    root.contains("board_layout_id") ? root.at("board_layout_id").get<std::string>() : std::string{};

		// Adopt the snapshot's map when it differs from ours BEFORE validating dimensions. A joining
		// client builds a local match before it knows the host's settings, so it can legitimately be on
		// a different map (e.g. 8x12 duel locally vs the host's 12x12 2v2). Without this the restore
		// bailed with "board dimensions mismatch" and the client never synced at all.
		if (!snap_layout_id.empty() && snap_layout_id != layout_spec_.layout_id) {
			if (snap_layout_id == std::string(k2v2BoardLayoutId)) {
				apply_layout_spec(make_2v2_map_layout());
			} else if (snap_layout_id == std::string(kDefaultBoardLayoutId)) {
				apply_layout_spec(make_default_map_layout());
			}
		}

		if (bw != board_width_ || bh != board_height_) {
			err = "board dimensions mismatch";
			return false;
		}
		const bool standard_duel = bw == kStandardBoardWidth && bh == kStandardBoardHeight;
		if (standard_duel &&
		    (snap_layout_id != kDefaultBoardLayoutId || layout_spec_.layout_id != kDefaultBoardLayoutId ||
		     board.cell_count() != 80)) {
			apply_layout_spec(make_default_map_layout());
		}
		layout_spec_.off_board_cells.clear();
		if (root.contains("off_board_default") && root["off_board_default"].is_string()) {
			const std::string treatment = root["off_board_default"].get<std::string>();
			layout_spec_.off_board_default =
			    treatment == "void" ? OffBoardTileTreatment::Void : OffBoardTileTreatment::Wall;
		} else if (root.contains("off_board_treatment") && root["off_board_treatment"].is_string()) {
			const std::string treatment = root["off_board_treatment"].get<std::string>();
			layout_spec_.off_board_default =
			    treatment == "void" ? OffBoardTileTreatment::Void : OffBoardTileTreatment::Wall;
		}
		if (root.contains("off_board_cells") && root["off_board_cells"].is_array()) {
			for (const auto& cell : root["off_board_cells"]) {
				if (!cell.is_object() || !cell.contains("x") || !cell.contains("y") || !cell.contains("treatment")) {
					continue;
				}
				const std::string treatment = cell["treatment"].get<std::string>();
				set_off_board_cell(layout_spec_, cell["x"].get<int>(), cell["y"].get<int>(),
				    treatment == "void" ? OffBoardTileTreatment::Void : OffBoardTileTreatment::Wall);
			}
		}
		const std::string gid = root.at("game_id").get<std::string>();
		if (gid != game_id_) {
			err = "game_id mismatch";
			return false;
		}
		// Restore game_mode so deck selection and discard rules match the host.
		if (root.contains("game_mode") && root["game_mode"].is_string()) {
			game_mode_ = game_mode_from_string(root["game_mode"].get<std::string>());
		}
		// Synced match settings (default off keeps older snapshots behaving as before).
		if (root.contains("match_settings") && root["match_settings"].is_object()) {
			match_settings.allow_deployment_undo =
			    root["match_settings"].value("allow_deployment_undo", false);
		}
		aether_clusters_.clear();
		if (root.contains("aether_clusters") && root["aether_clusters"].is_array()) {
			for (const auto& cj : root["aether_clusters"]) {
				aether_clusters_.push_back(aether_cluster_from_json(cj));
			}
		} else if (ver >= 3) {
			const AetherClusterSpec center_spec = make_standard_duel_center_aether_cluster();
			AetherClusterState legacy;
			legacy.cluster_id = center_spec.cluster_id;
			legacy.cells = center_spec.cells;
			legacy.damage_next = root.value("aether_damage_next", 1);
			if (root.contains("aether_last_sole_control_team") && !root["aether_last_sole_control_team"].is_null()) {
				legacy.last_sole_control_team = root["aether_last_sole_control_team"].get<int>();
			}
			if (root.contains("aether_teams_fired_this_round") && root["aether_teams_fired_this_round"].is_array()) {
				for (const auto& team_j : root["aether_teams_fired_this_round"]) {
					legacy.teams_fired_this_round.insert(team_j.get<int>());
				}
			}
			aether_clusters_.push_back(std::move(legacy));
		}
		scanner_clusters_.clear();
		if (root.contains("scanner_clusters") && root["scanner_clusters"].is_array()) {
			for (const auto& cj : root["scanner_clusters"]) {
				scanner_clusters_.push_back(scanner_cluster_from_json(cj));
			}
		} else if (standard_duel && layout_spec_.layout_id == kDefaultBoardLayoutId) {
			seed_standard_duel_scanner_tiles(*this);
		}
		omni_energy_clusters_.clear();
		if (root.contains("omni_energy_clusters") && root["omni_energy_clusters"].is_array()) {
			for (const auto& cj : root["omni_energy_clusters"]) {
				omni_energy_clusters_.push_back(omni_energy_cluster_from_json(cj));
			}
		} else if (standard_duel && layout_spec_.layout_id == kDefaultBoardLayoutId) {
			seed_standard_duel_omni_energy_tiles(*this);
		}
		// N6: Only advance the sequence; never regress (guards against stale out-of-order snapshots).
		if (root.contains("network_snap_seq") && root["network_snap_seq"].is_number_unsigned()) {
			const uint64_t incoming = root["network_snap_seq"].get<uint64_t>();
			if (incoming >= network_snap_seq_) network_snap_seq_ = incoming;
		} else if (root.contains("network_snap_seq") && root["network_snap_seq"].is_number_integer()) {
			const uint64_t incoming = static_cast<uint64_t>(std::max<int64_t>(0, root["network_snap_seq"].get<int64_t>()));
			if (incoming >= network_snap_seq_) network_snap_seq_ = incoming;
		}
		command_journal_.clear();
		if (root.contains("match_command_seq")) {
			const uint64_t cmd_seq = root["match_command_seq"].get<uint64_t>();
			match_next_command_seq_ = cmd_seq > 0 ? cmd_seq + 1 : 1;
		} else {
			match_next_command_seq_ = 1;
		}
		if (root.contains("command_journal") && root["command_journal"].is_array()) {
			for (const auto& ej : root["command_journal"]) {
				MatchCommandEntry entry;
				entry.seq = ej.at("seq").get<uint64_t>();
				entry.seat = ej.at("seat").get<int>();
				entry.line_utf8 = ej.at("line").get<std::string>();
				command_journal_.push_back(std::move(entry));
			}
			if (!command_journal_.empty()) {
				match_next_command_seq_ = command_journal_.back().seq + 1;
			}
		}

		clear_board(board);

		{
			std::istringstream iss(root.at("rng_state").get<std::string>());
			iss >> rng_;
		}

		// N5: Restore tentatively; validated against actual hand size after decks are loaded (below).
		if (root["pending_discard_player"].is_null()) pending_discard_player_.reset();
		else pending_discard_player_ = root["pending_discard_player"].get<int>();
		const char* pending_scan_key = "pending_scan";
		if (!root.contains("pending_scan") && root.contains("pending_scry")) {
			pending_scan_key = "pending_scry";
		}
		if (root.contains(pending_scan_key) && !root[pending_scan_key].is_null()) {
			const json& ps = root[pending_scan_key];
			PendingScanSelection pending;
			pending.player_id = ps.at("player_id").get<int>();
			pending.peeked = zone_ids_from_json(ps.at("peeked"));
			pending_scan_ = std::move(pending);
		} else {
			pending_scan_.reset();
		}

		// Conquering Territories: pending enter/groundwork target (optional, backward-compatible).
		pending_territory_target_.reset();
		if (root.contains("pending_territory_target") && root["pending_territory_target"].is_object()) {
			const json& pt = root["pending_territory_target"];
			PendingTerritoryTarget pending;
			pending.player_id = pt.value("player_id", 0);
			if (const auto e = pt.find("effects"); e != pt.end() && e->is_array()) {
				for (const auto& ej : *e) {
					pending.effects.push_back(territory_effect_from_json(ej));
				}
			}
			if (!pending.effects.empty()) {
				pending_territory_target_ = std::move(pending);
			}
		}

		if (root.contains("pending_territory_loot") && root["pending_territory_loot"].is_object()) {
			PendingTerritoryLoot pending;
			pending.player_id = root["pending_territory_loot"].value("player_id", 0);
			pending_territory_loot_ = std::move(pending);
		} else {
			pending_territory_loot_.reset();
		}

		pending_moves_.clear();

		const json& tm = root.at("turn_manager");
		turn_manager.players = tm.at("players").get<std::vector<int>>();
		turn_manager.current_player_index = tm.at("current_player_index").get<int>();
		turn_manager.round_number = tm.at("round_number").get<int>();
		turn_manager.current_phase = parse_phase(tm.at("current_phase").get<std::string>());
		turn_manager.set_skip_first_turn_draw_for_snapshot(tm.at("skip_first_turn_draw").get<bool>());
		if (tm.contains("deferred_turn_draw_seat") && !tm["deferred_turn_draw_seat"].is_null()) {
			turn_manager.set_deferred_turn_draw_seat_for_snapshot(tm["deferred_turn_draw_seat"].get<int>());
		} else {
			turn_manager.set_deferred_turn_draw_seat_for_snapshot(std::nullopt);
		}
		turn_manager.player_energy.clear();
		for (auto it = tm.at("player_energy").begin(); it != tm.at("player_energy").end(); ++it) {
			const int pid = std::stoi(it.key());
			std::map<EnergyType, int> pool;
			read_energy_pool_json(it.value(), pool);
			turn_manager.player_energy[pid] = std::move(pool);
		}
		turn_manager.player_tagged_float.clear();
		if (tm.contains("player_tagged_float")) {
			for (auto tag_it = tm.at("player_tagged_float").begin(); tag_it != tm.at("player_tagged_float").end(); ++tag_it) {
				const std::string& tag = tag_it.key();
				for (auto pid_it = tag_it.value().begin(); pid_it != tag_it.value().end(); ++pid_it) {
					const int pid = std::stoi(pid_it.key());
					std::map<EnergyType, int> pool;
					read_energy_pool_json(pid_it.value(), pool);
					turn_manager.player_tagged_float[tag][pid] = std::move(pool);
				}
			}
		}
		turn_manager.player_flux_energy_generated_total.clear();
		if (tm.contains("player_flux_energy_generated_total") && tm.at("player_flux_energy_generated_total").is_object()) {
			for (auto it = tm.at("player_flux_energy_generated_total").begin();
				 it != tm.at("player_flux_energy_generated_total").end(); ++it) {
				turn_manager.player_flux_energy_generated_total[std::stoi(it.key())] = it.value().get<int>();
			}
		}
		turn_manager.player_overload_applied_total.clear();
		if (tm.contains("player_overload_applied_total") && tm.at("player_overload_applied_total").is_object()) {
			for (auto it = tm.at("player_overload_applied_total").begin();
				 it != tm.at("player_overload_applied_total").end(); ++it) {
				turn_manager.player_overload_applied_total[std::stoi(it.key())] = it.value().get<int>();
			}
		}
		turn_manager.player_ability_damage_dealt_total.clear();
		if (tm.contains("player_ability_damage_dealt_total") && tm.at("player_ability_damage_dealt_total").is_object()) {
			for (auto it = tm.at("player_ability_damage_dealt_total").begin();
				 it != tm.at("player_ability_damage_dealt_total").end(); ++it) {
				turn_manager.player_ability_damage_dealt_total[std::stoi(it.key())] = it.value().get<int>();
			}
		}
		turn_manager.pending_energy_choices.clear();
		for (auto it = tm.at("pending_energy_choices").begin(); it != tm.at("pending_energy_choices").end(); ++it) {
			const int pid = std::stoi(it.key());
			std::vector<EnergyZone> zones;
			for (const auto& zj : it.value()) zones.push_back(zone_from_json(zj));
			turn_manager.pending_energy_choices[pid] = std::move(zones);
		}
		turn_manager.deploy_discount_per_unit.clear();
		if (tm.contains("deploy_discount_per_unit")) {
			for (auto it = tm.at("deploy_discount_per_unit").begin();
				 it != tm.at("deploy_discount_per_unit").end(); ++it) {
				turn_manager.deploy_discount_per_unit[std::stoi(it.key())] = it.value().get<int>();
			}
		}

		const json& sm = root.at("stack_manager");
		std::vector<StackItem> stk;
		for (const auto& sj : sm.at("stack")) stk.push_back(stack_item_from_json(sj));
		std::optional<int> pri;
		if (!sm.at("priority_player").is_null()) pri = sm.at("priority_player").get<int>();
		const int passes = sm.at("passes").get<int>();
		const int counter = sm.at("counter").get<int>();
		stack_manager.restore_network_snapshot(std::move(stk), pri, passes, counter);

		// ── Spell / Attack / Defense phase state ──────────────────────────────
		phase_action_queue_.clear();
		phase_action_group_boundaries_.clear();
		attack_declared_unit_ids_.clear();
		auto load_phase_entry = [&](const json& dj) {
			AttackPhaseEntry e;
			const std::string kind = dj.value("kind", "spell");
			if (kind == "attack") {
				e.is_attack = true;
				e.attack.attacker_id = dj.at("attacker_id").get<std::string>();
				e.attack.target_x = dj.at("target_x").get<int>();
				e.attack.target_y = dj.at("target_y").get<int>();
				e.attack.ranged = dj.value("ranged", false);
				e.attack.consumed_move_on_declare = dj.value("consumed_move_on_declare", false);
				e.attack.undo_moves_remaining = dj.value("undo_moves_remaining", 0);
				e.attack.undo_standard_moves_remaining = dj.value("undo_standard_moves_remaining", 0);
				attack_declared_unit_ids_.insert(e.attack.attacker_id);
			} else {
				e.is_attack = false;
				e.spell_item = stack_item_from_json(dj.at("spell_item"));
			}
			phase_action_queue_.push_back(std::move(e));
		};
		if (root.contains("phase_action_queue")) {
			for (const auto& dj : root.at("phase_action_queue")) load_phase_entry(dj);
			if (root.contains("phase_action_group_boundaries")) {
				for (const auto& bj : root.at("phase_action_group_boundaries"))
					phase_action_group_boundaries_.push_back(bj.get<size_t>());
			}
		} else {
			if (root.contains("pending_spell_declarations")) {
				for (const auto& sj : root.at("pending_spell_declarations")) {
					AttackPhaseEntry e;
					e.is_attack = false;
					e.spell_item = stack_item_from_json(sj);
					phase_action_queue_.push_back(std::move(e));
				}
			}
			if (root.contains("attack_phase_queue")) {
				for (const auto& dj : root.at("attack_phase_queue")) load_phase_entry(dj);
			}
		}
		reaction_window_forfeited_.clear();
		// Support both old key name (defense_window_forfeited) and new (reaction_window_forfeited).
		const char* forfeited_key = root.contains("reaction_window_forfeited") ? "reaction_window_forfeited" : "defense_window_forfeited";
		if (root.contains(forfeited_key)) {
			for (const auto& sv : root.at(forfeited_key)) reaction_window_forfeited_.insert(sv.get<int>());
		}
		reaction_window_priority_player_ = std::nullopt;
		const char* priority_key = root.contains("reaction_window_priority_player") ? "reaction_window_priority_player" : "defense_window_priority_player";
		if (root.contains(priority_key) && !root.at(priority_key).is_null()) {
			reaction_window_priority_player_ = root.at(priority_key).get<int>();
		}
		bonus_attack_phase_used_this_turn_ = root.value("bonus_attack_phase_used_this_turn", false);

		players_decks.clear();
		players_hands.clear();
		for (auto it = root.at("players_decks").begin(); it != root.at("players_decks").end(); ++it) {
			const int pid = std::stoi(it.key());
			Deck d;
			if (ver == kMatchSnapshotVersion) {
				load_deck_v2(d, it.value());
			} else {
				load_deck_v1(d, it.value());
			}
			players_decks[pid] = std::move(d);
			players_hands[pid] = &players_decks[pid].hand;
		}

		players_energy_zones.clear();
		for (auto it = root.at("players_energy_zones").begin(); it != root.at("players_energy_zones").end(); ++it) {
			const int pid = std::stoi(it.key());
			std::vector<EnergyZone> zones;
			for (const auto& zj : it.value()) zones.push_back(zone_from_json(zj));
			players_energy_zones[pid] = std::move(zones);
		}

		// N7: EnergyZoneDeck serializes only the remaining deck array, not RNG state.
		// The main game rng_ is saved/restored, so determinism is preserved for the primary game
		// RNG. EnergyZoneDeck uses the game rng_ seed at construction time; future draws after
		// restore will follow the saved rng_ state, which is correct.
		players_energy_zones_decks.clear();
		for (auto it = root.at("players_energy_zones_decks").begin(); it != root.at("players_energy_zones_decks").end(); ++it) {
			const int pid = std::stoi(it.key());
			EnergyZoneDeck ezd;
			for (const auto& zj : it.value().at("deck")) ezd.deck.push_back(zone_from_json(zj));
			players_energy_zones_decks[pid] = std::move(ezd);
		}

		// Conquering Territories: per-player last-conquered memory (optional; groundwork context).
		last_conquered_territory.clear();
		if (root.contains("last_conquered_territory") && root.at("last_conquered_territory").is_object()) {
			for (auto it = root.at("last_conquered_territory").begin(); it != root.at("last_conquered_territory").end(); ++it) {
				ConqueredTerritoryMemory mem;
				mem.has_value = true;
				mem.was_basic = it.value().value("was_basic", false);
				if (const auto c = it.value().find("color"); c != it.value().end() && c->is_string()) {
					mem.color = energy_type_from_string(c->get<std::string>()).value_or(EnergyType::Neutral);
				}
				last_conquered_territory[std::stoi(it.key())] = mem;
			}
		}

		seat_team_id.clear();
		if (root.contains("seat_team") && root.at("seat_team").is_object()) {
			for (auto it = root.at("seat_team").begin(); it != root.at("seat_team").end(); ++it) {
				seat_team_id[std::stoi(it.key())] = it.value().get<int>();
			}
		}
		for (int pid : turn_manager.players) {
			if (!seat_team_id.contains(pid)) {
				seat_team_id[pid] = pid;
			}
		}
		for (const auto& [pid, _] : players_decks) {
			if (!seat_team_id.contains(pid)) {
				seat_team_id[pid] = pid;
			}
		}

		if (root.contains("terrain") && root.at("terrain").is_array()) {
			for (const auto& cell : root.at("terrain")) {
				const int wx = cell.at("world_x").get<int>();
				const int wy = cell.at("world_y").get<int>();
				auto sq = board.get_square(wx, wy);
				if (!sq || !cell.contains("modifiers") || !cell.at("modifiers").is_array()) {
					continue;
				}
				sq->modifiers.clear();
				for (const auto& mj : cell.at("modifiers")) {
					SquareModifier m = square_modifier_from_json(mj);
					if (!m.name.empty()) {
						sq->modifiers.push_back(std::move(m));
					}
				}
			}
		}

		for (const auto& ej : root.at("entities")) {
			const int wx = ej.at("world_x").get<int>();
			const int wy = ej.at("world_y").get<int>();
			const std::string kind = ej.at("kind").get<std::string>();
			const json& data = ej.at("data");
			if (kind == "unit" || kind == "building") {
				auto u = std::make_shared<Unit>();
				read_entity_core_json(data, *u);
				read_unit_extra_json(data, *u);
				if (kind == "building") {
					u->entity_type = "building";
					u->moves_remaining_this_turn = 0;
				}
				if (u->shape.size() == 1 && u->shape[0].first == 0 && u->shape[0].second == 0 && u->occupied_positions.size() > 1) {
					u->shape.clear();
					for (const auto& oc : u->occupied_positions) {
						u->shape.push_back({oc.first - wx, oc.second - wy});
					}
					normalize_entity_shape(*u);
				}
				if (!board.place_entity(u, wx, wy)) {
					err = "failed to place " + kind + " at snapshot coords";
					return false;
				}
			} else {
				auto e = std::make_shared<Entity>();
				read_entity_core_json(data, *e);
				if (e->shape.size() == 1 && e->shape[0].first == 0 && e->shape[0].second == 0 && e->occupied_positions.size() > 1) {
					e->shape.clear();
					for (const auto& oc : e->occupied_positions) {
						e->shape.push_back({oc.first - wx, oc.second - wy});
					}
					normalize_entity_shape(*e);
				}
				if (!board.place_entity(e, wx, wy)) {
					err = "failed to place entity at snapshot coords";
					return false;
				}
			}
		}

		// Stamp Entity::team from the restored seat_team_id map.  note_entity_placed() is not called
		// during snapshot restore (board.place_entity bypasses GameState), so we do a single pass
		// here after all entities have been placed and team data is loaded.
		for (const auto& ent : board.all_entities()) {
			if (ent && ent->owner.has_value()) {
				ent->team = team_of_seat(*ent->owner);
			}
		}

		pending_moves_.clear();
		if (root.contains("pending_moves") && root.at("pending_moves").is_object()) {
			for (auto it = root.at("pending_moves").begin(); it != root.at("pending_moves").end(); ++it) {
				const int pid = std::stoi(it.key());
				const json& pm = it.value();
				PendingMoveSelection pms;
				pms.player_id = pm.at("player_id").get<int>();
				pms.unit_entity_id = pm.at("unit_entity_id").get<std::string>();
				pms.goal_x = pm.at("goal_x").get<int>();
				pms.goal_y = pm.at("goal_y").get<int>();
				pms.resolved_ax = pm.at("resolved_ax").get<int>();
				pms.resolved_ay = pm.at("resolved_ay").get<int>();
				pms.quarter_turns_cw = pm.at("quarter_turns_cw").get<int>();
				if (board.all_entities_map.find(pms.unit_entity_id) != board.all_entities_map.end()) {
					pending_moves_[pid] = pms;
				}
			}
		} else if (root.contains("pending_move") && !root.at("pending_move").is_null()) {
			const json& pm = root.at("pending_move");
			PendingMoveSelection pms;
			pms.player_id = pm.at("player_id").get<int>();
			pms.unit_entity_id = pm.at("unit_entity_id").get<std::string>();
			pms.goal_x = pm.at("goal_x").get<int>();
			pms.goal_y = pm.at("goal_y").get<int>();
			pms.resolved_ax = pm.at("resolved_ax").get<int>();
			pms.resolved_ay = pm.at("resolved_ay").get<int>();
			pms.quarter_turns_cw = pm.at("quarter_turns_cw").get<int>();
			if (board.all_entities_map.find(pms.unit_entity_id) != board.all_entities_map.end()) {
				pending_moves_[pms.player_id] = pms;
			}
		}

		rebuild_living_tokens_from_board();

		// N5: Validate pending_discard_player_ - clear it if the player's hand is not actually over kMaxHandSize.
		if (pending_discard_player_.has_value()) {
			const int pid = *pending_discard_player_;
			const auto deck_it = players_decks.find(pid);
			if (deck_it == players_decks.end() ||
			    static_cast<int>(deck_it->second.hand.size()) <= kMaxHandSize) {
				pending_discard_player_.reset();
			}
		}
		if (pending_scan_.has_value()) {
			const int pid = pending_scan_->player_id;
			const auto deck_it = players_decks.find(pid);
			if (deck_it == players_decks.end()) {
				pending_scan_.reset();
			} else {
				std::vector<CardInstanceId> validated;
				validated.reserve(pending_scan_->peeked.size());
				for (const CardInstanceId id : pending_scan_->peeked) {
					if (!id.is_valid() || !deck_it->second.pool.try_get(id)) {
						continue;
					}
					const auto deck_pos = std::find(deck_it->second.deck.begin(), deck_it->second.deck.end(), id);
					if (deck_pos == deck_it->second.deck.end()) {
						continue;
					}
					validated.push_back(id);
				}
				if (validated.empty()) {
					pending_scan_.reset();
				} else {
					pending_scan_->peeked = std::move(validated);
				}
			}
		}

		uint64_t max_spawn = 0;
		for (const auto& [_, ent] : board.all_entities_map) {
			if (ent) {
				max_spawn = std::max(max_spawn, ent->spawn_sequence);
			}
		}
		next_entity_spawn_sequence_ = std::max(next_entity_spawn_sequence_, max_spawn + 1);
		// N1: board.place_entity() bypasses GameState so passive_auras_dirty_ may still be false.
		// Force a full recompute so all aura fields are correct after restore.
		mark_passive_auras_dirty();
		refresh_passive_auras();
		return true;
	} catch (const std::exception& ex) {
		err = ex.what();
		return false;
	}
}

std::string wrap_match_snapshot_for_network_utf8(const std::string& snapshot_inner_utf8, const uint64_t snap_seq)
{
	nlohmann::json outer = nlohmann::json::object();
	outer["t"] = "snap";
	outer["v"] = kNetworkWireVersion;
	outer["snap_seq"] = snap_seq;
	try {
		outer["payload"] = nlohmann::json::parse(snapshot_inner_utf8);
	} catch (const nlohmann::json::exception&) {
		return {};
	} catch (const std::exception&) {
		return {};
	}
	const std::string wire = outer.dump();
	if (wire.size() > kMaxNetworkSnapshotUtf8Bytes) {
		return {};
	}
	return wire;
}

bool load_game_from_snapshot_utf8(std::unique_ptr<GameState>& out, const std::string& utf8, std::string& err)
{
	try {
		const nlohmann::json root = nlohmann::json::parse(utf8);
		return load_game_from_snapshot_json(out, root, err);
	} catch (const std::exception& ex) {
		err = ex.what();
		return false;
	}
}

bool load_game_from_snapshot_json(std::unique_ptr<GameState>& out, const nlohmann::json& root, std::string& err)
{
	try {
		const std::string gid = root.at("game_id").get<std::string>();
		const int bw = root.at("board_width").get<int>();
		const int bh = root.at("board_height").get<int>();
		std::unique_ptr<GameState> g;
		if (bw == kStandardBoardWidth && bh == kStandardBoardHeight) {
			g = std::make_unique<GameState>(gid, make_default_map_layout());
		} else {
			g = std::make_unique<GameState>(gid, bw, bh);
		}
		if (!g->apply_match_snapshot_json(root, err)) {
			return false;
		}
		out = std::move(g);
		return true;
	} catch (const std::exception& ex) {
		err = ex.what();
		return false;
	}
}

bool unwrap_snap_wire_utf8_for_replace(const std::string& wire, std::string& inner_snapshot_out, std::optional<uint64_t>& out_snap_seq,
                                       std::string& err)
{
	out_snap_seq.reset();
	try {
		nlohmann::json root = nlohmann::json::parse(wire);
		if (root.contains("t")) {
			const std::string msg_type = root.at("t").get<std::string>();
			// N3: snap_delta has a wire format but no apply path yet. Reject it explicitly so the
			// caller gets a clear error instead of silently treating it as a raw snapshot.
			if (msg_type == "snap_delta") {
				err = "snap_delta frames are not yet supported; send a full snap instead";
				return false;
			}
			if (msg_type != "snap") {
				// Not a typed wire frame - treat as raw inner snapshot JSON (pass-through)
				inner_snapshot_out = wire;
				return true;
			}
		} else {
			// No "t" field - raw inner snapshot JSON
			inner_snapshot_out = wire;
			return true;
		}
		if (!wire_version_ok(root, err)) {
			return false;
		}
		if (root.contains("snap_seq")) {
			if (root["snap_seq"].is_number_unsigned()) {
				out_snap_seq = root["snap_seq"].get<uint64_t>();
			} else if (root["snap_seq"].is_number_integer()) {
				out_snap_seq = static_cast<uint64_t>(std::max<int64_t>(0, root["snap_seq"].get<int64_t>()));
			}
		}
		inner_snapshot_out = root.at("payload").dump();
		return true;
	} catch (const std::exception& ex) {
		err = ex.what();
		return false;
	}
}

bool peek_wire_snap_seq_utf8(const std::string& wire_utf8, std::optional<uint64_t>& out_snap_seq, std::string& err)
{
	std::string inner;
	return unwrap_snap_wire_utf8_for_replace(wire_utf8, inner, out_snap_seq, err);
}

bool replace_game_from_wire_utf8(std::unique_ptr<GameState>& game_slot, const std::string& wire_utf8, std::string& err)
{
	std::string inner;
	std::optional<uint64_t> wire_seq;
	if (!unwrap_snap_wire_utf8_for_replace(wire_utf8, inner, wire_seq, err)) {
		return false;
	}
	std::unique_ptr<GameState> next;
	if (!load_game_from_snapshot_utf8(next, inner, err)) {
		return false;
	}
	game_slot = std::move(next);
	return true;
}

bool apply_match_snapshot_delta_utf8(GameState& game, const std::string& base_inner_utf8, const std::string& delta_json_utf8,
    std::string& err)
{
	try {
		const nlohmann::json base = nlohmann::json::parse(base_inner_utf8);
		const nlohmann::json delta = nlohmann::json::parse(delta_json_utf8);
		const nlohmann::json patched = base.patch(delta);
		return game.apply_match_snapshot_utf8(patched.dump(), err);
	} catch (const std::exception& ex) {
		err = ex.what();
		return false;
	}
}

bool apply_snap_delta_wire_utf8(GameState& game, const std::string& wire_utf8, const std::string& base_inner_utf8,
    const uint64_t base_seq, std::string& err)
{
	uint64_t out_base = 0;
	uint64_t out_snap = 0;
	std::string delta_json;
	if (!parse_match_snapshot_delta_wire_utf8(wire_utf8, out_base, out_snap, delta_json, err)) {
		return false;
	}
	if (out_base != base_seq) {
		err = "snap_delta base_seq mismatch";
		return false;
	}
	if (!apply_match_snapshot_delta_utf8(game, base_inner_utf8, delta_json, err)) {
		return false;
	}
	return true;
}

}  // namespace tactics
