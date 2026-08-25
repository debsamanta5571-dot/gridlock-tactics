#include "tactics/cards/card_catalog.hpp"

#include "tactics/cards/ability_catalog.hpp"
#include "tactics/cards/focus_spell.hpp"
#include "tactics/cards/unit_types.hpp"
#include "tactics/common/effect_keys.hpp"
#include "tactics/common/types.hpp"
#include "tactics/core/game_state.hpp"

#include <cctype>
#include <mutex>
#include <stdexcept>

namespace tactics {

namespace {

int stockpile_from_definition(const CardDefinition& def)
{
    for (const auto& kw : def.keywords) {
        if (kw.key == "stockpile" && kw.amount.has_value()) {
            return std::max(0, *kw.amount);
        }
    }
    return 0;
}

int multicast_from_definition(const CardDefinition& def)
{
    for (const auto& kw : def.keywords) {
        if (kw.key == "multicast" && kw.amount.has_value()) {
            return std::max(1, *kw.amount);
        }
    }
    return 1;
}

}  // namespace

bool definition_is_unit(const CardDefinition& def) { return def.type != "spell" && def.unit.has_value(); }

bool definition_is_spell(const CardDefinition& def) { return def.type == "spell" && def.spell.has_value(); }

const UnitCardDefinition& definition_unit(const CardDefinition& def)
{
    if (!def.unit) {
        throw std::runtime_error("definition_unit: not a unit card");
    }
    return *def.unit;
}

const SpellCardDefinition& definition_spell(const CardDefinition& def)
{
    if (!def.spell) {
        throw std::runtime_error("definition_spell: not a spell card");
    }
    return *def.spell;
}

const CardDefinition& definition_for_instance(const CardInstancePool& pool, const CardInstanceId id)
{
    const CardInstance& inst = pool.at(id);
    const CardDefinition* def = try_get_card_definition_ptr(inst.definition_id);
    if (!def) {
        throw std::runtime_error("definition_for_instance: missing catalog definition");
    }
    return *def;
}

const CardDefinition& definition_for_instance(const CardInstancePool& pool, const CardInstance& inst)
{
    (void)pool;
    const CardDefinition* def = try_get_card_definition_ptr(inst.definition_id);
    if (!def) {
        throw std::runtime_error("definition_for_instance: missing catalog definition");
    }
    return *def;
}

std::map<EnergyType, int> definition_energy_cost(const CardDefinition& def) { return def.energy_cost; }

int definition_total_energy_cost(const CardDefinition& def)
{
    int total = 0;
    for (const auto& [_, amount] : def.energy_cost) {
        total += std::max(0, amount);
    }
    return total;
}

namespace {

std::string card_key_from_source_card_id(const std::string& source_card_id)
{
    const auto pos = source_card_id.rfind('_');
    if (pos != std::string::npos && pos + 1 < source_card_id.size()) {
        const std::string suffix = source_card_id.substr(pos + 1);
        bool all_digits = !suffix.empty();
        for (const char ch : suffix) {
            if (!std::isdigit(static_cast<unsigned char>(ch))) {
                all_digits = false;
                break;
            }
        }
        if (all_digits) {
            return source_card_id.substr(0, pos);
        }
    }
    return source_card_id;
}

std::optional<int> deploy_cost_for_card_key(const std::string& key)
{
    if (key.empty()) {
        return std::nullopt;
    }
    CardDefinition def;
    if (!try_get_card_definition(key, def) || def.type == "spell" || !def.unit.has_value()) {
        return std::nullopt;
    }
    return definition_total_energy_cost(def);
}

}  // namespace

std::optional<int> entity_source_deploy_cost(const Entity& entity)
{
    if (!entity.source_card_id.empty()) {
        if (const auto direct = deploy_cost_for_card_key(entity.source_card_id)) {
            return direct;
        }
        if (const auto parsed = deploy_cost_for_card_key(card_key_from_source_card_id(entity.source_card_id))) {
            return parsed;
        }
    }
    if (!entity.entity_id.empty()) {
        if (const auto from_entity = deploy_cost_for_card_key(entity.entity_id)) {
            return from_entity;
        }
        if (const auto pos = entity.entity_id.rfind('_'); pos != std::string::npos && pos > 0) {
            if (const auto prefix = deploy_cost_for_card_key(entity.entity_id.substr(0, pos))) {
                return prefix;
            }
        }
    }
    return std::nullopt;
}

bool entity_satisfies_max_deploy_cost(const Entity& entity, const int max_cost)
{
    if (max_cost < 0) {
        return false;
    }
    if (entity.entity_type != "unit") {
        return false;
    }
    const std::optional<int> cost = entity_source_deploy_cost(entity);
    return cost.has_value() && *cost <= max_cost;
}

int batched_spell_total_cost_for_cast(const CardDefinition& def, const int x_amount)
{
    if (!definition_is_spell(def)) {
        return 0;
    }
    int total = definition_total_energy_cost(def);
    const SpellCardDefinition& spell = definition_spell(def);
    if (spell.x_cost_energy_type.has_value() && x_amount > 0) {
        total += x_amount;
    }
    return total;
}

bool definition_spell_has_x_cost(const CardDefinition& def)
{
    return definition_is_spell(def) && definition_spell(def).x_cost_energy_type.has_value();
}

int definition_spell_x_cost_min(const CardDefinition& def)
{
    if (!definition_spell_has_x_cost(def)) {
        return 0;
    }
    return std::max(0, definition_spell(def).x_cost_min);
}

int max_affordable_spell_x_amount(const GameState& game, const int player_id, const CardDefinition& def)
{
    if (!definition_spell_has_x_cost(def)) {
        return 0;
    }
    const SpellCardDefinition& spell = definition_spell(def);
    const int min_x = definition_spell_x_cost_min(def);
    int max_x = min_x - 1;
    for (int x = min_x; x <= 99; ++x) {
        auto cost = definition_energy_cost(def);
        cost[*spell.x_cost_energy_type] += x;
        if (game.turn_manager.can_afford(game, player_id, cost, ActionType::Spell)) {
            max_x = x;
        } else {
            break;
        }
    }
    return max_x;
}

int max_affordable_ability_x_amount(const GameState& game, const int player_id, const AbilitySpec& spec)
{
    if (!spec.x_cost_energy_type.has_value()) {
        return 0;
    }
    const int min_x = std::max(0, spec.x_cost_min);
    int max_x = min_x - 1;
    for (int x = min_x; x <= 99; ++x) {
        std::map<EnergyType, int> cost = spec.energy_cost;
        cost[*spec.x_cost_energy_type] += x;
        if (game.turn_manager.can_afford(game, player_id, cost, ActionType::Ability)) {
            max_x = x;
        } else {
            break;
        }
    }
    return max_x;
}

std::string definition_name(const CardDefinition& def) { return def.name; }

bool definition_has_keyword(const CardDefinition& def, const std::string& key)
{
    for (const auto& kw : def.keywords) {
        if (kw.key == key) {
            return true;
        }
    }
    return false;
}

std::optional<CardKeywordDefinition> definition_exalted_keyword(const CardDefinition& def)
{
    for (const auto& kw : def.keywords) {
        if (kw.key == "exalted") {
            return kw;
        }
    }
    return std::nullopt;
}

bool exalted_requirement_met(const GameState& game, const int player_id, const CardDefinition& def,
                             std::string* reason)
{
    const auto exalted = definition_exalted_keyword(def);
    if (!exalted.has_value()) {
        return true;
    }
    const std::string req = exalted->requirement.value_or("");
    const int threshold   = exalted->amount.value_or(0);
    if (req == "flux_generated") {
        const int total = player_flux_energy_generated_total(game.turn_manager, player_id);
        if (total < threshold) {
            if (reason) {
                *reason = "Exalted: need " + std::to_string(threshold)
                    + " flux energy generated this game (have " + std::to_string(total) + ")";
            }
            return false;
        }
        return true;
    }
    if (req == "overload_applied") {
        const int total = player_overload_applied_total(game.turn_manager, player_id);
        if (total < threshold) {
            if (reason) {
                *reason = "Exalted: need " + std::to_string(threshold)
                    + " overload applied this game (have " + std::to_string(total) + ")";
            }
            return false;
        }
        return true;
    }
    if (req == "ability_damage_dealt") {
        const int total = player_ability_damage_dealt_total(game.turn_manager, player_id);
        if (total < threshold) {
            if (reason) {
                *reason = "Exalted: need " + std::to_string(threshold)
                    + " ability damage dealt this game (have " + std::to_string(total) + ")";
            }
            return false;
        }
        return true;
    }
    if (reason) {
        *reason = "Exalted: unknown requirement \"" + req + "\"";
    }
    return false;
}

int definition_stockpile_amount(const CardDefinition& def) { return stockpile_from_definition(def); }

int definition_multicast_amount(const CardDefinition& def) { return multicast_from_definition(def); }

bool definition_spell_multicast_requires_per_copy_targets(const CardDefinition& def)
{
    if (!definition_is_spell(def) || definition_multicast_amount(def) <= 1) {
        return false;
    }
    if (spell_requires_focus_caster(def) || definition_spell_requires_stack_target(def)
        || definition_spell_requires_mandatory_board_cell(def)) {
        return true;
    }
    return false;
}

BoardTargetKind definition_spell_board_target_kind(const CardDefinition& def)
{
    const SpellCardDefinition& sp = definition_spell(def);
    if (sp.board_target_kind.has_value()) {
        return *sp.board_target_kind;
    }
    return effect_board_target_kind(sp.effect_key);
}

bool definition_spell_requires_mandatory_board_cell(const CardDefinition& def)
{
    const SpellCardDefinition& sp = definition_spell(def);
    if (sp.requires_mandatory_board_cell.has_value()) {
        return *sp.requires_mandatory_board_cell;
    }
    return effect_requires_board_target(sp.effect_key);
}

bool definition_spell_requires_stack_target(const CardDefinition& def)
{
    return target_definition_for_effect_key(definition_spell(def).effect_key).domain == TargetDomain::StackItem;
}

bool definition_spell_requires_player_seat_target(const CardDefinition& def)
{
    return target_definition_for_effect_key(definition_spell(def).effect_key).domain == TargetDomain::PlayerSeat;
}

bool definition_spell_is_modal(const CardDefinition& def)
{
    return definition_is_spell(def) && !definition_spell(def).modes.empty();
}

int definition_spell_modal_mode_count(const CardDefinition& def)
{
    if (!definition_is_spell(def)) {
        return 0;
    }
    return static_cast<int>(definition_spell(def).modes.size());
}

const SpellMode* try_definition_spell_mode(const CardDefinition& def, const int mode_index)
{
    if (!definition_is_spell(def)) {
        return nullptr;
    }
    const std::vector<SpellMode>& modes = definition_spell(def).modes;
    if (mode_index < 0 || mode_index >= static_cast<int>(modes.size())) {
        return nullptr;
    }
    return &modes[static_cast<size_t>(mode_index)];
}

std::string definition_spell_mode_effect_key(const CardDefinition& def, const int mode_index)
{
    if (const SpellMode* mode = try_definition_spell_mode(def, mode_index)) {
        return mode->effect_key;
    }
    return definition_spell(def).effect_key;
}

bool definition_spell_mode_requires_board_cell(const CardDefinition& def, const int mode_index)
{
    if (const SpellMode* mode = try_definition_spell_mode(def, mode_index)) {
        if (mode->requires_board_target) {
            return true;
        }
        return effect_requires_board_target(mode->effect_key);
    }
    return definition_spell_requires_mandatory_board_cell(def);
}

BoardTargetKind definition_spell_mode_board_target_kind(const CardDefinition& def, const int mode_index)
{
    if (const SpellMode* mode = try_definition_spell_mode(def, mode_index)) {
        if (mode->board_target_kind) {
            return *mode->board_target_kind;
        }
        return effect_board_target_kind(mode->effect_key);
    }
    return definition_spell_board_target_kind(def);
}

bool definition_has_soul_steal(const CardDefinition& def) { return definition_has_keyword(def, "soul_steal"); }

namespace {

bool player_has_seraphina_scorching_acceleration(const GameState& game, const int player_id)
{
    bool found = false;
    game.board.for_each_entity([&](const std::shared_ptr<Entity>& ent) {
        if (found || !ent || !ent->owner || *ent->owner != player_id || ent->current_health <= 0) {
            return;
        }
        if (entity_is_silenced(*ent)) {
            return;
        }
        for (const PassiveAbilitySpec& passive : ent->passive_abilities) {
            if (passive.key == "seraphina_scorching_acceleration" && !entity_passive_is_suppressed(*ent, passive.key)) {
                found = true;
                return;
            }
        }
    });
    return found;
}

bool spell_is_symphony_scorching_sphere(const CardDefinition& def)
{
    return definition_is_spell(def)
        && (def.key == "symphony_scorching_sphere" || definition_spell(def).effect_key == "scorching_sphere");
}

}  // namespace

EffectSpeed effective_spell_cast_speed(const GameState& game, const int player_id, const CardDefinition& def)
{
    if (!definition_is_spell(def)) {
        return EffectSpeed::Channeled;
    }
    const EffectSpeed base = definition_spell(def).speed;
    if (spell_is_symphony_scorching_sphere(def) && player_has_seraphina_scorching_acceleration(game, player_id)) {
        return EffectSpeed::Reflex;
    }
    return base;
}

std::shared_ptr<Unit> create_unit_from_definition(
    const CardDefinition& def, const CardInstance& inst, int owner, const std::string& entity_id)
{
    if (!definition_is_unit(def)) {
        return nullptr;
    }
    const UnitCardDefinition& ud = definition_unit(def);
    auto u = std::make_shared<Unit>();
    u->entity_id = entity_id;
    u->owner = owner;
    u->entity_type = ud.entity_type;
    u->unit_type = ud.unit_type;
    u->attack_type = ud.attack_type;
    u->base_health = ud.base_health;
    u->current_health = ud.current_health;
    u->movement = ud.movement;
    u->melee_range = ud.melee_range;
    u->melee_damage = ud.melee_damage;
    u->melee_damage_min = ud.melee_damage_min;
    u->melee_damage_max = ud.melee_damage_max;
    u->ranged_range = ud.ranged_range;
    u->ranged_deadzone = ud.ranged_deadzone;
    u->ranged_damage = ud.ranged_damage;
    u->ranged_damage_min = ud.ranged_damage_min;
    u->ranged_damage_max = ud.ranged_damage_max;
    u->crit_chance_percent = ud.crit_chance_percent;
    sync_unit_damage_ranges_from_nominal(*u);
    u->line_of_sight_blocked = ud.line_of_sight_blocked;
    u->shape = ud.shape;
    auto install_native_card_keyword = [&](const CardKeywordDefinition& attr) {
        if (attr.key == "stockpile") {
            return;
        }
        if (attr.key == "indestructible") {
            if (std::find(u->keywords.begin(), u->keywords.end(), attr.key) == u->keywords.end()) {
                u->keywords.push_back(attr.key);
            }
            return;
        }
        if (attr.amount.has_value()) {
            set_entity_attribute_amount(*u, attr.key, *attr.amount);
        } else {
            add_entity_attribute(*u, attr.key);
        }
    };
    for (const auto& attr : def.keywords) {
        install_native_card_keyword(attr);
    }
    for (const auto& attr : ud.keywords) {
        install_native_card_keyword(attr);
    }
    for (const auto& effect : ud.initial_effects) {
        add_entity_effect(*u, effect.key, effect.amount);
    }
    {
        std::string resolve_err;
        if (!resolve_passive_abilities_for_unit(ud.passive_ability_ids, ud.passive_abilities, u->passive_abilities, resolve_err)) {
            static_cast<void>(resolve_err);
        }
    }
    {
        std::string resolve_err;
        if (!resolve_activated_abilities_for_card(def.abilities, def.ability_overrides, u->activated_abilities, resolve_err)) {
            static_cast<void>(resolve_err);
        } else if (!ud.activated_abilities.empty()) {
            u->activated_abilities.insert(u->activated_abilities.end(),
                ud.activated_abilities.begin(), ud.activated_abilities.end());
        }
    }
    u->unit_types = def.unit_types;
    u->source_card_id = inst.public_id;
    normalize_entity_shape(*u);
    u->moves_remaining_this_turn = entity_can_move(*u) ? 1 + u->bonus_moves : 0;
    u->attacks_remaining_this_turn = 1 + u->bonus_attacks;
    u->has_moved_this_turn = false;
    u->has_attacked_this_turn = false;
    u->reactions_remaining_this_turn = 3;
    u->frenzy_triggered_this_turn = false;
    return u;
}

CardInstanceId deck_allocate_instance(Deck& deck, const CardDefId def_id, const int copy_index)
{
    CardDefinition def;
    if (!try_get_card_definition(def_id, def)) {
        return {};
    }
    const std::string public_id = def.key + "_" + std::to_string(copy_index);
  return deck.pool.emplace(def_id, public_id, stockpile_from_definition(def));
}

CardInstanceId deck_allocate_instance(Deck& deck, const std::string& def_key, const int copy_index)
{
    const CardDefId def_id = try_card_def_id_for_key(def_key);
    if (!def_id.is_valid()) {
        return {};
    }
    return deck_allocate_instance(deck, def_id, copy_index);
}

CardInstanceId deck_import_legacy_card(Deck& deck, const Card& legacy)
{
    const std::string effective_key = legacy.definition_key.empty() ? legacy.card_id : legacy.definition_key;
    CardDefId def_id = try_card_def_id_for_key(effective_key);
    if (!def_id.is_valid() && !effective_key.empty()) {
        CardDefinition imported;
        imported.key = effective_key;
        imported.name = legacy.name;
        imported.type = legacy.card_type;
        imported.rules_text = legacy.rules_text;
        imported.art_id = legacy.art_id;
        imported.tags = legacy.tags;
        imported.unit_types = legacy.unit_types;
        imported.energy_cost = legacy.energy_cost;
        imported.keywords.clear();
        for (const auto& kw : legacy.keywords) {
            imported.keywords.push_back({kw, std::nullopt});
        }
        if (const auto* uc = dynamic_cast<const UnitCard*>(&legacy)) {
            UnitCardDefinition ud;
            const Unit& u = uc->template_unit;
            ud.entity_type = u.entity_type;
            ud.unit_type = u.unit_type;
            ud.attack_type = u.attack_type;
            ud.base_health = u.base_health;
            ud.current_health = u.current_health;
            ud.movement = u.movement;
            ud.melee_range = u.melee_range;
            ud.melee_damage = u.melee_damage;
            ud.melee_damage_min = u.melee_damage_min;
            ud.melee_damage_max = u.melee_damage_max;
            ud.ranged_range = u.ranged_range;
            ud.ranged_deadzone = u.ranged_deadzone;
            ud.ranged_damage = u.ranged_damage;
            ud.ranged_damage_min = u.ranged_damage_min;
            ud.ranged_damage_max = u.ranged_damage_max;
            ud.crit_chance_percent = u.crit_chance_percent;
            ud.line_of_sight_blocked = u.line_of_sight_blocked;
            ud.shape = u.shape;
            ud.passive_abilities = u.passive_abilities;
            ud.passive_ability_ids.clear();
            imported.unit = std::move(ud);
        } else if (const auto* sp = dynamic_cast<const SpellCard*>(&legacy)) {
            SpellCardDefinition sd;
            sd.speed = sp->speed;
            sd.effect_key = sp->effect_key;
            sd.effect_payload = sp->effect_payload;
            sd.board_target_kind = sp->board_target_kind;
            sd.requires_mandatory_board_cell = sp->requires_mandatory_board_cell;
            sd.focus_range = sp->focus_range;
            sd.require_target_unit_types = sp->require_target_unit_types;
            sd.bonus_damage_unit_types = sp->bonus_damage_unit_types;
            sd.bonus_damage_amount = sp->bonus_damage_amount;
            imported.spell = std::move(sd);
        }
        {
            register_runtime_card_definition(std::move(imported));
        }
        def_id = try_card_def_id_for_key(effective_key);
    }
    if (!def_id.is_valid()) {
        return {};
    }
    const CardInstanceId id = deck.pool.emplace(def_id, legacy.card_id.empty() ? effective_key : legacy.card_id,
        legacy.stockpile_amount);
    CardInstance& inst = deck.pool.at(id);
    inst.stockpile_remaining = legacy.stockpile_remaining;
    inst.stockpile_used_this_turn = legacy.stockpile_used_this_turn;
    return id;
}

}  // namespace tactics
