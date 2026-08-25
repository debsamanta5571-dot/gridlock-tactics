#include "tactics/effects/effect_registry.hpp"

#include "tactics/core/board_target_policy.hpp"

#include "tactics/cards/unit_types.hpp"
#include "tactics/common/effect_keys.hpp"
#include "tactics/core/game_state.hpp"
#include "tactics/core/stack.hpp"

#include <algorithm>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace tactics {
namespace {

std::mutex g_effect_registry_mutex;
std::unordered_map<std::string, EffectDefinition> g_effect_registry;
bool g_builtins_loaded{false};

// E7: default_btk declares this effect's board-target affinity when it is used as an automated
// passive (no board target required, but pick_automated_target() still needs a direction).
EffectDefinition make_no_target_effect(std::string key, std::string display_name, std::string rules_text,
    std::vector<std::string> payload_keys, BoardTargetKind default_btk = BoardTargetKind::Own)
{
    EffectDefinition def;
    def.key = std::move(key);
    def.display_name = std::move(display_name);
    def.rules_text = std::move(rules_text);
    def.payload_keys = std::move(payload_keys);
    def.default_board_target_kind = default_btk;
    return def;
}

EffectDefinition make_board_entity_effect(std::string key, std::string display_name, std::string rules_text, BoardTargetKind kind,
    TargetRequirement requirement, std::vector<std::string> payload_keys)
{
    EffectDefinition def;
    def.key = std::move(key);
    def.display_name = std::move(display_name);
    def.rules_text = std::move(rules_text);
    def.target.domain = TargetDomain::BoardEntityCell;
    def.target.requirement = requirement;
    def.target.board_target_kind = kind;
    def.payload_keys = std::move(payload_keys);
    return def;
}

EffectDefinition make_stack_item_effect(
    std::string key, std::string display_name, std::string rules_text, std::vector<std::string> allowed_source_types)
{
    EffectDefinition def;
    def.key = std::move(key);
    def.display_name = std::move(display_name);
    def.rules_text = std::move(rules_text);
    def.target.domain = TargetDomain::StackItem;
    def.target.requirement = TargetRequirement::Required;
    def.target.allowed_stack_source_types = std::move(allowed_source_types);
    return def;
}

EffectDefinition make_player_seat_effect(std::string key, std::string display_name, std::string rules_text,
    std::vector<std::string> payload_keys)
{
    EffectDefinition def;
    def.key = std::move(key);
    def.display_name = std::move(display_name);
    def.rules_text = std::move(rules_text);
    def.target.domain = TargetDomain::PlayerSeat;
    def.target.requirement = TargetRequirement::Required;
    def.payload_keys = std::move(payload_keys);
    return def;
}

}  // namespace

void clear_effect_registry()
{
    std::lock_guard<std::mutex> lock(g_effect_registry_mutex);
    g_effect_registry.clear();
    g_builtins_loaded = false;
}

bool register_effect_definition(EffectDefinition def)
{
    std::lock_guard<std::mutex> lock(g_effect_registry_mutex);
    const bool existed = g_effect_registry.contains(def.key);
    g_effect_registry[def.key] = std::move(def);
    return !existed;
}

void ensure_builtin_effect_registry_loaded()
{
    {
        std::lock_guard<std::mutex> lock(g_effect_registry_mutex);
        if (g_builtins_loaded) {
            return;
        }
        g_builtins_loaded = true;
    }
    {
        auto deal = make_board_entity_effect(
            "deal_damage", "Deal Damage",
            "Deal damage to an enemy entity. Optional convert_all_bonus_damage_to_health: 1 converts all "
            "outgoing bonus damage (next-damage stacks, keyword bonuses, ability-damage aura, unit-type bonus) "
            "into bonus health on the source instead of adding to damage.",
            BoardTargetKind::Enemy, TargetRequirement::Required,
            {effect_keys::kPayloadAmount, effect_keys::kDamageType, "convert_all_bonus_damage_to_health"});
        deal.deals_damage = true;
        register_effect_definition(std::move(deal));
    }
    {
        auto attack = make_board_entity_effect(
            "basic_attack", "Basic Attack", "Attack an enemy at a board cell.", BoardTargetKind::Enemy, TargetRequirement::Required,
            {effect_keys::kAttackPreferRanged});
        attack.deals_damage = true;
        register_effect_definition(std::move(attack));
    }
    register_effect_definition(make_board_entity_effect(
        "heal", "Heal", "Heal an allied entity.", BoardTargetKind::Ally, TargetRequirement::Required, {effect_keys::kPayloadAmount}));
    register_effect_definition(make_board_entity_effect(
        "grant_bonus_health", "Grant Bonus Health",
        "Grant bonus health stacks to an allied unit (`amount`, default 1). Absorbs damage before normal HP.",
        BoardTargetKind::Ally, TargetRequirement::Required, {effect_keys::kPayloadAmount}));
    {
        // heal_boosted: same as heal but also applies ability_damage_bonus from the caster's aura
        // (e.g. Command Presence). Used by Jacquelynne's Mending Shot.
        auto hb = make_board_entity_effect(
            "heal_boosted", "Heal (Ability-Boosted)",
            "Heal an allied entity for `amount` plus the caster's ability-damage aura bonus. "
            "Scales with ability damage boosts (e.g. Command Presence). Can target self.",
            BoardTargetKind::Ally, TargetRequirement::Required, {effect_keys::kPayloadAmount});
        hb.scales_with_ability_damage = true;
        register_effect_definition(std::move(hb));
    }
    {
        auto gda = make_board_entity_effect(
            "grant_doubled_next_ability", "Doublecast",
            "The target unit's next activated ability is echoed: fired a second time with the same targets. "
            "Consumed when that unit next activates any ability.",
            BoardTargetKind::Ally, TargetRequirement::Required, {});
        register_effect_definition(std::move(gda));
    }
    register_effect_definition(make_board_entity_effect(
        "apply_overload_ally_draw", "Desperate Scan",
        "Apply `amount` overload stacks to an allied unit and draw `draw` cards. "
        "Can target self.",
        BoardTargetKind::Ally, TargetRequirement::Required, {effect_keys::kPayloadAmount}));
    register_effect_definition(make_board_entity_effect(
        "repair_structure_adjacent", "Repair Structure",
        "Heal an adjacent allied structure (`amount`); optional `base_amount` heals player bases.", BoardTargetKind::Ally,
        TargetRequirement::Required, {effect_keys::kPayloadAmount, effect_keys::kPayloadBaseAmount}));
    register_effect_definition(make_board_entity_effect(
        "grant_next_damage_bonus_adjacent", "Grant Next Damage Bonus",
        "Give an adjacent allied unit or structure bonus damage on its next damaging attack or ability.", BoardTargetKind::Ally,
        TargetRequirement::Required, {effect_keys::kPayloadAmount}));
    register_effect_definition(make_board_entity_effect(
        "grant_next_damage_bonus", "Grant Next Damage Bonus",
        "Grant target unit or structure bonus damage on its next damaging attack or ability.",
        BoardTargetKind::Ally, TargetRequirement::Required, {effect_keys::kPayloadAmount}));
    register_effect_definition(make_no_target_effect(
        "grant_attack_damage_turn_self", "Grant Attack Damage (Self, This Turn)",
        "Adds stacking +N attack damage until owner turn end. Payload: amount (default 2).",
        {effect_keys::kPayloadAmount}));
    register_effect_definition(make_no_target_effect(
        "grant_ranged_range_turn_self", "Grant Ranged Range (Self, This Turn)",
        "Adds stacking +N ranged range until owner turn end. Payload: amount (default 1).",
        {effect_keys::kPayloadAmount}));
    register_effect_definition(make_no_target_effect(
        "grant_next_damage_bonus_self", "Grant Next Damage Bonus (Self)",
        "Grant bonus damage on this unit's next damaging attack or ability.", {effect_keys::kPayloadAmount}));
    register_effect_definition(make_no_target_effect(
        "grant_next_bleed_self", "Grant Next Bleed (Self)",
        "This unit's next attack or ability applies bleed to each unit it damages. Payload: amount (default 3).", {effect_keys::kPayloadAmount}));
    register_effect_definition(make_no_target_effect(
        "grant_movement_speed_self", "Grant Movement Speed (Self)",
        "Grant +N movement speed this turn (Spellbound reactive). Payload: amount (default 1).",
        {effect_keys::kPayloadAmount}));
    register_effect_definition(make_no_target_effect(
        "grant_magus_charge_self", "Grant Arcane Charge (Self)",
        "Grant Arcane Charge stacks on the source unit (Spellbound reactive). Payload: amount (default 1).", {effect_keys::kPayloadAmount}));
    register_effect_definition(make_no_target_effect(
        "grant_mana_pylon_charge_self", "Grant Mana Pylon Charge (Self)",
        "Grant Mana Pylon charge stacks on the source structure. Payload: amount (default 1).",
        {effect_keys::kPayloadAmount}));
    register_effect_definition(make_no_target_effect(
        "grant_seraphina_resonance_self", "Grant Seraphina Resonance (Self)",
        "Spellbound: +N Resonance; at threshold create a temporary hand spell. Payload: amount, threshold, hand_expires_after_owner_turn_ends; string grant_card_key.",
        {effect_keys::kPayloadAmount, "threshold", "hand_expires_after_owner_turn_ends"}));
    // Last Gasp reactive effects (reactive_trigger: self_died).
    register_effect_definition(make_no_target_effect(
        "draw_cards_owner", "Draw Cards (Owner)",
        "Last Gasp: draw N cards for the unit's owner. Payload: amount (default 1).",
        {effect_keys::kPayloadAmount}));
    register_effect_definition(make_no_target_effect(
        "deal_damage_to_all_enemies_nearby", "Deal Damage to All Enemies Nearby",
        "Last Gasp: deal N damage to all enemy units in the 8 surrounding cells of the dying unit's tile. Payload: amount (default 1).",
        {effect_keys::kPayloadAmount}));
    {
        // Unified directional damage effect. Uses string_payload["shape"] to pick AoE shape.
        EffectDefinition dd;
        dd.key = "directional_damage";
        dd.display_name = "Directional Damage";
        dd.rules_text =
            "Deal damage to all units and structures in a directional area. Target cell sets direction (8-way). "
            "Payload: amount, shape (rectangle|helix|...), max_range, width, depth.";
        dd.target.domain = TargetDomain::BoardEntityCell;
        dd.target.requirement = TargetRequirement::Required;
        dd.target.board_target_kind = BoardTargetKind::Any;
        dd.target.area_effect = true;
        dd.deals_damage = true;
        dd.uses_directional_aim = true;
        dd.payload_keys = {effect_keys::kPayloadAmount, "max_range", "width", "depth"};
        register_effect_definition(std::move(dd));
    }
    {
        EffectDefinition ps;
        ps.key = "piercing_shot";
        ps.display_name = "Piercing Shot";
        ps.rules_text =
            "Fire a shot in one octilinear direction up to max_range. Hits every unit and structure along the ray. Does not damage bases. "
            "Target cell sets direction.";
        ps.target.domain = TargetDomain::BoardEntityCell;
        ps.target.requirement = TargetRequirement::Required;
        ps.target.board_target_kind = BoardTargetKind::Any;
        ps.target.area_effect = true;
        ps.deals_damage = true;
        ps.uses_directional_aim = true;
        ps.payload_keys = {effect_keys::kPayloadAmount, "max_range"};
        register_effect_definition(std::move(ps));
    }
    register_effect_definition(make_board_entity_effect(
        "apply_poison", "Apply Poison", "Apply poison stacks to any entity that can receive poison.", BoardTargetKind::Any, TargetRequirement::Required,
        {effect_keys::kPayloadAmount}));
    register_effect_definition(make_board_entity_effect(
        "apply_fire", "Apply Fire", "Apply fire stacks to any entity.", BoardTargetKind::Any, TargetRequirement::Required, {effect_keys::kPayloadAmount}));
    register_effect_definition(make_board_entity_effect(
        "apply_bleed", "Apply Bleed", "Apply bleed stacks to any entity that can receive bleed.", BoardTargetKind::Any, TargetRequirement::Required,
        {effect_keys::kPayloadAmount}));
    register_effect_definition(make_board_entity_effect(
        "apply_silenced", "Apply Silenced",
        "Apply Silenced to any entity. While silenced, keywords, passives, and buffs are disabled. Focus spells may still be cast from that unit.",
        BoardTargetKind::Any,
        TargetRequirement::Required, {effect_keys::kPayloadAmount}));
    register_effect_definition(make_board_entity_effect(
        "apply_silenced_owner_turn_end", "Apply Silenced Until Owner Turn End",
        "Apply Silenced to any entity until the end of that entity owner's turn (keywords, passives, and buffs disabled; focus casting allowed).",
        BoardTargetKind::Any, TargetRequirement::Required, {effect_keys::kPayloadAmount}));
    register_effect_definition(make_board_entity_effect(
        "apply_stealth", "Apply Stealth",
        "Apply Stealth stacks. Stealthed units cannot be directly targeted by enemy attacks or abilities; area effects still apply. Attacking removes all "
        "stealth. Lose 1 stack at the start of your turn.",
        BoardTargetKind::Ally, TargetRequirement::Required, {effect_keys::kPayloadAmount}));
    register_effect_definition(make_board_entity_effect(
        "apply_jammed", "Apply Jammed",
        "Apply Jammed stacks to any entity. While jammed, the entity cannot use activated abilities. Lose 1 stack at the end of its owner's turn.",
        BoardTargetKind::Any, TargetRequirement::Required, {effect_keys::kPayloadAmount}));
    register_effect_definition(make_board_entity_effect(
        "apply_rooted", "Apply Rooted",
        "Apply Rooted stacks to any entity. While rooted, the entity cannot move. Lose 1 stack at the start of its owner's turn.",
        BoardTargetKind::Any, TargetRequirement::Required, {effect_keys::kPayloadAmount}));
    register_effect_definition(make_board_entity_effect(
        "apply_stunned", "Apply Stunned",
        "Apply Stunned stacks. While stunned, the entity cannot move, attack, use abilities, or cast focus spells. Lose 1 stack at the start of its owner's turn.",
        BoardTargetKind::Any, TargetRequirement::Required, {effect_keys::kPayloadAmount}));
    {
        EffectDefinition pu = make_board_entity_effect(
            "push_unit", "Push Unit",
            "Push a unit `amount` tiles along a chosen direction. Abilities with a source entity push away from "
            "that source unless `dir_x`/`dir_y` override. Spells use push-direction aim: pick a unit, then an "
            "adjacent direction cell (`aim_x`/`aim_y`); `cardinal_only` restricts to orthogonal directions. "
            "Traversal applies terrain enter damage and tile overlay debuffs; void along the path destroys the unit. "
            "Blocked by another entity or off-board wall (default): mover and blocker each take 1 + remaining push "
            "tiles damage (wall: mover only). Off-map cells default to wall collision; per-coordinate void edges via "
            "BoardLayoutSpec.off_board_cells. Respects Immovable unless bypassed by Crushing Advance crush-push.",
            BoardTargetKind::Any, TargetRequirement::Required, {effect_keys::kPayloadAmount, "dir_x", "dir_y", "cardinal_only"});
        pu.uses_push_direction_aim = true;
        register_effect_definition(std::move(pu));
    }
    {
        EffectDefinition hs = make_board_entity_effect(
            "hookshot_pull", "Hookshot Pull",
            "Deal damage to a target unit and pull it toward the caster along a straight line (orthogonal or diagonal only). "
            "The pull continues until the target is adjacent to the caster or blocked. Payload: amount (damage, default 3), max_range (default 4).",
            BoardTargetKind::Enemy, TargetRequirement::Required, {effect_keys::kPayloadAmount, "max_range"});
        hs.deals_damage = true;
        register_effect_definition(std::move(hs));
    }
    register_effect_definition(make_board_entity_effect(
        "apply_overload", "Apply Overload",
        "Apply Overload stacks. Stacks do not decay. At 3 stacks, the entity explodes: deals 5 damage to itself and all adjacent entities (armor and shields apply). Clears all Overload on explosion.",
        BoardTargetKind::Any, TargetRequirement::Required, {effect_keys::kPayloadAmount}));
    register_effect_definition(make_no_target_effect(
        "apply_overload_self", "Apply Overload (Self)",
        "Apply Overload stacks to the source unit. No board target required.",
        {effect_keys::kPayloadAmount}));
    register_effect_definition(make_board_entity_effect(
        "grant_on_damage_apply_overload_adjacent", "Grant Next-Attack Overload (Ally)",
        "Boost: target allied unit or structure's next attack or ability applies 1 overload to each entity it damages. Consumed after firing.",
        BoardTargetKind::Ally, TargetRequirement::Required, {}));
    register_effect_definition(make_board_entity_effect(
        "grant_on_damage_apply_jammed_adjacent", "Grant Next-Attack Jammed (Ally)",
        "Boost: target allied unit or structure's next attack or ability applies 1 jammed to each entity it damages. Consumed after firing.",
        BoardTargetKind::Ally, TargetRequirement::Required, {}));
    register_effect_definition(make_board_entity_effect(
        "grant_medical_override", "Grant Medical Override",
        "Grant a primer until end of target owner's turn: this unit's deal_damage abilities heal allied targets "
        "for the damage amount instead of damaging them. Enemy targets are still damaged normally. "
        "Mutually exclusive with other on-damage primers.",
        BoardTargetKind::Ally, TargetRequirement::Required, {}));
    register_effect_definition(make_no_target_effect(
        "grant_next_ability_movement_reduction_self", "Grant Next-Ability Movement Reduction (Self)",
        "Prime this unit: its next damaging ability reduces 1 movement to each unit it damages until end of that unit's owner's turn.",
        {}));
    register_effect_definition(make_no_target_effect(
        "grant_next_ability_rooted_self", "Grant Next-Ability Rooted (Self)",
        "Prime this unit: its next damaging ability roots each unit it damages.",
        {}));
    register_effect_definition(make_board_entity_effect(
        "grant_next_ability_movement_reduction_ally", "Grant Next-Ability Movement Reduction (Ally)",
        "Prime target allied unit (or self): its next damaging ability reduces 1 movement to each unit it damages until end of that unit's owner's turn.",
        BoardTargetKind::Ally, TargetRequirement::Required, {}));
    register_effect_definition(make_board_entity_effect(
        "grant_next_ability_rooted_ally", "Grant Next-Ability Rooted (Ally)",
        "Prime target allied unit (or self): its next damaging ability roots each unit it damages.",
        BoardTargetKind::Ally, TargetRequirement::Required, {}));
    register_effect_definition(make_board_entity_effect(
        "remove_silenced", "Remove Silenced", "Remove Silenced stacks from any entity.", BoardTargetKind::Any, TargetRequirement::Required,
        {effect_keys::kPayloadAmount}));
    register_effect_definition(make_no_target_effect(
        "draw_cards", "Draw Cards", "Draw cards.", {effect_keys::kPayloadAmount}));
    register_effect_definition(make_player_seat_effect(
        "draw_cards_for_player", "Draw Cards For Player",
        "Choose a player seat; that player draws cards from their deck.",
        {effect_keys::kPayloadAmount}));
    register_effect_definition(make_player_seat_effect(
        "move_to_purgatory", "Move To Purgatory",
        "Choose a player seat; move the card with string_payload card_public_id from any zone into that player's purgatory "
        "(exile-like; not drawable).",
        {"card_public_id"}));
    register_effect_definition(make_no_target_effect(
        "draw_unit_cards", "Draw Unit Cards",
        "Draw up to `amount` cards whose definition type is unit. Ineligible cards remain in deck order.",
        {effect_keys::kPayloadAmount}));
    register_effect_definition(make_no_target_effect(
        "draw_spell_cards", "Draw Spell Cards",
        "Draw up to `amount` spell cards with total energy cost <= `max_total_cost`. Ineligible cards remain in deck order.",
        {effect_keys::kPayloadAmount, "max_total_cost"}));
    register_effect_definition(make_no_target_effect(
        "draw_focus_spell_cards", "Draw Focus Spell Cards",
        "Draw up to `amount` focus spell cards with total energy cost <= `max_total_cost`.",
        {effect_keys::kPayloadAmount, "max_total_cost"}));
    register_effect_definition(make_no_target_effect(
        "optional_discard_draw", "Optional Discard Draw",
        "You may discard a card from hand to draw a card (pending choice).",
        {}));
    register_effect_definition(make_no_target_effect(
        "scan", "Scan",
        "Peek at the top `amount` cards of your deck. Choose any of them to discard; the rest stay on top in order.",
        {effect_keys::kPayloadAmount}));
    register_effect_definition(make_no_target_effect(
        "gain_turquoise", "Gain Turquoise",
        "Gain turquoise floating energy. Optional string_payload[\"pool\"] routes to a tagged pool (e.g. \"spell_ability\" for flux energy).",
        {effect_keys::kPayloadAmount}));
    register_effect_definition(make_no_target_effect(
        "gain_turquoise_if_enemy_within", "Gain Turquoise If Enemy Within",
        "If a hostile unit is within `range` tiles (Chebyshev), gain turquoise via gain_turquoise routing.",
        {effect_keys::kPayloadAmount, "range"}));
    register_effect_definition(make_board_entity_effect(
        "grant_delayed_next_damage_bonus", "Grant Delayed Next Damage Bonus",
        "Store a pending next_damage_bonus on the target unit that is granted at the start of their owner's next turn. "
        "Payload: amount (default 8).",
        BoardTargetKind::Ally, TargetRequirement::Required, {effect_keys::kPayloadAmount}));
    register_effect_definition(make_board_entity_effect(
        "shocking_stimulus_aoe", "Shocking Stimulus AoE",
        "Target a board cell. Apply overload stacks and a temporary movement speed boost to all allied units in a 4x4 area centered on that cell. "
        "Boost lasts until owner_turn_end. Payload: overload (default 1), movement (default 1).",
        BoardTargetKind::Ally, TargetRequirement::Required, {"overload", "movement"}));
    {
        auto pr = make_board_entity_effect(
            "dynamic_deployable_replicate", "Dynamic Deployable Duplicator",
            "Focus. Deal damage equal to half the caster's attack roll to the target, then spawn a "
            "Replicator Bot adjacent to the target inheriting the caster's keywords. "
            "Bot HP and melee damage equal damage dealt (3-point range). Bot spawns stunned.",
            BoardTargetKind::Enemy, TargetRequirement::Required, {});
        pr.deals_damage = true;
        register_effect_definition(std::move(pr));
    }
    register_effect_definition(make_no_target_effect(
        "extend_aura_range", "Extend Aura Range",
        "Temporarily extend this unit's range-limited passive aura radius by the payload 'amount' (default 1). "
        "Duration: payload 'remaining_turns' (default 1) counted against string_payload 'expire_on' "
        "(default 'owner_turn_start'). Stacks on repeated Barrage casts.",
        {"amount", "remaining_turns"}));
    register_effect_definition(make_no_target_effect(
        "grant_permanent_aura_ability_damage", "Grant Permanent Aura Ability Damage",
        "Permanently increase bonus_ability_damage on all allied_units passives on the source entity. "
        "Payload: 'amount' (default 1). Used by Press the Advantage (The Boss).",
        {"amount"}));
    register_effect_definition(make_no_target_effect(
        "amplify_aura_stats", "Amplify Aura Stats",
        "Temporarily boost the stat grants of this unit's allied-unit auras. "
        "Payload: 'attack' (default 0), 'health' (default 0), 'ability_damage' (default 0). "
        "Expires at owner_turn_start.",
        {"attack", "health", "ability_damage"}));
    register_effect_definition(make_board_entity_effect(
        "apply_covering_fire", "Covering Fire",
        "Mark an allied unit: if an enemy attacks that unit this turn, the source unit reacts "
        "and fires a retaliatory shot at the attacker (after any counterattack). "
        "The marker expires at the start of the covered unit's owner's next turn.",
        BoardTargetKind::Ally, TargetRequirement::Required, {}));
    register_effect_definition(make_board_entity_effect(
        "apply_covering_fire_range_buff", "Cover Shot",
        "Mark an allied unit with covering fire (same rules as Covering Fire) and grant the "
        "caster +1 ranged range until the start of their next turn.",
        BoardTargetKind::Ally, TargetRequirement::Required, {}));
    register_effect_definition(make_board_entity_effect(
        "apply_covering_fire_stacks", "Overwatch",
        "Apply N covering fire stacks to the target ally. Each stack triggers one reaction shot "
        "when an enemy attacks that unit. Stacks expire at the start of the covered unit's owner's next turn.",
        BoardTargetKind::Ally, TargetRequirement::Required, {effect_keys::kPayloadAmount}));
    register_effect_definition(make_no_target_effect(
        "grant_stealth_self", "Go Dark",
        "Grant the caster stealth stacks. Stealthed units cannot be directly targeted by enemy "
        "attacks or abilities. Attacking removes all stealth stacks.",
        {effect_keys::kPayloadAmount}));
    register_effect_definition(make_no_target_effect(
        "heal_surrounding_allies", "Heal Surrounding Allies",
        "Heal all allied units in the 8-way surrounding cells of the source unit for `amount` HP each. "
        "Payload: amount (default 2).",
        {effect_keys::kPayloadAmount}));
    {
        auto cd = make_board_entity_effect(
            "cleanse", "Cleanse",
            "Remove all negative status stacks (poison, fire, bleed, silenced, jammed, rooted, stunned, overload) "
            "from the target allied unit.",
            BoardTargetKind::Ally, TargetRequirement::Required, {});
        register_effect_definition(std::move(cd));
    }
    register_effect_definition(make_board_entity_effect(
        "triage", "Triage",
        "Remove one randomly chosen negative status debuff (all stacks of that type) from the target allied unit.",
        BoardTargetKind::Ally, TargetRequirement::Required, {}));
    register_effect_definition(make_board_entity_effect(
        "heal_and_triage", "Heal and Triage",
        "Heal the target allied unit for `amount` HP (default 3), then remove one randomly chosen negative status debuff (all stacks of that type).",
        BoardTargetKind::Ally, TargetRequirement::Required, {effect_keys::kPayloadAmount}));
    register_effect_definition(make_no_target_effect(
        "grant_bonus_attack", "Grant Bonus Attack",
        "Grant this unit one additional attack action this turn. "
        "No board target required; uses source entity.",
        {}));
    register_effect_definition(make_no_target_effect(
        "spawn_conscripts_adjacent", "Spawn Conscripts Adjacent",
        "Spawn `count` Conscript tokens (Hybrid 2–4 / 2–4 range 2, 4 HP, movement 3) on random "
        "unoccupied surrounding cells of the source unit. Tokens receive deployment fatigue. "
        "Payload: count (default 2).",
        {"count"}));
    register_effect_definition(make_no_target_effect(
        "spawn_flame_trooper_adjacent", "Spawn Flame Trooper Adjacent",
        "Spawn 1 Flame Trooper token (Hybrid 5–8 / 2–5 range 2, 9 HP, movement 2) on a random "
        "unoccupied surrounding cell of the source unit. Token receives deployment fatigue.",
        {}));
    // second_wave: no board target - all stockpile cards in the controller's deck may be played
    // twice this turn instead of once. Each card tracks its own double-play consumption via
    // stockpile_double_play_used_this_turn. Cleared by refresh_turn_limited_card_attributes.
    register_effect_definition(make_no_target_effect(
        "second_wave", "Second Wave",
        "Until the end of this turn, each stockpile card you control may be deployed or played "
        "a second time. The once-per-turn restriction is lifted to two per turn.",
        {}));
    // mobilize: no board target - grants the caster's controller a per-unit deploy discount this turn.
    register_effect_definition(make_no_target_effect(
        "mobilize", "Mobilize",
        "Until end of turn, every unit you deploy costs 1 less neutral energy (minimum 0). "
        "Units with only colored/faction energy in their cost are unaffected. "
        "Payload: amount (neutral discount per unit, default 1).",
        {effect_keys::kPayloadAmount}));
    register_effect_definition(make_no_target_effect(
        "core_cracker_prime", "Prime Core",
        "Unlock a Core Cracker for this turn cycle (move, attack, abilities, reactions). "
        "Must be paid at the start of each of your turns except the deployment turn.",
        {}));
    {
        // core_cracker_breach: deal 15 damage to an adjacent enemy base, then destroy self.
        // Explicitly exempt from entity_immune_to_all_effects so bases can be targeted.
        // Payload: amount (damage dealt, default 15).
        auto ccb = make_board_entity_effect(
            "core_cracker_breach", "Breaching Charge",
            "Deal 15 damage to an adjacent enemy base, bypassing base immunity. "
            "The Core Cracker is destroyed after firing. Payload: amount (default 15).",
            BoardTargetKind::Enemy, TargetRequirement::Required, {effect_keys::kPayloadAmount});
        ccb.deals_damage = true;
        register_effect_definition(std::move(ccb));
    }
    {
        // bunker_buster_strike: single-target damage against an enemy base or structure.
        // Explicitly exempt from entity_immune_to_all_effects (bases can be targeted).
        // Payload: amount (damage dealt, default 5).
        auto bbs = make_board_entity_effect(
            "bunker_buster_strike", "Bunker Buster Strike",
            "Deal damage to an adjacent enemy base or structure. "
            "Bypasses the normal base effect immunity - bases can be targeted directly. "
            "Payload: amount (default 5).",
            BoardTargetKind::Enemy, TargetRequirement::Required, {effect_keys::kPayloadAmount});
        bbs.deals_damage = true;
        register_effect_definition(std::move(bbs));
    }
    register_effect_definition(make_no_target_effect(
        "grant_evasive_self", "Grant Evasive",
        "Grant Evasive stacks to the source unit. While any stacks remain, attacks targeting it have a 50% miss chance. Each stack lasts one owner turn (lose 1 at turn start). Evasive misses whiff with no body-block.",
        {"amount"}));
    {
        EffectDefinition hf;
        hf.key = "healing_flight";
        hf.display_name = "Healing Flight";
        hf.rules_text = "Fly orthogonally [min_range, max_range]. Drop heal pickups (amount, default 4) on each empty intermediate tile. Land on empty or pickup cell.";
        hf.payload_keys = {effect_keys::kPayloadAmount, "max_range", "min_range"};
        hf.uses_directional_aim = true;
        hf.movement_landing = true;
        register_effect_definition(std::move(hf));
    }
    {
        // bombing_run: Skyreaver dashes along a cardinal ray, damaging up to 2 enemies in
        // intermediate cells, then moves to the chosen landing cell. The landing cell must be
        // empty or pickup-occupied (pickups are collected on landing).
        // movement_landing = true: targeting shows every passable cell at [min_range, max_range]
        // per direction rather than just the farthest, so the player can pick any valid distance.
        // Payload: amount (damage per hit, default 3), max_range (default 3), min_range (default 2).
        EffectDefinition br;
        br.key = "bombing_run";
        br.display_name = "Bombing Run";
        br.rules_text = "Dash along a cardinal ray. Deal `amount` damage to up to 2 enemy units in "
                        "intermediate cells. Land on any empty (or pickup) tile at distance "
                        "[min_range, max_range]. Pickups are collected on landing. "
                        "Payload: amount (default 3), max_range (default 3), min_range (default 2).";
        br.payload_keys = {effect_keys::kPayloadAmount, "max_range", "min_range"};
        br.deals_damage = true;
        br.uses_directional_aim = true;
        br.movement_landing = true;
        br.default_board_target_kind = BoardTargetKind::Enemy;
        register_effect_definition(std::move(br));
    }
    {
        EffectDefinition tcs;
        tcs.key = "terra_cone_strike";
        tcs.display_name = "Terra Cone Strike";
        tcs.rules_text = "Choose an adjacent direction. Deal `amount` damage to enemies on that "
                         "cell, then `splash_amount` damage to enemies on the three cells behind "
                         "it (side-by-side row). Payload: amount (default 6), splash_amount "
                         "(default 3), max_range (default 1).";
        tcs.payload_keys = {effect_keys::kPayloadAmount, "splash_amount", "max_range"};
        tcs.deals_damage = true;
        tcs.uses_directional_aim = true;
        tcs.default_board_target_kind = BoardTargetKind::Enemy;
        register_effect_definition(std::move(tcs));
    }
    {
        EffectDefinition td;
        td.key = "terra_dash";
        td.display_name = "Terra Dash";
        td.rules_text = "Move to an empty (or pickup) cell exactly `exact_range` tiles away on "
                        "an octilinear ray. Pickups are collected on landing. "
                        "Payload: exact_range (default 3).";
        td.payload_keys = {"exact_range"};
        td.uses_directional_aim = true;
        td.movement_landing = true;
        td.default_board_target_kind = BoardTargetKind::Own;
        register_effect_definition(std::move(td));
    }
    // heal_self: heal the source entity (no board target). Used as the pickup_effect_key for
    // supply-drop pickups - when collected, the collecting unit is the source_entity_id and
    // is healed for `amount` HP. Payload: amount (default 4).
    register_effect_definition(make_no_target_effect(
        "heal_self", "Heal Self",
        "Heal the source entity for `amount` HP. Typically used as a pickup effect key - "
        "the collecting unit is healed when the pickup is picked up.",
        {effect_keys::kPayloadAmount}));
    {
        EffectDefinition edc;
        edc.key = "grant_aoe_stat_buff_turn_end";
        edc.display_name = "Grant AoE Stat Buff (Turn End)";
        edc.rules_text =
            "Grant allied units in a square area around the target cell +attack/+health until the end of the "
            "active turn. Payload: attack (default 3), health (default 3), radius (default 1 = 3x3).";
        edc.payload_keys = {"attack", "health", "radius"};
        edc.default_board_target_kind = BoardTargetKind::Own;
        register_effect_definition(std::move(edc));
    }
    register_effect_definition(make_board_entity_effect(
        "grant_damage_buff_turn_end", "Grant Damage Buff (Turn End)",
        "Grant target allied unit +amount attack damage until the end of the active turn.",
        BoardTargetKind::Ally, TargetRequirement::Required, {effect_keys::kPayloadAmount}));
    {
        EffectDefinition gs;
        gs.key = "gas_strike";
        gs.display_name = "Gas Strike";
        gs.rules_text = "Place a gas cloud overlay on the target tile and its 4 cardinal neighbors (5-tile cross). "
                        "Units on those tiles take 1 Poison immediately. "
                        "At the end of each unit owner's turn, units standing on a gas cloud tile take 1 Poison.";
        gs.target.domain = TargetDomain::BoardEntityCell;
        gs.target.requirement = TargetRequirement::Required;
        gs.target.board_target_kind = BoardTargetKind::Any;
        gs.default_board_target_kind = BoardTargetKind::Any;
        register_effect_definition(std::move(gs));
    }
    {
        EffectDefinition ss;
        ss.key = "scorching_sphere";
        ss.display_name = "Scorching Sphere";
        ss.rules_text =
            "Choose a board cell. Deal damage to all units in a 13-tile area (3×3 square plus one tile "
            "beyond the center of each edge). Apply Fire to every unit in the area. Place fire overlays "
            "on unoccupied tiles in the area.";
        ss.target.domain = TargetDomain::BoardEntityCell;
        ss.target.requirement = TargetRequirement::Required;
        ss.target.board_target_kind = BoardTargetKind::Any;
        ss.target.area_effect = true;
        ss.default_board_target_kind = BoardTargetKind::Any;
        ss.deals_damage = true;
        ss.payload_keys = {effect_keys::kPayloadAmount, "fire", "duration"};
        register_effect_definition(std::move(ss));
    }
    {
        EffectDefinition gg;
        gg.key = "gas_grenade";
        gg.display_name = "Gas Grenade";
        gg.rules_text = "Range 3. Place a gas cloud on the target tile and its two cleave flank tiles (3 tiles). "
                        "Units on those tiles take 1 Poison immediately. "
                        "Reapplying gas keeps the longer remaining duration; different overlays replace gas.";
        gg.target.domain = TargetDomain::BoardEntityCell;
        gg.target.requirement = TargetRequirement::Required;
        gg.target.board_target_kind = BoardTargetKind::Any;
        gg.default_board_target_kind = BoardTargetKind::Any;
        gg.payload_keys = {"duration"};
        register_effect_definition(std::move(gg));
    }
    {
        EffectDefinition gcd;
        gcd.key = "gas_chain_detonate";
        gcd.display_name = "Gas Detonation";
        gcd.rules_text = "Burst, range 3. Choose a tile with a gas cloud. Remove that gas, deal 3 damage to units on it, "
                         "then chain to each surrounding tile that also has gas (repeat remove + damage).";
        gcd.default_board_target_kind = BoardTargetKind::Any;
        gcd.payload_keys = {effect_keys::kPayloadAmount};
        register_effect_definition(std::move(gcd));
    }
    {
        EffectDefinition tt;
        tt.key = "true_transformation";
        tt.display_name = "True Transformation";
        tt.rules_text = "Permanently gain Multistrike 1 and Lifesteal and disable a named passive on self. Payload string: grant (default multistrike:1,lifesteal), suppress_passive (passive key).";
        register_effect_definition(std::move(tt));
    }
    {
        EffectDefinition tr;
        tr.key = "place_trench";
        tr.display_name = "Place Trench";
        tr.rules_text = "Turn an unmodified board tile on or Chebyshev-adjacent to a friendly unit into a trench. "
                        "Trench tiles cost 1.5 movement to enter. Non-flying 1x1 units on a trench gain +1 armor "
                        "(subject to the normal armor cap).";
        tr.default_board_target_kind = BoardTargetKind::Own;
        register_effect_definition(std::move(tr));
    }
    {
        // place_supply_drop: drop a heal pickup on any empty board cell within range.
        // The placed pickup heals `amount` HP when collected by any unit that steps on it.
        // targets_empty_cell = true: targeting scans empty cells in range rather than entities.
        EffectDefinition sd;
        sd.key = "place_supply_drop";
        sd.display_name = "Place Supply Drop";
        sd.rules_text = "Drop a supply pickup on any empty cell within Chebyshev range `range_max`. "
                        "The first unit to step on the pickup is healed for `amount` HP. "
                        "Payload: amount (heal amount, default 4).";
        sd.payload_keys = {effect_keys::kPayloadAmount};
        sd.targets_empty_cell = true;
        sd.default_board_target_kind = BoardTargetKind::Own;
        register_effect_definition(std::move(sd));
    }
    {
        EffectDefinition sw;
        sw.key = "spawn_shock_wire_adjacent";
        sw.display_name = "Spawn Shock Wire Adjacent";
        sw.rules_text = "Place a Shock Wire structure (no attacks, Shock Retaliation) on an empty cell Chebyshev-adjacent to the source. Payload: amount (HP, default 2).";
        sw.payload_keys = {effect_keys::kPayloadAmount};
        sw.targets_empty_cell = true;
        sw.default_board_target_kind = BoardTargetKind::Own;
        register_effect_definition(std::move(sw));
    }

    register_effect_definition(make_no_target_effect(
        "spawn_card_unit_deploy_zone", "Spawn Card Unit in Deploy Zone",
        "Spawn a token copy of the unit card named in string_payload card_key in the first free deploy-zone cell.",
        {"card_key"}));
    register_effect_definition(make_no_target_effect(
        "grant_player_base_heal", "Grant Player Base Heal",
        "Heal your player base for payload amount.",
        {effect_keys::kPayloadAmount}));
    register_effect_definition(make_no_target_effect(
        "grant_player_base_bonus_health", "Grant Player Base Bonus Health",
        "Grant your player base temporary bonus health (absorbed before real HP).",
        {effect_keys::kPayloadAmount}));
    register_effect_definition(make_no_target_effect(
        "grant_player_base_max_health", "Grant Player Base Max Health",
        "Increase your player base max and current HP by payload amount.",
        {effect_keys::kPayloadAmount}));
    register_effect_definition(make_no_target_effect(
        "spawn_deck_units_deploy_zone", "Spawn Deck Units in Deploy Zone",
        "Spawn units from the top deck_top cards (default 10) whose combined mana cost equals mana_budget "
        "(default 12), or the highest exact total below that if impossible. Units are placed on random valid "
        "tiles in your base deployment zone, stunned. Stockpile charges are consumed; cards with stockpile "
        "remaining stay in the deck.",
        {"mana_budget", "deck_top"}));
    register_effect_definition(make_no_target_effect(
        "summon_on_deployment_zone", "Summon on Deployment Zone",
        "Spawn hybrid Conscript tokens (2–4 damage, 4 HP, movement 3) on every unoccupied cell "
        "of your base deployment zone, up to max_count. Each conscript receives deployment fatigue.",
        {"max_count"}));
    register_effect_definition(make_board_entity_effect(
        "apply_vulnerable", "Apply Vulnerable",
        "Apply Vulnerable stacks to the target. Each stack increases incoming damage from "
        "attacks and abilities by 1 (DoTs are unaffected). Stacks decay at the end of the "
        "target's owner's turn. Payload: amount (stacks, default 1).",
        BoardTargetKind::Any, TargetRequirement::Required, {effect_keys::kPayloadAmount}));
    register_effect_definition(make_board_entity_effect(
        "apply_vulnerable_turn_end", "Apply Vulnerable (Turn End)",
        "Apply Vulnerable stacks that expire at the end of the active turn when cast (supports Fast on an opponent's turn). "
        "Each stack increases incoming attack/ability damage by 1. Payload: amount (default 1).",
        BoardTargetKind::Enemy, TargetRequirement::Required, {effect_keys::kPayloadAmount}));
    {
        auto rel_atk = make_board_entity_effect(
            "focus_relentless_attack", "Focus Relentless Attack",
            "Focus spell: the caster attacks a target enemy unit using its normal attack profile. "
            "The caster gains Relentless X only for this attack sequence (payload amount, default 3). "
            "Does not consume the unit's normal attack action for the turn.",
            BoardTargetKind::Enemy, TargetRequirement::Required, {effect_keys::kPayloadAmount});
        rel_atk.deals_damage = true;
        register_effect_definition(std::move(rel_atk));
    }
    register_effect_definition(make_no_target_effect(
        "grant_relentless_aura", "Grant Relentless Aura",
        "Grant surrounding allied units Relentless X until the end of the current turn. "
        "No board target - affects all living allies in the 8 surrounding cells. "
        "Payload: amount (relentless value, default 3).",
        {effect_keys::kPayloadAmount}));
    register_effect_definition(make_no_target_effect(
        "grant_movement_aura", "Grant Movement Aura",
        "Grant all surrounding allied units +N bonus movement until end of the current owner's turn. "
        "No board target - affects all living allied units in the 8 surrounding cells. "
        "Payload: amount (movement bonus, default 1).",
        {effect_keys::kPayloadAmount}));
    register_effect_definition(make_no_target_effect(
        "grant_damage_aura", "Grant Damage Aura",
        "Grant all surrounding allied units +N melee and ranged damage until end of the current owner's turn. "
        "No board target - affects all living allied units in the 8 surrounding cells. "
        "Payload: amount (damage bonus, default 1).",
        {effect_keys::kPayloadAmount}));
    register_effect_definition(make_board_entity_effect(
        "grant_multistrike_ally", "Grant Multistrike (Ally)",
        "Grant a targeted allied unit Multistrike X until end of the current owner's turn. "
        "Payload: amount (multistrike value, default 1).",
        BoardTargetKind::Ally, TargetRequirement::Required,
        {effect_keys::kPayloadAmount}));
    register_effect_definition(make_no_target_effect(
        "expand_death_shield_range_self", "Expand Death-Shield Range",
        "Permanently expand this entity's Sentinel Veil aura range by amount (default 1) by "
        "applying a never-expiring TemporaryEntityEffect with bonus_aura_range = amount. "
        "Used by Lady Concordia's Love ability. Payload: amount (default 1).",
        {effect_keys::kPayloadAmount}));
    register_effect_definition(make_no_target_effect(
        "swap_weapons", "Swap Weapons",
        "Toggle an alternate attack mode on this unit. First cast applies a persistent 'pistol_mode' "
        "effect that overrides ranged damage range and may suppress/grant keywords. "
        "Second cast removes it (swap back to default). "
        "Payload int: ranged_min, ranged_max, range_bonus. "
        "Payload string: suppress (comma-sep keywords), grant (comma-sep 'key:amount').",
        {"ranged_min", "ranged_max", "range_bonus"}));
    register_effect_definition(make_no_target_effect(
        "apply_dual_wield", "Dual Wield",
        "Removes any pistol_mode and applies a permanent 'dual_wield_mode' effect with overridden "
        "ranged damage range, keyword grants/suppression, and optional ability disables. "
        "Payload int: ranged_min, ranged_max, range_bonus. "
        "Payload string: suppress, grant (same as swap_weapons), disable (comma-sep ability keys).",
        {"ranged_min", "ranged_max", "range_bonus"}));
    register_effect_definition(make_no_target_effect(
        "apply_coordinated_fire", "Apply Coordinated Fire",
        "This unit enters coordinated fire mode for the rest of this turn: whenever any OTHER "
        "friendly unit attacks an enemy, this unit also fires a ranged shot at that enemy. "
        "Payload: amount (int, default 5) - maximum shots per activation. "
        "Each shot goes through the full ranged-attack path and CAN trigger counterattacks, "
        "but fires inline within the triggering attack with no extra spell-reaction window.",
        {effect_keys::kPayloadAmount}));
    {
        auto ag = make_board_entity_effect(
            "aoe_damage_square", "AoE Damage (Square)",
            "Deal flat damage to all entities in a square area around the target cell. "
            "Payload: amount (default 1), radius (default 1 = 3x3 area).",
            BoardTargetKind::Any, TargetRequirement::Required, {effect_keys::kPayloadAmount, "radius"});
        ag.deals_damage = true;
        ag.target.area_effect = true;
        register_effect_definition(std::move(ag));
    }
    {
        // High Explosive Round: LOS ranged shot that detonates in a 3×3 at the actual hit location.
        // Interception may redirect the explosion center to a blocking unit. Ignores LOS on explosion.
        EffectDefinition her;
        her.key          = "high_explosive_round";
        her.display_name = "High Explosive Round";
        her.rules_text   =
            "Fire a shell at a target enemy using LOS. "
            "Units in the shot path may intercept (50% per blocking unit-cell), redirecting the explosion. "
            "Wherever the shell hits, detonate for `amount` damage in a 3×3 area (explosion ignores LOS). "
            "Payload: amount (default 4).";
        her.target.domain           = TargetDomain::BoardEntityCell;
        her.target.requirement      = TargetRequirement::Required;
        her.target.board_target_kind = BoardTargetKind::Enemy;
        her.target.area_effect      = true;
        her.deals_damage            = true;
        her.payload_keys            = {effect_keys::kPayloadAmount};
        register_effect_definition(std::move(her));
    }
    register_effect_definition(make_no_target_effect(
        "artillery_mode", "Artillery Mode",
        "Grant this unit Trueshot, +`range_bonus` ranged range, and +`deadzone_bonus` ranged deadzone "
        "until the start of its owner's next turn. Payload: range_bonus (default 1), deadzone_bonus (default 1).",
        {"range_bonus", "deadzone_bonus"}));
    register_effect_definition(make_no_target_effect(
        "grant_first_strike_self", "Grant First Strike (Self)",
        "Grant First Strike to the source unit until end of this turn (owner_turn_end).",
        {}));
    register_effect_definition(make_no_target_effect(
        "apply_valiant_guard_self", "Valiant Guard (Self)",
        "Prime this unit: the next time a surrounding 1×1 ally is targeted by an attack, swap positions and this unit takes that attack.",
        {}));
    register_effect_definition(make_no_target_effect(
        "apply_barrier_self", "Barrier (Self)",
        "Grant the source unit +`amount` Barrier until end of this turn (blocks one damage instance per stack). Payload: amount (default 1).",
        {effect_keys::kPayloadAmount}));
    register_effect_definition(make_no_target_effect(
        "grant_cleave_self", "Grant Cleave (Self)",
        "Grant Cleave to the source unit until end of this turn (owner_turn_end).",
        {}));
    register_effect_definition(make_no_target_effect(
        "whirlwind_spray", "Whirlwind Spray",
        "Grant Whirlwind and apply range_bonus to ranged range until end of this turn "
        "(owner_turn_end). Payload: range_bonus (default -1).",
        {"range_bonus"}));
    register_effect_definition(make_no_target_effect(
        "grant_permanent_stat_growth_self", "Grant Permanent Stat Growth (Self)",
        "Permanently increase this unit's max HP and melee/ranged damage.", {effect_keys::kPayloadAmount, "health", "attack"}));
    register_effect_definition(make_board_entity_effect(
        "grant_permanent_stat_growth", "Grant Permanent Stat Growth",
        "Permanently increase target unit's max HP and melee/ranged damage.",
        BoardTargetKind::Any, TargetRequirement::Required, {effect_keys::kPayloadAmount, "health", "attack"}));
    register_effect_definition(make_board_entity_effect(
        "grant_stockpile_to_unit", "Grant Stockpile",
        "Grant target allied unit's source card Stockpile `amount` (default 2). Moves the card from in_play back to deck when granted.",
        BoardTargetKind::Ally, TargetRequirement::Required, {effect_keys::kPayloadAmount}));
    register_effect_definition(make_board_entity_effect(
        "grant_stat_growth_draw", "Grant Stat Growth + Draw",
        "Permanently grant target allied unit +'attack' damage and +'health' max HP, then draw 'draw' cards.",
        BoardTargetKind::Ally, TargetRequirement::Required, {"attack", "health", "draw"}));
    register_effect_definition(make_board_entity_effect(
        "liquid_data", "Liquid Data",
        "If target ally has deployment fatigue, grant it the surge attribute (may attack and use abilities this turn). "
        "Otherwise, refresh all abilities (resets ability uses remaining and barrage cast counts).",
        BoardTargetKind::Ally, TargetRequirement::Required, {}));
    register_effect_definition(make_board_entity_effect(
        "grant_reactive_armor", "Reactive Armor",
        "Grant target allied unit +1 Armor immediately; each time it takes damage, gain another +1 stacking "
        "Armor (cap 5). All armor from this ability is removed at the start of that unit's owner's next turn.",
        BoardTargetKind::Ally, TargetRequirement::Required, {}));
    register_effect_definition(make_board_entity_effect(
        "grant_bonus_move", "Grant Bonus Move",
        "Grant target unit extra move actions (persists until end of that unit's owner's turn). "
        "Optional overload applied as a side-effect. Can target ally or enemy. "
        "Payload: amount (default 1), overload (default 0).",
        BoardTargetKind::Any, TargetRequirement::Required,
        {"amount", "overload"}));
    register_effect_definition(make_no_target_effect(
        "gain_neutral", "Gain Neutral", "Gain neutral floating energy.", {effect_keys::kPayloadAmount}));
    register_effect_definition(make_no_target_effect(
        "gain_omni_flux", "Gain Omni Flux", "Gain Omni flux energy (payload \"amount\").",
        {effect_keys::kPayloadAmount}));
    register_effect_definition(make_no_target_effect(
        "spawn_requisition_trooper", "Spawn Requisition Trooper",
        "Spawn a Requisition Trooper (4 HP, 2-4 melee, 3 move) in your deployment zone.", {}));
    register_effect_definition(make_no_target_effect(
        "gain_orange", "Gain Orange",
        "Gain orange floating energy. Optional string_payload[\"pool\"] routes to a tagged pool (e.g. \"spell_ability\").",
        {effect_keys::kPayloadAmount}));
    {
        auto mortar = make_no_target_effect(
            "mortar_barrage", "Mortar Barrage",
            "Automated building passive: pick a random enemy unit in range [min_range, max_range] (Chebyshev, "
            "deadzone applied), deal `damage` to it and all entities on adjacent cells, apply `overload` stacks. "
            "Payload: damage, overload, min_range, max_range.",
            {"damage", "overload", "min_range", "max_range"});
        mortar.deals_damage = true;
        register_effect_definition(std::move(mortar));
    }
    register_effect_definition(make_stack_item_effect(
        "counter_spell", "Counter Spell", "Counter target spell or ability in the phase batch queue.", {"spell", "ability"}));
    register_effect_definition(make_stack_item_effect(
        "copy_allied_spell", "Copy Allied Spell",
        "Copy an allied batched spell (same total energy cost as chosen X). Queues a duplicate stack item with the same effect, targets, and speed.",
        {"spell", "focus_spell"}));
    {
        auto charm = make_board_entity_effect(
            "stun_and_damage", "Stun and Damage",
            "Deal damage to a target unit, then apply Stunned stacks. Payload: amount (damage, default 3), stun (stacks, default 1).",
            BoardTargetKind::Enemy, TargetRequirement::Required, {effect_keys::kPayloadAmount, "stun"});
        charm.deals_damage = true;
        register_effect_definition(std::move(charm));
    }
    {
        auto bolt = make_board_entity_effect(
            "magus_charge_strike", "Arcane Charge Strike",
            "Consume all Arcane Charge on the source unit. Deal damage equal to the charges consumed to an enemy unit or structure.",
            BoardTargetKind::Enemy, TargetRequirement::Required, {});
        bolt.deals_damage = true;
        register_effect_definition(std::move(bolt));
    }
    {
        auto burst = make_no_target_effect(
            "magus_charge_surrounding_burst", "Arcane Charge Surrounding Burst",
            "Consume all Arcane Charge on the source unit. Deal damage equal to the charges consumed to this unit and every unit, structure, or base in its surrounding cells.",
            {});
        burst.deals_damage = true;
        register_effect_definition(std::move(burst));
    }
    {
        auto multi = make_board_entity_effect(
            "multi_hit_damage", "Multi-Hit Damage",
            "Strike the same target multiple times in quick succession. Each hit triggers on-hit effects "
            "(overload, conduits, lifesteal, etc.) independently. Payload: amount (per hit), hits (number of hits).",
            BoardTargetKind::Enemy, TargetRequirement::Required,
            {effect_keys::kPayloadAmount, "hits"});
        multi.deals_damage = true;
        register_effect_definition(std::move(multi));
    }
    register_effect_definition(make_no_target_effect(
        "final_barrage", "Final Barrage",
        "Prime this building: stores charges, blast_damage, and blast_overload as entity effects. "
        "At the start of your next turn, final_barrage_detonate fires N shots then self-destructs. "
        "Payload: charges (default 6), self_destruct_damage (default 3), self_destruct_overload (default 1).",
        {"charges", "self_destruct_damage", "self_destruct_overload"}));
    register_effect_definition(make_no_target_effect(
        "consume_spell_orange_for_growth", "Starforged Feast",
        "Consume all floating orange energy in the spell_ability pool. "
        "For each point consumed, permanently gain +1 max HP and +1 damage.",
        {}));
    register_effect_definition(make_no_target_effect(
        "consume_floating_energy_for_storage", "Store Floating Energy",
        "Consume all of the controller's floating energy (unrestricted and tagged pools) and tap "
        "Ancient Frog (ancient_frog_store_passive) only: unrestricted float + turquoise flux energy "
        "(spell_ability tagged pool, turquoise only) once per owner turn end (leaves spell_ability orange "
        "for Starforged Feast), split evenly among "
        "frogs with storage room (turn-order remainder); does not tap zones. Stores on ancient_frog_stored_energy. "
        "Payload: max_storage (default 16).",
        {"max_storage"}));
    register_effect_definition(make_no_target_effect(
        "release_stored_energy_spell_turquoise", "Release Stored Spell Energy",
        "Reactive self_died handler: grant stored ancient_frog_stored_energy as turquoise flux energy (spell_ability pool).",
        {}));
    {
        auto detonate = make_no_target_effect(
            "final_barrage_detonate", "Final Barrage Detonate",
            "Automated start-of-turn detonation for a primed Jury-Rigged Mortar. "
            "Reads final_barrage_charges from entity effects; fires that many mortar shots then self-destructs. "
            "No-ops silently when charges == 0. "
            "Payload: damage (default 3), overload (default 1), min_range (default 2), max_range (default 4).",
            {"damage", "overload", "min_range", "max_range"});
        detonate.deals_damage = true;
        register_effect_definition(std::move(detonate));
    }
    {
        // Missile Storm: no player-selected target - the handler auto-picks the nearest enemies.
        EffectDefinition ms;
        ms.key = "missile_storm";
        ms.display_name = "Missile Storm";
        ms.rules_text =
            "Fire missiles at up to max_targets nearest enemy units within max_range. "
            "Deals amount damage to each; on-hit effects apply to every target.";
        ms.target.domain         = TargetDomain::None;
        ms.target.requirement    = TargetRequirement::None;
        ms.default_board_target_kind = BoardTargetKind::Enemy;
        ms.deals_damage = true;
        ms.payload_keys = {effect_keys::kPayloadAmount, "max_targets", "max_range"};
        register_effect_definition(std::move(ms));
    }
    {
        // aoe_damage_surrounding: no player target - handler uses source_entity_id to find
        // all 8-way surrounding units and damages each by `amount`.  Hits allies and enemies.
        auto aoe = make_no_target_effect(
            "aoe_damage_surrounding", "AoE Damage Surrounding",
            "Automated passive pulse: deal `amount` damage to every unit in the 8 surrounding cells "
            "of the source entity. Hits allied and enemy units alike (no team filter). "
            "Does not hit structures or bases. Payload: amount.",
            {effect_keys::kPayloadAmount});
        aoe.deals_damage = true;
        register_effect_definition(std::move(aoe));
    }
    {
        auto tv = make_no_target_effect(
            "thundering_vale", "Thundering Vale",
            "Focus spell: deal `amount` damage to every unit in the 4 orthogonally adjacent cells of the "
            "focus caster (`entity_adjacent_cells`), then push each surviving unit `push` tiles away from "
            "the caster. Diagonal cells are excluded (not surrounding / 8-way). Hits allied and enemy units "
            "alike. Respects Immovable on push. Payload: amount, push.",
            {effect_keys::kPayloadAmount, "push"});
        tv.deals_damage = true;
        register_effect_definition(std::move(tv));
    }
    register_effect_definition(make_board_entity_effect(
        "grant_passive_ability", "Grant Passive Ability",
        "Permanently grant the target unit a passive from the catalog. "
        "string_payload[\"passive_key\"] names the passive to grant. "
        "Duplicate grants are silently ignored.",
        BoardTargetKind::Any, TargetRequirement::Required,
        {}));
    {
        // cross_shot: fire 4 shots in cardinal directions at exactly `range` distance (LOS required).
        // No player-selected target. Each hit uses the caster's ranged damage profile.
        // On resolve, caster gains 1 Style stack. Payload: range (default 2).
        auto cs = make_no_target_effect(
            "cross_shot", "Cross Shot",
            "Fire a shot at each cell exactly `range` tiles away in the 4 cardinal directions (LOS required). "
            "Gain 1 Style stack. Uses caster's ranged damage profile.",
            {"range"});
        cs.deals_damage = true;
        register_effect_definition(std::move(cs));
    }
    {
        // x_shot: fire 4 shots in diagonal directions at exactly `range` distance (LOS required).
        // No player-selected target. Each hit uses the caster's ranged damage profile.
        // On resolve, caster gains 1 Style stack. Payload: range (default 2).
        auto xs = make_no_target_effect(
            "x_shot", "X Shot",
            "Fire a shot at each cell exactly `range` tiles away in the 4 diagonal directions (LOS required). "
            "Gain 1 Style stack. Uses caster's ranged damage profile.",
            {"range"});
        xs.deals_damage = true;
        register_effect_definition(std::move(xs));
    }
    {
        auto axs = make_no_target_effect(
            "alternating_cross_x_shot", "Cross-X Shot",
            "Alternates Cross Shot (cardinal) and X Shot (diagonal) at exactly `range` tiles away. "
            "Starts with Cross Shot; each use toggles pattern. Gain 1 Style stack on resolve.",
            {"range"});
        axs.deals_damage = true;
        register_effect_definition(std::move(axs));
    }
    {
        // unleash_style: consume all Style stacks, deal stacks*2 (+ability_damage_bonus) damage
        // to an adjacent enemy. Fizzles if caster has no style stacks.
        auto ul = make_board_entity_effect(
            "unleash_style", "Unleash",
            "Consume all Style stacks. Deal 2 damage per stack (plus ability damage bonus) to target. "
            "Fizzles if no Style stacks are present.",
            BoardTargetKind::Enemy, TargetRequirement::Required, {});
        ul.deals_damage = true;
        ul.scales_with_ability_damage = true;
        register_effect_definition(std::move(ul));
    }

    // Defective Augment: apply N jammed stacks to target unit and permanently grant +attack/+health.
    register_effect_definition(make_board_entity_effect(
        "apply_jammed_grant_stats", "Defective Augment",
        "Apply `jammed` stacks of jammed to target unit. Permanently grant it +`attack` damage and "
        "+`health` max HP. Can target ally or enemy. Payload: jammed, attack, health.",
        BoardTargetKind::Any, TargetRequirement::Required, {"jammed", "attack", "health"}));
    // Mind Hijack: swap all temporary non-stat effects between caster (focus source) and target unit.
    register_effect_definition(make_board_entity_effect(
        "swap_temporary_effects", "Mind Hijack",
        "Focus spell: swap all temporary buffs and debuffs (non-stat TemporaryEntityEffects) "
        "between the focus caster and the target unit.",
        BoardTargetKind::Any, TargetRequirement::Required, {}));
    // Sylvia's Special Blend: grant target unit a temporary flag this turn; channeled abilities/spells become reflex.
    register_effect_definition(make_board_entity_effect(
        "grant_slow_becomes_fast", "Sylvia's Special Blend",
        "Grant target unit a temporary effect until end of turn: this unit's slow spells and "
        "abilities are treated as fast.",
        BoardTargetKind::Any, TargetRequirement::Required, {}));
    // Strip for Parts: destroy target allied unit; gain floating orange energy = double its total cost.
    register_effect_definition(make_board_entity_effect(
        "sacrifice_ally_gain_mana", "Strip for Parts",
        "Destroy target allied unit. Gain floating orange energy equal to double that unit's total "
        "orange + neutral energy cost as flux energy (spell_ability tagged pool - spells and abilities only).",
        BoardTargetKind::Ally, TargetRequirement::Required, {}));
    {
        // The Starforged Aberration: randomly redistribute base stats among allied units in a W×H area.
        EffectDefinition eq;
        eq.key = "randomize_stats_area";
        eq.display_name = "The Starforged Aberration";
        eq.rules_text =
            "Target a board cell. Sum the base attack and base HP of all allied units in a "
            "width × height area centered on that cell, then randomly redistribute those totals "
            "among them (each unit ends with at least 1 attack and 1 HP). Payload: width, height.";
        eq.target.domain = TargetDomain::BoardEntityCell;
        eq.target.requirement = TargetRequirement::Required;
        eq.target.board_target_kind = BoardTargetKind::Any;
        // W×H area centered on the clicked cell - mark it so targeting shows the area highlight (matched
        // to the resolver's cells in preview_effect_aoe_blast_cells) instead of a single-cell target.
        eq.target.area_effect = true;
        eq.payload_keys = {"width", "height"};
        register_effect_definition(std::move(eq));
    }
    // Overly Affectionate Cyberware: grant Hyperactive Scanning (keyword mirror from surrounding units).
    {
        EffectDefinition cyberware = make_board_entity_effect(
            "grant_keyword_mirror_passive", "Overly Affectionate Cyberware",
            "Grant a unit with total energy cost <= max_deploy_cost Hyperactive Scanning "
            "(hyperactive_scanning): temporarily gain the keywords of surrounding units.",
            BoardTargetKind::Any, TargetRequirement::Required, {"max_deploy_cost"});
        register_effect_definition(std::move(cyberware));
    }
    // Defective Graft: stun target ally; grant a one-shot on-damaged reactive - next enemy hit triggers
    // 5 damage + 2 overload to all surrounding units.
    register_effect_definition(make_board_entity_effect(
        "grant_explosive_graft", "Explosive Graft",
        "Stun target allied unit and grant it a one-shot marker: the next time an enemy damages "
        "this unit, deal 5 damage and apply 2 overload to all units surrounding it. Marker consumed on fire.",
        BoardTargetKind::Ally, TargetRequirement::Required, {}));
    {
        // focused_lethal_draw: deal amount damage to target unit; if it dies, draw 1 card for controller.
        auto fld = make_board_entity_effect(
            "focused_lethal_draw", "1G-GY",
            "Deal `amount` damage to target unit. If this kills it, the controller draws 1 card.",
            BoardTargetKind::Any, TargetRequirement::Required, {effect_keys::kPayloadAmount});
        fld.deals_damage = true;
        register_effect_definition(std::move(fld));
    }
}

bool try_get_effect_definition(const std::string& key, EffectDefinition& out)
{
    ensure_builtin_effect_registry_loaded();
    std::lock_guard<std::mutex> lock(g_effect_registry_mutex);
    const auto it = g_effect_registry.find(key);
    if (it == g_effect_registry.end()) {
        return false;
    }
    out = it->second;
    return true;
}

bool is_known_effect_key(const std::string& key)
{
    EffectDefinition ignored;
    return try_get_effect_definition(key, ignored);
}

std::vector<std::string> all_registered_effect_keys()
{
    ensure_builtin_effect_registry_loaded();
    std::lock_guard<std::mutex> lock(g_effect_registry_mutex);
    std::vector<std::string> keys;
    keys.reserve(g_effect_registry.size());
    for (const auto& [k, _] : g_effect_registry) {
        keys.push_back(k);
    }
    std::sort(keys.begin(), keys.end());
    return keys;
}

void verify_effect_stack_handler_parity(const StackManager& stack)
{
    ensure_builtin_effect_registry_loaded();
    // Reactive-only effects are dispatched through the passive reactive_effect_key path
    // (see GameState reactive handling), not as stack handlers, so they are exempt from
    // the stack-handler parity requirement.
    static const std::unordered_set<std::string> kReactiveOnlyEffectKeys{
        "draw_cards_owner",
        "deal_damage_to_all_enemies_nearby",
    };
    std::vector<std::string> missing;
    for (const std::string& key : all_registered_effect_keys()) {
        if (kReactiveOnlyEffectKeys.count(key)) {
            continue;
        }
        if (!stack.has_effect_handler(key)) {
            missing.push_back(key);
        }
    }
    if (missing.empty()) {
        return;
    }
    std::string msg = "Effect registry / stack handler mismatch. Missing handlers: ";
    for (size_t i = 0; i < missing.size(); ++i) {
        if (i > 0) {
            msg += ", ";
        }
        msg += missing[i];
    }
    throw std::runtime_error(msg);
}

TargetDefinition target_definition_for_effect_key(const std::string& effect_key)
{
    EffectDefinition def;
    if (try_get_effect_definition(effect_key, def)) {
        return def.target;
    }
    return {};
}

bool effect_requires_board_target(const std::string& effect_key)
{
    const TargetDefinition target = target_definition_for_effect_key(effect_key);
    return target.domain == TargetDomain::BoardEntityCell && target.requirement == TargetRequirement::Required;
}

BoardTargetKind effect_board_target_kind(const std::string& effect_key)
{
    // E7: single registry lookup - board-entity effects return their declared target kind;
    // no-target / stack-item effects return the per-definition default (set in make_no_target_effect /
    // make_stack_item_effect). For completely-unregistered keys, fall back to the legacy heuristic.
    EffectDefinition def;
    if (try_get_effect_definition(effect_key, def)) {
        if (def.target.domain == TargetDomain::BoardEntityCell) {
            return def.target.board_target_kind;
        }
        return def.default_board_target_kind;
    }
    return default_board_target_kind_for_effect_key(effect_key);
}

ActionResult validate_targets_against_definition(
    const GameState& game, int controller_id, const TargetDefinition& target, const std::map<std::string, int>& targets,
    const std::string& stack_item_target_id, const std::string& effect_key)
{
    if (target.domain == TargetDomain::None || target.requirement == TargetRequirement::None) {
        return {true, "No target required", {}};
    }

    if (target.domain == TargetDomain::PlayerSeat) {
        const auto pit = targets.find(effect_keys::kTargetPlayerSeat);
        if (pit == targets.end()) {
            return {false, "This effect requires a target player", {}};
        }
        const int seat = pit->second;
        if (seat < 1) {
            return {false, "Invalid target player seat", {}};
        }
        if (std::find(game.turn_manager.players.begin(), game.turn_manager.players.end(), seat)
            == game.turn_manager.players.end()) {
            return {false, "Target player is not in this match", {}};
        }
        if (game.players_decks.find(seat) == game.players_decks.end()) {
            return {false, "Target player has no deck", {}};
        }
        return {true, "Player target allowed", {}};
    }

    if (target.domain == TargetDomain::StackItem) {
        if (stack_item_target_id.empty()) {
            return {false, "This effect requires a stack target id", {}};
        }
        const StackItem* stack_target = game.find_batched_item(stack_item_target_id);
        if (!stack_target) {
            return {false, "No batched item with id '" + stack_item_target_id + "'", {}};
        }
        if (!target.allowed_stack_source_types.empty()
            && std::find(target.allowed_stack_source_types.begin(), target.allowed_stack_source_types.end(), stack_target->source_type)
                == target.allowed_stack_source_types.end()) {
            return {false, "Stack target type '" + stack_target->source_type + "' is not allowed for this effect", {}};
        }
        return {true, "Stack target allowed", {}};
    }

    const auto xit = targets.find(effect_keys::kCellX);
    const auto yit = targets.find(effect_keys::kCellY);
    const bool has_cell = xit != targets.end() && yit != targets.end();
    if ((xit != targets.end()) != (yit != targets.end())) {
        return {false, "Target cell must include both x and y", {}};
    }
    if (!has_cell) {
        if (target.requirement == TargetRequirement::Required) {
            return {false, "This effect requires a target cell", {}};
        }
        return {true, "No optional target supplied", {}};
    }

    auto ent = game.board.entity_at(xit->second, yit->second);
    if (!ent) {
        const bool requires_entity = effect_key.empty()
            ? (target.area_effect ? target.board_target_kind == BoardTargetKind::Enemy : true)
            : effect_requires_entity_at_target_cell(effect_key);
        if (!requires_entity) {
            if (!game.board.get_square(xit->second, yit->second)) {
                return {false, "Target cell is not on the board", {}};
            }
            return {true, "Board cell target allowed", {}};
        }
        return {false, "No entity at target cell", {}};
    }
    if (!board_target_allows(game, target.board_target_kind, controller_id, *ent)) {
        return {false, "Target not allowed for " + board_target_kind_to_string(target.board_target_kind) + " targeting", {}};
    }
    if (!board_target_entity_allowed_for_effect(*ent, target.board_target_kind, effect_key)) {
        if (entity_is_base(*ent)) {
            return {false, "Player bases cannot be targeted by this effect", {}};
        }
        if (entity_is_structure(*ent)) {
            return {false, "Structures cannot be targeted by this unit effect", {}};
        }
        return {false, "Target entity type is not valid for this effect", {}};
    }
    if (!target.area_effect && enemy_direct_target_blocked_by_stealth(game, controller_id, *ent)) {
        return {false, "Stealthed units cannot be directly targeted", {}};
    }
    if (!entity_satisfies_unit_type_filter(*ent, target.require_target_unit_types)) {
        return {false, "Target does not have a required unit type", {}};
    }
    return {true, "Target allowed", {}};
}

ActionResult validate_effect_targets(
    const GameState& game, int controller_id, const std::string& effect_key, const std::map<std::string, int>& targets, const std::string& stack_item_target_id)
{
    return validate_targets_against_definition(
        game, controller_id, target_definition_for_effect_key(effect_key), targets, stack_item_target_id, effect_key);
}

}  // namespace tactics
