#include "tactics/cards/ability_catalog.hpp"

#include "tactics/cards/effect_definitions_io.hpp"
#include "tactics/common/effect_keys.hpp"
#include "tactics/effects/effect_registry.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <iostream>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace tactics {
namespace {

using json = nlohmann::json;

std::mutex g_catalog_mutex;
std::unordered_map<std::string, AbilitySpec> g_catalog;
bool g_builtins_loaded{false};
/** F3: When non-empty, used in place of `kBuiltinAbilityCatalogJson`. */
std::string g_ability_catalog_json_override{};

// Same schema as optional Content/TacticsData/ability_catalog.json (merge overrides).
// BEGIN_GENERATED_ABILITY_CATALOG
static const char kBuiltinAbilityCatalogJson[] =
    R"JSON({
  "abilities": {
    "field_bandage": {
      "key": "field_bandage",
      "name": "Field Bandage",
      "description": "",
      "speed": "channeled",
      "energy_cost": {
        "neutral": 1
      },
      "effect_key": "heal",
      "effect_payload": {
        "amount": 2
      },
      "requires_board_target": true,
      "board_target_kind": "ally",
      "normal_description": "Restore 2 HP to a target ally."
    },
    "combat_medic": {
      "key": "combat_medic",
      "name": "Combat Medic",
      "description": "Can be used in response to damage.",
      "speed": "reflex",
      "energy_cost": {
        "neutral": 1
      },
      "effect_key": "heal",
      "effect_payload": {
        "amount": 3
      },
      "requires_board_target": true,
      "board_target_kind": "ally",
      "normal_description": "Restore 3 HP to a target ally."
    },
    "quick_repairs": {
      "key": "quick_repairs",
      "name": "Quick Repairs",
      "description": "",
      "speed": "channeled",
      "energy_cost": {
        "orange": 1
      },
      "effect_key": "repair_structure_adjacent",
      "effect_payload": {
        "amount": 2,
        "base_amount": 2
      },
      "requires_board_target": true,
      "board_target_kind": "ally",
      "normal_description": "Heal 2 to an allied structure or base."
    },
    "tune_up": {
      "key": "tune_up",
      "name": "Tune-Up",
      "description": "{KW:boost|Boosts} an adjacent allied unit or structure (+2 damage on its next attack or ability). Stacks with other {KW:boost|boosts}.",
      "speed": "channeled",
      "energy_cost": {
        "orange": 2
      },
      "effect_key": "grant_next_damage_bonus_adjacent",
      "effect_payload": {
        "amount": 2
      },
      "requires_board_target": true,
      "board_target_kind": "ally",
      "normal_description": "{KW:boost|Boosts} an allied unit or structure with +2 damage on its next attack or ability."
    },
    "quick_shot": {
      "key": "quick_shot",
      "name": "Quick Shot",
      "description": "Targets enemies .",
      "speed": "reflex",
      "energy_cost": {
        "neutral": 1
      },
      "effect_key": "deal_damage",
      "effect_payload": {
        "amount": 2
      },
      "requires_board_target": true,
      "board_target_kind": "enemy",
      "uses_ranged_targeting": true,
      "range_max": 4,
      "normal_description": "Deal 2 damage to an enemy."
    },
    "rally_cry": {
      "key": "rally_cry",
      "name": "Rally Cry",
      "description": "",
      "speed": "blazing",
      "energy_cost": {
        "neutral": 1
      },
      "effect_key": "draw_cards",
      "effect_payload": {
        "amount": 1
      },
      "requires_board_target": false,
      "normal_description": "Draw 1 card immediately."
    },
    "requisition": {
      "key": "requisition",
      "name": "Requisition",
      "description": "",
      "speed": "channeled",
      "energy_cost": {
        "neutral": 1
      },
      "effect_key": "gain_neutral",
      "effect_payload": {
        "amount": 1
      },
      "requires_board_target": false,
      "normal_description": "Generate 1 neutral energy."
    },
    "overcharge_burst": {
      "key": "overcharge_burst",
      "name": "Overcharge Burst",
      "description": "Stacks with other {KW:boost|boosts}.",
      "speed": "blazing",
      "energy_cost": {
        "orange": 1
      },
      "effect_key": "grant_next_damage_bonus_self",
      "effect_payload": {
        "amount": 1
      },
      "requires_board_target": false,
      "normal_description": "{KW:boost|Boosts} self (+1 damage on your next attack or ability)."
    },
    "apply_overload_ability": {
      "key": "apply_overload_ability",
      "name": "Overload",
      "description": "{FX:overload|Overloaded} units take +1 damage from all sources.",
      "speed": "channeled",
      "energy_cost": {},
      "effect_key": "apply_overload",
      "effect_payload": {
        "amount": 1
      },
      "requires_board_target": true,
      "board_target_kind": "any",
      "normal_description": "Apply 1 {FX:overload} to a target."
    },
    "apply_jammed_ability": {
      "key": "apply_jammed_ability",
      "name": "Jam",
      "description": "{FX:jammed} units cannot use abilities until the status clears.",
      "speed": "channeled",
      "energy_cost": {},
      "effect_key": "apply_jammed",
      "effect_payload": {
        "amount": 1
      },
      "requires_board_target": true,
      "board_target_kind": "any",
      "normal_description": "Apply 1 {FX:jammed} to a target."
    },
    "starforged_growth": {
      "key": "starforged_growth",
      "name": "Starforged Growth",
      "description": "",
      "speed": "reflex",
      "energy_cost": {
        "neutral": 1,
        "orange": 1
      },
      "effect_key": "grant_permanent_stat_growth_self",
      "effect_payload": {
        "health": 1,
        "attack": 1
      },
      "requires_board_target": false,
      "uses_per_turn": 2,
      "normal_description": "Permanently gain +1 max HP and +1 damage."
    },
    "starforged_surge": {
      "key": "starforged_surge",
      "name": "Starforged Surge",
      "description": "",
      "speed": "reflex",
      "energy_cost": {
        "neutral": 1,
        "orange": 1
      },
      "effect_key": "grant_permanent_stat_growth_self",
      "effect_payload": {
        "health": 1,
        "attack": 1
      },
      "requires_board_target": false,
      "uses_per_turn": 3,
      "normal_description": "Permanently gain +1 max HP and +1 damage (3 uses per turn)."
    },
    "sylvia_static_charge": {
      "key": "sylvia_static_charge",
      "name": "Static Charge",
      "description": "Expires at your turn end if unused.",
      "speed": "channeled",
      "energy_cost": {
        "neutral": 1,
        "orange": 1
      },
      "effect_key": "grant_on_damage_apply_overload_adjacent",
      "requires_board_target": true,
      "board_target_kind": "ally",
      "normal_description": "{KW:boost|Boosts} an allied unit, structure, or yourself - its next attack or ability applies 1 {FX:overload} to each entity it damages."
    },
    "sylvia_jamming_array": {
      "key": "sylvia_jamming_array",
      "name": "Jamming Array",
      "description": "Expires at your turn end if unused. {FX:overload} and {FX:jammed} {KW:boost|boosts} are mutually exclusive on the same target.",
      "speed": "channeled",
      "energy_cost": {
        "neutral": 2,
        "orange": 3
      },
      "effect_key": "grant_on_damage_apply_jammed_adjacent",
      "requires_board_target": true,
      "board_target_kind": "ally",
      "normal_description": "{KW:boost|Boosts} an allied unit, structure, or yourself - its next attack or ability applies 1 {FX:jammed} to each entity it damages."
    },
    "energy_wave": {
      "key": "energy_wave",
      "name": "Energy Wave",
      "description": "Diagonal directions use a stacked-L footprint. Ignores cover. Counts as your attack.",
      "speed": "reflex",
      "energy_cost": {
        "neutral": 1,
        "orange": 1
      },
      "effect_key": "directional_damage",
      "effect_payload": {
        "amount": 2,
        "width": 3,
        "depth": 2,
        "max_range": 4
      },
      "effect_string_payload": {
        "shape": "rectangle"
      },
      "keywords": [
        "trueshot"
      ],
      "requires_board_target": true,
      "board_target_kind": "any",
      "consumes_attack_action": true,
      "normal_description": "Choose a direction ; deal 2 damage to everything in a 3×2 area in that direction."
    },
    "fury_strikes": {
      "key": "fury_strikes",
      "name": "Fury Strikes",
      "description": "On-hit effects and damage bonuses apply to every hit. Counts as your attack.",
      "speed": "channeled",
      "energy_cost": {
        "neutral": 1,
        "orange": 2
      },
      "effect_key": "multi_hit_damage",
      "effect_payload": {
        "amount": 1,
        "hits": 3,
        "max_range": 2,
        "cardinal_only": 1
      },
      "requires_board_target": true,
      "board_target_kind": "enemy",
      "consumes_attack_action": true,
      "normal_description": "Strike a target enemy three times for 1 damage each."
    },
    "missile_storm": {
      "key": "missile_storm",
      "name": "Missile Storm",
      "description": "",
      "speed": "reflex",
      "energy_cost": {
        "neutral": 2,
        "orange": 2
      },
      "effect_key": "missile_storm",
      "effect_payload": {
        "amount": 2,
        "max_targets": 4,
        "max_range": 4
      },
      "requires_board_target": false,
      "normal_description": "Fire missiles at up to 4 nearest enemies for 2 damage each."
    },
    "final_barrage": {
      "key": "final_barrage",
      "name": "Final Barrage",
      "description": "Mortar Barrage cannot target tiles (minimum range 2).",
      "speed": "channeled",
      "energy_cost": {
        "neutral": 3,
        "orange": 3
      },
      "effect_key": "final_barrage",
      "effect_payload": {
        "charges": 6,
        "self_destruct_damage": 3,
        "self_destruct_overload": 1
      },
      "requires_board_target": false,
      "normal_description": "At start of your next turn, fire Mortar Barrage 6 times, then self-destruct for 3 damage and 1 {FX:overload} to all entities."
    },
    "starforged_feast": {
      "key": "starforged_feast",
      "name": "Starforged Feast",
      "description": "",
      "speed": "channeled",
      "energy_cost": {},
      "effect_key": "consume_spell_orange_for_growth",
      "requires_board_target": false,
      "normal_description": "Consume all {GL:flux_energy|flux energy}; for each point consumed, permanently gain +1 max HP and +1 damage."
    },
    "quick_heal": {
      "key": "quick_heal",
      "name": "Quick Heal",
      "description": "",
      "speed": "reflex",
      "energy_cost": {
        "orange": 1
      },
      "effect_key": "heal",
      "effect_payload": {
        "amount": 4
      },
      "requires_board_target": true,
      "board_target_kind": "ally",
      "uses_ranged_targeting": true,
      "range_max": 2,
      "normal_description": "Heal a target allied unit for 4."
    },
    "medical_override": {
      "key": "medical_override",
      "name": "Medical Override",
      "description": "",
      "speed": "channeled",
      "energy_cost": {
        "neutral": 2,
        "orange": 1
      },
      "effect_key": "grant_medical_override",
      "requires_board_target": true,
      "board_target_kind": "ally",
      "uses_ranged_targeting": true,
      "range_max": 1,
      "normal_description": "Grant an allied unit {FX:medical_override} until the end of that unit's turn."
    },
    "piercing_shot": {
      "key": "piercing_shot",
      "name": "Piercing Shot",
      "description": "Ignores {FX:shield}, {GL:armor}, and {FX:bonus_health}; ignores line of sight; does not damage bases.",
      "speed": "channeled",
      "energy_cost": {
        "neutral": 2,
        "orange": 1
      },
      "effect_key": "piercing_shot",
      "effect_payload": {
        "amount": 3,
        "max_range": 8
      },
      "effect_string_payload": {
        "shape": "line"
      },
      "keywords": [
        "pierce"
      ],
      "requires_board_target": true,
      "board_target_kind": "any",
      "normal_description": "Fire a piercing bolt in one direction up to {RANGE}8 dealing 3 damage to every unit and structure hit."
    },
    "volt_surge": {
      "key": "volt_surge",
      "name": "Volt Surge",
      "description": "",
      "speed": "channeled",
      "energy_cost": {
        "neutral": 2,
        "orange": 1
      },
      "effect_key": "apply_overload_self",
      "effect_payload": {
        "amount": 2
      },
      "requires_board_target": false,
      "normal_description": "Gain 2 {FX:overload} on self."
    },
    "deploy_shock_wire": {
      "key": "deploy_shock_wire",)JSON"
    R"JSON(      "name": "Deploy Shock Wire",
      "description": "Shock Wire has no attacks. Hybrid units using ranged attacks do not trigger retaliation.",
      "speed": "channeled",
      "energy_cost": {
        "neutral": 1,
        "orange": 1
      },
      "effect_key": "spawn_shock_wire_adjacent",
      "range_max": 1,
      "requires_board_target": false,
      "uses_per_turn": 2,
      "normal_description": "Deploy Shock Wire {N}{O} (channeled): place a 2 HP Shock Wire on an {ADJACENT} empty cell; melee attackers that damage it receive 1 {FX:overload}."
    },
    "terra_cone_strike": {
      "key": "terra_cone_strike",
      "name": "Seismic Cone",
      "description": "Counts as your attack. Splash hits the three tiles directly behind the primary cell.",
      "speed": "reflex",
      "energy_cost": {
        "neutral": 1,
        "orange": 1
      },
      "effect_key": "terra_cone_strike",
      "effect_payload": {
        "amount": 6,
        "splash_amount": 3,
        "max_range": 1
      },
      "requires_board_target": true,
      "consumes_attack_action": true,
      "normal_description": "Seismic Cone {N}{O} (reflex, attack): deal 6 damage to tile and 3 damage to the three tiles behind it in a side-by-side row."
    },
    "terra_dash": {
      "key": "terra_dash",
      "name": "Terra Dash",
      "description": "Cannot pass through occupied tiles.",
      "speed": "channeled",
      "energy_cost": {
        "orange": 1
      },
      "effect_key": "terra_dash",
      "effect_payload": {
        "exact_range": 3,
        "min_range": 3,
        "max_range": 3
      },
      "requires_board_target": true,
      "normal_description": "Terra Dash {O} (channeled): dash exactly 3 tiles in a straight line to an empty cell or pickup."
    },
    "replicate_power": {
      "key": "replicate_power",
      "name": "Replicate Power",
      "speed": "channeled",
      "energy_cost": {
        "orange": 1,
        "neutral": 1
      },
      "effect_key": "grant_permanent_stat_growth_self",
      "effect_payload": {
        "attack": 1,
        "health": 0
      },
      "requires_board_target": false,
      "description": "",
      "normal_description": "Permanently gain +1 damage."
    },
    "debilitator_drag_line": {
      "key": "debilitator_drag_line",
      "name": "Drag Line",
      "speed": "channeled",
      "energy_cost": {
        "neutral": 1,
        "orange": 1
      },
      "effect_key": "grant_next_ability_movement_reduction_ally",
      "requires_board_target": true,
      "board_target_kind": "ally",
      "description": "Expires at your turn end if unused.",
      "normal_description": "{KW:boost|Boosts} an allied unit (or yourself) - its next damaging ability reduces 1 movement to each unit it damages."
    },
    "debilitator_anchor_line": {
      "key": "debilitator_anchor_line",
      "name": "Anchor Line",
      "speed": "channeled",
      "energy_cost": {
        "neutral": 2,
        "orange": 2
      },
      "effect_key": "grant_next_ability_rooted_ally",
      "requires_board_target": true,
      "board_target_kind": "ally",
      "description": "Expires at your turn end if unused. Can coexist with Drag Line on the same target.",
      "normal_description": "{KW:boost|Boosts} an allied unit (or yourself) - its next damaging ability {FX:rooted|roots} each unit it damages."
    },
    "sanglante_true_transformation": {
      "key": "sanglante_true_transformation",
      "name": "True Transformation",
      "speed": "reflex",
      "energy_cost": {
        "gallantry": 2
      },
      "effect_key": "true_transformation",
      "effect_string_payload": {
        "grant": "multistrike:1,lifesteal",
        "suppress_passive": "sanglante_bloodlust"
      },
      "requires_board_target": false,
      "description": "",
      "normal_description": "True Transformation {G}{G} (reflex): permanently gain {KW:multistrike} 1 and {KW:lifesteal}; Bloodlust is disabled."
    },
    "trench_sweeper_gas_grenade": {
      "key": "trench_sweeper_gas_grenade",
      "name": "Gas Grenade",
      "speed": "channeled",
      "energy_cost": {
        "neutral": 1,
        "gallantry": 1
      },
      "effect_key": "gas_grenade",
      "effect_payload": {
        "duration": 2
      },
      "requires_board_target": true,
      "board_target_kind": "any",
      "uses_ranged_targeting": true,
      "range_max": 3,
      "description": "Units on those tiles take 1 {FX:poison} immediately. Reapplying gas keeps the longer duration.",
      "normal_description": "Gas Grenade {N}{G} (channeled): place gas on target tile and its volley flank tiles (3 tiles) for 2 turns."
    },
    "fumigant_gas_detonation": {
      "key": "fumigant_gas_detonation",
      "name": "Gas Detonation",
      "speed": "blazing",
      "energy_cost": {
        "neutral": 2,
        "gallantry": 1
      },
      "effect_key": "gas_chain_detonate",
      "effect_payload": {
        "amount": 3
      },
      "requires_board_target": true,
      "board_target_kind": "any",
      "uses_ranged_targeting": true,
      "range_max": 3,
      "description": "",
      "normal_description": "Gas Detonation {N}{N}{G} (blazing): detonate gas at a tile for 3 damage, chaining to surrounding gas."
    },
    "supply_drop": {
      "key": "supply_drop",
      "name": "Supply Drop",
      "speed": "channeled",
      "energy_cost": {
        "neutral": 1,
        "gallantry": 1
      },
      "effect_key": "place_supply_drop",
      "effect_payload": {
        "amount": 4
      },
      "requires_board_target": true,
      "uses_ranged_targeting": true,
      "range_max": 2,
      "description": "The first unit to collect it heals 4 HP.",
      "normal_description": "Supply Drop {N}{G} (channeled): place a +4 HP heal pickup on an empty tile."
    },
    "inspire": {
      "key": "inspire",
      "name": "Inspire",
      "speed": "channeled",
      "energy_cost": {
        "gallantry": 1,
        "neutral": 1
      },
      "effect_key": "amplify_aura_stats",
      "effect_payload": {
        "attack": 1,
        "health": 1
      },
      "barrage": true,
      "barrage_cost": {
        "neutral": 1
      },
      "description": "Barrage: pay {N} again to extend.",
      "normal_description": "Inspire {G}{N} (channeled, barrage {N}): allied auras from this unit grant +1 attack and +1 HP until your next turn."
    },
    "coordinate": {
      "key": "coordinate",
      "name": "Coordinate",
      "speed": "channeled",
      "energy_cost": {
        "gallantry": 2,
        "neutral": 1
      },
      "effect_key": "extend_aura_range",
      "effect_payload": {
        "amount": 1
      },
      "barrage": true,
      "barrage_cost": {
        "neutral": 1
      },
      "description": "Barrage: pay {N} again to extend.",
      "normal_description": "Coordinate {G}{G}{N} (channeled, barrage {N}): increase this unit's aura range by 1 until your next turn."
    },
    "covering_shot": {
      "key": "covering_shot",
      "name": "Covering Shot",
      "speed": "channeled",
      "energy_cost": {
        "neutral": 2,
        "gallantry": 1
      },
      "effect_key": "apply_covering_fire",
      "board_target_kind": "ally",
      "uses_ranged_targeting": true,
      "range_max": 3,
      "description": "Each application is a separate reaction stack. Unused stacks decay at the start of your turn.",
      "normal_description": "Covering Shot {N}{G} (channeled): mark an allied unit - when an enemy attacks that ally this turn, this unit fires back."
    },
    "blitz": {
      "key": "blitz",
      "name": "Blitz",
      "speed": "channeled",
      "energy_cost": {
        "gallantry": 2,
        "neutral": 1
      },
      "effect_key": "grant_bonus_attack",
      "description": "",
      "normal_description": "Blitz {G}{G}{N} (channeled): gain a bonus attack this turn."
    },
    "field_dressing": {
      "key": "field_dressing",
      "name": "Field Dressing",
      "speed": "channeled",
      "energy_cost": {
        "gallantry": 1
      },
      "effect_key": "heal_surrounding_allies",
      "effect_payload": {
        "amount": 2
      },
      "description": "",
      "normal_description": "Field Dressing {G} (channeled): heal surrounding allies for 2 HP."
    },
    "triage": {
      "key": "triage",
      "name": "Triage",
      "speed": "channeled",
      "energy_cost": {
        "gallantry": 1,
        "neutral": 2
      },
      "effect_key": "triage",
      "board_target_kind": "ally",
      "uses_ranged_targeting": true,
      "range_max": 1,
      "description": "",
      "normal_description": "Triage {G}{N}{N} (channeled): remove one random debuff from an ally."
    },
    "coordinated_fire": {
      "key": "coordinated_fire",
      "name": "Coordinated Fire",
      "speed": "channeled",
      "energy_cost": {
        "gallantry": 2,
        "neutral": 2
      },
      "consumes_attack_action": true,
      "effect_key": "apply_coordinated_fire",
      "effect_payload": {
        "amount": 5,
        "damage_min": 3,
        "damage_max": 6
      },
      "description": "Counts as an attack.",
      "normal_description": "Coordinated Fire {G}{G}{N}{N} (channeled, costs attack action): when a friendly unit attacks an enemy, this unit also fires (3–6 damage), up to 5 shots until your next turn."
    },
    "deploy_conscripts": {
      "key": "deploy_conscripts",
      "name": "Deploy Conscripts",
      "speed": "channeled",
      "energy_cost": {
        "gallantry": 2,
        "neutral": 1
      },
      "effect_key": "spawn_conscripts_adjacent",
      "effect_payload": {
        "count": 2
      },
      "description": "",
      "normal_description": "Deploy Conscripts {G}{G}{N} (channeled): spawn 2 Conscript tokens on random unoccupied surrounding cells."
    },
    "deploy_flame_trooper": {
      "key": "deploy_flame_trooper",
      "name": "Deploy Flame Trooper",
      "speed": "channeled",
      "energy_cost": {
        "gallantry": 3,
        "neutral": 2
      },
      "effect_key": "spawn_flame_trooper_adjacent",
      "description": "",
      "normal_description": "Deploy Heavy Trooper {G}{G}{G}{N}{N} (channeled): spawn 1 Heavy Trooper token cell."
    },
    "bunker_buster": {
      "key": "bunker_buster",
      "name": "Bunker Buster",
      "speed": "channeled",
      "energy_cost": {
        "gallantry": 3
      },
      "effect_key": "bunker_buster_strike",
      "effect_payload": {
        "amount": 5
      },
      "board_target_kind": "enemy",
      "uses_ranged_targeting": true,
      "range_max": 1,
      "require_target_unit_types": [
        "base",
        "building"
      ],
      "description": "",
      "normal_description": "Bunker Buster {G}{G}{G} (channeled): deal 5 damage to an enemy base or structure."
    },
    "bombing_run": {
      "key": "bombing_run",
      "name": "Bombing Run",
      "speed": "channeled",
      "energy_cost": {
        "neutral": 2,
        "gallantry": 1
      },
      "effect_key": "bombing_run",
      "effect_payload": {
        "amount": 3,
        "max_range": 3,
        "min_range": 2,
        "cardinal_only": 1
      },
      "description": "Must land on an empty tile.",
      "normal_description": "Bombing Run {N}{G} (channeled): fly orthogonally 2–3 tiles, dealing 3 damage to each enemy unit passed over."
    },
    "rally": {
      "key": "rally",
      "name": "Rally",
      "speed": "channeled",
      "energy_cost": {
        "gallantry": 3,
        "neutral": 2
      },
      "effect_key": "grant_relentless_aura",
      "effect_payload": {
        "amount": 3
      },
      "description": "",
      "normal_description": "Rally {G}{G}{G}{N}{N} (channeled): surrounding allied units gain {KW:relentless} 3 until end of your turn."
    },
    "expose": {
      "key": "expose",
      "name": "Expose",
      "speed": "channeled",
      "energy_cost": {
        "gallantry": 1,
        "neutral": 1
      },)JSON"
    R"JSON(      "effect_key": "apply_vulnerable",
      "effect_payload": {
        "amount": 1
      },
      "board_target_kind": "any",
      "uses_ranged_targeting": true,
      "range_max": 2,
      "description": "",
      "normal_description": "Expose {G}{N} (channeled): apply 1 {FX:vulnerable} to a target until end of their turn."
    },
    "swap_weapons": {
      "key": "swap_weapons",
      "name": "Swap Weapons",
      "speed": "channeled",
      "energy_cost": {
        "gallantry": 1
      },
      "effect_key": "swap_weapons",
      "effect_payload": {
        "ranged_min": 3,
        "ranged_max": 4,
        "range_bonus": 1
      },
      "effect_string_payload": {
        "suppress": "cleave",
        "grant": "multistrike:1"
      },
      "description": "{KW:volley} is suppressed while active.",
      "normal_description": "Swap Weapons {G} (channeled): switch to ranged 3–4 with +1 range and {KW:multistrike} 1 until end of turn."
    },
    "apply_dual_wield": {
      "key": "apply_dual_wield",
      "name": "Dual Wield",
      "speed": "channeled",
      "energy_cost": {
        "gallantry": 2,
        "neutral": 2
      },
      "effect_key": "apply_dual_wield",
      "effect_payload": {
        "ranged_min": 3,
        "ranged_max": 6,
        "range_bonus": 1
      },
      "effect_string_payload": {
        "grant": "multistrike:1",
        "disable": "swap_weapons"
      },
      "description": "Swap Weapons is disabled while active.",
      "normal_description": "Dual Wield {G}{G}{N}{N} (channeled): switch to ranged 3–6 with +1 range and {KW:multistrike} 1 until end of turn."
    },
    "cover_shot": {
      "key": "cover_shot",
      "name": "Cover Shot",
      "speed": "channeled",
      "energy_cost": {
        "gallantry": 2
      },
      "effect_key": "apply_covering_fire_range_buff",
      "requires_board_target": true,
      "board_target_kind": "ally",
      "uses_ranged_targeting": true,
      "range_max": 2,
      "description": "",
      "normal_description": "Cover Shot {G}{G} (channeled): grant an ally {FX:covering_fire} until your next turn; the {FX:covering_fire} reaction and this unit's ranged range gain +1."
    },
    "overwatch": {
      "key": "overwatch",
      "name": "Overwatch",
      "speed": "channeled",
      "energy_cost": {
        "neutral": 1,
        "gallantry": 1
      },
      "effect_key": "apply_covering_fire_stacks",
      "effect_payload": {
        "amount": 2
      },
      "requires_board_target": true,
      "board_target_kind": "ally",
      "uses_ranged_targeting": true,
      "range_max": 4,
      "description": "",
      "normal_description": "Overwatch {N}{G} (channeled): place 2 {FX:covering_fire|covering-fire} stacks on an ally."
    },
    "go_dark": {
      "key": "go_dark",
      "name": "Go Dark",
      "speed": "channeled",
      "energy_cost": {
        "gallantry": 3,
        "neutral": 1
      },
      "effect_key": "grant_stealth_self",
      "description": "",
      "normal_description": "Go Dark {G}{G}{G}{N} (channeled): gain 1 {FX:stealth}."
    },
    "core_cracker_prime_ability": {
      "key": "core_cracker_prime_ability",
      "name": "Prime Core",
      "speed": "channeled",
      "energy_cost": {
        "gallantry": 3
      },
      "effect_key": "core_cracker_prime",
      "requires_board_target": false,
      "description": "",
      "normal_description": "Prime Core {G}{G}{G} (channeled): prime this unit; it may act and react until your next turn."
    },
    "core_cracker_breach_ability": {
      "key": "core_cracker_breach_ability",
      "name": "Breaching Charge",
      "speed": "channeled",
      "energy_cost": {
        "gallantry": 1
      },
      "effect_key": "core_cracker_breach",
      "effect_payload": {
        "amount": 15
      },
      "board_target_kind": "enemy",
      "range_min": 1,
      "range_max": 1,
      "require_target_entity_types": [
        "base"
      ],
      "consumes_attack_action": false,
      "description": "Pierces base immunity.",
      "normal_description": "Breaching Charge {G} (channeled): deal 15 damage to an enemy base, then destroy this unit."
    },
    "dottie_healing_flight": {
      "key": "dottie_healing_flight",
      "name": "Healing Flight",
      "speed": "channeled",
      "energy_cost": {
        "neutral": 2,
        "gallantry": 1
      },
      "effect_key": "healing_flight",
      "effect_payload": {
        "amount": 4,
        "max_range": 5,
        "min_range": 2,
        "cardinal_only": 1
      },
      "requires_board_target": true,
      "uses_ranged_targeting": false,
      "description": "Land on an empty tile or pickup.",
      "normal_description": "Healing Flight {N}{N}{G} (channeled): fly orthogonally 2–5 tiles, dropping +4 HP heal pickups on empty cells along the path."
    },
    "dottie_evasive_maneuver": {
      "key": "dottie_evasive_maneuver",
      "name": "Evasive Maneuver",
      "speed": "channeled",
      "energy_cost": {
        "gallantry": 2
      },
      "effect_key": "grant_evasive_self",
      "effect_payload": {
        "amount": 1
      },
      "requires_board_target": false,
      "description": "Stacks extend duration.",
      "normal_description": "{KW:evasive} Maneuver {G}{G} (channeled): gain {KW:evasive} (50% miss chance)."
    },
    "galvanized_bunker_whirlwind_spray": {
      "key": "galvanized_bunker_whirlwind_spray",
      "name": "Whirlwind Spray",
      "speed": "channeled",
      "energy_cost": {
        "gallantry": 2
      },
      "effect_key": "whirlwind_spray",
      "effect_payload": {
        "range_bonus": -1
      },
      "requires_board_target": false,
      "description": "",
      "normal_description": "{KW:whirlwind} Spray {G}{G} (channeled): gain {KW:whirlwind} and −1 ranged range until end of your turn."
    },
    "miasma_billow": {
      "key": "miasma_billow",
      "name": "Billow",
      "speed": "channeled",
      "energy_cost": {
        "neutral": 1,
        "gallantry": 1
      },
      "effect_key": "grant_cleave_self",
      "requires_board_target": false,
      "description": "",
      "normal_description": "Billow {N}{G} (channeled): gain {KW:cleave} until end of your turn."
    },
    "mending_shot": {
      "key": "mending_shot",
      "name": "Mending Shot",
      "speed": "reflex",
      "energy_cost": {
        "orange": 1
      },
      "effect_key": "heal_boosted",
      "effect_payload": {
        "amount": 5
      },
      "requires_board_target": true,
      "board_target_kind": "ally",
      "uses_ranged_targeting": true,
      "range_max": 2,
      "description": "Also gains the caster’s aura ability-damage bonus (e.g. {KW:command|Command Presence}).",
      "normal_description": "Mending Shot {O} (reflex): heal a unit (or yourself) for 5; consumes all pending next-damage {KW:boost|boosts} as extra healing."
    },
    "doublecast": {
      "key": "doublecast",
      "name": "Doublecast",
      "speed": "channeled",
      "energy_cost": {
        "neutral": 2,
        "orange": 3
      },
      "effect_key": "grant_doubled_next_ability",
      "requires_board_target": true,
      "board_target_kind": "ally",
      "description": "The echo resolves before the original on the stack.",
      "normal_description": "Doublecast {N}{N}{O}{O}{O} (channeled): target a unit or yourself - that unit’s next activated ability casts twice on the same targets."
    },
    "high_explosive_round": {
      "key": "high_explosive_round",
      "name": "High Explosive Round",
      "speed": "reflex",
      "energy_cost": {
        "neutral": 2,
        "orange": 3
      },
      "effect_key": "high_explosive_round",
      "effect_payload": {
        "amount": 4
      },
      "requires_board_target": true,
      "board_target_kind": "enemy",
      "uses_ranged_targeting": true,
      "consumes_attack_action": true,
      "description": "Shot may be body-intercepted (50% per blocking unit), redirecting the blast. Explosion ignores LOS. Counts as your attack.",
      "normal_description": "High Explosive Round {N}{N}{O}{O}{O} (reflex, attack): fire a shell at an enemy; detonate for 4 damage in a 3×3 area where it lands."
    },
    "artillery_mode": {
      "key": "artillery_mode",
      "name": "Artillery Mode",
      "speed": "channeled",
      "energy_cost": {
        "neutral": 1,
        "orange": 1
      },
      "effect_key": "artillery_mode",
      "description": "",
      "normal_description": "Artillery Mode {N}{O} (channeled): gain {KW:trueshot}, +1 {RANGE}, and deadzone 1 until start of your next turn."
    },
    "amplify": {
      "key": "amplify",
      "name": "Amplify",
      "speed": "channeled",
      "energy_cost": {
        "neutral": 1,
        "orange": 1
      },
      "effect_key": "extend_aura_range",
      "effect_payload": {
        "amount": 1
      },
      "uses_per_turn": 2,
      "description": "Stacks with Press the Advantage.",
      "normal_description": "Amplify {N}{O} (channeled): increase {KW:command|Command Presence} range by 1 until start of your next turn."
    },
    "cross_x_shot": {
      "key": "cross_x_shot",
      "name": "Cross-X Shot",
      "speed": "channeled",
      "energy_cost": {
        "neutral": 2,
        "orange": 1
      },
      "effect_key": "alternating_cross_x_shot",
      "effect_payload": {
        "range": 2
      },
      "uses_per_turn": 2,
      "description": "Line of sight required to each target cell. Starts as Cross Shot, then alternates to X Shot on each use.",
      "normal_description": "Cross-X Shot {N}{N}{O} (channeled, 2 uses): fire at enemies exactly {RANGE}2 away - first use is the 4 cardinal directions, then the 4 diagonals, alternating each use; uses your ranged damage; gain 1 {FX:style}."
    },
    "press_the_advantage": {
      "key": "press_the_advantage",
      "name": "Press the Advantage",
      "speed": "channeled",
      "energy_cost": {
        "neutral": 2,
        "orange": 1
      },
      "effect_key": "grant_permanent_aura_ability_damage",
      "effect_payload": {
        "amount": 1
      },
      "description": "Stacks on repeated use.",
      "normal_description": "Press the Advantage {N}{N}{O} (channeled): permanently increase {KW:command|Command Presence} ability damage by +1."
    },
    "cross_shot": {
      "key": "cross_shot",
      "name": "Cross Shot",
      "speed": "channeled",
      "energy_cost": {
        "neutral": 2,
        "orange": 1
      },
      "effect_key": "cross_shot",
      "effect_payload": {
        "range": 2
      },
      "description": "Line of sight required to each target cell.",
      "normal_description": "Cross Shot {N}{N}{O} (channeled): fire at enemies exactly {RANGE}2 away in the 4 cardinal directions; uses your ranged damage; gain 1 {FX:style}."
    },
    "x_shot": {
      "key": "x_shot",
      "name": "X Shot",
      "speed": "channeled",
      "energy_cost": {
        "neutral": 2,
        "orange": 1
      },
      "effect_key": "x_shot",
      "effect_payload": {
        "range": 2
      },
      "description": "Line of sight required to each target cell.",
      "normal_description": "X Shot {N}{N}{O} (channeled): fire at enemies exactly {RANGE}2 away on the 4 diagonals; uses your ranged damage; gain 1 {FX:style}."
    },
    "unleash": {
      "key": "unleash",
      "name": "Unleash",
      "speed": "channeled",
      "energy_cost": {
        "neutral": 2,
        "orange": 2
      },
      "effect_key": "unleash_style",
      "requires_board_target": true,
      "board_target_kind": "enemy",
      "uses_ranged_targeting": true,
      "range_max": 1,
      "scales_with_ability_damage": true,
      "description": "Also scales with pending {KW:boost|boosts} on Debonair (+ ability bonuses).",
      "normal_description": "Unleash {N}{N}{O}{O} (channeled): deal 2 damage per {FX:style} stack to a surrounding enemy, then consume all {FX:style}."
    },
    "spellcasting_squire_draw_spell": {)JSON"
    R"JSON(      "key": "spellcasting_squire_draw_spell",
      "name": "Scribe's Insight",
      "description": "Ineligible spells stay in the deck; only matching spells are removed.",
      "speed": "channeled",
      "energy_cost": {
        "neutral": 1,
        "turquoise": 2
      },
      "effect_key": "draw_spell_cards",
      "effect_payload": {
        "amount": 1,
        "max_total_cost": 3
      },
      "requires_board_target": false,
      "normal_description": "Draw a spell from your deck that costs 3 or less."
    },
    "orphic_knight_verse_strike": {
      "key": "orphic_knight_verse_strike",
      "name": "Orphic Verse",
      "description": "Requires an attack action. Bonus damage includes damage buffs, keyword bonuses, and ability-damage aura.",
      "speed": "reflex",
      "energy_cost": {
        "turquoise": 1
      },
      "effect_key": "deal_damage",
      "effect_payload": {
        "amount": 1,
        "convert_all_bonus_damage_to_health": 1
      },
      "requires_board_target": true,
      "board_target_kind": "enemy",
      "consumes_attack_action": true,
      "normal_description": "Deal 1 damage to an enemy; convert all bonus damage into {FX:bonus_health} on this unit."
    },
    "mae_and_faye_echo_spell": {
      "key": "mae_and_faye_echo_spell",
      "name": "Echo Spell",
      "description": "Click a batch queue entry when the ability is armed.",
      "speed": "reflex",
      "energy_cost": {
        "turquoise": 1
      },
      "x_cost": {
        "type": "turquoise",
        "min": 0
      },
      "effect_key": "copy_allied_spell",
      "requires_board_target": false,
      "normal_description": "Copy an allied spell on the batch queue with total energy cost X. Pay {T} + X."
    },
    "mae_and_faye_charm_and_harm": {
      "key": "mae_and_faye_charm_and_harm",
      "name": "Charm & Harm",
      "description": "Targets enemy units .",
      "speed": "channeled",
      "energy_cost": {
        "turquoise": 2
      },
      "effect_key": "stun_and_damage",
      "effect_payload": {
        "amount": 3,
        "stun": 1
      },
      "requires_board_target": true,
      "board_target_kind": "enemy",
      "uses_ranged_targeting": true,
      "range_max": 3,
      "require_target_unit_types": [
        "unit"
      ],
      "normal_description": "Deal 3 damage to an enemy unit and apply 1 {FX:stunned|Stun}."
    },
    "magus_apprentice_arcane_bolt": {
      "key": "magus_apprentice_arcane_bolt",
      "name": "Arcane Bolt",
      "description": "Targets enemies .",
      "speed": "reflex",
      "energy_cost": {
        "neutral": 1,
        "turquoise": 1
      },
      "effect_key": "magus_charge_strike",
      "requires_board_target": true,
      "board_target_kind": "enemy",
      "uses_ranged_targeting": true,
      "range_max": 2,
      "require_target_unit_types": [
        "unit",
        "structure"
      ],
      "normal_description": "Consume all Arcane Charge. Deal that much damage to an enemy unit or structure."
    },
    "magus_apprentice_arcane_surge": {
      "key": "magus_apprentice_arcane_surge",
      "name": "Arcane Surge",
      "description": "",
      "speed": "reflex",
      "energy_cost": {
        "neutral": 1,
        "turquoise": 2
      },
      "effect_key": "magus_charge_surrounding_burst",
      "requires_board_target": false,
      "normal_description": "Consume all Arcane Charge. Deal that much damage to this unit and each surrounding unit, structure, and base."
    },
    "silvermane_cavalry_silver_charge": {
      "key": "silvermane_cavalry_silver_charge",
      "name": "Silver Charge",
      "description": "If your attack kills the target, they do not counterattack while you have {KW:first_strike}.",
      "speed": "reflex",
      "energy_cost": {
        "turquoise": 2
      },
      "effect_key": "grant_first_strike_self",
      "requires_board_target": false,
      "normal_description": "Gain {KW:first_strike} until end of turn."
    },
    "sir_garrick_valiant_guard": {
      "key": "sir_garrick_valiant_guard",
      "name": "Valiant Guard",
      "description": "Garrick may counterattack afterward if able (range, reactions, not {FX:stunned}). The ally does not counter.",
      "speed": "channeled",
      "energy_cost": {
        "neutral": 1,
        "turquoise": 1
      },
      "effect_key": "apply_valiant_guard_self",
      "requires_board_target": false,
      "normal_description": "The next time a surrounding 1×1 ally is targeted by an attack, swap positions and take that attack."
    },
    "sir_garrick_bulwark": {
      "key": "sir_garrick_bulwark",
      "name": "Bulwark",
      "description": "{FX:barrier} blocks one hit of damage; Piercing ignores {FX:barrier}.",
      "speed": "reflex",
      "energy_cost": {
        "neutral": 1,
        "turquoise": 1
      },
      "effect_key": "apply_barrier_self",
      "requires_board_target": false,
      "normal_description": "Gain 1 {FX:barrier} until end of this turn."
    },
    "disregarded_rangers_serrated_bolt": {
      "key": "disregarded_rangers_serrated_bolt",
      "name": "Serrated Bolt",
      "description": "{FX:bleed} stacks decay at the owner's turn-end and deal damage equal to the stack count.",
      "speed": "channeled",
      "energy_cost": {
        "neutral": 1
      },
      "effect_key": "grant_next_bleed_self",
      "effect_payload": {
        "amount": 3
      },
      "requires_board_target": false,
      "normal_description": "Your next attack or ability applies 3 {FX:bleed} to each unit it damages."
    },
    "disregarded_rangers_hookshot": {
      "key": "disregarded_rangers_hookshot",
      "name": "Hookshot",
      "description": "{RANGE}4; pull is orthogonal or diagonal only and stops when the target is adjacent or blocked.",
      "speed": "channeled",
      "energy_cost": {
        "neutral": 1,
        "turquoise": 1
      },
      "effect_key": "hookshot_pull",
      "effect_payload": {
        "amount": 3,
        "max_range": 4
      },
      "requires_board_target": true,
      "board_target_kind": "enemy",
      "uses_ranged_targeting": true,
      "range_max": 4,
      "normal_description": "Deal 3 damage to a unit and pull it toward you along a straight line."
    },
    "war_time_belles_battle_hymn": {
      "key": "war_time_belles_battle_hymn",
      "name": "Battle Hymn",
      "description": "",
      "speed": "channeled",
      "energy_cost": {
        "neutral": 1,
        "gallantry": 1
      },
      "effect_key": "grant_damage_aura",
      "effect_payload": {
        "amount": 1
      },
      "requires_board_target": false,
      "normal_description": "Battle Hymn {N}{G} (channeled): surrounding allied units gain +1 damage this turn."
    },
    "war_time_belles_cheers_of_battle": {
      "key": "war_time_belles_cheers_of_battle",
      "name": "The Cheers of Battle",
      "description": "",
      "speed": "channeled",
      "energy_cost": {
        "gallantry": 3,
        "neutral": 1
      },
      "effect_key": "grant_multistrike_ally",
      "effect_payload": {
        "amount": 1
      },
      "requires_board_target": true,
      "board_target_kind": "ally",
      "range_max": 1,
      "normal_description": "The Cheers of Battle {G}{G}{G}{N} (channeled): grant an allied unit {KW:multistrike} 1 this turn."
    },
    "lady_concordia_love": {
      "key": "lady_concordia_love",
      "name": "Concordia's Love",
      "description": "",
      "speed": "channeled",
      "energy_cost": {
        "gallantry": 2,
        "neutral": 2
      },
      "effect_key": "expand_death_shield_range_self",
      "effect_payload": {
        "amount": 1
      },
      "normal_description": "Concordia's Love {G}{G}{N}{N} (channeled): permanently expand Concordia's Promise range by 1."
    },
    "sunkissed_calibration": {
      "key": "sunkissed_calibration",
      "name": "Sunkissed Calibration",
      "description": "",
      "speed": "reflex",
      "energy_cost": {
        "neutral": 1,
        "orange": 3
      },
      "effect_key": "grant_permanent_stat_growth_self",
      "effect_payload": {
        "health": 1,
        "attack": 2
      },
      "requires_board_target": false,
      "uses_per_turn": 2,
      "normal_description": "Permanently gain +2 damage and +1 max HP."
    },
    "base_overcharge": {
      "key": "base_overcharge",
      "name": "Overcharge Batteries",
      "description": "Stacks with itself until end of turn.",
      "speed": "blazing",
      "energy_cost": {
        "neutral": 3
      },
      "effect_key": "grant_attack_damage_turn_self",
      "effect_payload": {
        "amount": 2
      },
      "requires_board_target": false,
      "uses_per_turn": 0,
      "uses_per_game": 3,
      "normal_description": "Blazing {N}{N}{N}: gain +2 attack damage this turn (stacks)."
    },
    "base_extend_range": {
      "key": "base_extend_range",
      "name": "Elevate Turrets",
      "description": "",
      "speed": "blazing",
      "energy_cost": {
        "neutral": 3
      },
      "effect_key": "grant_ranged_range_turn_self",
      "effect_payload": {
        "amount": 1
      },
      "requires_board_target": false,
      "uses_per_turn": 0,
      "normal_description": "Blazing {N}{N}{N}: gain +1 ranged range this turn."
    }
  }
})JSON";
// END_GENERATED_ABILITY_CATALOG

bool merge_abilities_object(const json& root, std::string& err_out)
{
	const auto it = root.find("abilities");
	if (it == root.end() || !it->is_object()) {
		err_out = "ability catalog: missing top-level \"abilities\" object";
		return false;
	}
	std::vector<AbilitySpec> parsed;
	parsed.reserve(it->size());
	for (auto e = it->begin(); e != it->end(); ++e) {
		if (!e.value().is_object()) {
			err_out = "ability catalog: entry for \"" + e.key() + "\" must be an object";
			return false;
		}
		AbilitySpec a;
		if (!effect_io::read_ability_spec_object(e.value(), a, err_out, "ability catalog." + e.key())) {
			return false;
		}
		if (a.key.empty()) {
			a.key = e.key();
		}
		// Validate effect_key against the registered effect handlers.
		// Only warns - a typo produces a runtime no-op rather than a hard error.
		if (!a.effect_key.empty() && !is_known_effect_key(a.effect_key)) {
			std::cerr << "[ability_catalog] WARNING: ability \"" << a.key
			          << "\" references unknown effect_key \"" << a.effect_key
			          << "\" -- check for typos\n";
		}
		parsed.push_back(std::move(a));
	}
	std::lock_guard<std::mutex> lock(g_catalog_mutex);
	for (AbilitySpec& a : parsed) {
		const std::string key = a.key;
		g_catalog.erase(key);
		g_catalog.emplace(key, std::move(a));
	}
	return true;
}

}  // namespace

void clear_ability_catalog()
{
	std::lock_guard<std::mutex> lock(g_catalog_mutex);
	g_catalog.clear();
	g_builtins_loaded = false;
}

bool load_ability_catalog_from_json_utf8(const std::string& utf8, std::string& err_out)
{
	try {
		json root = json::parse(utf8);
		return merge_abilities_object(root, err_out);
	} catch (const std::exception& ex) {
		err_out = ex.what();
		return false;
	}
}

void ensure_builtin_ability_catalog_loaded()
{
	{
		std::lock_guard<std::mutex> lock(g_catalog_mutex);
		if (g_builtins_loaded) {
			return;
		}
	}
	// F3: use the override JSON when set, otherwise fall back to the compiled-in literal.
	std::string json_src;
	{
		std::lock_guard<std::mutex> lock(g_catalog_mutex);
		json_src = g_ability_catalog_json_override.empty() ? std::string{kBuiltinAbilityCatalogJson} : g_ability_catalog_json_override;
	}
	std::string err;
	if (load_ability_catalog_from_json_utf8(json_src, err)) {
		std::lock_guard<std::mutex> lock(g_catalog_mutex);
		g_builtins_loaded = true;
		return;
	}
	AbilitySpec bandage;
	bandage.key = "field_bandage";
	bandage.name = "Field Bandage";
	bandage.speed = EffectSpeed::Channeled;
	bandage.energy_cost = {{EnergyType::Neutral, 1}};
	bandage.effect_key = "heal";
	bandage.effect_payload = {{effect_keys::kPayloadAmount, 2}};
	bandage.requires_board_target = true;
	bandage.board_target_kind = BoardTargetKind::Ally;
	AbilitySpec medic;
	medic.key = "combat_medic";
	medic.name = "Combat Medic";
	medic.speed = EffectSpeed::Reflex;
	medic.energy_cost = {{EnergyType::Neutral, 1}};
	medic.effect_key = "heal";
	medic.effect_payload = {{effect_keys::kPayloadAmount, 3}};
	medic.requires_board_target = true;
	medic.board_target_kind = BoardTargetKind::Ally;
	AbilitySpec shot;
	shot.key = "quick_shot";
	shot.name = "Quick Shot";
	shot.speed = EffectSpeed::Reflex;
	shot.energy_cost = {{EnergyType::Neutral, 1}};
	shot.effect_key = "deal_damage";
	shot.effect_payload = {{effect_keys::kPayloadAmount, 2}};
	shot.requires_board_target = true;
	shot.board_target_kind = BoardTargetKind::Enemy;
	AbilitySpec rally;
	rally.key = "rally_cry";
	rally.name = "Rally Cry";
	rally.speed = EffectSpeed::Blazing;
	rally.energy_cost = {{EnergyType::Neutral, 1}};
	rally.effect_key = "draw_cards";
	rally.effect_payload = {{effect_keys::kPayloadAmount, 1}};
	rally.requires_board_target = false;
	AbilitySpec req;
	req.key = "requisition";
	req.name = "Requisition";
	req.speed = EffectSpeed::Channeled;
	req.energy_cost = {{EnergyType::Neutral, 1}};
	req.effect_key = "gain_neutral";
	req.effect_payload = {{effect_keys::kPayloadAmount, 1}};
	req.requires_board_target = false;
	std::lock_guard<std::mutex> lock(g_catalog_mutex);
	g_catalog[bandage.key] = std::move(bandage);
	g_catalog[medic.key] = std::move(medic);
	g_catalog[shot.key] = std::move(shot);
	g_catalog[rally.key] = std::move(rally);
	g_catalog[req.key] = std::move(req);
	g_builtins_loaded = true;
}

void set_builtin_ability_catalog_json_override(std::string utf8_json)
{
	std::lock_guard<std::mutex> lock(g_catalog_mutex);
	g_ability_catalog_json_override = std::move(utf8_json);
	// Reset so the next ensure_builtin_ability_catalog_loaded() call picks up the new JSON.
	g_builtins_loaded = false;
	g_catalog.clear();
}

bool try_get_ability_from_catalog(const std::string& id, AbilitySpec& out)
{
	ensure_builtin_ability_catalog_loaded();
	std::lock_guard<std::mutex> lock(g_catalog_mutex);
	auto it = g_catalog.find(id);
	if (it == g_catalog.end()) {
		return false;
	}
	out = it->second;
	return true;
}

void append_abilities_from_catalog_ids(UnitCard& card, std::initializer_list<const char*> ids)
{
	ensure_builtin_ability_catalog_loaded();
	std::lock_guard<std::mutex> lock(g_catalog_mutex);
	for (const char* id : ids) {
		auto it = g_catalog.find(id);
		if (it != g_catalog.end()) {
			card.template_unit.activated_abilities.push_back(it->second);
		}
	}
}

std::string ability_catalog_fingerprint_utf8()
{
	ensure_builtin_ability_catalog_loaded();
	std::vector<std::string> keys;
	{
		std::lock_guard<std::mutex> lock(g_catalog_mutex);
		keys.reserve(g_catalog.size());
		for (const auto& [k, _] : g_catalog) {
			keys.push_back(k);
		}
	}
	std::sort(keys.begin(), keys.end());
	std::string blob;
	for (const std::string& k : keys) {
		blob += k;
		blob.push_back('\n');
	}
	const std::hash<std::string> hasher;
	return std::to_string(hasher(blob));
}

}  // namespace tactics
