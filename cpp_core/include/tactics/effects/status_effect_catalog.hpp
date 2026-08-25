#pragma once

#include <optional>
#include <string>
#include <vector>
#include <functional>
#include <unordered_map>

namespace tactics {

struct StatusEffectSpec {
    std::string key;
    std::string display_name;
    std::string expire_on;  // e.g. owner_turn_end, never
    int default_stacks{1};
    bool blocks_activated_abilities{false};
    int explosion_threshold{0};
    int explosion_damage{0};
    /** True for buffs (beneficial temporary effects, stat grants, protective reactions, etc.).
     *  Used by UI and any future "dispel all buffs" mechanic.  Adding is_positive=true to a new
     *  effect here is all that is needed for those mechanics to pick it up automatically. */
    bool is_positive{false};
    /** True for debuffs that the cleanse effect (and any future "remove all negatives" mechanic)
     *  should strip from a target.  Adding is_negative=true to a new status here is all that is
     *  needed to make Triage / cleanse handle it automatically. */
    bool is_negative{false};
};

void ensure_builtin_status_effect_catalog_loaded();
bool try_get_status_effect_spec(const std::string& key, StatusEffectSpec& out);
/** All registered status specs (builtins + project JSON when loaded). */
void collect_all_status_effect_specs(std::vector<StatusEffectSpec>& out);
/** Returns true when `key` names a registered effect that carries is_positive=true.
 *  Unknown keys return false. Safe to call from any thread after catalog load. */
bool is_positive_status_effect(const std::string& key);
/** Returns true when `key` names a registered effect that carries is_negative=true.
 *  Unknown keys return false. Safe to call from any thread after catalog load. */
bool is_negative_status_effect(const std::string& key);
std::string status_effect_catalog_fingerprint_utf8();
bool load_status_effect_catalog_from_json_utf8(const std::string& json_utf8, std::string& err_out);
bool load_project_status_effect_catalog(
    const std::function<bool(const std::string& rel_path, std::string& out_utf8, std::string& err_out)>& read_file,
    std::string& err_out);

}  // namespace tactics
