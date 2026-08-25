#include "tactics/effects/status_effect_catalog.hpp"

#include <algorithm>
#include <functional>
#include <mutex>
#include <unordered_map>
#include <nlohmann/json.hpp>

namespace tactics {
namespace {
using json = nlohmann::json;

std::mutex g_status_mutex;
std::unordered_map<std::string, StatusEffectSpec> g_status_catalog;
bool g_status_builtins_loaded{false};

void load_builtins()
{
    g_status_catalog.clear();

    // is_negative=true marks debuffs that `cleanse` (and any future "remove all negatives"
    // mechanic) will strip automatically.  Adding a new debuff here is all that is needed.
    // Helper: build a StatusEffectSpec with is_negative=true.
    auto neg = [](const char* key, const char* name, const char* expire,
                  bool blocks_abilities = false,
                  int expl_thresh = 0, int expl_dmg = 0) {
        StatusEffectSpec s;
        s.key = key; s.display_name = name; s.expire_on = expire;
        s.default_stacks = 1; s.blocks_activated_abilities = blocks_abilities;
        s.explosion_threshold = expl_thresh; s.explosion_damage = expl_dmg;
        s.is_negative = true;
        return s;
    };

    // Helper: build a StatusEffectSpec with is_positive=true.
    auto pos = [](const char* key, const char* name, const char* expire = "never") {
        StatusEffectSpec s;
        s.key = key; s.display_name = name; s.expire_on = expire;
        s.default_stacks = 1;
        s.is_positive = true;
        return s;
    };

    // ── Negative entity-effect stacks (EntityEffectInstance.key) ─────────────
    g_status_catalog["poison"]   = neg("poison",   "Poison",   "owner_turn_end");
    g_status_catalog["fire"]     = neg("fire",      "Fire",     "owner_turn_end");
    g_status_catalog["bleed"]    = neg("bleed",     "Bleed",    "owner_turn_end");
    g_status_catalog["rooted"]   = neg("rooted",    "Rooted",   "owner_turn_end");
    g_status_catalog["stunned"]  = neg("stunned",   "Stunned",  "owner_turn_end");
    g_status_catalog["silenced"] = neg("silenced",  "Silenced", "owner_turn_end");
    g_status_catalog["jammed"]   = neg("jammed",    "Jammed",   "owner_turn_end", /*blocks_abilities=*/true);
    g_status_catalog["overload"]    = neg("overload",    "Overload",    "never",
                                       /*blocks_abilities=*/false, /*expl_thresh=*/3, /*expl_dmg=*/5);
    g_status_catalog["vulnerable"]  = neg("vulnerable",  "Vulnerable",  "owner_turn_end");

    // ── Positive entity-effect stacks (EntityEffectInstance.key) ─────────────
    g_status_catalog["boost"]                  = pos("boost",                  "Boost");
    g_status_catalog["next_damage_bonus"]    = pos("next_damage_bonus",    "Next Damage Bonus");
    g_status_catalog["next_ability_doubled"] = pos("next_ability_doubled", "Next Ability Doubled");
    g_status_catalog["bonus_health"]         = pos("bonus_health",         "Bonus Health");
    g_status_catalog["shield"]               = pos("shield",               "Shield");
    g_status_catalog["barrier"]              = pos("barrier",              "Barrier",              "owner_turn_end");

    // ── Negative temporary effects (TemporaryEntityEffect.effect_id) ──────────
    g_status_catalog["deployment_fatigue"]   = neg("deployment_fatigue",   "Deployment Fatigue", "owner_turn_start");

    // ── Positive temporary effects (TemporaryEntityEffect.effect_id) ──────────
    g_status_catalog["defend_stance"]           = pos("defend_stance",           "Defend Stance",          "owner_turn_start");
    g_status_catalog["dash_movement"]           = pos("dash_movement",           "Dash Movement",          "owner_turn_start");
    g_status_catalog["recover_stance"]          = pos("recover_stance",          "Recover Stance",         "owner_turn_start");
    g_status_catalog["on_damage_apply_overload"]= pos("on_damage_apply_overload","On-Hit: Apply Overload", "owner_turn_end");
    g_status_catalog["on_damage_apply_overload_next_ability"] = pos("on_damage_apply_overload_next_ability", "Next Attack/Ability: Apply Overload", "owner_turn_end");
    g_status_catalog["on_damage_apply_bleed_next_ability"]= pos("on_damage_apply_bleed_next_ability","Next Attack/Ability: Apply Bleed", "owner_turn_end");
    g_status_catalog["on_damage_apply_jammed"]  = pos("on_damage_apply_jammed",  "On-Hit: Apply Jammed",   "owner_turn_end");
    g_status_catalog["on_damage_apply_jammed_next_ability"] = pos("on_damage_apply_jammed_next_ability", "Next Attack/Ability: Apply Jammed", "owner_turn_end");
    g_status_catalog["medical_override"]        = pos("medical_override",        "Medical Override",       "owner_turn_end");
    g_status_catalog["on_damage_apply_movement_reduction_next_ability"] = pos("on_damage_apply_movement_reduction_next_ability", "Next Ability: Slow on Hit", "owner_turn_end");
    g_status_catalog["on_damage_apply_rooted_next_ability"] = pos("on_damage_apply_rooted_next_ability", "Next Ability: Root on Hit", "owner_turn_end");
    g_status_catalog["volatile_surge_buff"]     = pos("volatile_surge_buff",     "Volatile Surge",         "owner_turn_start");
    g_status_catalog["shocking_stimulus_movement"] = pos("shocking_stimulus_movement", "Stimulus Movement", "owner_turn_start");
    g_status_catalog["delayed_next_damage_bonus"]= pos("delayed_next_damage_bonus","Delayed Damage Bonus", "owner_turn_start");
    g_status_catalog["aura_range_boost"]        = pos("aura_range_boost",        "Aura Range Boost",       "owner_turn_start");
    g_status_catalog["aura_stats_boost"]        = pos("aura_stats_boost",        "Aura Stats Boost",       "owner_turn_start");
    g_status_catalog["covering_fire"]           = pos("covering_fire",           "Covering Fire",          "owner_turn_start");
    g_status_catalog["artillery_mode_buff"]     = pos("artillery_mode_buff",     "Artillery Mode",         "owner_turn_start");
    g_status_catalog["bonus_move_grant"]        = pos("bonus_move_grant",        "Bonus Move",             "owner_turn_start");
    g_status_catalog["reactive_armor_grant"]    = pos("reactive_armor_grant",    "Reactive Armor",         "owner_turn_start");
    g_status_catalog["grant_cleave_self_buff"]  = pos("grant_cleave_self_buff",  "Cleave",                 "owner_turn_end");
    g_status_catalog["whirlwind_spray_buff"]    = pos("whirlwind_spray_buff",    "Whirlwind Spray",        "owner_turn_end");
    g_status_catalog["grant_first_strike_self_buff"] = pos("grant_first_strike_self_buff", "First Strike",  "owner_turn_end");
    g_status_catalog["valiant_guard"]           = pos("valiant_guard",           "Valiant Guard",          "owner_turn_start");
    g_status_catalog["relentless_aura_grant"]   = pos("relentless_aura_grant",   "Relentless",             "owner_turn_end");
    g_status_catalog["movement_aura_grant"]     = pos("movement_aura_grant",     "March Aura",             "owner_turn_end");
    g_status_catalog["damage_aura_grant"]       = pos("damage_aura_grant",       "Battle Hymn",            "owner_turn_end");
    g_status_catalog["multistrike_ally_grant"]  = pos("multistrike_ally_grant",  "Multistrike",            "owner_turn_end");
    g_status_catalog["hyperactive_scanning"]    = pos("hyperactive_scanning",    "Hyperactive Scanning",   "never");
    g_status_catalog["stealth"]                 = pos("stealth",                 "Stealth",                "owner_turn_end");
    g_status_catalog["style"]                  = pos("style",                   "Style");  // Debonair: stacks consumed by Unleash
    g_status_catalog["explosive_graft"]        = pos("explosive_graft",         "Explosive Graft");
}

}  // namespace

bool load_status_effect_catalog_from_json_utf8(const std::string& json_utf8, std::string& err_out)
{
    try {
        const json root = json::parse(json_utf8);
        if (!root.is_object() || !root.contains("status_effects") || !root.at("status_effects").is_array()) {
            err_out = "expected object with status_effects array";
            return false;
        }

        std::unordered_map<std::string, StatusEffectSpec> next_catalog;
        for (const auto& row : root.at("status_effects")) {
            if (!row.is_object()) {
                continue;
            }
            StatusEffectSpec spec;
            spec.key = row.value("key", std::string{});
            if (spec.key.empty()) {
                continue;
            }
            spec.display_name = row.value("display_name", spec.key);
            spec.expire_on = row.value("expire_on", std::string{"owner_turn_end"});
            spec.default_stacks = row.value("default_stacks", 1);
            spec.blocks_activated_abilities = row.value("blocks_activated_abilities", false);
            spec.explosion_threshold = row.value("explosion_threshold", 0);
            spec.explosion_damage = row.value("explosion_damage", 0);
            spec.is_positive = row.value("is_positive", false);
            spec.is_negative = row.value("is_negative", false);
            next_catalog[spec.key] = std::move(spec);
        }
        if (next_catalog.empty()) {
            err_out = "status_effects contained no valid entries";
            return false;
        }
        {
            std::lock_guard<std::mutex> lock(g_status_mutex);
            g_status_catalog = std::move(next_catalog);
            g_status_builtins_loaded = true;
        }
        return true;
    } catch (const std::exception& ex) {
        err_out = ex.what();
        return false;
    }
}

bool load_project_status_effect_catalog(
    const std::function<bool(const std::string& rel_path, std::string& out_utf8, std::string& err_out)>& read_file,
    std::string& err_out)
{
    std::string utf8;
    if (!read_file("TacticsData/status_effects.json", utf8, err_out)) {
        return false;
    }
    return load_status_effect_catalog_from_json_utf8(utf8, err_out);
}

void ensure_builtin_status_effect_catalog_loaded()
{
    std::lock_guard<std::mutex> lock(g_status_mutex);
    if (g_status_builtins_loaded) {
        return;
    }
    load_builtins();
    g_status_builtins_loaded = true;
}

bool try_get_status_effect_spec(const std::string& key, StatusEffectSpec& out)
{
    ensure_builtin_status_effect_catalog_loaded();
    std::lock_guard<std::mutex> lock(g_status_mutex);
    const auto it = g_status_catalog.find(key);
    if (it == g_status_catalog.end()) {
        return false;
    }
    out = it->second;
    return true;
}

void collect_all_status_effect_specs(std::vector<StatusEffectSpec>& out)
{
    ensure_builtin_status_effect_catalog_loaded();
    std::lock_guard<std::mutex> lock(g_status_mutex);
    out.clear();
    out.reserve(g_status_catalog.size());
    for (const auto& [_, spec] : g_status_catalog) {
        out.push_back(spec);
    }
}

bool is_positive_status_effect(const std::string& key)
{
    StatusEffectSpec spec;
    return try_get_status_effect_spec(key, spec) && spec.is_positive;
}

bool is_negative_status_effect(const std::string& key)
{
    StatusEffectSpec spec;
    return try_get_status_effect_spec(key, spec) && spec.is_negative;
}

std::string status_effect_catalog_fingerprint_utf8()
{
    ensure_builtin_status_effect_catalog_loaded();
    std::vector<std::string> keys;
    {
        std::lock_guard<std::mutex> lock(g_status_mutex);
        keys.reserve(g_status_catalog.size());
        for (const auto& [k, _] : g_status_catalog) {
            keys.push_back(k);
        }
    }
    std::sort(keys.begin(), keys.end());
    std::string blob;
    for (const std::string& k : keys) {
        blob += k;
        blob.push_back('\n');
    }
    return std::to_string(std::hash<std::string>{}(blob));
}

}  // namespace tactics
