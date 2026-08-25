#include "tactics/cards/unit_types.hpp"

#include "tactics/cards/cards.hpp"

#include <algorithm>
#include <cctype>

namespace tactics {
namespace {

std::string trim_copy(std::string s)
{
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
        s.erase(s.begin());
    }
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
        s.pop_back();
    }
    return s;
}

bool is_valid_unit_type_id(const std::string& id)
{
    if (id.empty()) {
        return false;
    }
    const char c0 = id[0];
    if (!std::isalpha(static_cast<unsigned char>(c0)) && c0 != '_') {
        return false;
    }
    for (size_t i = 1; i < id.size(); ++i) {
        const char c = id[i];
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') {
            return false;
        }
    }
    return true;
}

void append_unique_unit_type(std::vector<std::string>& out, const std::string& raw)
{
    const std::string norm = normalize_unit_type_id(raw);
    if (norm.empty()) {
        return;
    }
    if (std::find(out.begin(), out.end(), norm) == out.end()) {
        out.push_back(norm);
    }
}

}  // namespace

std::string normalize_unit_type_id(const std::string& raw)
{
    std::string s = trim_copy(raw);
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (!is_valid_unit_type_id(s)) {
        return {};
    }
    return s;
}

bool entity_implied_unit_type_matches(const Entity& entity, const std::string& need)
{
    if (need == "structure") {
        return entity_is_structure(entity);
    }
    if (need == "base") {
        return entity_is_base(entity);
    }
    if (need == "building") {
        return entity_is_building(entity);
    }
    if (need == "unit") {
        return entity.entity_type == "unit";
    }
    return false;
}

bool entity_satisfies_unit_type_filter(const Entity& entity, const std::vector<std::string>& required)
{
    if (required.empty()) {
        return true;
    }
    for (const std::string& need : required) {
        if (entity_implied_unit_type_matches(entity, need)) {
            return true;
        }
        for (const std::string& have : entity.unit_types) {
            if (have == need) {
                return true;
            }
        }
    }
    return false;
}

int unit_type_bonus_damage_for_target(const Entity& target, const std::vector<std::string>& bonus_types, int bonus_amount)
{
    if (bonus_amount <= 0 || bonus_types.empty()) {
        return 0;
    }
    return entity_satisfies_unit_type_filter(target, bonus_types) ? bonus_amount : 0;
}

bool card_has_any_unit_type(const Card& card, const std::string& type_id)
{
    const std::string norm = normalize_unit_type_id(type_id);
    if (norm.empty()) {
        return false;
    }
    return std::find(card.unit_types.begin(), card.unit_types.end(), norm) != card.unit_types.end();
}

bool read_unit_types_json_array(const nlohmann::json& j, std::vector<std::string>& out, std::string& err, const std::string& path)
{
    out.clear();
    if (!j.is_array()) {
        err = path + " must be an array";
        return false;
    }
    for (size_t i = 0; i < j.size(); ++i) {
        if (!j[i].is_string()) {
            err = path + "[" + std::to_string(i) + "] must be a string";
            return false;
        }
        const std::string norm = normalize_unit_type_id(j[i].get<std::string>());
        if (norm.empty()) {
            err = path + "[" + std::to_string(i) + "] is not a valid unit type id (use letters, digits, underscore)";
            return false;
        }
        if (std::find(out.begin(), out.end(), norm) == out.end()) {
            out.push_back(norm);
        }
    }
    return true;
}

}  // namespace tactics
