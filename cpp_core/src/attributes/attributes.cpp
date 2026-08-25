#include "tactics/attributes/attributes.hpp"

#include "tactics/cards/cards.hpp"

#include <algorithm>
#include <array>
#include <sstream>

namespace tactics {
namespace {

const std::array<AttributeSpec, 52> kAttributes{{
    AttributeSpec{
        .key = "flying",
        .display_name = "Flying",
        .rules_text =
            "Ignores terrain movement costs, can move over blockers, does not block line of sight or movement, ignores line of sight when attacking, and ignores enemy low cover.",
        .ignores_terrain_movement_cost = true,
        .moves_over_blockers = true,
        .does_not_block_line_of_sight = true,
        .does_not_block_movement = true,
        .ignores_attack_line_of_sight = true,
        .ignores_low_cover_evasion = true,
    },
    AttributeSpec{
        .key = "reach",
        .display_name = "Reach",
        .rules_text = "Can make melee attacks up to range 2.",
        .minimum_melee_range = 2,
    },
    AttributeSpec{
        .key = "low_profile",
        .display_name = "Low Profile",
        .rules_text = "Does not block line of sight, even if its tile would normally be a blocker.",
        .does_not_block_line_of_sight = true,
    },
    AttributeSpec{
        .key = "loose_formation",
        .display_name = "Loose Formation",
        .rules_text =
            "Does not block movement, line of sight, or ranged body-blocking. Other units may move through its occupied cells but cannot end there.",
        .does_not_block_line_of_sight = true,
        .does_not_block_movement = true,
    },
    AttributeSpec{
        .key = "vigilance",
        .display_name = "Vigilance",
        .rules_text = "Has unlimited reactions each turn.",
        .unlimited_reactions = true,
    },
    AttributeSpec{
        .key = "return_fire",
        .display_name = "Return Fire",
        .rules_text = "Can counterattack with ranged attacks when the attacker is within ranged limits. Cannot return fire at melee range if this unit has a ranged deadzone.",
        .can_counterattack_with_ranged = true,
    },
    AttributeSpec{
        .key = "shadowstrike",
        .display_name = "Shadowstrike",
        .rules_text = "Attacks do not trigger counterattacks or other combat reactions, but still enter the normal attack queue and open the Defense window.",
        .attacks_skip_reactions = true,
    },
    AttributeSpec{
        .key = "magic_resist",
        .display_name = "Magic Resist",
        .rules_text = "Reduces magic damage received by this value, up to 3 per hit.",
        .reduces_incoming_damage = true,
    },
    AttributeSpec{
        .key = "pierce",
        .display_name = "Pierce",
        .rules_text = "Damage ignores Shield, Armor, and Bonus Health.",
        .pierces_damage_prevention = true,
    },
    AttributeSpec{
        .key = "lifesteal",
        .display_name = "Lifesteal",
        .rules_text = "Damage this unit deals heals it for the same amount of HP actually removed from the target.",
        .heals_on_damage_dealt = true,
    },
    AttributeSpec{
        .key = "soul_steal",
        .display_name = "Soul Steal",
        .rules_text =
            "Damage this source deals heals an allied base for the same amount of HP actually removed. If several allied bases exist, choose which one to heal.",
        .heals_allied_base_on_damage_dealt = true,
    },
    AttributeSpec{
        .key = "trueshot",
        .display_name = "Trueshot",
        .rules_text = "Ignores line of sight and enemy low cover when attacking.",
        .ignores_attack_line_of_sight = true,
        .ignores_low_cover_evasion = true,
    },
    AttributeSpec{
        .key = "fire_resistance",
        .display_name = "Fire Resistance",
        .rules_text = "Fire stacks can still be gained and spread, but Fire deals no damage to this entity.",
    },
    AttributeSpec{
        .key = "poison_resistance",
        .display_name = "Poison Resistance",
        .rules_text = "Prevents Poison stacks from being applied to this entity.",
    },
    AttributeSpec{
        .key = "bleed_resistance",
        .display_name = "Bleed Resistance",
        .rules_text = "Prevents Bleed stacks from being applied to this entity.",
    },
    AttributeSpec{
        .key = "true_immunity",
        .display_name = "True Immunity",
        .rules_text = "Prevents Poison and Bleed stacks. Fire stacks can still be gained and spread, but Fire deals no damage.",
    },
    AttributeSpec{
        .key = "evasive",
        .display_name = "Evasive",
        .rules_text = "While this unit has Evasive stacks, attacks against it have a 50% chance to miss. Loses 1 stack at the start of its controller's turn. A miss deals no damage and does not body-block redirect.",
    },
    AttributeSpec{
        .key = "stockpile",
        .display_name = "Stockpile",
        .rules_text = "May be played multiple times up to its stockpile count, paying costs each time, but only once per turn.",
        .card_retains_until_charges_depleted = true,
        .card_once_per_turn = true,
    },
    AttributeSpec{
        .key = "exalted",
        .display_name = "Exalted",
        .rules_text =
            "Has an additional play requirement beyond its energy cost. The requirement is listed on the card "
            "(e.g. flux energy generated over the course of the game).",
    },
    AttributeSpec{
        .key = "boost",
        .display_name = "Boost",
        .rules_text =
            "Boosts your next attack or activated ability, then is consumed. Different boosts stack on the same "
            "action; duplicate stacks queue for later actions. Some expire at your turn end if unused.",
    },
    AttributeSpec{
        .key = "focus",
        .display_name = "Focus",
        .rules_text = "Cast from a friendly unit you control. Uses that unit's keywords. Board targets obey spell range and line of sight from the caster.",
    },
    AttributeSpec{
        .key = "regen",
        .display_name = "Regen",
        .rules_text = "At the end of your turn, restore HP equal to Regen (before poison, fire, and bleed damage).",
    },
    AttributeSpec{
        .key = "thorns",
        .display_name = "Thorns",
        .rules_text = "Whenever this unit takes melee attack damage, deal Thorns damage back to the attacker (even if this unit dies or the hit is a counterattack).",
    },
    AttributeSpec{
        .key = "taunt",
        .display_name = "Taunt",
        .rules_text =
            "Enemy units directly adjacent (orthogonal, not diagonal) cannot move away and must target this unit. Pathfinding cannot pass through enemy taunt tiles.",
    },
    AttributeSpec{
        .key = "frenzy",
        .display_name = "Frenzy",
        .rules_text = "The first time each turn this unit kills an enemy unit, refresh its move and attack.",
    },
    AttributeSpec{
        .key = "haste",
        .display_name = "Haste",
        .rules_text =
            "On the turn this unit is deployed, it may move normally but cannot attack, defend, dash, or use activated abilities.",
    },
    AttributeSpec{
        .key = "surge",
        .display_name = "Surge",
        .rules_text =
            "On the turn this unit is deployed, it may attack, defend, dash, and use activated abilities but cannot move.",
    },
    AttributeSpec{
        .key = "charge",
        .display_name = "Charge",
        .rules_text = "Ignores deployment fatigue on the turn it is deployed (may move, use abilities, and attack normally).",
    },
    AttributeSpec{
        .key = "large_unit",
        .display_name = "Large Unit",
        .rules_text =
            "Ignores rough terrain and diagonal movement penalties. "
            "Does not fall into void unless 50% or more of its tiles overlap void squares.",
        .ignores_terrain_movement_cost = true,
        .ignores_diagonal_movement_cost = true,
        .uses_void_threshold_for_landing = true,
    },
    AttributeSpec{
        .key = "crit_immunity",
        .display_name = "Crit Immunity",
        .rules_text = "Critical hits against this unit deal the rolled damage instead of max ×1.5.",
        .immune_to_crits = true,
    },
    AttributeSpec{
        .key = "volley",
        .display_name = "Volley",
        .rules_text = "Ranged attacks are restricted to the 4 cardinal directions. Each attack fires a "
                      "reverse cone N tiles deep (where N is the Volley number). The primary target "
                      "takes full damage. Each row further back is (2d-1) tiles wide and deals 33% "
                      "less damage per depth step (d=2: 67%, d=3: ~45%, etc.). Each cone tile is "
                      "resolved with independent LOS - if blocked by a nearer entity, that shot "
                      "misses silently without transferring damage.",
    },
    AttributeSpec{
        .key = "cleave",
        .display_name = "Cleave",
        .rules_text = "Attacks also hit enemies in cells 4-way adjacent to the primary target, excluding cells the attacker occupies. At most 3 enemies for a 1x1 target.",
    },
    AttributeSpec{
        .key = "whirlwind",
        .display_name = "Whirlwind",
        .rules_text = "Attacks also strike all enemies surrounding the attacker (8-way), not just the primary target.",
    },
    AttributeSpec{
        .key = "slippery",
        .display_name = "Slippery",
        .rules_text = "Can move through occupied cells (allies and enemies) during pathing. Cannot end movement on an occupied cell.",
        .moves_over_blockers = true,
    },
    AttributeSpec{
        .key = "base_breaker",
        .display_name = "Base-Breaker",
        .rules_text = "Basic attacks deal extra damage to base entities. The amount is the keyword's numeric value.",
        .bonus_damage_vs_base = true,
    },
    AttributeSpec{
        .key = "command",
        .display_name = "Command",
        .rules_text =
            "Single-tile (1×1) friendly units with total energy cost equal to or less than this value "
            "may be deployed on any cell orthogonally adjacent to this unit, in addition to the normal "
            "deployment zone. The deploying player still pays full energy cost.",
        .grants_forward_deploy = true,
    },
    AttributeSpec{
        .key = "relentless",
        .display_name = "Relentless",
        .rules_text =
            "When this unit attacks, it continues attacking the same target for each Relentless stack. "
            "After every hit, the target is forced to counterattack regardless of reactions, stun, LOS, "
            "or ranged restrictions, including when the hit would remove them from the board. Other reactions (covering fire, etc.) then fire with normal requirements. "
            "The extra attacks do not open additional spell reaction windows.",
    },
    AttributeSpec{
        .key = "multistrike",
        .display_name = "Multistrike",
        .rules_text =
            "When this unit attacks, it strikes the same target X additional times within the same attack. "
            "All strikes deal damage and trigger on-hit effects (lifesteal, thorns, conduits) independently. "
            "Only one counterattack and one set of reactions occur - at the end of the full attack. "
            "Thorns are especially effective because they trigger on each melee strike.",
    },
    AttributeSpec{
        .key = "coordinated",
        .display_name = "Coordinated",
        .rules_text =
            "Deal +X bonus damage when another ally has already damaged the same target this turn. "
            "X is the keyword's numeric value.",
        .bonus_damage_when_coordinated = true,
    },
    AttributeSpec{
        .key = "entrenched",
        .display_name = "Entrenched",
        .rules_text =
            "Deal +X bonus damage while this unit has not moved this turn. "
            "X is the keyword's numeric value.",
        .bonus_damage_when_stationary = true,
    },
    AttributeSpec{
        .key = "precise",
        .display_name = "Precise",
        .rules_text = "Always deals the maximum value of its damage range.",
        .uses_max_damage = true,
    },
    AttributeSpec{
        .key = "berserk",
        .display_name = "Berserk",
        .rules_text =
            "Deal +X bonus damage while this unit is below half its maximum HP. "
            "X is the keyword's numeric value.",
        .bonus_damage_when_low_hp = true,
    },
    AttributeSpec{
        .key = "defender",
        .display_name = "Defender",
        .rules_text =
            "All reactions from this unit - counterattacks, return fire, and covering fire - deal +X bonus damage. "
            "X is the keyword's numeric value.",
        .bonus_damage_on_reactions = true,
    },
    AttributeSpec{
        .key = "spearhead",
        .display_name = "Spearhead",
        .rules_text =
            "This structure still requires the normal deployment conditions (deploy zone or adjacent to a "
            "friendly unit), but may only be placed if at least one footprint cell is also within X tiles "
            "(Chebyshev) of an enemy base. X is the keyword's numeric value.",
        .restricts_deploy_to_enemy_base_range = true,
    },
    AttributeSpec{
        .key = "immovable",
        .display_name = "Immovable",
        .rules_text =
            "Cannot be repositioned by any external effect (pushes, pulls, forced displacement, or "
            "similar abilities). The unit may still use its own voluntary movement actions normally. "
            "Suppressed while silenced.",
    },
    AttributeSpec{
        .key = "indestructible",
        .display_name = "Indestructible",
        .rules_text =
            "Cannot take damage from any source (all incoming damage is reduced to 0, including "
            "piercing damage) and cannot be destroyed by any effect. Entities with this keyword "
            "also block entry by Crushing Advance and similar forced-entry movement. "
            "Cannot be granted, mirrored, or copied onto other units. Suppressed while silenced.",
    },
    AttributeSpec{
        .key = "crushes_on_move",
        .display_name = "Crushing Advance",
        .rules_text =
            "This unit may move to any tile regardless of occupancy, except tiles occupied by an "
            "indestructible entity or a unit of the same type. Entities on the destination are "
            "dealt 3 damage and pushed away from this unit's geometric center (direction with the "
            "highest alignment to the push vector is tried first; all 8 directions are candidates); "
            "if no surrounding tile is available they are destroyed. This unit cannot gain bonus "
            "move-points or extra moves from any source. Suppressed while silenced.",
    },
    AttributeSpec{
        .key = "last_gasp",
        .display_name = "Last Gasp",
        .rules_text =
            "When this unit dies, its Last Gasp effect triggers. The effect is defined by this unit's "
            "passive (reactive_trigger: self_died). Fires after the unit is removed from the board. "
            "Not suppressed by silence - the unit is already dead when it fires.",
    },
    AttributeSpec{
        .key = "spellbound",
        .display_name = "Spellbound",
        .rules_text =
            "Whenever you play a spell, this unit's Spellbound effect triggers. The effect is defined "
            "by this unit's passive (reactive trigger owner_spell_played). Triggers when the spell is "
            "cast (queued to the batch), not when it resolves. Does not cost a reaction. Abilities do not "
            "trigger Spellbound. Suppressed while silenced.",
    },
    AttributeSpec{
        .key = "multicast",
        .display_name = "Multicast",
        .rules_text =
            "When you cast this spell, it is queued to the batch stack up to X separate times (X is the keyword "
            "value). If the spell needs targets, choose 1 to X different targets - each skipped copy is not "
            "queued. Each copy resolves independently. Each queue counts as a separate spell cast for "
            "Spellbound and similar on-cast effects. You pay the spell's energy cost once.",
    },
    AttributeSpec{
        .key = "conduit",
        .display_name = "Conduit",
        .rules_text =
            "While this unit or structure is on the board under your control, your spells deal extra damage "
            "equal to this entity's Conduit value. Conduit from all of your units and structures combines.",
    },
    AttributeSpec{
        .key = "first_strike",
        .display_name = "First Strike",
        .rules_text =
            "When this unit's attack kills an enemy unit, that unit does not counterattack.",
        .suppresses_counterattack_on_kill = true,
    },
}};

template <typename Predicate>
bool any_entity_attribute(const Entity& e, Predicate pred)
{
    if (entity_is_silenced(e)) {
        return false;
    }
    for (const std::string& key : e.keywords) {
        if (const AttributeSpec* spec = find_attribute_spec(key); spec && pred(*spec)) {
            return true;
        }
    }
    for (const std::string& key : e.aura_granted_keywords) {
        if (const AttributeSpec* spec = find_attribute_spec(key); spec && pred(*spec)) {
            return true;
        }
    }
    for (const TemporaryEntityEffect& effect : e.temporary_effects) {
        for (const PassiveAttributeGrant& grant : effect.granted_attributes) {
            if (const AttributeSpec* spec = find_attribute_spec(grant.key); spec && pred(*spec)) {
                return true;
            }
        }
    }
    return false;
}

void consume_one_shield(Entity& e)
{
    reduce_entity_effect(e, "shield", 1);
}

void consume_one_barrier(Entity& e)
{
    reduce_entity_effect(e, "barrier", 1);
}

}  // namespace

const AttributeSpec* find_attribute_spec(const std::string& key)
{
    const auto it = std::find_if(kAttributes.begin(), kAttributes.end(), [&](const AttributeSpec& spec) { return spec.key == key; });
    return it == kAttributes.end() ? nullptr : &*it;
}

std::vector<AttributeSpec> all_attribute_specs()
{
    return {kAttributes.begin(), kAttributes.end()};
}

std::string attribute_display_name(const std::string& key)
{
    if (const AttributeSpec* spec = find_attribute_spec(key)) {
        return spec->display_name;
    }
    return key;
}

bool attribute_is_non_copyable(const std::string& key)
{
    return key == "indestructible";
}

bool entity_is_indestructible(const Entity& e)
{
    return entity_has_native_keyword(e, "indestructible");
}

std::string attribute_rules_text(const std::string& key)
{
    if (const AttributeSpec* spec = find_attribute_spec(key)) {
        return spec->rules_text;
    }
    return {};
}

std::string format_attribute_names(const std::vector<std::string>& keys)
{
    std::ostringstream out;
    bool first = true;
    for (const std::string& key : keys) {
        if (!first) {
            out << ", ";
        }
        first = false;
        out << attribute_display_name(key);
    }
    return out.str();
}

bool has_large_unit(const Entity& e)
{
    return any_entity_attribute(e, [](const AttributeSpec& spec) { return spec.uses_void_threshold_for_landing; });
}

bool entity_ignores_diagonal_movement_cost(const Entity& e)
{
    return any_entity_attribute(e, [](const AttributeSpec& spec) { return spec.ignores_diagonal_movement_cost; });
}

bool entity_uses_void_threshold_for_landing(const Entity& e)
{
    return any_entity_attribute(e, [](const AttributeSpec& spec) { return spec.uses_void_threshold_for_landing; });
}

bool has_flying(const Entity& e)
{
    return ignores_terrain_movement_cost(e) && moves_over_blockers(e);
}

bool has_reach(const Entity& e)
{
    return minimum_melee_range_from_attributes(e) >= 2;
}

bool ignores_terrain_movement_cost(const Entity& e)
{
    return any_entity_attribute(e, [](const AttributeSpec& spec) { return spec.ignores_terrain_movement_cost; });
}

bool moves_over_blockers(const Entity& e)
{
    return any_entity_attribute(e, [](const AttributeSpec& spec) { return spec.moves_over_blockers; });
}

int minimum_melee_range_from_attributes(const Entity& e)
{
    if (entity_is_silenced(e)) {
        return 0;
    }
    int out = 0;
    for (const std::string& key : e.keywords) {
        if (const AttributeSpec* spec = find_attribute_spec(key)) {
            out = std::max(out, spec->minimum_melee_range);
        }
    }
    for (const std::string& key : e.aura_granted_keywords) {
        if (const AttributeSpec* spec = find_attribute_spec(key)) {
            out = std::max(out, spec->minimum_melee_range);
        }
    }
    for (const TemporaryEntityEffect& effect : e.temporary_effects) {
        for (const PassiveAttributeGrant& grant : effect.granted_attributes) {
            if (const AttributeSpec* spec = find_attribute_spec(grant.key)) {
                out = std::max(out, spec->minimum_melee_range);
            }
        }
    }
    return out;
}

bool does_not_block_line_of_sight(const Entity& e)
{
    return any_entity_attribute(e, [](const AttributeSpec& spec) { return spec.does_not_block_line_of_sight; });
}

bool entity_blocks_line_of_sight(const Entity& e)
{
    return e.line_of_sight_blocked && !does_not_block_line_of_sight(e);
}

bool does_not_block_movement(const Entity& e)
{
    return any_entity_attribute(e, [](const AttributeSpec& spec) { return spec.does_not_block_movement; });
}

bool has_vigilance(const Entity& e)
{
    return any_entity_attribute(e, [](const AttributeSpec& spec) { return spec.unlimited_reactions; });
}

bool has_crit_immunity(const Entity& e)
{
    return any_entity_attribute(e, [](const AttributeSpec& spec) { return spec.immune_to_crits; });
}

bool has_return_fire(const Entity& e)
{
    return any_entity_attribute(e, [](const AttributeSpec& spec) { return spec.can_counterattack_with_ranged; });
}

bool has_shadowstrike(const Entity& e)
{
    return any_entity_attribute(e, [](const AttributeSpec& spec) { return spec.attacks_skip_reactions; });
}

bool has_first_strike(const Entity& e)
{
    return any_entity_attribute(e, [](const AttributeSpec& spec) { return spec.suppresses_counterattack_on_kill; });
}

bool has_pierce(const Entity& e)
{
    return any_entity_attribute(e, [](const AttributeSpec& spec) { return spec.pierces_damage_prevention; });
}

bool has_lifesteal(const Entity& e)
{
    return any_entity_attribute(e, [](const AttributeSpec& spec) { return spec.heals_on_damage_dealt; });
}

bool has_soul_steal(const Entity& e)
{
    return any_entity_attribute(e, [](const AttributeSpec& spec) { return spec.heals_allied_base_on_damage_dealt; });
}

bool card_has_soul_steal(const Card& c)
{
    return card_has_attribute(c, "soul_steal");
}

bool ignores_attack_line_of_sight(const Entity& e)
{
    return any_entity_attribute(e, [](const AttributeSpec& spec) { return spec.ignores_attack_line_of_sight; });
}

bool ignores_low_cover_evasion(const Entity& e)
{
    return any_entity_attribute(e, [](const AttributeSpec& spec) { return spec.ignores_low_cover_evasion; });
}

bool ability_has_trueshot(const AbilitySpec& ability)
{
    return ability_has_attribute(ability, "trueshot");
}

bool damage_source_ignores_attack_line_of_sight(const Entity& source, const AbilitySpec* ability)
{
    if (ignores_attack_line_of_sight(source)) {
        return true;
    }
    return ability && ability_has_trueshot(*ability);
}

bool damage_source_ignores_low_cover_evasion(const Entity& source, const AbilitySpec* ability)
{
    if (ignores_low_cover_evasion(source)) {
        return true;
    }
    return ability && ability_has_trueshot(*ability);
}

bool ability_has_pierce(const AbilitySpec& a)
{
    for (const std::string& key : a.keywords) {
        if (const AttributeSpec* spec = find_attribute_spec(key); spec && spec->pierces_damage_prevention) {
            return true;
        }
    }
    return false;
}

bool ability_has_lifesteal(const AbilitySpec& a)
{
    for (const std::string& key : a.keywords) {
        if (const AttributeSpec* spec = find_attribute_spec(key); spec && spec->heals_on_damage_dealt) {
            return true;
        }
    }
    return false;
}

bool ability_has_soul_steal(const AbilitySpec& a)
{
    for (const std::string& key : a.keywords) {
        if (const AttributeSpec* spec = find_attribute_spec(key); spec && spec->heals_allied_base_on_damage_dealt) {
            return true;
        }
    }
    return false;
}

bool damage_source_has_lifesteal(const Entity* source, bool ability_grants_lifesteal)
{
    if (ability_grants_lifesteal) {
        return true;
    }
    if (!source) {
        return false;
    }
    return has_lifesteal(*source);
}

bool damage_source_has_soul_steal(const Entity* source, const bool ability_grants_soul_steal)
{
    if (ability_grants_soul_steal) {
        return true;
    }
    if (!source) {
        return false;
    }
    return has_soul_steal(*source);
}

int base_breaker_bonus(const Entity& e)
{
    if (entity_is_silenced(e)) {
        return 0;
    }
    return entity_attribute_amount(e, "base_breaker", 0);
}

int relentless_value(const Entity& e)
{
    if (entity_is_silenced(e)) {
        return 0;
    }
    return entity_attribute_amount(e, "relentless", 0);
}

int multistrike_value(const Entity& e)
{
    if (entity_is_silenced(e)) {
        return 0;
    }
    return entity_attribute_amount(e, "multistrike", 0);
}

int command_value(const Entity& e)
{
    if (entity_is_silenced(e)) {
        return 0;
    }
    return entity_attribute_amount(e, "command", 0);
}

int coordinated_value(const Entity& e)
{
    if (entity_is_silenced(e)) {
        return 0;
    }
    return entity_attribute_amount(e, "coordinated", 0);
}

int entrenched_value(const Entity& e)
{
    if (entity_is_silenced(e)) {
        return 0;
    }
    return entity_attribute_amount(e, "entrenched", 0);
}

bool has_precise(const Entity& e)
{
    return any_entity_attribute(e, [](const AttributeSpec& spec) { return spec.uses_max_damage; });
}

int berserk_value(const Entity& e)
{
    if (entity_is_silenced(e)) {
        return 0;
    }
    return entity_attribute_amount(e, "berserk", 0);
}

int defender_value(const Entity& e)
{
    if (entity_is_silenced(e)) {
        return 0;
    }
    return entity_attribute_amount(e, "defender", 0);
}

int spearhead_value(const Entity& e)
{
    if (entity_is_silenced(e)) {
        return 0;
    }
    return entity_attribute_amount(e, "spearhead", 0);
}

int armor_value(const Entity& e)
{
    if (entity_is_silenced(e)) {
        return 0;
    }
    // Defend stance adds +1 armor here so the value is accurate everywhere (UI, tooltips, targeting).
    // keyword_amounts["armor"] is never modified by defend; passive/temp armor use separate paths.
    int v = entity_attribute_amount(e, "armor") + e.aura_bonus_armor;
    if (entity_has_defend_stance(e)) {
        v += 1;
    }
    return v;
}

int reduce_damage_by_armor(const Entity& e, int raw_damage, int terrain_armor_bonus)
{
    if (raw_damage <= 0) {
        return 0;
    }
    int armor = armor_value(e);
    if (terrain_armor_bonus > 0) {
        armor += terrain_armor_bonus;
    }
    if (armor <= 0) {
        return raw_damage;
    }
    return std::max(0, raw_damage - std::min(armor, 5));
}

int magic_resist_value(const Entity& e)
{
    if (entity_is_silenced(e)) {
        return 0;
    }
    return entity_attribute_amount(e, "magic_resist");
}

int regen_value(const Entity& e)
{
    if (entity_is_silenced(e)) {
        return 0;
    }
    return entity_attribute_amount(e, "regen");
}

int thorns_value(const Entity& e)
{
    if (entity_is_silenced(e)) {
        return 0;
    }
    return entity_attribute_amount(e, "thorns");
}

int conduit_value(const Entity& e)
{
    return entity_attribute_amount(e, "conduit", 0);
}

int reduce_damage_by_magic_resist(const Entity& e, int raw_damage)
{
    if (raw_damage <= 0) {
        return 0;
    }
    const int resist = magic_resist_value(e);
    if (resist <= 0) {
        return raw_damage;
    }
    return std::max(0, raw_damage - std::min(resist, 3));
}

int bonus_health_value(const Entity& e)
{
    if (entity_is_silenced(e)) {
        return 0;
    }
    return entity_effect_amount(e, "bonus_health");
}

bool has_shield(const Entity& e)
{
    if (entity_is_silenced(e)) {
        return false;
    }
    return entity_effect_amount(e, "shield") > 0;
}

bool has_barrier(const Entity& e)
{
    if (entity_is_silenced(e)) {
        return false;
    }
    return entity_effect_amount(e, "barrier") > 0;
}

int incoming_damage_display_amount(const Entity& e, int raw_damage, bool pierce, const DamageType damage_type,
    int terrain_armor_bonus)
{
    if (raw_damage <= 0) {
        return 0;
    }
    if (entity_is_indestructible(e)) {
        return 0;
    }
    int remaining = raw_damage;
    const bool pure = damage_type == DamageType::Pure;
    if (!pierce && !pure && has_shield(e)) {
        return 0;
    }
    if (!pierce && !pure && has_barrier(e)) {
        return 0;
    }
    if (!pierce && !pure) {
        if (damage_type == DamageType::Magic) {
            remaining = reduce_damage_by_magic_resist(e, remaining);
        } else {
            remaining = reduce_damage_by_armor(e, remaining, terrain_armor_bonus);
        }
    }
    return std::max(0, remaining);
}

int apply_incoming_damage(Entity& e, int raw_damage, bool pierce, const DamageType damage_type, int terrain_armor_bonus)
{
    if (raw_damage <= 0) {
        return 0;
    }
    // Indestructible: immune to all incoming damage regardless of source or pierce flag.
    if (entity_is_indestructible(e)) {
        return 0;
    }
    int remaining = raw_damage;
    const bool pure = damage_type == DamageType::Pure;
    if (!pierce && !pure && has_shield(e)) {
        consume_one_shield(e);
        return 0;
    }
    if (!pierce && !pure && has_barrier(e)) {
        consume_one_barrier(e);
        return 0;
    }
    if (!pierce && !pure) {
        if (damage_type == DamageType::Magic) {
            remaining = reduce_damage_by_magic_resist(e, remaining);
        } else {
            remaining = reduce_damage_by_armor(e, remaining, terrain_armor_bonus);
        }
    }
    if (remaining <= 0) {
        return 0;
    }
    int dealt = 0;
    const int bonus = pierce ? 0 : bonus_health_value(e);
    if (bonus > 0) {
        const int absorbed = std::min(bonus, remaining);
        reduce_entity_effect(e, "bonus_health", absorbed);
        remaining -= absorbed;
        dealt += absorbed;
    }
    if (remaining > 0) {
        const int hp_before = e.current_health;
        e.current_health = std::max(0, e.current_health - remaining);
        dealt += std::max(0, hp_before - e.current_health);
    }
    return dealt;
}

}  // namespace tactics
