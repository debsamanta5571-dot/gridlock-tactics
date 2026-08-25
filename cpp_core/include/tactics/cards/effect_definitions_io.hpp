#pragma once

#include "tactics/cards/effect_definitions.hpp"
#include "tactics/cards/cards.hpp"
#include "tactics/common/types.hpp"

#include <map>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace tactics::effect_io {

using json = nlohmann::json;

bool read_energy_cost_object(const json& j, std::map<EnergyType, int>& out, std::string& err, const std::string& path);
bool read_passive_stat_grants_object(const json& j, PassiveStatGrant& out, std::string& err, const std::string& path);
bool board_target_from_json_object(const json& j, std::optional<BoardTargetKind>& out, std::string& err, const std::string& path);
void read_string_int_map_object(const json& j, std::map<std::string, int>& out);
std::vector<PassiveAttributeGrant> passive_grants_from_keyword_keys(const std::vector<std::string>& keys);

bool read_ability_spec_object(const json& aj, AbilitySpec& out, std::string& err, const std::string& path);
bool read_passive_spec_object(const json& v, PassiveAbilitySpec& out, std::string& err, const std::string& path);
bool read_ability_override_patch_object(const json& j, AbilityOverridePatch& out, std::string& err, const std::string& path);

}  // namespace tactics::effect_io