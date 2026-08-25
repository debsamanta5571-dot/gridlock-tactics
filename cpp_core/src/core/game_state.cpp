#include "tactics/core/game_state.hpp"

#include "tactics/apps/sandbox_match.hpp"
#include "tactics/core/passive_action_order.hpp"

#include "tactics/cards/card_catalog.hpp"
#include "tactics/cards/card_runtime.hpp"
#include "tactics/cards/focus_spell.hpp"
#include "tactics/cards/passive_catalog.hpp"
#include "tactics/content/card_glossary.hpp"
#include "tactics/cards/unit_types.hpp"
#include "tactics/combat/ability_resolve_viz.hpp"
#include "tactics/combat/taunt.hpp"

#include "tactics/actions/actions.hpp"
#include "tactics/actions/move_resolution.hpp"
#include "tactics/common/effect_keys.hpp"
#include "tactics/core/stack_targets.hpp"
#include "tactics/entities/entity.hpp"
#include "tactics/entities/player_base.hpp"
#include "tactics/attributes/attributes.hpp"
#include "tactics/board/adjacency.hpp"
#include "tactics/board/grid.hpp"
#include "tactics/board/movement_policy.hpp"
#include "tactics/board/push_displacement.hpp"
#include "tactics/board/trench.hpp"
#include "tactics/board/tile_modifiers.hpp"
#include "tactics/combat/combat_resolver.hpp"
#include "tactics/combat/aoe_shapes.hpp"
#include "tactics/combat/directional_area.hpp"
#include "tactics/effects/effect_traits.hpp"
#include "tactics/combat/evasive.hpp"
#include "tactics/combat/low_cover.hpp"
#include "tactics/combat/soul_steal.hpp"
#include "tactics/common/effect_keys.hpp"
#include "tactics/effects/effect_registry.hpp"
#include "tactics/effects/effect_traits.hpp"
#include "tactics/effects/status_effect_catalog.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <queue>
#include <random>
#include <set>
#include <unordered_map>
#include <stdexcept>
#include <sstream>
#include <string>

namespace tactics {

namespace {

// Forward declarations for fire helpers defined later in this file but used earlier.
int place_fire_tile_overlays_on_unoccupied(GameState& game, const StackItem& item,
    const std::vector<std::pair<int, int>>& cells, const int duration);
void fire_units_on_cells(GameState& game, const std::vector<std::pair<int, int>>& cells, const char* source);

int movement_path_terrain_damage(const GameState& game, const Entity& entity, const std::vector<std::pair<int, int>>& path) {
    return path_footprint_terrain_damage([&game](int x, int y) { return game.board.get_square(x, y); }, entity, path);
}

namespace {

bool restore_batched_spell_card_refund(Deck& deck, const CardInstanceId card_id, const bool from_reserves,
    const bool had_stockpile, const int stockpile_remaining, const bool stockpile_used_this_turn,
    const bool stockpile_double_play_used)
{
    if (!card_id.is_valid()) {
        return true;
    }
    // Stockpile spells with charges left after a play are returned straight to hand/reserves
    // (see Deck::play_card / play_card_from_reserves) - they never enter discard. Only pull the
    // card back from discard when it actually went there; otherwise it is already in a live zone
    // and we just need to restore its stockpile charge state below.
    const bool already_live =
        std::find(deck.hand.begin(), deck.hand.end(), card_id) != deck.hand.end()
        || std::find(deck.reserves.begin(), deck.reserves.end(), card_id) != deck.reserves.end();
    if (!already_live) {
        const bool returned = from_reserves
            ? deck.return_batched_spell_from_discard_to_reserves(card_id)
            : deck.return_batched_spell_from_discard_to_hand(card_id);
        if (!returned) {
            return false;
        }
    }
    if (had_stockpile) {
        CardInstance& inst = deck.pool.at(card_id);
        inst.stockpile_remaining = stockpile_remaining;
        inst.stockpile_used_this_turn = stockpile_used_this_turn;
        inst.stockpile_double_play_used_this_turn = stockpile_double_play_used;
    }
    return true;
}

bool stack_queued_batch_caster_still_valid(const GameState& game, const StackItem& item)
{
    if (item.source_entity_id.empty() || item.source_type != "ability") {
        return true;
    }
    const auto src_it = game.board.all_entities_map.find(item.source_entity_id);
    if (src_it == game.board.all_entities_map.end() || !src_it->second) {
        return false;
    }
    const Entity& source = *src_it->second;
    if (source.current_health <= 0) {
        return false;
    }
    return !entity_is_jammed(source) && !entity_is_stunned(source);
}

bool stack_queued_batch_item_still_valid(GameState& game, const StackItem& item)
{
    if (item.source_type == "focus_spell") {
        return queued_focus_spell_still_valid(game, item);
    }
    if (!stack_queued_batch_caster_still_valid(game, item)) {
        return false;
    }
    const auto xit = item.targets.find(effect_keys::kCellX);
    const auto yit = item.targets.find(effect_keys::kCellY);
    if (xit == item.targets.end() || yit == item.targets.end()) {
        return true;
    }
    const auto resolved = resolve_stack_board_target(game, item, item.effect_key);
    if (!resolved.status.ok) {
        return false;
    }
    if (resolved.status.message.find("fizzled") != std::string::npos) {
        return false;
    }
    if (!resolved.target && effect_requires_entity_at_target_cell(item.effect_key)) {
        return false;
    }
    return resolved.status.ok;
}

}  // namespace

/** Remove all stacks of one randomly chosen negative status on `target`. Returns the key removed. */
std::optional<std::string> remove_one_random_negative_status(Entity& target, std::mt19937& rng)
{
    std::vector<std::string> negative_keys_present;
    for (const EntityEffectInstance& eff : target.entity_effects) {
        if (eff.amount > 0 && is_negative_status_effect(eff.key)
                && std::find(negative_keys_present.begin(), negative_keys_present.end(), eff.key)
                    == negative_keys_present.end()) {
            negative_keys_present.push_back(eff.key);
        }
    }
    if (negative_keys_present.empty()) {
        return std::nullopt;
    }
    std::uniform_int_distribution<int> pick(0, static_cast<int>(negative_keys_present.size()) - 1);
    const std::string& key = negative_keys_present[pick(rng)];
    const int amt = entity_effect_amount(target, key);
    if (amt > 0) {
        reduce_entity_effect(target, key, amt);
    }
    return key;
}

constexpr const char kEnergyStorageEffectKey[] = "consume_floating_energy_for_storage";
constexpr const char kEnergyStorageEntityEffectKey[] = "ancient_frog_stored_energy";

int max_energy_storage_for_entity(const Entity& entity)
{
    int max_storage = 16;
    for (const PassiveAbilitySpec& passive : entity.passive_abilities) {
        if (passive.automated_effect_key == kEnergyStorageEffectKey) {
            const auto it = passive.automated_effect_payload.find("max_storage");
            if (it != passive.automated_effect_payload.end()) {
                max_storage = std::max(0, it->second);
            }
            break;
        }
    }
    return max_storage;
}

bool entity_has_ancient_frog_store_passive(const Entity& entity)
{
    for (const PassiveAbilitySpec& passive : entity.passive_abilities) {
        if (passive.key == "ancient_frog_store_passive") {
            return true;
        }
    }
    return false;
}

/** Ancient Frog only: unrestricted float + flux-energy (spell_ability) tagged pool. */
int drain_ancient_frog_floating_energy(GameState& game, const int controller_id)
{
    int gained = 0;
    auto& unrestricted = game.turn_manager.player_energy[controller_id];
    for (const EnergyType et : kEnergyBillingAllTypes) {
        const int amt = std::max(0, unrestricted[et]);
        gained += amt;
        unrestricted[et] = 0;
    }
    auto tag_it = game.turn_manager.player_tagged_float.find("spell_ability");
    if (tag_it != game.turn_manager.player_tagged_float.end()) {
        auto pit = tag_it->second.find(controller_id);
        if (pit != tag_it->second.end()) {
            const int amt = std::max(0, pit->second[EnergyType::Turquoise]);
            gained += amt;
            pit->second[EnergyType::Turquoise] = 0;
        }
    }
    return gained;
}

}  // namespace

GameState::GameState(std::string game_id, std::optional<uint64_t> rng_seed)
    : GameState(std::move(game_id), make_default_map_layout(), rng_seed) {}

GameState::GameState(std::string game_id, int width, int height, std::optional<uint64_t> rng_seed)
    : GameState(std::move(game_id), make_standard_duel_layout(width, height), rng_seed) {}

GameState::GameState(std::string game_id, BoardLayoutSpec layout, std::optional<uint64_t> rng_seed)
    : turn_manager(board), game_id_(std::move(game_id)), layout_spec_(std::move(layout)) {
    board_width_ = layout_spec_.nominal_width > 0 ? layout_spec_.nominal_width : 1;
    board_height_ = layout_spec_.nominal_height > 0 ? layout_spec_.nominal_height : 1;
    if (board_width_ < kPlayerBaseWidth || board_height_ < 1) {
        throw std::invalid_argument("GameState: board width must be >= 4 and height must be >= 1");
    }
    if (rng_seed) {
        rng_.seed(*rng_seed);
    } else {
        std::random_device rd;
        rng_.seed(rd());
    }
    apply_layout_spec(layout_spec_);
    register_default_effects();
}

void GameState::apply_layout_spec(const BoardLayoutSpec& layout)
{
    for (const auto& e : board.all_entities()) {
        board.remove_entity(e);
    }
    board.clear_geometry();
    layout_spec_ = layout;
    board_width_ = layout_spec_.nominal_width > 0 ? layout_spec_.nominal_width : 1;
    board_height_ = layout_spec_.nominal_height > 0 ? layout_spec_.nominal_height : 1;
    if (!apply_board_layout(board, layout_spec_)) {
        throw std::runtime_error("GameState: failed to apply board layout");
    }
}

bool GameState::is_deploy_zone_cell_for_player(int player_id, int x, int y) const
{
    const std::optional<BoardRectZone> zone = deploy_zone_for_player(layout_spec_, player_id);
    if (!zone) {
        return board.get_square(x, y) != nullptr;
    }
    return rect_zone_contains(*zone, x, y);
}

bool GameState::can_deploy_entity_at(int player_id, const std::shared_ptr<Entity>& entity, int anchor_x, int anchor_y,
    int unit_cost) const
{
    if (!entity) {
        return false;
    }

    // Compute footprint cells once; all rules below use them.
    const auto offsets = entity_shape_offsets(*entity);
    std::vector<std::pair<int,int>> footprint;
    footprint.reserve(offsets.size());
    for (const auto& [dx, dy] : offsets) footprint.push_back({anchor_x + dx, anchor_y + dy});

    // ── Normal positioning rules ────────────────────────────────────────────────────────────────
    // Determine whether the placement satisfies at least one of the three standard rules.
    bool position_ok = false;

    const std::optional<BoardRectZone> zone = deploy_zone_for_player(layout_spec_, player_id);
    if (!zone) {
        position_ok = true;
    }

    // Primary rule: all footprint cells must lie within the player's deployment zone.
    if (!position_ok && zone) {
        bool in_deploy_zone = true;
        for (const auto& [fx, fy] : footprint) {
            if (!rect_zone_contains(*zone, fx, fy)) { in_deploy_zone = false; break; }
        }
        if (in_deploy_zone) position_ok = true;
    }

    // Secondary rule (structures only): any footprint cell adjacent (Chebyshev 1)
    // to a cell occupied by a friendly unit also counts as a legal placement.
    // In team games, allied-team units also count as friendly for placement purposes.
    if (!position_ok && entity_is_building(*entity)) {
        for (const auto& candidate : board.all_entities()) {
            if (!candidate->owner || teams_hostile(*this, player_id, *candidate->owner)) continue;
            if (candidate->entity_type != "unit") continue;
            if (!candidate->position) continue;
            const auto unit_offsets = entity_shape_offsets(*candidate);
            const auto& [ux, uy] = *candidate->position;
            for (const auto& [udx, udy] : unit_offsets) {
                const int ucx = ux + udx;
                const int ucy = uy + udy;
                for (const auto& [fx, fy] : footprint) {
                    if (std::abs(fx - ucx) <= 1 && std::abs(fy - ucy) <= 1) {
                        position_ok = true;
                        goto secondary_done;
                    }
                }
            }
        }
        secondary_done:;
    }

    // Tertiary rule (Command keyword): any footprint cell orthogonally adjacent (4-way) to a
    // friendly non-silenced unit whose Command X >= unit_cost allows deploy there.
    // In team games, allied-team Command units also enable this deploy path.
    // Only 1×1 units (single-tile footprint) may use this path.
    if (!position_ok && unit_cost >= 0 && footprint.size() == 1) {
        for (const auto& candidate : board.all_entities()) {
            if (!candidate->owner || teams_hostile(*this, player_id, *candidate->owner)) continue;
            if (candidate->entity_type != "unit") continue;
            if (!candidate->position) continue;
            const int cmd = command_value(*candidate);
            if (cmd <= 0 || cmd < unit_cost) continue;
            const auto unit_offsets = entity_shape_offsets(*candidate);
            const auto& [ux, uy] = *candidate->position;
            for (const auto& [udx, udy] : unit_offsets) {
                const int ucx = ux + udx;
                const int ucy = uy + udy;
                for (const auto& [fx, fy] : footprint) {
                    // Orthogonal adjacency only (4-way per project vocabulary).
                    if (std::abs(fx - ucx) + std::abs(fy - ucy) == 1) {
                        position_ok = true;
                        goto tertiary_done;
                    }
                }
            }
        }
        tertiary_done:;
    }

    if (!position_ok) return false;

    // ── Spearhead constraint (structures only) ──────────────────────────────────────────────────
    // Spearhead X is an ADDITIONAL requirement on top of normal placement rules: the structure's
    // footprint must also be within X Chebyshev tiles of at least one enemy base cell.
    const int spearhead_range = spearhead_value(*entity);
    if (entity_is_building(*entity) && spearhead_range > 0) {
        for (const auto& candidate : board.all_entities()) {
            if (!entity_is_base(*candidate) || !candidate->owner || !candidate->position) continue;
            if (!teams_hostile(*this, player_id, *candidate->owner)) continue;
            const auto base_offsets = entity_shape_offsets(*candidate);
            const auto& [bx, by] = *candidate->position;
            for (const auto& [bdx, bdy] : base_offsets) {
                const int bcx = bx + bdx;
                const int bcy = by + bdy;
                for (const auto& [fx, fy] : footprint) {
                    if (std::max(std::abs(fx - bcx), std::abs(fy - bcy)) <= spearhead_range) {
                        return true;
                    }
                }
            }
        }
        return false;  // Normal rules passed but not within spearhead range of any enemy base.
    }

    return true;
}

std::mt19937& GameState::rng() { return rng_; }

const std::mt19937& GameState::rng() const { return rng_; }

std::string GameState::apply_overload_stacks(const std::shared_ptr<Entity>& target, const int amount,
                                             const std::optional<int> applier_player_id)
{
    if (!target || amount <= 0) {
        return {};
    }
    // overload_resistance halves incoming stack amounts (floor). A 1-stack application has no effect.
    // Combined with the blast-loop halving, resistant entities accumulate slower AND take less damage.
    const int effective = entity_passive_mechanic_active(*target, "overload_resistance") ? round_down_half(amount) : amount;
    if (effective <= 0) {
        return target->entity_id + " resisted overload (overload_resistance)";
    }
    int attributed_player = applier_player_id.value_or(-1);
    if (attributed_player < 0 && resolving_stack_item_.has_value()) {
        attributed_player = resolving_stack_item_->controller_id;
    }
    if (attributed_player >= 0) {
        turn_manager.player_overload_applied_total[attributed_player] += effective;
    }
    if (!add_entity_effect(*target, "overload", effective)) {
        return target->entity_id + " is immune to overload";
    }
    // Overload now explodes at end of turn (not immediately). Stacks persist until then.
    // See process_end_of_turn_dot: >kOverloadExplosionThreshold stacks → 1 damage, reset.
    const int total = entity_effect_amount(*target, "overload");
    return "Applied " + std::to_string(amount) + " overload to " + target->entity_id
           + " (" + std::to_string(total) + " stack(s))";
}

void GameState::begin_stack_effect_resolution(const StackItem* item)
{
    ability_damage_dealt_this_stack_resolution_.clear();
    resolving_stack_item_ = item ? std::optional<StackItem>(*item) : std::nullopt;
}

void GameState::end_stack_effect_resolution()
{
    if (resolving_stack_item_.has_value()) {
        emit_ability_resolve_result_label_popups_if_needed(*this);
    }
    resolving_stack_item_.reset();
}

const StackItem* GameState::resolving_stack_item_ptr() const
{
    return resolving_stack_item_.has_value() ? &*resolving_stack_item_ : nullptr;
}

void GameState::enqueue_ability_damage_popup_event(AbilityResolveVizHit hit)
{
    ability_damage_popup_events_.push_back(std::move(hit));
    if (ability_resolve_viz_capturing_) {
        ability_resolve_viz_capture_buffer_.push_back(ability_damage_popup_events_.back());
    }
}

bool GameState::is_resolving_ability_stack_effect() const
{
    return resolving_stack_item_.has_value() && resolving_stack_item_->source_type == "ability";
}

bool GameState::should_emit_ability_damage_popup() const
{
    if (ability_resolve_viz_capturing_) {
        return true;
    }
    if (!resolving_stack_item_.has_value()) {
        return false;
    }
    const std::string& source_type = resolving_stack_item_->source_type;
    return source_type == "ability" || source_type == "spell" || source_type == "focus_spell";
}

bool GameState::try_consume_ability_damage_popup_event(AbilityResolveVizHit& out)
{
    if (ability_damage_popup_events_.empty()) {
        return false;
    }
    out = ability_damage_popup_events_.front();
    ability_damage_popup_events_.erase(ability_damage_popup_events_.begin());
    return true;
}

void GameState::log_ability_damage_popup(const Entity& target, const int amount, const bool is_heal)
{
    if (!combat_visualization_enabled_ || amount <= 0 || !should_emit_ability_damage_popup()) {
        return;
    }
    AbilityResolveVizHit hit;
    hit.entity_id = target.entity_id;
    if (target.position) {
        hit.grid_x = target.position->first;
        hit.grid_y = target.position->second;
    } else if (!target.occupied_positions.empty()) {
        hit.grid_x = target.occupied_positions.front().first;
        hit.grid_y = target.occupied_positions.front().second;
    }
    hit.amount = amount;
    hit.is_heal = is_heal;
    if (resolving_stack_item_.has_value()) {
        const StackItem& item = *resolving_stack_item_;
        hit.event_label = item.source_name.empty() ? item.effect_key : item.source_name;
    }
    enqueue_ability_damage_popup_event(std::move(hit));
}

void GameState::backfill_pending_ability_damage_popup_labels(const std::string& label)
{
    if (label.empty()) {
        return;
    }
    for (AbilityResolveVizHit& hit : ability_damage_popup_events_) {
        if (hit.event_label.empty()) {
            hit.event_label = label;
        }
    }
}

bool GameState::has_ability_damage_popup_for_entity(const std::string& entity_id) const
{
    if (entity_id.empty()) {
        return false;
    }
    for (const AbilityResolveVizHit& hit : ability_damage_popup_events_) {
        if (hit.entity_id == entity_id) {
            return true;
        }
    }
    return false;
}

void GameState::try_record_ability_resolve_viz_hit(const Entity& target, const int amount, const bool is_heal)
{
    log_ability_damage_popup(target, amount, is_heal);
}

void GameState::note_ability_damage_dealt(const std::string& entity_id)
{
    if (!entity_id.empty()) {
        ability_damage_dealt_this_stack_resolution_.insert(entity_id);
    }
}

void GameState::note_exalted_ability_damage_dealt(const int damage)
{
    if (damage <= 0 || !resolving_stack_item_.has_value()) {
        return;
    }
    const StackItem& item = *resolving_stack_item_;
    if (item.source_type != "ability" || item.controller_id < 0) {
        return;
    }
    turn_manager.player_ability_damage_dealt_total[item.controller_id] += damage;
}

namespace {

void strip_on_damage_primer_by_id(Entity& entity, const char* effect_id)
{
    if (!effect_id || effect_id[0] == '\0') {
        return;
    }
    auto& effects = entity.temporary_effects;
    effects.erase(std::remove_if(effects.begin(), effects.end(),
                      [effect_id](const TemporaryEntityEffect& effect) { return effect.effect_id == effect_id; }),
        effects.end());
}

void strip_next_ability_on_hit_primers(Entity& entity)
{
    strip_on_damage_primer_by_id(entity, effect_keys::kOnDamageApplyMovementReductionNextAbilityEffectId);
    strip_on_damage_primer_by_id(entity, effect_keys::kOnDamageApplyRootedNextAbilityEffectId);
    strip_on_damage_primer_by_id(entity, effect_keys::kOnDamageApplyBleedNextAbilityEffectId);
    strip_on_damage_primer_by_id(entity, effect_keys::kOnDamageApplyOverloadNextAbilityEffectId);
    strip_on_damage_primer_by_id(entity, effect_keys::kOnDamageApplyJammedNextAbilityEffectId);
}

void apply_movement_reduction_debuff(GameState& game, const std::shared_ptr<Entity>& victim, const int amount)
{
    if (!victim || amount <= 0) {
        return;
    }
    TemporaryEntityEffect slow;
    slow.effect_id = "movement_reduction";
    slow.name = "Movement Reduction";
    slow.rules_text = "-" + std::to_string(amount) + " movement until end of this unit's owner's turn.";
    slow.expire_on = "owner_turn_end";
    slow.remaining_turns = 1;
    slow.stat_grants.bonus_movement = -amount;
    game.add_temporary_effect(victim, std::move(slow));
}

}  // namespace

void GameState::consume_next_ability_on_hit_primers_if_needed(const std::string& entity_id)
{
    if (entity_id.empty() || !ability_damage_dealt_this_stack_resolution_.count(entity_id)) {
        return;
    }
    const auto it = board.all_entities_map.find(entity_id);
    if (it != board.all_entities_map.end() && it->second) {
        strip_next_ability_on_hit_primers(*it->second);
    }
    ability_damage_dealt_this_stack_resolution_.erase(entity_id);
}

namespace {
void apply_gas_on_hit_tiles(GameState& game, const Entity& source, const std::shared_ptr<Entity>& victim, int damage_dealt);
}  // namespace

void GameState::apply_on_damage_dealt_primer_statuses(const Entity& source, const std::shared_ptr<Entity>& victim, const int damage_dealt,
    const bool from_basic_attack)
{
    if (damage_dealt <= 0 || !victim || !board.all_entities_map.contains(victim->entity_id)) {
        return;
    }
    const std::optional<int> applier = source.owner ? std::optional<int>(*source.owner) : std::nullopt;
    if (entity_on_damage_applies_overload(source)) {
        apply_overload_stacks(victim, 1, applier);
    }
    // shock_on_hit: fires when damage to any unit - apply 1 overload to that unit.
    if (entity_has_shock_on_hit(source) && victim->entity_type == "unit") {
        apply_overload_stacks(victim, 1, applier);
    }
    // Overload explosion can destroy the victim synchronously; re-check before applying jammed.
    if (victim->current_health <= 0 || !board.all_entities_map.contains(victim->entity_id)) {
        return;
    }
    if (entity_on_damage_applies_jammed(source)) {
        add_entity_effect(*victim, "jammed", 1);
    }
    if (victim->current_health <= 0 || !board.all_entities_map.contains(victim->entity_id)) {
        return;
    }
    if (entity_on_damage_applies_bleed(source)) {
        add_entity_effect(*victim, "bleed", 1);
    }
    if (entity_on_damage_applies_overload_next_ability(source)) {
        apply_overload_stacks(victim, 1, applier);
        if (!source.entity_id.empty()) {
            const auto src_it = board.all_entities_map.find(source.entity_id);
            if (src_it != board.all_entities_map.end() && src_it->second) {
                strip_on_damage_primer_by_id(*src_it->second, effect_keys::kOnDamageApplyOverloadNextAbilityEffectId);
            }
        }
    }
    if (victim->current_health > 0 && board.all_entities_map.contains(victim->entity_id)
            && entity_on_damage_applies_jammed_next_ability(source)) {
        add_entity_effect(*victim, "jammed", 1);
        if (!source.entity_id.empty()) {
            const auto src_it = board.all_entities_map.find(source.entity_id);
            if (src_it != board.all_entities_map.end() && src_it->second) {
                strip_on_damage_primer_by_id(*src_it->second, effect_keys::kOnDamageApplyJammedNextAbilityEffectId);
            }
        }
    }
    if (entity_has_gas_on_hit(source)) {
        apply_gas_on_hit_tiles(*this, source, victim, damage_dealt);
    }
    if (entity_has_fire_on_hit(source) && victim->current_health > 0
            && board.all_entities_map.contains(victim->entity_id)) {
        const int fire_stacks = std::max(1, entity_passive_mechanic_amount(source, "fire_on_hit", 1));
        add_entity_effect(*victim, "fire", fire_stacks);
    }
    if (!from_basic_attack && victim->entity_type == "unit") {
        if (entity_on_damage_applies_movement_reduction_next_ability(source)) {
            apply_movement_reduction_debuff(*this, victim, 1);
        }
        if (victim->current_health <= 0 || !board.all_entities_map.contains(victim->entity_id)) {
            return;
        }
        if (entity_on_damage_applies_rooted_next_ability(source)) {
            add_entity_effect(*victim, "rooted", 1);
        }
        if (!source.entity_id.empty()) {
            note_ability_damage_dealt(source.entity_id);
        }
    }
}

namespace {

// Trigger predicates live here so a new reactive trigger only needs a branch in this function.
bool passive_requires_source_phase_survival(const PassiveAbilitySpec& passive)
{
    const auto it = passive.reactive_string_payload.find("source_survive_until");
    return it != passive.reactive_string_payload.end() && it->second == "phase_resolution";
}

bool reactive_trigger_condition_met(const std::string& trigger, const Entity& victim)
{
    if (trigger == "damage_dealt_enemy_unit_survive") {
        return victim.entity_type == "unit" && victim.current_health > 0;
    }
    if (trigger == "damage_dealt_enemy_unit") {
        return victim.entity_type == "unit";
    }
    if (trigger == "damage_dealt_enemy_survive") {
        return victim.current_health > 0;
    }
    if (trigger == "damage_dealt_enemy") {
        return true;
    }
    return false;  // Unknown / future trigger - no-op
}

// N4/F2: Helper for reactive passive energy-gain effects. Handles all energy colors uniformly.
// To add support for a new reactive effect type, add a branch here alongside the energy checks.
// `amount_override`: when >= 0, uses this value instead of reading from payload (for capped grants).
void apply_reactive_energy_gain(TurnManager& turn_manager, const int controller, const PassiveAbilitySpec& passive,
    const int amount_override = -1)
{
    const std::string& fx = passive.reactive_effect_key;
    int amount = amount_override >= 0 ? amount_override : 1;
    if (amount_override < 0) {
        if (const auto it = passive.reactive_effect_payload.find(effect_keys::kPayloadAmount);
            it != passive.reactive_effect_payload.end()) {
            amount = std::max(0, it->second);
        }
    }
    if (amount <= 0) return;

    // Determine target EnergyType from effect key.
    EnergyType etype = EnergyType::Neutral;
    if      (fx == "gain_orange")  etype = EnergyType::Orange;
    else if (fx == "gain_neutral") etype = EnergyType::Neutral;
    else if (fx == "gain_red")     etype = EnergyType::Red;
    else if (fx == "gain_turquoise") etype = EnergyType::Turquoise;
    else if (fx == "gain_purple")  etype = EnergyType::Purple;
    else if (fx == "gain_green")   etype = EnergyType::Green;
    else if (fx == "gain_omni")    etype = EnergyType::Omni;
    else return; // unknown effect key - no-op

    // If a pool tag is specified, route into the tagged float pool.
    const auto pool_it = passive.reactive_string_payload.find("pool");
    if (pool_it != passive.reactive_string_payload.end() && !pool_it->second.empty()) {
        credit_tagged_float(turn_manager, pool_it->second, controller, etype, amount);
        return;
    }

    // Otherwise deposit into the unrestricted pool.
    auto itp = turn_manager.player_energy.find(controller);
    if (itp == turn_manager.player_energy.end()) return;
    itp->second[etype] += amount;
    // Additional reactive effects (draw_cards, apply_status, etc.) can be added here
}

// Spawn helper for "spawn_melee_robot_on_hit" reactive effect.
// Finds an unoccupied 4-way adjacent cell of the victim's footprint, creates a melee robot token
// with HP and nominal attack equal to damage_dealt, and places it on the board.
// The robot is owned by the source's player and has movement 3.
void spawn_replication_robot(GameState& game, const Entity& source, const Entity& victim, int damage_dealt)
{
    if (!victim.position || !source.owner) {
        return;
    }

    // Gather unoccupied adjacent (4-way) cells of the victim (handles multi-tile victims).
    std::vector<std::pair<int, int>> candidates;
    for (const auto& [cx, cy] : entity_adjacent_cells(victim)) {
        const auto sq = game.board.get_square(cx, cy);
        if (sq && !sq->occupied) {
            candidates.emplace_back(cx, cy);
        }
    }
    if (candidates.empty()) {
        return;
    }

    std::uniform_int_distribution<int> pick(0, static_cast<int>(candidates.size()) - 1);
    const auto [spawn_x, spawn_y] = candidates[pick(game.rng())];

    const int d = std::max(1, damage_dealt);
    auto robot = std::make_shared<Unit>();
    robot->entity_type = "unit";
    robot->unit_type = "Replicator Bot";
    robot->owner = source.owner;
    robot->attack_type = AttackType::Melee;
    robot->base_health = d;
    robot->current_health = d;
    robot->melee_damage = d;
    robot->movement = 3;
    robot->shape = {{0, 0}};
    normalize_entity_shape(*robot);
    sync_unit_damage_ranges_from_nominal(*robot);
    game.assign_monotonic_entity_id(robot, "replicator_bot_" + source.entity_id);

    if (!game.board.place_entity(robot, spawn_x, spawn_y)) {
        return;
    }
    game.note_entity_placed(robot);
    // Spawn with 1 stunned stack so the bot cannot act until its owner's next turn start.
    add_entity_effect(*robot, "stunned", 1);
    game.mark_passive_auras_dirty();
}

// Spawn helper for the "dynamic_deployable_replicate" focus spell.
// Creates a melee Replicator Bot adjacent to the victim's footprint, inheriting the caster's
// keywords (minus shape/deck-dependent ones). HP and nominal melee damage both equal damage_dealt;
// sync_unit_damage_ranges_from_nominal then gives the natural 3-point spread (e.g. 1–3 for d=2).
// Bot spawns stunned.
void spawn_dynamic_deployable_duplicator_bot(GameState& game, const Entity& caster, const Entity& victim, int damage_dealt)
{
    if (!caster.owner) {
        return;
    }

    // Collect unoccupied adjacent (4-way) cells of the victim's footprint.
    std::vector<std::pair<int, int>> candidates;
    for (const auto& [cx, cy] : entity_adjacent_cells(victim)) {
        const auto sq = game.board.get_square(cx, cy);
        if (sq && !sq->occupied) {
            candidates.emplace_back(cx, cy);
        }
    }
    if (candidates.empty()) {
        return;
    }

    std::uniform_int_distribution<int> pick(0, static_cast<int>(candidates.size()) - 1);
    const auto [spawn_x, spawn_y] = candidates[pick(game.rng())];

    const int d = std::max(1, damage_dealt);
    auto robot = std::make_shared<Unit>();
    robot->entity_type = "unit";
    robot->unit_type = "Replicator Bot";
    robot->owner = caster.owner;
    robot->attack_type = AttackType::Melee;
    robot->base_health = d;
    robot->current_health = d;
    robot->melee_damage = d;  // damage_range_from_nominal(d) → (d-1)–(d+1)
    robot->movement = 3;
    robot->shape = {{0, 0}};
    game.assign_monotonic_entity_id(robot, "replicator_bot_" + caster.entity_id);

    // Inherit caster's permanent keywords; skip shape/deck-dependent ones.
    for (const auto& kw : caster.keywords) {
        if (kw == "large_unit" || kw == "stockpile" || kw == "focus") continue;
        robot->keywords.push_back(kw);
        const auto amt_it = caster.keyword_amounts.find(kw);
        if (amt_it != caster.keyword_amounts.end()) {
            robot->keyword_amounts[kw] = amt_it->second;
        }
    }

    normalize_entity_shape(*robot);
    sync_unit_damage_ranges_from_nominal(*robot);

    if (!game.board.place_entity(robot, spawn_x, spawn_y)) {
        return;
    }
    game.note_entity_placed(robot);
    add_entity_effect(*robot, "stunned", 1);
    game.mark_passive_auras_dirty();
}

// --- Mortar / Final Barrage shared helpers ----------------------------------------------------
// Mortar Barrage (passive) and Final Barrage (activation + detonation) share the same firing model:
// pick a random enemy unit in range, blast its footprint, and apply damage+overload to everything
// caught in it. These helpers are the single source of truth for that behavior so the two effects
// (and the Final Barrage self-destruct) can never drift apart.

// Apply a mortar-style blast to a set of victims: deal `damage` (non-pierce, so armor/shields apply),
// then apply `overload` stacks, then destroy whatever died. All damage is dealt before any overload
// so ordering is deterministic regardless of which victims die to the initial hit. Entities that have
// already left the board mid-loop are skipped. Does NOT refresh passive auras - the caller decides
// when to refresh (the self-destruct path destroys the source first).
void apply_mortar_blast_to_victims(GameState& game, const std::vector<std::shared_ptr<Entity>>& victims,
    int damage, int overload, const std::optional<int> applier_player_id = std::nullopt)
{
    std::vector<std::shared_ptr<Entity>> to_destroy;
    for (const auto& v : victims) {
        if (!v || !game.board.all_entities_map.contains(v->entity_id)) continue;
        if (damage > 0) apply_incoming_damage(*v, damage, /*pierce=*/false);
        if (v->current_health <= 0) to_destroy.push_back(v);
    }
    for (const auto& v : victims) {
        if (!v || !game.board.all_entities_map.contains(v->entity_id)) continue;
        if (overload > 0) game.apply_overload_stacks(v, overload, applier_player_id);
    }
    for (const auto& v : to_destroy) {
        if (game.board.all_entities_map.contains(v->entity_id)) game.destroy_board_entity(v);
    }
}

// Gather all entities standing on the target's own cells or any of its 8-way surrounding cells
// (the mortar blast footprint), de-duplicated by entity id. Hits units, structures, and bases.
std::vector<std::shared_ptr<Entity>> collect_mortar_blast_victims(GameState& game, const Entity& target)
{
    std::set<std::pair<int, int>> blast_cells;
    if (target.position) {
        for (const auto& [tdx, tdy] : entity_shape_offsets(target)) {
            blast_cells.insert({target.position->first + tdx, target.position->second + tdy});
        }
    }
    for (const auto& [cx, cy] : entity_surrounding_cells(target)) {
        blast_cells.insert({cx, cy});
    }
    std::vector<std::shared_ptr<Entity>> victims;
    std::set<std::string> seen;
    for (const auto& [bx, by] : blast_cells) {
        auto ent = game.board.entity_at(bx, by);
        if (!ent || !seen.insert(ent->entity_id).second) continue;
        if (ent->entity_type != "unit" && !entity_is_structure(*ent) && !entity_is_base(*ent)) continue;
        victims.push_back(ent);
    }
    return victims;
}

bool hostile_unit_within_chebyshev(const GameState& game, const Entity& source, const int max_range)
{
    if (!source.owner || max_range < 0) {
        return false;
    }
    for (const auto& ent : game.board.all_entities()) {
        if (!ent || ent->current_health <= 0) {
            continue;
        }
        if (ent->entity_id == source.entity_id) {
            continue;
        }
        if (ent->entity_type != "unit") {
            continue;
        }
        if (!ent->owner || !teams_hostile(game, *ent->owner, *source.owner)) {
            continue;
        }
        int best_dist = INT_MAX;
        if (ent->position) {
            for (const auto& [tdx, tdy] : entity_shape_offsets(*ent)) {
                const int tx = ent->position->first + tdx;
                const int ty = ent->position->second + tdy;
                best_dist = std::min(best_dist, min_chebyshev_entity_to_cell(source, tx, ty));
            }
        } else {
            for (const auto& [tx, ty] : ent->occupied_positions) {
                best_dist = std::min(best_dist, min_chebyshev_entity_to_cell(source, tx, ty));
            }
        }
        if (best_dist != INT_MAX && best_dist <= max_range) {
            return true;
        }
    }
    return false;
}

// Fire a single mortar shot from `src`: pick a uniformly random enemy (any entity type) whose
// nearest cell is in [min_range, max_range] (Chebyshev), blast its footprint, then refresh passive
// auras. Returns false if the source is gone or there were no valid targets in range.
bool fire_mortar_shot(GameState& game, const Entity& src, int controller,
    int damage, int overload, int min_range, int max_range)
{
    if (!game.board.all_entities_map.contains(src.entity_id)) return false;
    std::vector<std::shared_ptr<Entity>> candidates;
    for (const auto& ent : game.board.all_entities()) {
        if (!ent || ent->current_health <= 0) continue;
        if (!ent->owner || !teams_hostile(game, *ent->owner, controller)) continue;
        if (!ent->position) continue;
        int best_dist = INT_MAX;
        for (const auto& [tdx, tdy] : entity_shape_offsets(*ent)) {
            const int tx = ent->position->first + tdx;
            const int ty = ent->position->second + tdy;
            best_dist = std::min(best_dist, min_chebyshev_entity_to_cell(src, tx, ty));
        }
        if (best_dist >= min_range && best_dist <= max_range) candidates.push_back(ent);
    }
    if (candidates.empty()) return false;
    std::uniform_int_distribution<int> pick(0, static_cast<int>(candidates.size()) - 1);
    const auto& target = candidates[pick(game.rng())];
    if (!target->position) return false;

    // Snapshot primary target HP before the blast so we can compute damage dealt for the viz.
    const int target_hp_before = target->current_health;
    const std::string target_id = target->entity_id;

    const std::optional<int> applier = src.owner ? std::optional<int>(*src.owner) : std::nullopt;
    apply_mortar_blast_to_victims(game, collect_mortar_blast_victims(game, *target), damage, overload, applier);
    game.refresh_passive_auras();

    // Log a passive viz event so the Unreal side can show the mortar-shot animation.
    if (game.combat_visualization_enabled()) {
        int target_hp_after = 0;
        const auto target_after_it = game.board.all_entities_map.find(target_id);
        if (target_after_it != game.board.all_entities_map.end() && target_after_it->second) {
            target_hp_after = target_after_it->second->current_health;
        }
        const int damage_dealt = std::max(0, target_hp_before - target_hp_after);
        game.log_passive_attack_viz_event({src.entity_id, target_id, damage_dealt, "mortar_shot"});
    }

    return true;
}

}  // namespace

namespace {
struct ReactiveDispatchCtx {
    GameState* game{nullptr};
    Unit* actor_unit{nullptr};
    Entity* mutable_actor{nullptr};
    const Entity* victim_entity{nullptr};
    const Entity* dying_entity{nullptr};
    Entity* attacker_entity{nullptr};
    std::optional<int> killer_owner;
    std::optional<int> player_id;
    int damage_dealt{0};
    int damage_taken{0};
    bool from_melee{false};
    std::optional<EffectSpeed> spell_speed;
    const std::string* batch_item_id{nullptr};
};
bool dispatch_reactive_effect_key(ReactiveDispatchCtx& ctx, const PassiveAbilitySpec& passive);
}  // namespace (reactive dispatch forward declarations)

void GameState::queue_pending_reactive(const std::string& source_id, const Entity& victim_snapshot, int damage_dealt)
{
    pending_reactives_.push_back({source_id, victim_snapshot, damage_dealt});
}

void GameState::queue_phase_pending_reactive(const std::string& source_id, const Entity& victim_snapshot, int damage_dealt)
{
    phase_pending_reactives_.push_back({source_id, victim_snapshot, damage_dealt});
}

namespace {

enum class ReactiveSurviveDeferral {
    Exchange,
    Phase,
};

void apply_passive_reactive_on_damage_dealt_filtered(
    GameState& game, const Entity& source, const Entity& victim, const int damage_dealt,
    const ReactiveSurviveDeferral deferral)
{
    if (damage_dealt <= 0 || entity_is_silenced(source) || source.passive_abilities.empty()) {
        return;
    }
    if (!victim.owner || !source.owner || !teams_hostile(game, *victim.owner, *source.owner)) {
        return;
    }
    const auto src_it = game.board.all_entities_map.find(source.entity_id);
    if (src_it == game.board.all_entities_map.end() || !src_it->second || src_it->second->current_health <= 0) {
        return;
    }
    const int controller = *source.owner;
    Entity& mutable_source = *src_it->second;

    for (const PassiveAbilitySpec& passive : source.passive_abilities) {
        if (passive.reactive_trigger.empty() || passive.reactive_effect_key.empty()) {
            continue;
        }
        const bool phase_survive = passive_requires_source_phase_survival(passive);
        if (phase_survive && deferral != ReactiveSurviveDeferral::Phase) {
            continue;
        }
        if (!phase_survive && deferral == ReactiveSurviveDeferral::Phase) {
            continue;
        }
        if (!reactive_trigger_condition_met(passive.reactive_trigger, victim)) {
            continue;
        }
        ReactiveDispatchCtx ctx;
        ctx.game = &game;
        ctx.mutable_actor = &mutable_source;
        ctx.victim_entity = &victim;
        ctx.damage_dealt = damage_dealt;
        ctx.player_id = controller;
        if (dispatch_reactive_effect_key(ctx, passive)) {
            continue;
        }
        const auto cap_it = passive.reactive_effect_payload.find("turn_cap");
        if (cap_it != passive.reactive_effect_payload.end() && cap_it->second > 0) {
            const int cap = cap_it->second;
            const std::string tally_key = "reactive_tally_" + passive.key;
            const int tally = entity_effect_amount(mutable_source, tally_key);
            if (tally >= cap) {
                continue;
            }
            const auto amt_it = passive.reactive_effect_payload.find(effect_keys::kPayloadAmount);
            const int full_amount = (amt_it != passive.reactive_effect_payload.end()) ? std::max(0, amt_it->second) : 1;
            const int granted = std::min(full_amount, cap - tally);
            if (granted > 0) {
                apply_reactive_energy_gain(game.turn_manager, controller, passive, granted);
                add_entity_effect(mutable_source, tally_key, granted);
            }
        } else {
            apply_reactive_energy_gain(game.turn_manager, controller, passive);
        }
    }
}

}  // namespace

void GameState::fire_pending_reactives()
{
    // Move the list out first so any reactives triggered inside cannot re-enter and
    // double-fire (e.g. a reactive that itself deals damage and defers another reactive).
    std::vector<PendingReactive> batch = std::move(pending_reactives_);
    pending_reactives_.clear();
    for (const PendingReactive& pr : batch) {
        const auto it = board.all_entities_map.find(pr.source_id);
        if (it == board.all_entities_map.end() || !it->second || it->second->current_health <= 0) {
            continue;  // Source did not survive the exchange - reactive suppressed.
        }
        apply_passive_reactive_on_damage_dealt_filtered(
            *this, *it->second, pr.victim_snapshot, pr.damage_dealt, ReactiveSurviveDeferral::Exchange);
    }
}

void GameState::fire_phase_pending_reactives()
{
    std::vector<PendingReactive> batch = std::move(phase_pending_reactives_);
    phase_pending_reactives_.clear();
    for (const PendingReactive& pr : batch) {
        const auto it = board.all_entities_map.find(pr.source_id);
        if (it == board.all_entities_map.end() || !it->second || it->second->current_health <= 0) {
            continue;  // Source did not survive until phase resolution completed.
        }
        apply_passive_reactive_on_damage_dealt_filtered(
            *this, *it->second, pr.victim_snapshot, pr.damage_dealt, ReactiveSurviveDeferral::Phase);
    }
}

bool GameState::entity_has_phase_survive_damage_reactive(const Entity& source) const
{
    for (const PassiveAbilitySpec& passive : source.passive_abilities) {
        if (passive.reactive_trigger.empty() || passive.reactive_effect_key.empty()) {
            continue;
        }
        if (passive_requires_source_phase_survival(passive)) {
            return true;
        }
    }
    return false;
}

bool GameState::is_draining_phase_action_queue() const
{
    return combat_viz_pause_.draining;
}

// Mirror helper for "surrounding_buff_mirror" passive (Estelle Novara etc.).
// After a stackable entity-effect buff is applied to an entity (e.g. next_damage_bonus), call this
// to propagate that same buff to any surrounding unit that holds the passive.
// Does NOT chain: the mirrored application does not trigger another propagation.
static void propagate_buff_to_surrounding_mirror_passives(
    GameState& game, const Entity& buffed_entity, const std::string& buff_key, int amount)
{
    if (amount <= 0) { return; }
    for (const auto& [cx, cy] : entity_surrounding_cells(buffed_entity)) {
        auto neighbor = game.board.entity_at(cx, cy);
        if (!neighbor) { continue; }
        if (neighbor->entity_id == buffed_entity.entity_id) { continue; }
        if (entity_is_silenced(*neighbor)) { continue; }
        for (const auto& passive : neighbor->passive_abilities) {
            if (passive.key == "surrounding_buff_mirror") {
                add_entity_effect(*neighbor, buff_key, amount);
                break;
            }
        }
    }
}

// Overload for TemporaryEntityEffect - propagates on-hit primers / temporary buffs (e.g. Sylvia's
// Jamming Array) to surrounding Resonance Echo units. Strips conflicting on-damage primers from
// the mirror target first, then adds a copy of the effect. Does NOT chain.
static void propagate_temporary_buff_to_surrounding_mirror_passives(
    GameState& game, const Entity& buffed_entity, TemporaryEntityEffect effect)
{
    for (const auto& [cx, cy] : entity_surrounding_cells(buffed_entity)) {
        auto neighbor = game.board.entity_at(cx, cy);
        if (!neighbor || neighbor->entity_id == buffed_entity.entity_id) { continue; }
        if (entity_is_silenced(*neighbor)) { continue; }
        for (const auto& passive : neighbor->passive_abilities) {
            if (passive.key == "surrounding_buff_mirror") {
                // On-damage primers are mutually exclusive; strip existing before mirroring.
                auto& effs = neighbor->temporary_effects;
                effs.erase(std::remove_if(effs.begin(), effs.end(),
                    [](const TemporaryEntityEffect& e) {
                        return e.effect_id == effect_keys::kOnDamageApplyOverloadNextAbilityEffectId
                            || e.effect_id == effect_keys::kOnDamageApplyJammedNextAbilityEffectId
                            || e.effect_id == effect_keys::kMedicalOverrideEffectId;
                    }), effs.end());
                // add_temporary_effect takes a copy; pass by value is intentional.
                game.add_temporary_effect(neighbor, effect);
                break;
            }
        }
    }
}

// DESIGN RULE - survive conditions on damage-dealt reactives:
// Exchange survival: defer_reactive=true → pending_reactives_ → fire_pending_reactives() after
//   counterattack (attacks) or stack handler return (abilities). Source must be alive at fire time.
// Phase survival: reactive_string_payload.source_survive_until = "phase_resolution" →
//   phase_pending_reactives_ → fire_phase_pending_reactives() when the batched phase queue
//   finishes (finalize_phase_action_queue_teardown), or immediately after a lone Blazing resolve
//   outside that drain. Source must still be alive at phase-end fire time.

// Called from destroy_board_entity BEFORE the entity is removed, so its temporary_effects are
// still readable. Finds all units that were covering the dying entity and fires their
// "covered_unit_died" reactive passives.

void apply_permanent_stat_growth_to_unit(Unit& unit, const int health_gain, const int attack_gain)
{
    if (health_gain > 0) {
        unit.base_health += health_gain;
        unit.current_health += health_gain;
    }
    if (attack_gain > 0) {
        sync_unit_damage_ranges_from_nominal(unit);
        unit.melee_damage_min += attack_gain;
        unit.melee_damage_max += attack_gain;
        unit.melee_damage += attack_gain;
        if (unit.is_ranged || unit.attack_type == AttackType::Ranged) {
            unit.ranged_damage_min += attack_gain;
            unit.ranged_damage_max += attack_gain;
            unit.ranged_damage += attack_gain;
        }
    }
}

void GameState::apply_passive_reactive_on_enemy_unit_killed(const Entity& killer, const Entity& victim)
{
    if (victim.entity_type != "unit" || victim.current_health > 0) {
        return;
    }
    if (!victim.owner || !killer.owner || !teams_hostile(*this, *killer.owner, *victim.owner)) {
        return;
    }
    const auto src_it = board.all_entities_map.find(killer.entity_id);
    if (src_it == board.all_entities_map.end() || !src_it->second || src_it->second->current_health <= 0) {
        return;
    }
    if (entity_is_silenced(*src_it->second) || src_it->second->passive_abilities.empty()) {
        return;
    }
    auto unit = std::dynamic_pointer_cast<Unit>(src_it->second);
    if (!unit) {
        return;
    }
    bool changed = false;
    for (const PassiveAbilitySpec& passive : unit->passive_abilities) {
        if (passive.reactive_trigger != "enemy_unit_killed" || passive.reactive_effect_key.empty()) {
            continue;
        }
        if (entity_passive_is_suppressed(*unit, passive.key)) {
            continue;
        }
        ReactiveDispatchCtx ctx;
        ctx.game = this;
        ctx.actor_unit = unit.get();
        ctx.mutable_actor = unit.get();
        ctx.killer_owner = killer.owner;
        if (dispatch_reactive_effect_key(ctx, passive)) {
            changed = true;
        }
    }
    if (changed) {
        mark_passive_auras_dirty();
        refresh_passive_auras();
    }
}

// Last Gasp: fires after the unit is destroyed. Silence check is skipped - unit is already dead.
void GameState::apply_passive_reactive_on_self_died(const Entity& dying_entity)
{
    if (dying_entity.entity_type != "unit" || !dying_entity.owner) {
        return;
    }
    if (dying_entity.passive_abilities.empty()) {
        return;
    }
    const int player_id = *dying_entity.owner;
    for (const PassiveAbilitySpec& passive : dying_entity.passive_abilities) {
        if (passive.reactive_trigger != "self_died" || passive.reactive_effect_key.empty()) {
            continue;
        }
        ReactiveDispatchCtx ctx;
        ctx.game = this;
        ctx.dying_entity = &dying_entity;
        ctx.player_id = player_id;
        if (dispatch_reactive_effect_key(ctx, passive)) {
            continue;
        }
        apply_reactive_energy_gain(turn_manager, player_id, passive);
    }
}

void GameState::apply_passive_reactive_on_covered_unit_died(const Entity& dying_entity)
{
    // Collect unique covering source IDs from the dying entity's covering_fire stacks.
    std::vector<std::string> covering_sources;
    for (const TemporaryEntityEffect& eff : dying_entity.temporary_effects) {
        if (eff.effect_id != "covering_fire" || eff.source_id.empty()) continue;
        if (std::find(covering_sources.begin(), covering_sources.end(), eff.source_id)
                == covering_sources.end()) {
            covering_sources.push_back(eff.source_id);
        }
    }
    if (covering_sources.empty()) return;

    for (const auto& source_id : covering_sources) {
        auto src_it = board.all_entities_map.find(source_id);
        if (src_it == board.all_entities_map.end() || !src_it->second
                || src_it->second->current_health <= 0) {
            continue;
        }
        auto& source = src_it->second;
        if (entity_is_silenced(*source) || source->passive_abilities.empty()) continue;

        for (const PassiveAbilitySpec& passive : source->passive_abilities) {
            if (passive.reactive_trigger != "covered_unit_died") continue;
            if (passive.reactive_effect_key.empty()) continue;

            if (passive.reactive_effect_key == "grant_multistrike_eot") {
                // Increment an accumulated temp effect so entity_attribute_amount (max semantics)
                // correctly reflects the growing bonus: start one above the keyword base so each
                // subsequent death raises the ceiling by 1.
                auto existing = std::find_if(source->temporary_effects.begin(),
                                              source->temporary_effects.end(),
                                              [](const TemporaryEntityEffect& e) {
                                                  return e.effect_id == "multistrike_eot_bonus";
                                              });
                if (existing != source->temporary_effects.end()) {
                    // Increment the multistrike amount and refresh the duration on each new death.
                    for (auto& grant : existing->granted_attributes) {
                        if (grant.key == "multistrike" && grant.amount.has_value()) {
                            grant.amount = *grant.amount + 1;
                            break;
                        }
                    }
                    existing->remaining_turns = 2;
                } else {
                    // First trigger: initialize at (base_multistrike + 1) so max(base, temp)
                    // returns base+1 - entity_attribute_amount takes the maximum of all sources.
                    const auto base_it = source->keyword_amounts.find("multistrike");
                    const int base_ms  = base_it != source->keyword_amounts.end() ? base_it->second : 0;
                    TemporaryEntityEffect tmp;
                    tmp.effect_id       = "multistrike_eot_bonus";
                    tmp.source_id       = source_id;
                    tmp.name            = "Avenger";
                    tmp.expire_on       = "owner_turn_start";
                    tmp.remaining_turns = 2;
                    PassiveAttributeGrant grant;
                    grant.key    = "multistrike";
                    grant.amount = base_ms + 1;
                    tmp.granted_attributes.push_back(grant);
                    source->temporary_effects.push_back(std::move(tmp));
                }
                mark_passive_auras_dirty();
            }
        }
    }
    if (passive_auras_dirty_) refresh_passive_auras();
}

void GameState::apply_passive_reactive_on_damage_dealt(const Entity& source, const Entity& victim, const int damage_dealt)
{
    apply_passive_reactive_on_damage_dealt_filtered(
        *this, source, victim, damage_dealt, ReactiveSurviveDeferral::Exchange);
}


void GameState::apply_passive_reactive_on_damage_taken(const Entity& victim, Entity& attacker, const bool from_melee,
    const int damage_taken)
{
    if (damage_taken <= 0 || !from_melee || entity_is_silenced(victim) || victim.passive_abilities.empty()) {
        return;
    }
    if (!attacker.owner || !victim.owner || !teams_hostile(*this, *attacker.owner, *victim.owner)) {
        return;
    }
    for (const PassiveAbilitySpec& passive : victim.passive_abilities) {
        if (passive.reactive_trigger != "damage_taken_melee" || passive.reactive_effect_key.empty()) {
            continue;
        }
        ReactiveDispatchCtx ctx;
        ctx.game = this;
        ctx.attacker_entity = &attacker;
        ctx.damage_taken = damage_taken;
        ctx.from_melee = from_melee;
        (void)dispatch_reactive_effect_key(ctx, passive);
    }
}

int owner_conduit_total(const GameState& game, const int owner_seat)
{
    int total = 0;
    for (const auto& ent : game.board.all_entities()) {
        if (!ent || ent->current_health <= 0 || !ent->owner || *ent->owner != owner_seat) {
            continue;
        }
        if (ent->entity_type != "unit" && !entity_is_structure(*ent)) {
            continue;
        }
        total += conduit_value(*ent);
    }
    return total;
}

namespace {

bool reactive_passive_matches_spell_speed(const PassiveAbilitySpec& passive,
    const std::optional<EffectSpeed>& spell_speed)
{
    const auto it = passive.reactive_string_payload.find("required_spell_speed");
    if (it == passive.reactive_string_payload.end() || it->second.empty()) {
        return true;
    }
    if (!spell_speed.has_value()) {
        return false;
    }
    if (it->second == "reflex") {
        return *spell_speed == EffectSpeed::Reflex;
    }
    if (it->second == "channeled") {
        return *spell_speed == EffectSpeed::Channeled;
    }
    if (it->second == "blazing") {
        return *spell_speed == EffectSpeed::Blazing;
    }
    return true;
}

void apply_grant_movement_speed_self(GameState& game, const std::shared_ptr<Entity>& unit,
    const int amount, const std::string& source_id)
{
    if (!unit || amount <= 0 || unit->current_health <= 0) {
        return;
    }
    TemporaryEntityEffect move_eff;
    move_eff.effect_id = "spellbound_movement_speed";
    move_eff.source_id = source_id;
    move_eff.name = "Movement Speed";
    move_eff.rules_text = "+" + std::to_string(amount) + " movement speed this turn.";
    move_eff.expire_on = "owner_turn_end";
    move_eff.remaining_turns = 1;
    move_eff.stat_grants.bonus_movement = amount;
    game.add_temporary_effect(unit, std::move(move_eff));
}

#include "game_state_reactive_dispatch.inc"

}  // namespace

// Lady Concordia's Promise: when an allied unit within aura_range takes damage from an enemy,
// Concordia permanently gains +1 attack and heals 1 HP.
void GameState::apply_passive_reactive_on_ally_took_damage(
    const Entity& victim, const Entity& attacker, const int damage)
{
    if (damage <= 0 || !victim.owner || !attacker.owner) return;
    if (!teams_hostile(*this, *victim.owner, *attacker.owner)) return; // must be hostile source
    if (victim.entity_type != "unit") return;

    for (auto& [eid, entity_ptr] : board.all_entities_map) {
        if (!entity_ptr || entity_ptr->current_health <= 0 || !entity_ptr->owner) continue;
        if (*entity_ptr->owner == *attacker.owner) continue; // must be on victim's team
        if (!teams_hostile(*this, *entity_ptr->owner, *attacker.owner)) {
            // Entity is an ally of the victim. Now check passives.
        } else { continue; }
        if (entity_ptr.get() == &victim) continue; // victim itself doesn't trigger its own nearby passives here
        if (entity_is_silenced(*entity_ptr) || entity_ptr->passive_abilities.empty()) continue;

        auto unit = std::dynamic_pointer_cast<Unit>(entity_ptr);
        if (!unit) continue;

        bool changed = false;
        for (const PassiveAbilitySpec& passive : unit->passive_abilities) {
            if (passive.reactive_trigger != "ally_took_damage_nearby") continue;
            if (passive.reactive_effect_key.empty()) continue;
            if (entity_passive_is_suppressed(*unit, passive.key)) continue;

            // Range check (Chebyshev). aura_range == -1 means global.
            if (passive.aura_range >= 0) {
                int dist = INT_MAX;
                for (const auto& [tcx, tcy] : (victim.occupied_positions.empty()
                        ? victim.shape : victim.occupied_positions)) {
                    dist = std::min(dist, min_chebyshev_entity_to_cell(*unit, tcx, tcy));
                }
                if (dist == INT_MAX || dist > passive.aura_range) continue;
            }

            ReactiveDispatchCtx ctx;
            ctx.game = this;
            ctx.actor_unit = unit.get();
            ctx.mutable_actor = unit.get();
            if (dispatch_reactive_effect_key(ctx, passive)) {
                changed = true;
            }
        }
        if (changed) mark_passive_auras_dirty();
    }
}

// Spellbound: passive side effect only - never consumes reactions_remaining_this_turn.
void GameState::apply_passive_reactive_on_owner_spell_played(
    const int player_id, const std::optional<EffectSpeed> spell_speed)
{
    for (const auto& [entity_id, entity_ptr] : board.all_entities_map) {
        static_cast<void>(entity_id);
        if (!entity_ptr || entity_ptr->current_health <= 0 || !entity_ptr->owner
                || *entity_ptr->owner != player_id) {
            continue;
        }
        if (entity_is_silenced(*entity_ptr) || entity_ptr->passive_abilities.empty()) {
            continue;
        }
        Entity& unit = *entity_ptr;
        for (const PassiveAbilitySpec& passive : unit.passive_abilities) {
            if (passive.reactive_trigger != "owner_spell_played" || passive.reactive_effect_key.empty()) {
                continue;
            }
            if (entity_passive_is_suppressed(unit, passive.key)) {
                continue;
            }
            if (!reactive_passive_matches_spell_speed(passive, spell_speed)) {
                continue;
            }

            ReactiveDispatchCtx ctx;
            ctx.game = this;
            ctx.mutable_actor = &unit;
            ctx.player_id = player_id;
            if (dispatch_reactive_effect_key(ctx, passive)) {
                continue;
            }
            const auto cap_it = passive.reactive_effect_payload.find("turn_cap");
            if (cap_it != passive.reactive_effect_payload.end() && cap_it->second > 0) {
                const int cap = cap_it->second;
                const std::string tally_key = "reactive_tally_" + passive.key;
                const int tally = entity_effect_amount(unit, tally_key);
                if (tally >= cap) {
                    continue;
                }
                const auto amt_it = passive.reactive_effect_payload.find(effect_keys::kPayloadAmount);
                const int full_amount =
                    (amt_it != passive.reactive_effect_payload.end()) ? std::max(0, amt_it->second) : 1;
                const int granted = std::min(full_amount, cap - tally);
                if (granted > 0) {
                    apply_reactive_energy_gain(turn_manager, player_id, passive, granted);
                    add_entity_effect(unit, tally_key, granted);
                }
            } else {
                apply_reactive_energy_gain(turn_manager, player_id, passive);
            }
        }
    }
}

void GameState::apply_passive_reactive_on_allied_damaging_spell_played(
    const int caster_player_id, const std::string& effect_key, const std::string& batch_item_id)
{
    if (batch_item_id.empty() || !effect_key_deals_damage(effect_key)) {
        return;
    }
    for (const auto& ent : all_living_entities_oldest_first(board)) {
        if (!ent || !ent->owner || ent->current_health <= 0) {
            continue;
        }
        if (teams_hostile(*this, caster_player_id, *ent->owner)) {
            continue;
        }
        if (entity_is_silenced(*ent)
                || entity_effect_amount(*ent, effect_keys::kManaPylonChargeEffectId) <= 0) {
            continue;
        }
        for (const PassiveAbilitySpec& passive : ent->passive_abilities) {
            if (passive.reactive_trigger != "ally_damaging_spell_played") {
                continue;
            }
            if (entity_passive_is_suppressed(*ent, passive.key)) {
                continue;
            }
            if (passive.reactive_effect_key == "consume_mana_pylon_charge_spell_conduit") {
                int conduit_bonus = 1;
                if (const auto amt_it = passive.reactive_effect_payload.find(effect_keys::kPayloadAmount);
                    amt_it != passive.reactive_effect_payload.end()) {
                    conduit_bonus = std::max(0, amt_it->second);
                }
                if (conduit_bonus <= 0) {
                    continue;
                }
                reduce_entity_effect(*ent, effect_keys::kManaPylonChargeEffectId, 1);
                (void)adjust_batched_item_ability_damage_bonus(batch_item_id, conduit_bonus);
                return;
            }
        }
    }
}

bool GameState::adjust_batched_item_ability_damage_bonus(const std::string& item_id, const int delta)
{
    if (item_id.empty() || delta == 0) {
        return false;
    }
    for (auto& entry : phase_action_queue_) {
        if (!entry.is_attack && entry.spell_item.item_id == item_id) {
            entry.spell_item.ability_damage_bonus += delta;
            return true;
        }
    }
    return false;
}

namespace {

int overlay_duration_from_payload(const std::map<std::string, int>& payload, const int default_duration)
{
    const auto it = payload.find("duration");
    return it != payload.end() ? std::max(0, it->second) : default_duration;
}

int place_gas_cloud_overlays(GameState& game, const StackItem& item, const std::vector<std::pair<int, int>>& cells, const int duration)
{
    int cells_affected = 0;
    for (const auto& [tx, ty] : cells) {
        const auto sq = game.board.get_square(tx, ty);
        if (!sq) {
            continue;
        }
        SquareModifier gas_mod;
        gas_mod.name = kGasCloudOverlayName;
        gas_mod.layer = TileLayer::Overlay;
        gas_mod.owner_seat = item.controller_id;
        gas_mod.duration = duration;
        merge_overlay_modifier(*sq, std::move(gas_mod));
        ++cells_affected;
    }
    return cells_affected;
}


void poison_units_on_cells(GameState& game, const std::vector<std::pair<int, int>>& cells, const char* source)
{
    std::set<std::string> poisoned_this_cast;
    for (const auto& [tx, ty] : cells) {
        const auto ent = game.board.entity_at(tx, ty);
        if (!ent || poisoned_this_cast.count(ent->entity_id)) {
            continue;
        }
        if (ent->current_health <= 0) {
            continue;
        }
        if (!add_entity_effect(*ent, "poison", 1, source)) {
            continue;
        }
        poisoned_this_cast.insert(ent->entity_id);
    }
}

int place_fire_tile_overlays_on_unoccupied(GameState& game, const StackItem& item,
    const std::vector<std::pair<int, int>>& cells, const int duration)
{
    int cells_affected = 0;
    for (const auto& [tx, ty] : cells) {
        const auto sq = game.board.get_square(tx, ty);
        if (!sq || game.board.entity_at(tx, ty)) {
            continue;
        }
        SquareModifier fire_mod;
        fire_mod.name = kFireTileOverlayName;
        fire_mod.layer = TileLayer::Overlay;
        fire_mod.owner_seat = item.controller_id;
        fire_mod.duration = duration;
        merge_overlay_modifier(*sq, std::move(fire_mod));
        ++cells_affected;
    }
    return cells_affected;
}

void fire_units_on_cells(GameState& game, const std::vector<std::pair<int, int>>& cells, const char* source)
{
    std::set<std::string> fired_this_cast;
    for (const auto& [tx, ty] : cells) {
        const auto ent = game.board.entity_at(tx, ty);
        if (!ent || fired_this_cast.count(ent->entity_id)) {
            continue;
        }
        if (ent->current_health <= 0) {
            continue;
        }
        if (!add_entity_effect(*ent, "fire", 1, source)) {
            continue;
        }
        fired_this_cast.insert(ent->entity_id);
    }
}

bool cell_has_gas_overlay(const GameState& game, const int x, const int y)
{
    const auto sq = game.board.get_square(x, y);
    if (!sq) {
        return false;
    }
    const auto* ov = square_overlay_modifier(*sq);
    return ov && ov->name == kGasCloudOverlayName;
}

void clear_gas_overlay_at(GameState& game, const int x, const int y)
{
    const auto sq = game.board.get_square(x, y);
    if (!sq) {
        return;
    }
    const auto* ov = square_overlay_modifier(*sq);
    if (ov && ov->name == kGasCloudOverlayName) {
        clear_overlay_modifier(*sq);
    }
}

void apply_gas_on_hit_tiles(GameState& game, const Entity& source, const std::shared_ptr<Entity>& victim, const int damage_dealt)
{
    if (damage_dealt <= 0 || !victim) {
        return;
    }
    std::vector<std::pair<int, int>> cells;
    if (!victim->occupied_positions.empty()) {
        cells = victim->occupied_positions;
    } else if (victim->position) {
        cells.push_back(*victim->position);
    } else {
        return;
    }
    StackItem overlay_ctx;
    // Use -1 (no owner) when the source unit has no owner seat so the cloud is treated as
    // ownerless and decays on every player's turn-end (via the eliminated-owner path in
    // process_tile_overlay_effects), rather than being tied to the non-existent seat 0.
    overlay_ctx.controller_id = source.owner.value_or(-1);
    place_gas_cloud_overlays(game, overlay_ctx, cells, 2);
    poison_units_on_cells(game, cells, "gas_on_hit");
}


struct DeckSpawnCandidate {
    CardInstanceId id;
    int cost{0};
    CardDefinition def;
};

std::vector<DeckSpawnCandidate> collect_deck_unit_candidates(Deck& deck, const int max_deck_cards = -1)
{
    std::vector<DeckSpawnCandidate> out;
    int deck_index = 0;
    for (const CardInstanceId cid : deck.deck) {
        if (max_deck_cards > 0 && deck_index >= max_deck_cards) {
            break;
        }
        ++deck_index;
        CardInstance* inst = deck.pool.try_get(cid);
        if (!inst || !inst->definition_id.is_valid()) {
            continue;
        }
        CardDefinition def;
        if (!try_get_card_definition(inst->definition_id, def)) {
            continue;
        }
        if (def.type != "unit" || !definition_is_unit(def)) {
            continue;
        }
        const int cost = definition_total_energy_cost(def);
        if (cost <= 0) {
            continue;
        }
        out.push_back(DeckSpawnCandidate{cid, cost, def});
    }
    return out;
}

int max_exact_subset_sum_leq(const std::vector<int>& costs, const int budget)
{
    if (budget <= 0 || costs.empty()) {
        return 0;
    }
    std::vector<bool> reachable(static_cast<std::size_t>(budget) + 1, false);
    reachable[0] = true;
    for (const int cost : costs) {
        for (int sum = budget; sum >= cost; --sum) {
            reachable[static_cast<std::size_t>(sum)] =
                reachable[static_cast<std::size_t>(sum)] || reachable[static_cast<std::size_t>(sum - cost)];
        }
    }
    for (int sum = budget; sum >= 1; --sum) {
        if (reachable[static_cast<std::size_t>(sum)]) {
            return sum;
        }
    }
    return 0;
}

void enumerate_subset_indices_with_sum(const int idx, const int current_sum, const int target,
    const std::vector<int>& costs, std::vector<int>& chosen, std::vector<std::vector<int>>& out)
{
    if (current_sum == target) {
        out.push_back(chosen);
        return;
    }
    if (idx >= static_cast<int>(costs.size()) || current_sum > target) {
        return;
    }
    enumerate_subset_indices_with_sum(idx + 1, current_sum, target, costs, chosen, out);
    chosen.push_back(idx);
    enumerate_subset_indices_with_sum(idx + 1, current_sum + costs[idx], target, costs, chosen, out);
    chosen.pop_back();
}

void mark_unit_footprint_blocked(const Entity& entity, const int anchor_x, const int anchor_y,
    std::set<std::pair<int, int>>& blocked)
{
    for (const auto& [dx, dy] : entity_shape_offsets(entity)) {
        blocked.insert({anchor_x + dx, anchor_y + dy});
    }
}

std::vector<std::pair<int, int>> valid_deploy_anchors_for_unit(const GameState& game, const int player_id,
    const std::shared_ptr<Entity>& entity, const int unit_cost, const std::set<std::pair<int, int>>& blocked)
{
    std::vector<std::pair<int, int>> anchors;
    const std::optional<BoardRectZone> zone = deploy_zone_for_player(game.board_layout(), player_id);
    if (!zone || !entity) {
        return anchors;
    }
    for (int dy = 0; dy < zone->height; ++dy) {
        for (int dx = 0; dx < zone->width; ++dx) {
            const int ax = zone->anchor_x + dx;
            const int ay = zone->anchor_y + dy;
            bool overlaps = false;
            for (const auto& [fx, fy] : entity_shape_offsets(*entity)) {
                if (blocked.count({ax + fx, ay + fy})) {
                    overlaps = true;
                    break;
                }
            }
            if (overlaps) {
                continue;
            }
            if (!game.can_deploy_entity_at(player_id, entity, ax, ay, unit_cost)) {
                continue;
            }
            if (!game.board.can_place_entity_at(entity, ax, ay)) {
                continue;
            }
            anchors.push_back({ax, ay});
        }
    }
    return anchors;
}

bool try_assign_random_placements(GameState& game, const int player_id, const std::vector<DeckSpawnCandidate>& picks,
    std::vector<std::pair<int, int>>& anchors_out, std::mt19937& rng)
{
    if (picks.empty()) {
        return true;
    }
    std::vector<std::size_t> order(picks.size());
    for (std::size_t i = 0; i < picks.size(); ++i) {
        order[i] = i;
    }
    std::shuffle(order.begin(), order.end(), rng);

    std::vector<std::shared_ptr<Entity>> temps;
    temps.reserve(picks.size());
    for (const auto& pick : picks) {
        const CardInstance& inst = game.players_decks.at(player_id).pool.at(pick.id);
        temps.push_back(create_unit_from_definition(pick.def, inst, player_id, "spawn_plan_temp"));
        if (!temps.back()) {
            return false;
        }
    }

    anchors_out.assign(picks.size(), {-1, -1});
    std::set<std::pair<int, int>> blocked;

    const std::function<bool(std::size_t)> assign = [&](const std::size_t slot) -> bool {
        if (slot >= order.size()) {
            return true;
        }
        const std::size_t pick_idx = order[slot];
        const auto& pick = picks[pick_idx];
        auto anchors = valid_deploy_anchors_for_unit(game, player_id, temps[pick_idx], pick.cost, blocked);
        if (anchors.empty()) {
            return false;
        }
        std::shuffle(anchors.begin(), anchors.end(), rng);
        for (const auto& anchor : anchors) {
            mark_unit_footprint_blocked(*temps[pick_idx], anchor.first, anchor.second, blocked);
            anchors_out[pick_idx] = anchor;
            if (assign(slot + 1)) {
                return true;
            }
            for (const auto& [fx, fy] : entity_shape_offsets(*temps[pick_idx])) {
                blocked.erase({anchor.first + fx, anchor.second + fy});
            }
            anchors_out[pick_idx] = {-1, -1};
        }
        return false;
    };

    return assign(0);
}

ActionResult execute_spawn_deck_units_deploy_zone(GameState& game, const StackItem& item)
{
    auto deck_it = game.players_decks.find(item.controller_id);
    if (deck_it == game.players_decks.end()) {
        return ActionResult{false, "spawn_deck_units_deploy_zone: unknown controller player", {}};
    }
    Deck& deck = deck_it->second;
    const int budget = std::max(0, payload_int(item, "mana_budget", 12));
    if (budget <= 0) {
        return ActionResult{true, "spawn_deck_units_deploy_zone: zero budget (fizzled)", {}};
    }

    const int deck_top = payload_int(item, "deck_top", 10);

    const auto candidates = collect_deck_unit_candidates(deck, deck_top);
    if (candidates.empty()) {
        return ActionResult{true, "spawn_deck_units_deploy_zone: no unit cards in deck (fizzled)", {}};
    }

    std::vector<int> costs;
    costs.reserve(candidates.size());
    for (const auto& c : candidates) {
        costs.push_back(c.cost);
    }

    const int target_sum = max_exact_subset_sum_leq(costs, budget);
    if (target_sum <= 0) {
        return ActionResult{true, "spawn_deck_units_deploy_zone: no exact mana combination (fizzled)", {}};
    }

    std::vector<int> chosen;
    std::vector<std::vector<int>> subsets;
    enumerate_subset_indices_with_sum(0, 0, target_sum, costs, chosen, subsets);
    if (subsets.empty()) {
        return ActionResult{true, "spawn_deck_units_deploy_zone: no subset for target sum (fizzled)", {}};
    }
    std::shuffle(subsets.begin(), subsets.end(), game.rng());

    std::vector<DeckSpawnCandidate> picks;
    std::vector<std::pair<int, int>> anchors;
    bool planned = false;
    for (const auto& subset : subsets) {
        picks.clear();
        picks.reserve(subset.size());
        for (const int idx : subset) {
            picks.push_back(candidates[static_cast<std::size_t>(idx)]);
        }
        anchors.clear();
        if (try_assign_random_placements(game, item.controller_id, picks, anchors, game.rng())) {
            planned = true;
            break;
        }
    }
    if (!planned) {
        return ActionResult{true,
            "spawn_deck_units_deploy_zone: no placeable combination for " + std::to_string(target_sum) + " mana (fizzled)",
            {}};
    }

    int spawned = 0;
    for (std::size_t i = 0; i < picks.size(); ++i) {
        const auto& pick = picks[i];
        const auto& anchor = anchors[i];
        const CardInstance& inst = deck.pool.at(pick.id);
        auto unit = create_unit_from_definition(pick.def, inst, item.controller_id, "_pending");
        if (!unit) {
            continue;
        }
        game.assign_monotonic_entity_id(unit, pick.def.name);
        if (!game.board.place_entity(unit, anchor.first, anchor.second)) {
            continue;
        }
        add_entity_effect(*unit, "stunned", 1);
        game.mark_core_cracker_deployed(unit);
        if (!deck.commit_unit_played_from_deck(pick.id)) {
            game.board.remove_entity(unit);
            continue;
        }
        game.register_unit_deployed(item.controller_id, pick.id, unit);
        ++spawned;
    }

    game.mark_passive_auras_dirty();
    if (spawned == 0) {
        return ActionResult{true, "spawn_deck_units_deploy_zone: placement failed (fizzled)", {}};
    }
    return ActionResult{true,
        "Spawned " + std::to_string(spawned) + " unit(s) from deck (" + std::to_string(target_sum) + " mana)",
        {}};
}

/** Turn-order compensation: later seats receive Field Requisition (2p: P2; 4p: P3–P4). */
bool seat_receives_field_requisition(const int seat, const int player_count)
{
    if (player_count <= 0 || seat <= 0) {
        return false;
    }
    return seat > player_count / 2;
}

void inject_field_requisition_signature_cards(GameState& game)
{
    const bool is_sandbox = game.game_mode() == GameMode::Sandbox || game_id_is_sandbox(game.game_id());
    if (is_sandbox) {
        return;
    }
    // Pre-match setting: the host can turn off the going-second Field Requisition compensation.
    if (!game.field_requisition_enabled()) {
        return;
    }
    const CardDefId sig_def = try_card_def_id_for_key("field_requisition");
    if (sig_def == kInvalidCardDefId) {
        return;
    }
    const int player_count = static_cast<int>(game.turn_manager.players.size());
    for (const int player_id : game.turn_manager.players) {
        if (!seat_receives_field_requisition(player_id, player_count)) {
            continue;
        }
        auto deck_it = game.players_decks.find(player_id);
        if (deck_it == game.players_decks.end()) {
            continue;
        }
        Deck& deck = deck_it->second;
        const CardInstanceId sig_cid =
            deck.pool.emplace(sig_def, "field_requisition_p" + std::to_string(player_id), 0);
        deck.hand.push_back(sig_cid);
    }
}

}  // namespace

#include "game_state_effects.inc"  // GameState::register_default_effects() body

void GameState::add_player(int player_id, const std::string&) {
    if (players_decks.contains(player_id)) return;
    turn_manager.add_player(player_id);
    // Prefer explicit game_mode_; fall back to game_id substrings for older callers.
    const bool is_sandbox = game_mode_ == GameMode::Sandbox || game_id_is_sandbox(game_id_);
    const bool is_footprint = game_mode_ == GameMode::FootprintTest ||
        (game_mode_ == GameMode::Default && game_id_.find("footprint_test") != std::string::npos);
    const bool is_test_deck = game_mode_ == GameMode::TestDeck ||
        (game_mode_ == GameMode::Default && game_id_.find("test_deck") != std::string::npos);
    if (is_sandbox) {
        players_decks[player_id] = create_sandbox_deck_from_catalog();
        players_energy_zones_decks[player_id] = EnergyZoneDeck(rng_);
    } else if (is_footprint) {
        players_decks[player_id] = create_footprint_test_deck();
        players_energy_zones_decks[player_id] = EnergyZoneDeck(rng_);
    } else if (is_test_deck) {
        players_decks[player_id] = create_test_deck(rng_);
        players_energy_zones_decks[player_id] = EnergyZoneDeck(rng_);
    } else if (const auto active = get_active_match_deck_list()) {
        std::string deck_err;
        players_decks[player_id] = create_deck_from_deck_list(*active, rng_, true, &deck_err);
        if (players_decks[player_id].deck.empty() && !deck_err.empty()) {
            players_decks[player_id] = create_starter_deck(rng_);
        }
        players_energy_zones_decks[player_id] = EnergyZoneDeck::from_deck_list(*active, rng_);
    } else {
        players_decks[player_id] = create_starter_deck(rng_);
        if (const std::optional<DeckListDefinition> starter = try_get_starter_deck_list()) {
            players_energy_zones_decks[player_id] = EnergyZoneDeck::from_deck_list(*starter, rng_);
        } else {
            players_energy_zones_decks[player_id] = EnergyZoneDeck(rng_);
        }
    }
    players_hands[player_id] = &players_decks[player_id].hand;
    if (!is_sandbox) {
        players_decks[player_id].draw(kOpeningHandSize, rng_);
    }
    players_energy_zones[player_id] = {};
    if (!seat_team_id.contains(player_id)) {
        seat_team_id[player_id] = player_id;
    }
}

void GameState::set_player_deck_from_list(int player_id, const DeckListDefinition& deck_list) {
    if (!players_decks.contains(player_id)) {
        return;  // seat must already exist
    }
    std::string deck_err;
    Deck rebuilt = create_deck_from_deck_list(deck_list, rng_, true, &deck_err);
    if (rebuilt.deck.empty() && rebuilt.hand.empty()) {
        return;  // invalid deck list - keep the existing deck
    }
    players_decks[player_id] = std::move(rebuilt);
    players_hands[player_id] = &players_decks[player_id].hand;
    players_decks[player_id].draw(kOpeningHandSize, rng_);
    players_energy_zones_decks[player_id] = EnergyZoneDeck::from_deck_list(deck_list, rng_);
    players_energy_zones[player_id] = {};
}

void GameState::mark_ability_used_this_turn(const std::shared_ptr<Entity>& entity, const std::string& ability_key) {
    if (!entity || ability_key.empty()) {
        return;
    }
    for (const AbilitySpec& ability : entity->activated_abilities) {
        if (ability.key == ability_key) {
            entity_consume_ability_use(*entity, ability);
            return;
        }
    }
}

void GameState::refresh_ability_uses_for_entity(const std::shared_ptr<Entity>& entity) {
    if (!entity) {
        return;
    }
    refresh_entity_ability_uses(*entity);
    entity->barrage_cast_counts_this_turn.clear();
}

uint64_t GameState::bump_network_snap_seq() { return ++network_snap_seq_; }

uint64_t GameState::record_authority_command(const int seat, std::string line_utf8)
{
    return append_match_command_journal(command_journal_, match_next_command_seq_, seat, std::move(line_utf8));
}

uint64_t GameState::match_command_seq() const
{
    return match_next_command_seq_ > 0 ? match_next_command_seq_ - 1 : 0;
}

void GameState::clear_command_journal()
{
    command_journal_.clear();
    match_next_command_seq_ = 1;
}

void GameState::mark_passive_auras_dirty()
{
    passive_auras_dirty_ = true;
}

void GameState::refresh_passive_auras() {
    if (!passive_auras_dirty_) {
        return;
    }
    passive_auras_dirty_ = false;
    struct HealthCarry {
        std::shared_ptr<Entity> entity;
        int old_max{0};
        int damage_marked{0};
    };
    std::vector<HealthCarry> health;
    health.reserve(board.all_entities_map.size());
    std::vector<std::shared_ptr<Entity>> entities;
    entities.reserve(board.all_entities_map.size());
    // Build allies_by_seat in the same pass as entity collection.
    std::unordered_map<int, std::vector<std::shared_ptr<Unit>>> allies_by_seat;
    for (const auto& [_, entity] : board.all_entities_map) {
        if (!entity) {
            continue;
        }
        entities.push_back(entity);
        const int old_max = entity_effective_base_health(*entity);
        health.push_back({entity, old_max, std::max(0, old_max - entity->current_health)});
        entity->aura_granted_keywords.clear();
        entity->aura_keyword_amounts.clear();
        entity->aura_bonus_attack = 0;
        entity->aura_bonus_health = 0;
        entity->aura_bonus_melee_damage = 0;
        entity->aura_bonus_ranged_damage = 0;
        entity->aura_bonus_movement = 0;
        entity->aura_bonus_ability_damage = 0;
        entity->aura_bonus_armor = 0;
        entity->survive_lethal_percent = 0;
        entity->survive_lethal_bonus_attack = 0;
        entity->survive_lethal_bonus_health = 0;
        // Collect allied units while we're already iterating.
        // Key by team_id, not seat, so that cross-owner allied units share the same bucket
        // and auras like allied_units reach all teammates in multiplayer team games.
        const auto unit = std::dynamic_pointer_cast<Unit>(entity);
        if (unit && unit->entity_type == "unit" && unit->owner) {
            allies_by_seat[team_of_seat(*unit->owner)].push_back(unit);
        }
    }

    auto apply_stats = [](Entity& target, const PassiveStatGrant& stats) {
        target.aura_bonus_attack += stats.bonus_attack;
        target.aura_bonus_health += stats.bonus_health;
        target.aura_bonus_melee_damage += stats.bonus_melee_damage;
        target.aura_bonus_ranged_damage += stats.bonus_ranged_damage;
        // Crushing Advance: cannot receive bonus movement from aura sources.
        if (!entity_has_attribute(target, "crushes_on_move")) {
            target.aura_bonus_movement += stats.bonus_movement;
        }
        target.aura_bonus_ability_damage += stats.bonus_ability_damage;
        target.aura_bonus_armor += stats.bonus_armor;
    };
    auto apply_attributes = [](Entity& target, const std::vector<PassiveAttributeGrant>& grants) {
        for (const PassiveAttributeGrant& grant : grants) {
            if (grant.key.empty() || attribute_is_non_copyable(grant.key)) {
                continue;
            }
            if (std::find(target.aura_granted_keywords.begin(), target.aura_granted_keywords.end(), grant.key) ==
                target.aura_granted_keywords.end()) {
                target.aura_granted_keywords.push_back(grant.key);
            }
            if (grant.amount.has_value()) {
                target.aura_keyword_amounts[grant.key] = std::max(target.aura_keyword_amounts[grant.key], std::max(0, *grant.amount));
            }
        }
    };

    for (const auto& source : entities) {
        if (!source || source->current_health <= 0 || source->passive_abilities.empty() || entity_is_silenced(*source)) {
            continue;
        }
        // Compute once per source - used for both range check and stat amplification.
        const PassiveStatGrant source_temp = temporary_stat_grants_for_entity(*source);
        for (const PassiveAbilitySpec& passive : source->passive_abilities) {
            if (passive.applies_to == "self") {
                if (!entity_satisfies_unit_type_filter(*source, passive.affects_unit_types)) {
                    continue;
                }
                apply_stats(*source, passive.stat_grants);
                apply_attributes(*source, passive.granted_attributes);
                // survive_lethal_percent: take max (two sources don't stack past the highest value).
                if (passive.stat_grants.survive_lethal_percent > 0) {
                    source->survive_lethal_percent = std::max(
                        source->survive_lethal_percent, passive.stat_grants.survive_lethal_percent);
                }
                // survive_lethal bonuses: sum (each source contributes independently).
                source->survive_lethal_bonus_attack += passive.stat_grants.survive_lethal_bonus_attack;
                source->survive_lethal_bonus_health += passive.stat_grants.survive_lethal_bonus_health;
                continue;
            }
            if ((passive.applies_to != "allied_units" && passive.applies_to != "allied_structures") || !source->owner) {
                continue;
            }
            // Compute effective aura range: passive.aura_range + any temporary range bonus on the source.
            // aura_range == -1 means unlimited (global aura - applies to all allies).
            const int effective_range = (passive.aura_range >= 0)
                ? passive.aura_range + source_temp.bonus_aura_range
                : -1;
            // Build effective stat grants: base passive grants + temporary source amplifiers.
            PassiveStatGrant effective_grants = passive.stat_grants;
            effective_grants.bonus_attack += source_temp.bonus_aura_attack;
            effective_grants.bonus_health += source_temp.bonus_aura_health;
            effective_grants.bonus_ability_damage += source_temp.bonus_aura_ability_damage;
            if (passive.applies_to == "allied_units") {
                const auto allies_it = allies_by_seat.find(team_of_seat(*source->owner));
                if (allies_it == allies_by_seat.end()) {
                    continue;
                }
                for (const auto& target_unit : allies_it->second) {
                    if (entity_is_silenced(*target_unit)) {
                        continue;
                    }
                    if (!entity_satisfies_unit_type_filter(*target_unit, passive.affects_unit_types)) {
                        continue;
                    }
                    if (effective_range >= 0) {
                        int dist = INT_MAX;
                        for (const auto& [tcx, tcy] : (target_unit->occupied_positions.empty()
                                ? target_unit->shape : target_unit->occupied_positions)) {
                            dist = std::min(dist, min_chebyshev_entity_to_cell(*source, tcx, tcy));
                        }
                        if (dist == INT_MAX || dist > effective_range) {
                            continue;
                        }
                    }
                    apply_stats(*target_unit, effective_grants);
                    apply_attributes(*target_unit, passive.granted_attributes);
                    // Sentinel Veil (allied_units survive_lethal_percent aura): grant death-shield
                    // to this ally only if it has not already used its save this turn.
                    if (passive.stat_grants.survive_lethal_percent > 0
                            && !target_unit->death_shield_used_this_turn) {
                        target_unit->survive_lethal_percent = std::max(
                            target_unit->survive_lethal_percent,
                            passive.stat_grants.survive_lethal_percent);
                    }
                }
            } else {
                for (const auto& target : entities) {
                    if (!target || !target->owner || teams_hostile(*this, *source->owner, *target->owner)) {
                        continue;
                    }
                    if (!entity_is_building(*target)) {
                        continue;
                    }
                    if (entity_is_silenced(*target)) {
                        continue;
                    }
                    if (!entity_satisfies_unit_type_filter(*target, passive.affects_unit_types)) {
                        continue;
                    }
                    if (effective_range >= 0) {
                        int dist = INT_MAX;
                        for (const auto& [tcx, tcy] : (target->occupied_positions.empty()
                                ? target->shape : target->occupied_positions)) {
                            dist = std::min(dist, min_chebyshev_entity_to_cell(*source, tcx, tcy));
                        }
                        if (dist == INT_MAX || dist > effective_range) {
                            continue;
                        }
                    }
                    apply_stats(*target, effective_grants);
                    apply_attributes(*target, passive.granted_attributes);
                }
            }
        }
    }

    // Hyperactive Scanning: mirror keywords from 8-way surrounding units. Snapshot each unit's
    // post-aura keywords first so holders cannot pick up keywords another holder gained this pass.
    struct MirroredKeywordGrant {
        std::string key;
        int amount{0};
    };
    std::unordered_map<std::string, std::vector<MirroredKeywordGrant>> keywords_by_entity_id;
    keywords_by_entity_id.reserve(entities.size());
    for (const auto& entity : entities) {
        if (!entity || entity->current_health <= 0 || entity->entity_type != "unit") {
            continue;
        }
        auto& grants = keywords_by_entity_id[entity->entity_id];
        for (const std::string& kw : entity->keywords) {
            const auto it = entity->keyword_amounts.find(kw);
            grants.push_back({kw, it == entity->keyword_amounts.end() ? 0 : it->second});
        }
        for (const std::string& kw : entity->aura_granted_keywords) {
            const auto it = entity->aura_keyword_amounts.find(kw);
            grants.push_back({kw, it == entity->aura_keyword_amounts.end() ? 0 : it->second});
        }
        for (const TemporaryEntityEffect& eff : entity->temporary_effects) {
            if (temporary_effect_suppresses_gained_keyword_grants(eff)) {
                continue;
            }
            for (const PassiveAttributeGrant& g : eff.granted_attributes) {
                if (slug_is_status_state_not_gained_keyword(g.key)) {
                    continue;
                }
                grants.push_back({g.key, g.amount.value_or(0)});
            }
        }
    }
    auto mirror_surrounding_keyword = [](Entity& holder, const std::string& key, int amount) {
        if (key.empty() || attribute_is_non_copyable(key) || slug_is_status_state_not_gained_keyword(key)) {
            return;
        }
        if (std::find(holder.aura_granted_keywords.begin(), holder.aura_granted_keywords.end(), key) ==
            holder.aura_granted_keywords.end()) {
            holder.aura_granted_keywords.push_back(key);
        }
        if (amount > 0) {
            holder.aura_keyword_amounts[key] = std::max(holder.aura_keyword_amounts[key], amount);
        }
    };
    for (const auto& entity : entities) {
        if (!entity || entity->current_health <= 0 || entity_is_silenced(*entity)
                || entity->entity_type != "unit") {
            continue;
        }
        bool has_hyperactive = false;
        for (const PassiveAbilitySpec& passive : entity->passive_abilities) {
            if (passive.key == "hyperactive_scanning") {
                has_hyperactive = true;
                break;
            }
        }
        if (!has_hyperactive) {
            continue;
        }
        for (const auto& [cx, cy] : entity_surrounding_cells(*entity)) {
            const std::shared_ptr<Entity> neighbor = board.entity_at(cx, cy);
            if (!neighbor || neighbor.get() == entity.get()) {
                continue;
            }
            const auto grants_it = keywords_by_entity_id.find(neighbor->entity_id);
            if (grants_it == keywords_by_entity_id.end()) {
                continue;
            }
            for (const MirroredKeywordGrant& grant : grants_it->second) {
                mirror_surrounding_keyword(*entity, grant.key, grant.amount);
            }
        }
    }

    for (const HealthCarry& entry : health) {
        if (!entry.entity) {
            continue;
        }
        const int new_max = entity_effective_base_health(*entry.entity);
        if (entry.entity->current_health > new_max) {
            entry.entity->current_health = new_max;
            continue;
        }
        entry.entity->current_health = std::min(new_max, std::max(0, new_max - entry.damage_marked));
    }
}

void GameState::add_temporary_effect(const std::shared_ptr<Entity>& target, TemporaryEntityEffect effect)
{
    if (!target || entity_immune_to_all_effects(*target)) {
        return;
    }
    refresh_passive_auras();
    const int old_max = entity_effective_base_health(*target);
    if (effect.effect_id.empty()) {
        effect.effect_id = "temp_" + std::to_string(target->temporary_effects.size() + 1);
    }
    if (effect.remaining_turns == 0 && effect.expire_on != "never") {
        effect.remaining_turns = 1;
    }
    effect.granted_attributes.erase(
        std::remove_if(effect.granted_attributes.begin(), effect.granted_attributes.end(),
            [](const PassiveAttributeGrant& grant) { return attribute_is_non_copyable(grant.key); }),
        effect.granted_attributes.end());
    // Capture before move so we can apply action counters after the push.
    const int bonus_moves   = effect.stat_grants.bonus_moves;
    const int bonus_attacks = effect.stat_grants.bonus_attacks;
    target->temporary_effects.push_back(std::move(effect));
    refresh_passive_auras();
    const int new_max = entity_effective_base_health(*target);
    if (new_max > old_max) {
        target->current_health = std::min(new_max, target->current_health + (new_max - old_max));
    } else {
        target->current_health = std::min(target->current_health, new_max);
    }
    // Immediately credit the action counters so the unit can use them this turn.
    // Crushing Advance: cannot receive bonus moves from any source (temporary effects included).
    if (bonus_moves > 0 && entity_can_move(*target) && !entity_has_attribute(*target, "crushes_on_move")) {
        target->moves_remaining_this_turn += bonus_moves;
    }
    if (bonus_attacks > 0) {
        target->bonus_attacks_remaining_this_turn += bonus_attacks;
    }
}


void GameState::expire_vulnerable_turn_end_for_turn(const int controller_id) {
    vulnerable_turn_end_pending_.erase(
        std::remove_if(vulnerable_turn_end_pending_.begin(), vulnerable_turn_end_pending_.end(),
            [&](const VulnerableTurnEndEntry& entry) {
                if (entry.turn_owner_id != controller_id) {
                    return false;
                }
                if (const auto it = board.all_entities_map.find(entry.entity_id);
                        it != board.all_entities_map.end() && it->second) {
                    reduce_entity_effect(*it->second, "vulnerable", entry.amount);
                }
                return true;
            }),
        vulnerable_turn_end_pending_.end());
}

void GameState::expire_active_turn_buffs_for_turn(const int controller_id) {
    for (const ActiveTurnBuffEntry& entry : active_turn_buff_pending_) {
        if (entry.turn_owner_id != controller_id) {
            continue;
        }
        const auto it = board.all_entities_map.find(entry.entity_id);
        if (it == board.all_entities_map.end() || !it->second) {
            continue;
        }
        auto& effects = it->second->temporary_effects;
        effects.erase(
            std::remove_if(effects.begin(), effects.end(),
                [&](const TemporaryEntityEffect& eff) { return eff.effect_id == entry.effect_id; }),
            effects.end());
    }
    active_turn_buff_pending_.erase(
        std::remove_if(active_turn_buff_pending_.begin(), active_turn_buff_pending_.end(),
            [&](const ActiveTurnBuffEntry& entry) { return entry.turn_owner_id == controller_id; }),
        active_turn_buff_pending_.end());
    mark_passive_auras_dirty();
    refresh_passive_auras();
}

void GameState::expire_temporary_effects_for_turn(int player_id, const std::string& expire_on)
{
    for (const auto& entity : living_entities_for_owner_oldest_first(board, player_id)) {
        const int old_max = entity_effective_base_health(*entity);
        const int damage_marked = std::max(0, old_max - entity->current_health);
        const bool had_deployment_fatigue = entity_has_deployment_fatigue(*entity);
        int expired_bonus_moves   = 0;
        int expired_bonus_attacks = 0;
        int expired_next_damage_bonus = 0;
        for (TemporaryEntityEffect& effect : entity->temporary_effects) {
            if (effect.expire_on == expire_on && effect.expire_on != "never" && effect.remaining_turns > 0) {
                if (expire_on == "owner_turn_start"
                    && effect.effect_id == effect_keys::kRecoverStanceEffectId
                    && effect.remaining_turns == 1
                    && entity_effect_amount(*entity, effect_keys::kRecoverStanceDamageTakenKey) <= 0) {
                    HealPacket hp;
                    hp.target = entity;
                    hp.amount = 2;
                    hp.source_label = "Recover";
                    (void)apply_heal_packet(*this, hp);
                }
                --effect.remaining_turns;
                if (effect.remaining_turns <= 0) {
                    expired_bonus_moves           += effect.stat_grants.bonus_moves;
                    expired_bonus_attacks         += effect.stat_grants.bonus_attacks;
                    expired_next_damage_bonus     += effect.stat_grants.on_expire_next_damage_bonus;
                }
            }
        }
        entity->temporary_effects.erase(
            std::remove_if(entity->temporary_effects.begin(), entity->temporary_effects.end(), [](const TemporaryEntityEffect& effect) {
                return effect.expire_on != "never" && effect.remaining_turns <= 0;
            }),
            entity->temporary_effects.end());
        remove_entity_effect(*entity, effect_keys::kRecoverStanceDamageTakenKey);
        if (expired_bonus_moves > 0) {
            entity->moves_remaining_this_turn = std::max(0, entity->moves_remaining_this_turn - expired_bonus_moves);
        }
        if (expired_bonus_attacks > 0) {
            entity->bonus_attacks_remaining_this_turn = std::max(0, entity->bonus_attacks_remaining_this_turn - expired_bonus_attacks);
        }
        if (expired_next_damage_bonus > 0) {
            add_entity_effect(*entity, "next_damage_bonus", expired_next_damage_bonus, "delayed_bonus");
        }
        if (had_deployment_fatigue && !entity_has_deployment_fatigue(*entity)) {
            refresh_entity_ability_uses(*entity);
            entity->barrage_cast_counts_this_turn.clear();
        }
        const int new_max = entity_effective_base_health(*entity);
        entity->current_health = std::min(new_max, std::max(0, new_max - damage_marked));
        entity->current_health = std::min(entity->current_health, new_max);
    }
    refresh_passive_auras();
}

bool GameState::player_has_ancient_frog_storage_consumers(const int player_id) const
{
    for (const auto& entity : living_entities_for_owner_oldest_first(board, player_id)) {
        if (!entity || entity->current_health <= 0) {
            continue;
        }
        if (entity_is_silenced(*entity) || entity_immune_to_all_effects(*entity)) {
            continue;
        }
        if (!entity_has_ancient_frog_store_passive(*entity)) {
            continue;
        }
        if (!std::dynamic_pointer_cast<Unit>(entity)) {
            continue;
        }
        const int room = std::max(0,
            max_energy_storage_for_entity(*entity)
            - entity_effect_amount(*entity, kEnergyStorageEntityEffectKey));
        if (room > 0) {
            return true;
        }
    }
    return false;
}

void GameState::begin_owner_turn_end_energy_storage(const int turn_owner_id)
{
    EnergyStorageTurnSplit split;
    split.turn_owner_id = turn_owner_id;
    energy_storage_turn_split_ = std::move(split);
}

int GameState::energy_storage_share_for_unit(const int controller_id, const std::string& entity_id)
{
    if (!energy_storage_turn_split_ || energy_storage_turn_split_->turn_owner_id != controller_id) {
        return 0;
    }
    if (!energy_storage_turn_split_->drained) {
        const auto order_ranks = compute_passive_action_order_ranks(board);
        std::vector<std::string> consumers;
        for (const auto& entity : living_entities_for_owner_oldest_first(board, controller_id)) {
            if (!entity || entity->current_health <= 0) {
                continue;
            }
            if (entity_is_silenced(*entity) || entity_immune_to_all_effects(*entity)) {
                continue;
            }
            if (!entity_has_ancient_frog_store_passive(*entity)) {
                continue;
            }
            if (!std::dynamic_pointer_cast<Unit>(entity)) {
                continue;
            }
            const int room = std::max(0,
                max_energy_storage_for_entity(*entity)
                - entity_effect_amount(*entity, kEnergyStorageEntityEffectKey));
            if (room <= 0) {
                continue;
            }
            consumers.push_back(entity->entity_id);
        }
        std::sort(consumers.begin(), consumers.end(),
            [&order_ranks](const std::string& a, const std::string& b) {
                const auto rank = [&order_ranks](const std::string& id) {
                    const auto it = order_ranks.find(id);
                    return it == order_ranks.end() ? INT_MAX : it->second;
                };
                const int ra = rank(a);
                const int rb = rank(b);
                if (ra != rb) {
                    return ra < rb;
                }
                return a < b;
            });

        const int total = consumers.empty() ? 0 : drain_ancient_frog_floating_energy(*this, controller_id);
        energy_storage_turn_split_->drained = true;
        const int n = static_cast<int>(consumers.size());
        if (n > 0 && total > 0) {
            const int base_share = divide_rounded_down(total, n);
            const int remainder = total % n;
            for (int i = 0; i < n; ++i) {
                energy_storage_turn_split_->share_by_entity_id[consumers[static_cast<std::size_t>(i)]]
                    = base_share + (i < remainder ? 1 : 0);
            }
        }
    }

    const auto it = energy_storage_turn_split_->share_by_entity_id.find(entity_id);
    return it == energy_storage_turn_split_->share_by_entity_id.end() ? 0 : it->second;
}

void GameState::process_start_of_turn_status_effects(int player_id)
{
    for (const auto& entity : living_entities_for_owner_oldest_first(board, player_id)) {
        if (entity_effect_amount(*entity, "stealth") > 0) {
            reduce_entity_effect(*entity, "stealth", 1);
        }
        if (entity_effect_amount(*entity, "evasive") > 0) {
            reduce_entity_effect(*entity, "evasive", 1);
        }
    }
}

void GameState::process_end_of_turn_regen(int player_id)
{
    // Wave 1: all owned entities heal from regen, oldest-deployed first.
    for (const auto& entity : living_entities_for_owner_oldest_first(board, player_id)) {
        const int regen = regen_value(*entity);
        if (regen > 0) {
            apply_entity_heal(*entity, regen);
        }
    }
}

void GameState::process_end_of_turn_dot(int player_id)
{
    // Wave 3: poison, fire, bleed, jammed, overload explosions - oldest-deployed first.
    // Rooted and stunned decay in a second pass after all DOT (see end of this function).
    struct FireSpread {
        std::shared_ptr<Entity> target;
    };
    std::vector<FireSpread> fire_spreads;
    std::set<std::string> fire_spread_ids;
    for (const auto& entity : living_entities_for_owner_oldest_first(board, player_id)) {
        if (!board.all_entities_map.contains(entity->entity_id)) {
            continue;
        }
        const int poison = entity_effect_amount(*entity, "poison");
        if (poison > 0) {
            if (!entity_has_poison_resistance(*entity)) {
                apply_incoming_damage(*entity, poison, true);
            }
            reduce_entity_effect(*entity, "poison", 1);
            if (entity->current_health <= 0) {
                destroy_board_entity(entity);
                continue;
            }
        }

        const int fire = entity_effect_amount(*entity, "fire");
        if (fire > 0) {
            if (!entity_has_fire_resistance(*entity)) {
                apply_incoming_damage(*entity, fire, true);
            }
            reduce_entity_effect(*entity, "fire", 1);
            if (entity->current_health <= 0) {
                destroy_board_entity(entity);
                continue;
            }
            // Only survivors spread fire; a unit killed this tick does not ignite neighbors.
            for (const auto& [cx, cy] : entity_surrounding_cells(*entity)) {
                auto neighbor = board.entity_at(cx, cy);
                if (neighbor && neighbor != entity && fire_spread_ids.insert(neighbor->entity_id).second) {
                    fire_spreads.push_back({neighbor});
                }
            }
        }

        // F9: Bleed stacks decay here (end of owner's turn) but bleed DAMAGE only fires in
        // process_bleed_after_movement(). A stationary unit loses stacks without taking damage.
        // This is intentional design: bleed punishes movement. Document it here to prevent
        // surprise when writing bleed-synergy cards.
        if (entity && board.all_entities_map.contains(entity->entity_id)) {
            reduce_entity_effect(*entity, "bleed", 1);
        }
        if (entity && board.all_entities_map.contains(entity->entity_id)) {
            reduce_entity_effect(*entity, "jammed", 1);
        }
        if (entity && board.all_entities_map.contains(entity->entity_id)) {
            reduce_entity_effect(*entity, "vulnerable", 1);
        }

        // Overload: stacks never decay mid-turn. At end of the affected entity's owner's turn,
        // if stacks >= kOverloadExplosionThreshold (3), explode:
        //   damage = kOverloadExplosionDamage + (stacks - threshold)  [5 base + 1 per stack above 3]
        // Hits the entity AND all adjacent entities (armor applies, pierce=false).
        // Normally stacks reset to 0 after the blast. Exception: entities with overload_resistance
        // retain all stacks (they explode every turn the condition is met).
        if (entity && board.all_entities_map.contains(entity->entity_id)) {
            const int overload_stacks = entity_effect_amount(*entity, "overload");
            if (overload_stacks >= kOverloadExplosionThreshold) {
                const int explosion_damage = kOverloadExplosionDamage
                                            + (overload_stacks - kOverloadExplosionThreshold);
                const bool retains_overload = entity_passive_mechanic_active(*entity, "overload_resistance");
                if (!retains_overload) {
                    reduce_entity_effect(*entity, "overload", overload_stacks);
                }

                // Collect self + all adjacent entities before applying damage.
                std::vector<std::shared_ptr<Entity>> blast_targets;
                std::set<std::string> seen;
                if (board.all_entities_map.contains(entity->entity_id)) {
                    blast_targets.push_back(entity);
                    seen.insert(entity->entity_id);
                }
                for (const auto& [cx, cy] : entity_surrounding_cells(*entity)) {
                    auto neighbor = board.entity_at(cx, cy);
                    if (neighbor && seen.insert(neighbor->entity_id).second) {
                        blast_targets.push_back(neighbor);
                    }
                }

                // Apply damage to all blast targets first, then destroy dead ones.
                // overload_resistance halves the explosion damage for that target.
                std::vector<std::shared_ptr<Entity>> to_destroy;
                for (const auto& t : blast_targets) {
                    if (!board.all_entities_map.contains(t->entity_id)) continue;
                    const int dmg = entity_passive_mechanic_active(*t, "overload_resistance")
                                    ? round_down_half(explosion_damage)
                                    : explosion_damage;
                    apply_incoming_damage(*t, dmg, /*pierce=*/false);
                    if (t->current_health <= 0) to_destroy.push_back(t);
                }
                for (const auto& t : to_destroy) {
                    if (board.all_entities_map.contains(t->entity_id)) destroy_board_entity(t);
                }
                refresh_passive_auras();

                // overload_feed: any other entity with this keyword within its stated Chebyshev
                // range absorbs the consumed stacks as new overload on itself.
                // Exception: if the exploding entity has overload_feed, skip the scan entirely  - 
                // overload_feed units (i.e. Volt Spires) never absorb from each other's explosions.
                if (entity_passive_mechanic_amount(*entity, "overload_feed", 0) <= 0) {
                    for (const auto& [fid, feeder] : board.all_entities_map) {
                        if (!feeder || feeder->entity_id == entity->entity_id) continue;
                        const int feed_range = entity_passive_mechanic_amount(*feeder, "overload_feed", 0);
                        if (feed_range <= 0) continue;
                        // Compute minimum Chebyshev distance between feeder and the exploded entity.
                        int min_dist = INT_MAX;
                        for (const auto& [ex, ey] : entity->occupied_positions) {
                            min_dist = std::min(min_dist, min_chebyshev_entity_to_cell(*feeder, ex, ey));
                        }
                        if (min_dist == INT_MAX && entity->position) {
                            min_dist = min_chebyshev_entity_to_cell(*feeder,
                                entity->position->first, entity->position->second);
                        }
                        if (min_dist <= feed_range) {
                            apply_overload_stacks(feeder, overload_stacks);
                        }
                    }
                }

                if (!board.all_entities_map.contains(entity->entity_id)) {
                    continue; // entity destroyed by its own explosion
                }
            }
        }
    }

    std::sort(fire_spreads.begin(), fire_spreads.end(), [](const FireSpread& a, const FireSpread& b) {
        if (!a.target || !b.target) {
            return static_cast<bool>(a.target) < static_cast<bool>(b.target);
        }
        return entity_spawn_older(*a.target, *b.target);
    });

    for (const FireSpread& spread : fire_spreads) {
        if (spread.target && board.all_entities_map.contains(spread.target->entity_id)) {
            add_entity_effect(*spread.target, "fire", 1);
        }
    }

    // Tick fire on pickups. Pickups have no owner so they are not in the main entity loop above,
    // but they accept fire stacks. Fire deals pierce damage and destroys any pickup it hits
    // (1 HP). Fire spread from burning pickups is handled in the main loop (neighbors are already
    // collected when the spreader ticks); pickup spread is not added here to avoid a second-wave
    // problem where pickups spread into the fire_spreads vector mid-loop.
    {
        std::vector<std::shared_ptr<Entity>> burning_pickups;
        for (const auto& [_, ent] : board.all_entities_map) {
            if (ent && entity_is_pickup(*ent) && ent->position
                    && entity_effect_amount(*ent, "fire") > 0) {
                burning_pickups.push_back(ent);
            }
        }
        for (const auto& pickup : burning_pickups) {
            if (!board.all_entities_map.contains(pickup->entity_id)) continue;
            const int fire = entity_effect_amount(*pickup, "fire");
            if (fire <= 0) continue;
            apply_incoming_damage(*pickup, fire, /*pierce=*/true);
            reduce_entity_effect(*pickup, "fire", 1);
            if (pickup->current_health <= 0) {
                destroy_board_entity(pickup);
            }
        }
    }

    // Second pass: rooted and stunned decay after all DOT has resolved.
    for (const auto& entity : living_entities_for_owner_oldest_first(board, player_id)) {
        if (!board.all_entities_map.contains(entity->entity_id)) {
            continue;
        }
        if (entity_effect_amount(*entity, "rooted") > 0) {
            reduce_entity_effect(*entity, "rooted", 1);
        }
        if (entity_effect_amount(*entity, "stunned") > 0) {
            reduce_entity_effect(*entity, "stunned", 1);
        }
        decay_silenced_owner_turn_end_stacks(*entity);
        decay_barrier_owner_turn_end_stacks(*entity);
    }
    refresh_passive_auras();
}

void GameState::process_tile_overlay_effects(int player_id)
{
    // Wave 2.5: apply effects from tile overlays to entities owned by player_id, then tick
    // down duration on overlays owned by player_id.
    // Called before process_end_of_turn_dot so the applied poison ticks in the same wave.
    // Order: effects fire first, then duration decrements - so units take damage on the final
    // tick (duration 1→0) before the overlay is removed.

    // --- Pass 1: apply overlay effects to units owned by player_id ---
    std::set<std::string> already_affected;  // deduplicate large units spanning multiple tiles
    for (const auto& entity : living_entities_for_owner_oldest_first(board, player_id)) {
        if (!board.all_entities_map.contains(entity->entity_id)) {
            continue;
        }
        if (already_affected.count(entity->entity_id)) {
            continue;
        }
        if (!entity->position) {
            continue;
        }
        const auto cells = entity->occupied_positions.empty()
            ? std::vector<std::pair<int, int>>{*entity->position}
            : entity->occupied_positions;
        bool on_gas = false;
        bool on_fire_tile = false;
        for (const auto& [cx, cy] : cells) {
            const auto sq = board.get_square(cx, cy);
            if (!sq) continue;
            const auto* ov = square_overlay_modifier(*sq);
            if (ov && ov->name == kGasCloudOverlayName) {
                on_gas = true;
            }
            if (ov && ov->name == kFireTileOverlayName) {
                on_fire_tile = true;
            }
            if (on_gas && on_fire_tile) {
                break;
            }
        }
        if (!on_gas && !on_fire_tile) {
            continue;
        }
        already_affected.insert(entity->entity_id);
        if (on_gas) {
            add_entity_effect(*entity, "poison", 1, "gas_cloud");
        }
        if (on_fire_tile) {
            add_entity_effect(*entity, "fire", 1, "fire_tile");
        }
    }

    // --- Pass 2: tick down duration on overlays owned by player_id; remove expired ones ---
    // Also tick overlays whose owner has been eliminated (no living entities on the board),
    // so that a disconnected player's gas clouds don't persist indefinitely.
    auto owner_has_living_units = [&](const std::optional<int>& owner_seat) -> bool {
        if (!owner_seat.has_value()) return false;  // no owner - treat as eliminated, decay every turn-end
        for (const auto& [id, ent] : board.all_entities_map) {
            if (ent && ent->owner && *ent->owner == *owner_seat && ent->current_health > 0) {
                return true;
            }
        }
        return false;
    };
    const auto bounds = board.cell_bounds();
    for (int gy = bounds.min_y; gy <= bounds.max_y; ++gy) {
        for (int gx = bounds.min_x; gx <= bounds.max_x; ++gx) {
            const auto sq = board.get_square(gx, gy);
            if (!sq || sq->modifiers.empty()) continue;
            bool any_expired = false;
            for (auto& m : sq->modifiers) {
                if (m.layer != TileLayer::Overlay) continue;
                // Tick at the owning player's turn-end, or at any turn-end if they're eliminated.
                if (m.owner_seat != player_id && owner_has_living_units(m.owner_seat)) continue;
                if (!m.duration.has_value()) continue;  // nullopt = infinite - never expires
                --(*m.duration);
                if (*m.duration == 0) any_expired = true;
            }
            if (any_expired) {
                sq->modifiers.erase(
                    std::remove_if(sq->modifiers.begin(), sq->modifiers.end(),
                        [](const SquareModifier& m) {
                            return m.layer == TileLayer::Overlay && m.duration.has_value() && *m.duration == 0;
                        }),
                    sq->modifiers.end());
            }
        }
    }
}

void GameState::process_bleed_after_movement(const std::shared_ptr<Entity>& entity)
{
    if (!entity || !board.all_entities_map.contains(entity->entity_id)) {
        return;
    }
    const int bleed = entity_effect_amount(*entity, "bleed");
    if (bleed <= 0) {
        return;
    }
    if (!entity_has_bleed_resistance(*entity)) {
        apply_incoming_damage(*entity, bleed, true);
    }
    if (entity->current_health <= 0) {
        destroy_board_entity(entity);
    } else {
        refresh_passive_auras();
    }
}

std::vector<CardInstanceId> GameState::draw_cards(int player_id, int amount) {
    if (amount < 0) return {};
    auto it = players_decks.find(player_id);
    if (it == players_decks.end()) return {};
    return it->second.draw(amount, rng_);
}

bool GameState::move_card_to_purgatory(const int player_id, const CardInstanceId id)
{
    if (!id.is_valid()) {
        return false;
    }
    auto it = players_decks.find(player_id);
    if (it == players_decks.end()) {
        return false;
    }
    if (!it->second.pool.try_get(id)) {
        return false;
    }
    if (const CardInstance* inst = it->second.pool.try_get(id)) {
        if (!inst->public_id.empty()) {
            living_tokens_by_card_id_[player_id].erase(inst->public_id);
        }
    }
    it->second.move_card_to_purgatory(id);
    return true;
}

bool GameState::move_card_to_purgatory_by_public_id(const int player_id, const std::string& public_id)
{
    if (public_id.empty()) {
        return false;
    }
    auto it = players_decks.find(player_id);
    if (it == players_decks.end()) {
        return false;
    }
    const CardInstanceId card = it->second.find_card_by_public_id(public_id);
    if (!card.is_valid()) {
        return false;
    }
    return move_card_to_purgatory(player_id, card);
}

std::optional<CardInstanceId> GameState::grant_temporary_hand_card(
    const int player_id, const std::string& def_key, const int hand_expires_after_owner_turn_ends)
{
    auto it = players_decks.find(player_id);
    if (it == players_decks.end()) {
        return std::nullopt;
    }
    Deck& deck = it->second;
    const CardInstanceId id = deck_allocate_instance(deck, def_key, static_cast<int>(deck.pool.size()));
    if (!id.is_valid()) {
        return std::nullopt;
    }
    CardInstance& inst = deck.pool.at(id);
    inst.hand_expires_after_owner_turn_ends = std::max(0, hand_expires_after_owner_turn_ends);
    deck.hand.push_back(id);
    return id;
}

void GameState::expire_temporary_hand_cards_for_player(const int player_id)
{
    auto it = players_decks.find(player_id);
    if (it == players_decks.end()) {
        return;
    }
    Deck& deck = it->second;
    std::vector<CardInstanceId> vanished;
    for (const CardInstanceId id : deck.hand) {
        CardInstance* inst = deck.pool.try_get(id);
        if (!inst || inst->hand_expires_after_owner_turn_ends <= 0) {
            continue;
        }
        inst->hand_expires_after_owner_turn_ends -= 1;
        if (inst->hand_expires_after_owner_turn_ends <= 0) {
            vanished.push_back(id);
        }
    }
    for (const CardInstanceId id : vanished) {
        (void)move_card_to_purgatory(player_id, id);
    }
}

std::vector<CardInstanceId> GameState::draw_unit_cards(int player_id, int amount) {
    if (amount < 0) return {};
    auto it = players_decks.find(player_id);
    if (it == players_decks.end()) return {};
    return it->second.draw_unit_cards(amount, rng_);
}

std::vector<CardInstanceId> GameState::draw_spell_cards(int player_id, int amount, int max_total_cost) {
    if (amount < 0 || max_total_cost < 0) {
        return {};
    }
    auto it = players_decks.find(player_id);
    if (it == players_decks.end()) {
        return {};
    }
    return it->second.draw_spell_cards(amount, max_total_cost, rng_);
}

std::vector<CardInstanceId> GameState::draw_focus_spell_cards(const int player_id, const int amount,
    const int max_total_cost)
{
    if (amount < 0 || max_total_cost < 0) {
        return {};
    }
    const auto it = players_decks.find(player_id);
    if (it == players_decks.end()) {
        return {};
    }
    return it->second.draw_focus_spell_cards(amount, max_total_cost, rng_);
}

ActionResult GameState::territory_loot_discard_at(const int player_id, const int idx_1based)
{
    if (!pending_territory_loot_ || pending_territory_loot_->player_id != player_id) {
        return {false, "You are not choosing a territory loot discard", {}};
    }
    auto deck_it = players_decks.find(player_id);
    if (deck_it == players_decks.end()) {
        return {false, "No deck", {}};
    }
    if (idx_1based < 1 || idx_1based > static_cast<int>(deck_it->second.hand.size())) {
        return {false, "Invalid card index", {}};
    }
    const CardInstanceId card_id = deck_it->second.hand[static_cast<size_t>(idx_1based - 1)];
    if (const CardInstance* inst = deck_it->second.pool.try_get(card_id)) {
        if (!inst->public_id.empty()) {
            living_tokens_by_card_id_[player_id].erase(inst->public_id);
        }
    }
    if (!deck_it->second.discard_hand_card_at_1based(idx_1based)) {
        return {false, "Invalid card index", {}};
    }
    (void)draw_cards(player_id, 1);
    pending_territory_loot_.reset();
    return {true, "Discarded a card and drew 1", {}};
}

ActionResult GameState::territory_loot_skip(const int player_id)
{
    if (!pending_territory_loot_ || pending_territory_loot_->player_id != player_id) {
        return {false, "You are not choosing a territory loot discard", {}};
    }
    pending_territory_loot_.reset();
    return {true, "Skipped territory loot", {}};
}

ActionResult GameState::discard_hand_card_at(int player_id, int idx_1based) {
    if (!pending_discard_player_ || *pending_discard_player_ != player_id) {
        return {false, "You are not choosing discards to end the turn", {}};
    }
    auto it = players_decks.find(player_id);
    if (it == players_decks.end()) return {false, "No deck", {}};
    if (idx_1based < 1 || idx_1based > static_cast<int>(it->second.hand.size())) {
        return {false, "Invalid card index", {}};
    }
    const CardInstanceId card_id = it->second.hand[static_cast<size_t>(idx_1based - 1)];
    if (const CardInstance* inst = it->second.pool.try_get(card_id)) {
        if (!inst->public_id.empty()) {
            living_tokens_by_card_id_[player_id].erase(inst->public_id);
        }
    }
    if (!it->second.discard_hand_card_at_1based(idx_1based)) return {false, "Invalid card index", {}};
    if (it->second.hand.size() <= static_cast<size_t>(kMaxHandSize)) {
        pending_discard_player_.reset();
        turn_manager.end_turn(*this);
        stack_manager.clear_for_new_turn(turn_manager.current_player());
        // Same as end_current_turn(): refresh auras so the next turn does not keep a stale cache.
        refresh_passive_auras();
    }
    return {true, "Discarded", {}};
}

const std::vector<CardInstanceId>* GameState::pending_scan_peeked_for(const int player_id) const
{
    if (!pending_scan_ || pending_scan_->player_id != player_id) {
        return nullptr;
    }
    return &pending_scan_->peeked;
}

ActionResult GameState::scan_discard_at(const int player_id, const int idx_1based)
{
    if (!pending_scan_ || pending_scan_->player_id != player_id) {
        return {false, "You are not choosing scan discards", {}};
    }
    auto& peeked = pending_scan_->peeked;
    if (idx_1based < 1 || idx_1based > static_cast<int>(peeked.size())) {
        return {false, "Invalid scan card index", {}};
    }
    auto deck_it = players_decks.find(player_id);
    if (deck_it == players_decks.end()) {
        return {false, "No deck", {}};
    }
    const CardInstanceId card_id = peeked[static_cast<size_t>(idx_1based - 1)];
    if (!card_id.is_valid() || !deck_it->second.pool.try_get(card_id)) {
        return {false, "Invalid scan card", {}};
    }
    const bool in_deck = std::find(deck_it->second.deck.begin(), deck_it->second.deck.end(), card_id)
        != deck_it->second.deck.end();
    if (!in_deck) {
        return {false, "Scan card is not in deck", {}};
    }
    if (const CardInstance* inst = deck_it->second.pool.try_get(card_id)) {
        if (!inst->public_id.empty()) {
            living_tokens_by_card_id_[player_id].erase(inst->public_id);
        }
    }
    deck_it->second.discard_card_to_pile(card_id);
    peeked.erase(peeked.begin() + static_cast<std::ptrdiff_t>(idx_1based - 1));
    if (peeked.empty()) {
        pending_scan_.reset();
        complete_deferred_turn_draw_if_ready();
        try_resume_paused_phase_queue_after_scan();
    }
    return {true, "Discarded scanned card", {}};
}

ActionResult GameState::scan_finish(const int player_id)
{
    if (!pending_scan_ || pending_scan_->player_id != player_id) {
        return {false, "You are not choosing scan discards", {}};
    }
    pending_scan_.reset();
    complete_deferred_turn_draw_if_ready();
    try_resume_paused_phase_queue_after_scan();
    return {true, "Scan complete", {}};
}

void GameState::complete_deferred_turn_draw_if_ready()
{
    if (IsAwaitingScan() || !turn_manager.deferred_turn_draw_seat_) {
        return;
    }
    const int seat = *turn_manager.deferred_turn_draw_seat_;
    turn_manager.deferred_turn_draw_seat_.reset();
    const auto cp = turn_manager.current_player();
    if (!cp || *cp != seat || turn_manager.current_phase != TurnPhase::Energy) {
        return;
    }
    draw_cards(seat, 1);
}

void GameState::try_resume_paused_phase_queue_after_scan()
{
    if (pending_scan_ || !combat_viz_pause_.draining) {
        return;
    }
    if (!execute_phase_action_queue(combat_viz_pause_.is_bonus)) {
        return;
    }
    apply_deferred_phase_transition_after_queue();
}

bool GameState::has_pending_move_for(int player_id) const {
    return pending_moves_.find(player_id) != pending_moves_.end();
}

std::optional<PendingMoveSelection> GameState::get_pending_move_for(int player_id) const {
    const auto it = pending_moves_.find(player_id);
    if (it == pending_moves_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::shared_ptr<Unit> GameState::unit_at_validation_pose(const std::shared_ptr<Unit>& unit) const {
    if (!unit || !unit->owner) {
        return unit;
    }
    const std::optional<PendingMoveSelection> pending = get_pending_move_for(*unit->owner);
    if (!pending || pending->unit_entity_id != unit->entity_id) {
        return unit;
    }
    auto preview = std::make_shared<Unit>(*unit);
    preview->position = {pending->resolved_ax, pending->resolved_ay};
    auto shape = entity_shape_offsets(*preview);
    if (pending->quarter_turns_cw != 0) {
        rotate_shape_offsets_n_quarters_cw(shape, pending->quarter_turns_cw);
    }
    preview->shape = shape;
    preview->occupied_positions.clear();
    for (const auto& [dx, dy] : preview->shape) {
        preview->occupied_positions.push_back({pending->resolved_ax + dx, pending->resolved_ay + dy});
    }
    return preview;
}

void GameState::clear_pending_move_for(int player_id) {
    pending_moves_.erase(player_id);
}

void GameState::reconcile_pending_move_for_unit_selection(const int player_id, const std::string& unit_entity_id)
{
    if (!has_pending_move_for(player_id)) {
        return;
    }
    const std::optional<PendingMoveSelection> pending = get_pending_move_for(player_id);
    if (pending && pending->unit_entity_id == unit_entity_id) {
        return;
    }
    clear_pending_move_for(player_id);
}

ActionResult GameState::apply_move_preview(int player_id, const std::shared_ptr<Unit>& unit, int goal_x, int goal_y) {
    refresh_passive_auras();
    if (!unit || !entity_owned_by(*unit, player_id)) {
        return {false, "Not your unit", {}};
    }
    if (core_cracker_shutdown_blocks_actions(*unit)) {
        return {false, "Core Cracker must Prime Core (3 gallantry, slow) before moving", {}};
    }
    if (!entity_can_move(*unit)) {
        return {false, "Buildings cannot move", {}};
    }
    if (!unit_may_move_this_phase(unit->entity_id)) {
        return {false, "That unit cannot move after queuing an action this phase", {}};
    }
    if (!unit->position) {
        return {false, "Unit has no position", {}};
    }
    if (unit->moves_remaining_this_turn <= 0) {
        if (deployment_fatigue_blocks_move(*unit)) {
            return {false, "Cannot move on the turn this unit was deployed", {}};
        }
        return {false, "No moves remaining this turn", {}};
    }
    if (turn_manager.current_phase == TurnPhase::BonusAttackDeclaration) {
        if (!unit_may_move_during_bonus_attack_declaration(*unit)) {
            return {false, "Only units with bonus attacks or bonus moves may move during bonus attack declaration", {}};
        }
    }
    const auto prev_it = pending_moves_.find(player_id);
    if (prev_it != pending_moves_.end() && prev_it->second.unit_entity_id != unit->entity_id) {
        return {false, "Cancel your current pending move before previewing another unit", {}};
    }
    const auto res = resolve_move_goal(*this, unit, goal_x, goal_y);
    if (!res.ok) {
        return {false, res.message, {}};
    }
    int keep_q = 0;
    if (prev_it != pending_moves_.end() && prev_it->second.unit_entity_id == unit->entity_id && prev_it->second.goal_x == goal_x &&
        prev_it->second.goal_y == goal_y) {
        keep_q = prev_it->second.quarter_turns_cw;
    }
    PendingMoveSelection pm;
    pm.player_id = player_id;
    pm.unit_entity_id = unit->entity_id;
    pm.goal_x = goal_x;
    pm.goal_y = goal_y;
    pm.resolved_ax = res.resolved_ax;
    pm.resolved_ay = res.resolved_ay;
    pm.quarter_turns_cw = keep_q;
    if (!rotation_fits_at_anchor(*this, unit, pm.resolved_ax, pm.resolved_ay, pm.quarter_turns_cw, nullptr)) {
        return {false, "Footprint does not fit at destination with current rotation (use move_rotate or pick another cell)", {}};
    }
    PhaseUndoEntry undo;
    undo.player_id = player_id;
    undo.kind = PhaseUndoKind::MovePreview;
    undo.prior_pending = get_pending_move_for(player_id);
    push_phase_undo(std::move(undo));
    pending_moves_[player_id] = std::move(pm);
    return {true, "Move preview set - move_rotate, then move_confirm or move_cancel", {}};
}

ActionResult GameState::apply_pending_move_rotation_delta(int player_id, int delta_quarters_cw) {
    const auto pit = pending_moves_.find(player_id);
    if (pit == pending_moves_.end()) {
        return {false, "No pending move", {}};
    }
    auto it = board.all_entities_map.find(pit->second.unit_entity_id);
    if (it == board.all_entities_map.end() || !it->second) {
        clear_pending_move_for(player_id);
        return {false, "Unit no longer on board", {}};
    }
    auto u = std::dynamic_pointer_cast<Unit>(it->second);
    if (!u || !entity_owned_by(*u, player_id)) {
        clear_pending_move_for(player_id);
        return {false, "Unit not controllable", {}};
    }
    if (!entity_can_move(*u)) {
        clear_pending_move_for(player_id);
        return {false, "Buildings cannot move", {}};
    }
    const int q = pit->second.quarter_turns_cw + delta_quarters_cw;
    if (!rotation_fits_at_anchor(*this, u, pit->second.resolved_ax, pit->second.resolved_ay, q, nullptr)) {
        return {false, "That rotation does not fit at the chosen destination", {}};
    }
    PhaseUndoEntry undo;
    undo.player_id = player_id;
    undo.kind = PhaseUndoKind::MoveRotation;
    undo.prior_rotation = pit->second.quarter_turns_cw;
    push_phase_undo(std::move(undo));
    pit->second.quarter_turns_cw = q;
    return {true, "Pending rotation updated", {}};
}

ActionResult GameState::confirm_pending_move(int player_id) {
    refresh_passive_auras();
    const auto pit = pending_moves_.find(player_id);
    if (pit == pending_moves_.end()) {
        return {false, "No pending move to confirm", {}};
    }
    const PendingMoveSelection saved = pit->second;
    auto it = board.all_entities_map.find(saved.unit_entity_id);
    if (it == board.all_entities_map.end() || !it->second) {
        clear_pending_move_for(player_id);
        return {false, "Unit no longer on board", {}};
    }
    auto u = std::dynamic_pointer_cast<Unit>(it->second);
    if (!u || !entity_owned_by(*u, player_id)) {
        clear_pending_move_for(player_id);
        return {false, "Not your unit", {}};
    }
    if (!entity_can_move(*u)) {
        clear_pending_move_for(player_id);
        return {false, "Buildings cannot move", {}};
    }
    if (!u->position) {
        clear_pending_move_for(player_id);
        return {false, "Unit has no position", {}};
    }
    if (u->moves_remaining_this_turn <= 0) {
        clear_pending_move_for(player_id);
        if (deployment_fatigue_blocks_move(*u)) {
            return {false, "Cannot move on the turn this unit was deployed", {}};
        }
        return {false, "No moves remaining this turn", {}};
    }
    if (!unit_may_move_this_phase(u->entity_id)) {
        clear_pending_move_for(player_id);
        return {false, "That unit cannot move after queuing an action this phase", {}};
    }
    if (turn_manager.current_phase == TurnPhase::BonusAttackDeclaration) {
        if (!unit_may_move_during_bonus_attack_declaration(*u)) {
            return {false, "Only units with bonus attacks or bonus moves may move during bonus attack declaration", {}};
        }
    }
    const auto res = resolve_move_goal(*this, u, saved.goal_x, saved.goal_y);
    if (!res.ok || res.resolved_ax != saved.resolved_ax || res.resolved_ay != saved.resolved_ay) {
        clear_pending_move_for(player_id);
        return {false, "Move no longer valid; preview cleared", {}};
    }
    const auto path = board.find_path(u, saved.resolved_ax, saved.resolved_ay, this);
    if (!path || !u->position) {
        clear_pending_move_for(player_id);
        return {false, "Move path no longer valid; preview cleared", {}};
    }
    if (!path_respects_taunt_stop(*this, *u, *path)) {
        clear_pending_move_for(player_id);
        return {false, "Move no longer valid under taunt; preview cleared", {}};
    }
    const int terrain_damage = movement_path_terrain_damage(*this, *u, *path);

    // Collect any pickups occupying the destination footprint before performing rotation/placement
    // checks and the actual place_entity call. Pickups are temporarily removed so those checks see
    // a clear destination. They are restored if the move ultimately fails; otherwise they are
    // consumed and their effects fire after the mover is successfully placed.
    struct PickupRecord { std::shared_ptr<Entity> pickup; std::pair<int, int> cell; };
    std::vector<PickupRecord> pickups_collected;
    {
        const auto dest_offs = entity_shape_offsets(*u);
        for (const auto& [dx, dy] : dest_offs) {
            const int wx = saved.resolved_ax + dx;
            const int wy = saved.resolved_ay + dy;
            const auto sq = board.get_square(wx, wy);
            if (sq && sq->occupied && sq->entity && entity_is_pickup(*sq->entity)) {
                pickups_collected.push_back({sq->entity, {wx, wy}});
            }
        }
        for (const auto& rec : pickups_collected) {
            board.remove_entity(rec.pickup);
        }
    }

    // Crushing Advance: collect non-pickup entities on the destination footprint and
    // temporarily remove them so rotation/placement checks succeed. They are restored on
    // failure; on success they receive 3 damage and are pushed to a surrounding cell
    // (or destroyed if no surrounding cell is available). Ignores Immovable on targets.
    struct CrushedRecord { std::shared_ptr<Entity> entity; std::pair<int,int> original_anchor; };
    std::vector<CrushedRecord> crushed_entities;
    const bool is_crusher = entity_has_attribute(*u, "crushes_on_move");
    if (is_crusher) {
        const auto dest_offs = entity_shape_offsets(*u);
        std::set<std::string> seen_crush;
        for (const auto& [dx, dy] : dest_offs) {
            const int wx = saved.resolved_ax + dx;
            const int wy = saved.resolved_ay + dy;
            const auto sq = board.get_square(wx, wy);
            if (sq && sq->occupied && sq->entity && sq->entity != u
                    && !entity_is_pickup(*sq->entity)
                    && seen_crush.insert(sq->entity->entity_id).second) {
                const auto anchor = sq->entity->position.value_or(std::make_pair(wx, wy));
                crushed_entities.push_back({sq->entity, anchor});
            }
        }
        for (const auto& rec : crushed_entities) {
            board.remove_entity(rec.entity);
        }
    }

    std::vector<std::pair<int, int>> final_shape;
    if (!rotation_fits_at_anchor(*this, u, saved.resolved_ax, saved.resolved_ay, saved.quarter_turns_cw, &final_shape)) {
        // Restore pickups and crushed entities before aborting.
        for (const auto& rec : pickups_collected) {
            board.place_entity(rec.pickup, rec.cell.first, rec.cell.second);
        }
        for (const auto& rec : crushed_entities) {
            board.place_entity(rec.entity, rec.original_anchor.first, rec.original_anchor.second);
        }
        return {false, "Rotation blocked at destination", {}};
    }
    const auto old_anchor = *u->position;
    const auto old_shape = entity_shape_offsets(*u);
    board.remove_entity(u);
    const int qn = ((saved.quarter_turns_cw % 4) + 4) % 4;
    if (qn == 0) {
        u->shape = old_shape;
    } else {
        u->shape = std::move(final_shape);
    }
    if (!board.place_entity(u, saved.resolved_ax, saved.resolved_ay)) {
        u->shape = old_shape;
        // Restore pickups and crushed entities before aborting.
        for (const auto& rec : pickups_collected) {
            board.place_entity(rec.pickup, rec.cell.first, rec.cell.second);
        }
        for (const auto& rec : crushed_entities) {
            board.place_entity(rec.entity, rec.original_anchor.first, rec.original_anchor.second);
        }
        if (!board.place_entity(u, old_anchor.first, old_anchor.second)) {
            clear_pending_move_for(player_id);
            return {false, "Critical: failed to restore unit after blocked confirm", {}};
        }
        clear_pending_move_for(player_id);
        return {false, "Could not place unit at destination", {}};
    }
    if (pickups_collected.empty() && crushed_entities.empty()) {
        PhaseUndoEntry undo;
        undo.player_id = player_id;
        undo.kind = PhaseUndoKind::MoveConfirm;
        undo.unit_entity_id = u->entity_id;
        undo.old_anchor = old_anchor;
        undo.old_shape = old_shape;
        undo.old_moves_remaining = u->moves_remaining_this_turn;
        undo.old_standard_moves_remaining = u->standard_moves_remaining_this_turn;
        undo.old_has_moved = u->has_moved_this_turn;
        push_phase_undo(std::move(undo));
    }
    consume_move_action_on_confirm(*u);
    clear_pending_move_for(player_id);

    // Fire pickup effects. Each pickup's effect_key is dispatched as a Burst StackItem with the
    // mover as both source and controller. Pickups with no effect_key are silently consumed.
    for (const auto& rec : pickups_collected) {
        const auto& pk = rec.pickup;
        if (!pk->pickup_effect_key.empty() && u->owner && stack_manager.has_effect_handler(pk->pickup_effect_key)) {
            StackItem pk_item;
            pk_item.source_type = "pickup";
            pk_item.source_name = pk->entity_id;
            pk_item.source_entity_id = u->entity_id;
            pk_item.controller_id = *u->owner;
            pk_item.effect_key = pk->pickup_effect_key;
            pk_item.speed = EffectSpeed::Blazing;
            pk_item.payload = pk->pickup_payload;
            stack_manager.add_item(*this, std::move(pk_item));
        }
    }

    // Crushing Advance: damage + push (or destroy) each crushed entity.
    // Immovable is intentionally ignored - the push bypasses it by design.
    // Push direction is prioritized away from the Core Cracker's geometric center so entities are
    // flung in the direction that makes physical sense (away from the mass that hit them).
    const auto cc_shape_offs = entity_shape_offsets(*u);
    float cc_cx = 0.0f, cc_cy = 0.0f;
    for (const auto& [ddx, ddy] : cc_shape_offs) {
        cc_cx += static_cast<float>(saved.resolved_ax + ddx) + 0.5f;
        cc_cy += static_cast<float>(saved.resolved_ay + ddy) + 0.5f;
    }
    if (!cc_shape_offs.empty()) {
        cc_cx /= static_cast<float>(cc_shape_offs.size());
        cc_cy /= static_cast<float>(cc_shape_offs.size());
    }
    for (const auto& crec : crushed_entities) {
        auto& ce = crec.entity;
        if (!ce || ce->current_health <= 0) continue;
        // Deal 3 crush damage (no pierce, just flat damage).
        apply_incoming_damage(*ce, 3, /*pierce=*/false);
        if (ce->current_health <= 0) {
            destroy_board_entity(ce);
            continue;
        }
        // Compute push vector: from Core Cracker center toward crushed entity center.
        const auto [ox, oy] = crec.original_anchor;
        const auto ce_shape_offs = entity_shape_offsets(*ce);
        float ce_cx = 0.0f, ce_cy = 0.0f;
        for (const auto& [ddx, ddy] : ce_shape_offs) {
            ce_cx += static_cast<float>(ox + ddx) + 0.5f;
            ce_cy += static_cast<float>(oy + ddy) + 0.5f;
        }
        if (!ce_shape_offs.empty()) {
            ce_cx /= static_cast<float>(ce_shape_offs.size());
            ce_cy /= static_cast<float>(ce_shape_offs.size());
        }
        const float push_vx = ce_cx - cc_cx;
        const float push_vy = ce_cy - cc_cy;
        // Sort all 8 directions by descending dot product with the push vector so the most
        // "away from the crusher" direction is tried first.
        std::array<std::pair<int,int>, 8> sorted_dirs{{
            {0,-1},{1,0},{0,1},{-1,0},{1,-1},{1,1},{-1,1},{-1,-1}
        }};
        std::stable_sort(sorted_dirs.begin(), sorted_dirs.end(),
            [push_vx, push_vy](const std::pair<int,int>& a, const std::pair<int,int>& b) {
                const float da = static_cast<float>(a.first) * push_vx
                               + static_cast<float>(a.second) * push_vy;
                const float db = static_cast<float>(b.first) * push_vx
                               + static_cast<float>(b.second) * push_vy;
                return da > db;
            });
        // Try to push to a surrounding cell around the entity's original anchor.
        bool pushed = false;
        for (const auto& [ddx, ddy] : sorted_dirs) {
            if (board.place_entity(ce, ox + ddx, oy + ddy)) {
                resolve_terrain_after_forced_movement(ce);
                pushed = true;
                break;
            }
        }
        if (!pushed) {
            destroy_board_entity(ce);
        }
    }

    int invalidated_queue = cancel_stale_queued_batch_items_after_entity_move(u->entity_id);
    for (const auto& crec : crushed_entities) {
        if (crec.entity && board.all_entities_map.contains(crec.entity->entity_id)) {
            invalidated_queue += cancel_stale_queued_batch_items_after_entity_move(crec.entity->entity_id);
        }
    }
    const auto append_invalidated = [invalidated_queue](std::string msg) -> std::string {
        if (invalidated_queue > 0) {
            msg += " Cancelled " + std::to_string(invalidated_queue) + " queued action(s) invalidated by movement.";
        }
        return msg;
    };

    process_bleed_after_movement(u);
    if (u->current_health <= 0 || !board.all_entities_map.contains(u->entity_id)) {
        return {true, append_invalidated("Move confirmed; bleed destroyed the unit"), {}};
    }
    if (terrain_damage > 0) {
        const int applied_terrain_damage = apply_incoming_damage(*u, terrain_damage);
        if (u->current_health <= 0) {
            destroy_board_entity(u);
            return {true, append_invalidated("Move confirmed; terrain dealt " + std::to_string(applied_terrain_damage) + " damage and destroyed the unit"), {}};
        }
        refresh_passive_auras();
        return {true, append_invalidated(applied_terrain_damage > 0
            ? "Move confirmed; terrain dealt " + std::to_string(applied_terrain_damage) + " damage"
            : "Move confirmed"), {}};
    }
    refresh_passive_auras();
    return {true, append_invalidated("Move confirmed"), {}};
}

ActionResult GameState::resolve_terrain_after_forced_movement(const std::shared_ptr<Entity>& entity) {
    if (!entity || entity->occupied_positions.empty()) {
        return {false, "No placed entity for terrain resolution", {}};
    }
    // Immovable entities are immune to all forced-movement consequences (including void-fall).
    if (entity_is_immovable(*entity)) {
        return {true, entity->entity_id + " is Immovable - terrain consequence suppressed", {}};
    }
    std::vector<const GridSquare*> occupied;
    occupied.reserve(entity->occupied_positions.size());
    for (const auto& [x, y] : entity->occupied_positions) {
        occupied.push_back(board.get_square(x, y).get());
    }
    if (entity_should_fall_into_void(*entity, occupied)) {
        const std::string id = entity->entity_id;
        destroy_board_entity(entity);
        return {true, id + " fell into the void", {}};
    }
    return {true, "No forced terrain effect", {}};
}

void GameState::repair_standard_duel_board_geometry_if_needed()
{
    if (board_width_ != kStandardBoardWidth || board_height_ != kStandardBoardHeight) {
        return;
    }
    if (board.cell_count() == 80 && layout_spec_.layout_id == kDefaultBoardLayoutId) {
        return;
    }
    apply_layout_spec(make_default_map_layout());
}

void GameState::ensure_player_bases()
{
    auto place_if_missing = [&](int player_id, const std::optional<PlayerBaseZone>& zone) {
        if (!zone) {
            return;
        }
        const std::string id = "base_p" + std::to_string(player_id);
        if (board.all_entities_map.contains(id)) {
            return;
        }
        if (std::find(turn_manager.players.begin(), turn_manager.players.end(), player_id) ==
            turn_manager.players.end()) {
            return;
        }
        auto base = make_player_base(player_id);
        if (!board.place_entity(base, zone->anchor_x, zone->anchor_y)) {
            throw std::runtime_error("GameState: failed to place base for player " + std::to_string(player_id));
        }
        note_entity_placed(base);
        refresh_ability_uses_for_entity(base);
    };
    place_if_missing(1, layout_spec_.base_zone_p1);
    place_if_missing(2, layout_spec_.base_zone_p2);
    place_if_missing(3, layout_spec_.base_zone_p3);
    place_if_missing(4, layout_spec_.base_zone_p4);
}

void GameState::start_game() {
    ensure_player_bases();
    if (!turn_manager.players.empty()) {
        turn_manager.start_turn(*this);
        refresh_passive_auras();
        if (turn_manager.current_phase == TurnPhase::Main) {
            if (auto cp = turn_manager.current_player()) stack_manager.open_main_window(*cp);
        }
    }
}

ActionResult GameState::choose_energy_zone(int player_id, int choice_index) {
    auto r = turn_manager.choose_energy_zone(*this, player_id, choice_index);
    if (r.ok && turn_manager.current_phase == TurnPhase::Main) {
        if (auto cp = turn_manager.current_player()) stack_manager.open_main_window(*cp);
    }
    return r;
}

ActionResult GameState::skip_energy_zone(int player_id) {
    auto r = turn_manager.skip_energy_zone(*this, player_id);
    if (r.ok && turn_manager.current_phase == TurnPhase::Main) {
        if (auto cp = turn_manager.current_player()) stack_manager.open_main_window(*cp);
    }
    return r;
}

namespace {
// Resolve one territory effect through the shared effect_key pipeline (same as spells/abilities).
void resolve_territory_effect(GameState& game, const int player_id, const TerritoryEffect& eff,
    const std::map<std::string, int>& targets, const std::string& target_entity_id)
{
    if (eff.effect_key.empty()) {
        return;
    }
    StackItem item;
    item.item_id = "territory";
    item.source_type = "territory";
    item.source_name = "Territory";
    item.controller_id = player_id;
    item.effect_key = eff.effect_key;
    item.payload = eff.payload;
    item.string_payload = eff.string_payload;
    item.board_target_kind = eff.board_target_kind;
    if (eff.requires_target) {
        item.targets = targets;
        item.target_entity_id = target_entity_id;
    }
    game.stack_manager.resolve_item_direct(game, item);
}
}  // namespace

bool GameState::on_territory_conquered(const int player_id, EnergyZone& placed)
{
    // Snapshot the previously-conquered territory BEFORE recording this one - groundwork
    // matches against the *previous* conquest.
    ConqueredTerritoryMemory prev;
    if (const auto it = last_conquered_territory.find(player_id); it != last_conquered_territory.end()) {
        prev = it->second;
    }

    for (const GroundworkTrigger& gw : placed.groundwork) {
        if (!gw.destroy_if_unmet) {
            continue;
        }
        const bool matches = prev.has_value && prev.was_basic && prev.color == gw.color;
        if (!matches) {
            auto& zones = players_energy_zones[player_id];
            if (!zones.empty() && &zones.back() == &placed) {
                zones.pop_back();
            }
            return false;
        }
    }

    // Targeted enter/groundwork effects are collected here and resolved one at a time once the
    // player picks a target (see resolve_territory_target).
    std::vector<TerritoryEffect> targeted;

    bool groundwork_ignore_depleted = false;
    for (const GroundworkTrigger& gw : placed.groundwork) {
        const bool matches = prev.has_value && prev.was_basic && prev.color == gw.color;
        if (!matches) {
            continue;
        }
        if (gw.ignore_depleted) {
            groundwork_ignore_depleted = true;
        }
        if (gw.effect && !gw.effect->effect_key.empty()) {
            if (gw.effect->requires_target) {
                targeted.push_back(*gw.effect);
            } else {
                resolve_territory_effect(*this, player_id, *gw.effect, {}, {});
            }
        }
    }

    // `depleted`: the land's use starts spent, unless a groundwork trigger ignores it.
    placed.depleted = placed.enters_depleted && !groundwork_ignore_depleted;
    placed.land_use_available = placed.has_land_abilities() ? (placed.depleted ? 0 : 1) : 0;

    for (const TerritoryEffect& eff : placed.enter_effects) {
        if (eff.effect_key.empty()) {
            continue;
        }
        if (eff.requires_target) {
            targeted.push_back(eff);
        } else {
            resolve_territory_effect(*this, player_id, eff, {}, {});
        }
    }

    ConqueredTerritoryMemory mem;
    mem.has_value = true;
    mem.was_basic = placed.is_basic;
    mem.color = placed.color.value_or(EnergyType::Neutral);
    last_conquered_territory[player_id] = mem;

    // Open a pending-target request for any targeted enter/groundwork effects (FIFO).
    if (!targeted.empty()) {
        PendingTerritoryTarget pending;
        pending.player_id = player_id;
        pending.effects = std::move(targeted);
        pending_territory_target_ = std::move(pending);
    }
    return true;
}

std::string GameState::pending_territory_target_effect_key(const int player_id) const
{
    if (!pending_territory_target_ || pending_territory_target_->player_id != player_id
        || pending_territory_target_->effects.empty()) {
        return {};
    }
    return pending_territory_target_->effects.front().effect_key;
}

const TerritoryEffect* GameState::pending_territory_front_effect(const int player_id) const
{
    if (!pending_territory_target_ || pending_territory_target_->player_id != player_id
        || pending_territory_target_->effects.empty()) {
        return nullptr;
    }
    return &pending_territory_target_->effects.front();
}

ActionResult GameState::resolve_territory_target(const int player_id, const std::map<std::string, int>& targets,
    const std::string& target_entity_id)
{
    if (!pending_territory_target_ || pending_territory_target_->player_id != player_id) {
        return {false, "No territory target pending.", {}};
    }
    if (pending_territory_target_->effects.empty()) {
        pending_territory_target_.reset();
        return {false, "No territory target pending.", {}};
    }
    const TerritoryEffect eff = pending_territory_target_->effects.front();
    resolve_territory_effect(*this, player_id, eff, targets, target_entity_id);
    pending_territory_target_->effects.erase(pending_territory_target_->effects.begin());
    if (pending_territory_target_->effects.empty()) {
        pending_territory_target_.reset();
    }
    return {true, "Territory effect resolved.", {}};
}

ActionResult GameState::skip_territory_target(const int player_id)
{
    if (!pending_territory_target_ || pending_territory_target_->player_id != player_id
        || pending_territory_target_->effects.empty()) {
        pending_territory_target_.reset();
        return {true, "No territory target to skip.", {}};
    }
    pending_territory_target_->effects.erase(pending_territory_target_->effects.begin());
    if (pending_territory_target_->effects.empty()) {
        pending_territory_target_.reset();
    }
    return {true, "Territory effect skipped.", {}};
}

ActionResult GameState::use_land(const int player_id, const int territory_index, const int ability_index,
    const std::map<std::string, int>& targets, const std::string& target_entity_id)
{
    const auto it = players_energy_zones.find(player_id);
    if (it == players_energy_zones.end() || it->second.empty()) {
        return {false, "No territories.", {}};
    }
    std::vector<EnergyZone>& zones = it->second;
    if (territory_index < 0 || territory_index >= static_cast<int>(zones.size())) {
        return {false, "Invalid territory index.", {}};
    }
    EnergyZone& z = zones[static_cast<std::size_t>(territory_index)];
    if (!z.has_land_abilities()) {
        return {false, z.name + " has no land ability.", {}};
    }
    if (z.land_use_available <= 0) {
        return {false, z.name + " is depleted this turn.", {}};
    }
    if (ability_index < 0 || ability_index >= static_cast<int>(z.land_abilities.size())) {
        return {false, "Invalid land-ability index.", {}};
    }
    const TerritoryAbility& ab = z.land_abilities[static_cast<std::size_t>(ability_index)];

    // Non-energy "special" land abilities are channeled speed: only usable on the controller's own
    // main phase. Pure energy-generating abilities are blazing (usable any time the player has a
    // land charge; they resolve immediately).
    if (ab.is_special_ability()) {
        const auto cp = turn_manager.current_player();
        const bool own_main = cp && *cp == player_id
            && (turn_manager.current_phase == TurnPhase::Main
                || turn_manager.current_phase == TurnPhase::SecondMain);
        if (!own_main) {
            return {false, z.name + "'s ability can only be used on your own main phase.", {}};
        }
    }

    if (!ab.cost.empty()) {
        if (!turn_manager.can_afford(*this, player_id, ab.cost)) {
            return {false, "Cannot afford " + z.name + " land ability.", {}};
        }
        turn_manager.spend_energy(*this, player_id, ab.cost);
    }

    for (const auto& [etype, amount] : ab.energy_produced) {
        if (amount <= 0) {
            continue;
        }
        if (ab.produces_flux) {
            credit_tagged_float(turn_manager, "spell_ability", player_id, etype, amount);
        } else {
            turn_manager.player_energy[player_id][etype] += amount;
        }
    }

    if (!ab.effect.effect_key.empty()) {
        resolve_territory_effect(*this, player_id, ab.effect, targets, target_entity_id);
    }

    const bool sacrifice = ab.sacrifice_self;
    const std::string land_name = z.name;
    const std::string ability_label = ab.name.empty() ? std::string{} : ": " + ab.name;

    if (sacrifice) {
        zones.erase(zones.begin() + territory_index);
        return {true, "Sacrificed " + land_name + ability_label, {}};
    }

    // A land has ONE shared use per turn across all its abilities.
    z.land_use_available = 0;
    z.depleted = true;
    return {true, "Used " + land_name + ability_label, {}};
}

ActionResult GameState::perform_action(int player_id, GameAction& action) {
    refresh_passive_auras();
    if (pending_discard_player_) {
        return {false, "Discard to hand limit first", {}};
    }
    if (pending_scan_) {
        return {false, "Finish scanning first", {}};
    }
    if (pending_territory_loot_) {
        return {false, "Finish territory loot choice first", {}};
    }

    const auto phase = turn_manager.current_phase;
    const auto active = turn_manager.current_player();

    if (phase == TurnPhase::Energy) {
        return {false, "Resolve Energy Phase first", {}};
    }

    if (phase == TurnPhase::AttackDeclaration || phase == TurnPhase::BonusAttackDeclaration) {
        // Only the active player may act during an Attack Declaration phase.
        if (!active || *active != player_id) {
            return {false, "Only active player during Attack Declaration", {}};
        }
        if (action.action_type != ActionType::Combat
            && action.action_type != ActionType::Move
            && action.action_type != ActionType::Spell
            && action.action_type != ActionType::Ability) {
            return {false, "Only attacks, moves, and reflex actions now", {}};
        }
        // Fall through - AttackAction::execute queues rather than executing.
    } else if (phase == TurnPhase::SpellWindow || phase == TurnPhase::SecondSpellWindow
               || phase == TurnPhase::Defense || phase == TurnPhase::BonusDefense) {
        // Only the current priority player may act.
        if (!reaction_window_priority_player_.has_value() || *reaction_window_priority_player_ != player_id) {
            return {false, "Not your priority in reaction window", {}};
        }
        if (action.action_type != ActionType::Spell && action.action_type != ActionType::Ability) {
            return {false, "Only reflex spells/abilities in reaction window", {}};
        }
        reaction_window_played_this_turn_ = true;
    } else if (phase == TurnPhase::Main || phase == TurnPhase::SecondMain) {
        if (action.action_type == ActionType::Combat) {
            return {false, "Attacks must be declared during the Attack Declaration phase.", {}};
        }
    } else {
        return {false, "Actions cannot be performed in the current phase.", {}};
    }

    if (has_pending_move_for(player_id) && !action.allows_during_own_pending_move()) {
        return {false, "Confirm or cancel pending move first", {}};
    }
    // Sandbox is a setup/testing tool: allow deploying units for any seat regardless of whose turn
    // it is, so both sides can be placed without cycling turns. (Other actions stay turn-gated.)
    const bool sandbox_free_deploy =
        (game_mode_ == GameMode::Sandbox || game_id_is_sandbox(game_id_)) && dynamic_cast<DeployAction*>(&action) != nullptr;
    if (action.requires_active_player() && active && *active != player_id && !sandbox_free_deploy) {
        return {false, "It is player " + std::to_string(*active) + "'s turn", {}};
    }
    auto cost = action.get_cost(*this);
    const ActionType atype = action.action_type;
    if (!cost.empty() && !turn_manager.can_afford(*this, player_id, cost, atype)) {
        return {false, "Not enough energy", {}};
    }
    auto valid = action.validate(*this);
    if (!valid.ok) return valid;
    auto res = action.execute(*this);
    std::optional<EnergySpendRecord> energy_record;
    if (res.ok && !cost.empty()) {
        if (turn_manager.can_afford(*this, player_id, cost, atype)) {
            energy_record = turn_manager.spend_energy_recorded(*this, player_id, cost, atype);
        }
    }
    if (res.ok) {
        if (dynamic_cast<DeployAction*>(&action) && !match_settings.allow_deployment_undo) {
            // Deploy normally seals the turn (no undo). With the match setting on, fall through
            // and record a Deploy undo entry so it can be reversed like a batched action.
            clear_phase_undo_stack();
        } else {
            record_phase_undo_after_action(player_id, action, res, cost, energy_record);
        }
        mark_passive_auras_dirty();
        (void)atype;
        refresh_passive_auras();
    }
    return res;
}

bool GameState::end_current_turn() {
    if (pending_discard_player_) return false;
    if (pending_scan_) return false;
    if (pending_territory_loot_) return false;

    const auto phase = turn_manager.current_phase;
    // Cannot end turn while a reaction window is open.
    if (phase == TurnPhase::SpellWindow || phase == TurnPhase::SecondSpellWindow
            || phase == TurnPhase::Defense || phase == TurnPhase::BonusDefense) {
        return false;
    }
    // Cannot end turn from an Attack Declaration phase if any actions are queued (must commit).
    if ((phase == TurnPhase::AttackDeclaration || phase == TurnPhase::BonusAttackDeclaration)
            && !attack_phase_queue().empty()) {
        return false;
    }

    auto cp = turn_manager.current_player();
    if (!cp) return false;
    if (has_pending_move_for(*cp)) {
        return false;
    }
    auto hit = players_decks.find(*cp);
    // Same game_mode_ check as add_player(); do not search game_id here.
    const bool bSandbox = game_mode_ == GameMode::Sandbox || game_id_is_sandbox(game_id_);
    if (!bSandbox && hit != players_decks.end() && hit->second.hand.size() > static_cast<size_t>(kMaxHandSize)) {
        pending_discard_player_ = *cp;
        return true;
    }

    clear_phase_undo_stack();
    // Clean up all phase state for the next turn.
    phase_action_queue_.clear();
    phase_action_group_boundaries_.clear();
    attack_declared_unit_ids_.clear();
    phase_action_queue_epoch_ = 0;
    unit_phase_batch_state_.clear();
    reaction_window_forfeited_.clear();
    reaction_window_priority_player_ = std::nullopt;
    reaction_window_played_this_turn_ = false;
    bonus_attack_phase_used_this_turn_ = false;

    turn_manager.end_turn(*this);
    stack_manager.clear_for_new_turn(turn_manager.current_player());
    refresh_passive_auras();
    return true;
}

bool GameState::can_pass_priority(const int player_id) const
{
    if (pending_discard_player_) {
        return false;
    }
    if (pending_scan_) {
        return false;
    }
    if (pending_territory_loot_) {
        return false;
    }
    const auto phase = turn_manager.current_phase;
    if (phase == TurnPhase::SpellWindow || phase == TurnPhase::SecondSpellWindow) {
        return can_pass_spell_window(player_id);
    }
    if (phase == TurnPhase::Defense || phase == TurnPhase::BonusDefense) {
        return can_pass_defense_window(player_id);
    }
    if (phase == TurnPhase::AttackDeclaration || phase == TurnPhase::BonusAttackDeclaration) {
        return false;
    }
    if (phase == TurnPhase::Main || phase == TurnPhase::SecondMain) {
        const auto active = turn_manager.current_player();
        if (!active || *active != player_id) {
            return false;
        }
        return !has_pending_move_for(player_id);
    }
    return false;
}

ActionResult GameState::pass_priority(int player_id) {
    clear_phase_undo_for_player(player_id);
    refresh_passive_auras();
    if (pending_discard_player_) {
        return {false, "Finish discarding before passing priority", {}};
    }
    if (pending_scan_) {
        return {false, "Finish scanning before passing priority", {}};
    }
    if (pending_territory_loot_) {
        return {false, "Finish territory loot choice before passing priority", {}};
    }
    const auto phase = turn_manager.current_phase;
    if (phase == TurnPhase::SpellWindow || phase == TurnPhase::SecondSpellWindow) {
        return pass_spell_window(player_id);
    }
    if (phase == TurnPhase::Defense || phase == TurnPhase::BonusDefense) {
        return pass_defense_window(player_id);
    }
    if (phase == TurnPhase::AttackDeclaration || phase == TurnPhase::BonusAttackDeclaration) {
        return {false, "Use attack_commit to finish declaring attacks.", {}};
    }
    if (phase == TurnPhase::Main || phase == TurnPhase::SecondMain) {
        if (has_pending_move_for(player_id)) {
            return {false, "Confirm or cancel your pending move before passing priority", {}};
        }
        return end_main_phase(player_id);
    }
    return {false, "Cannot pass priority in the current phase.", {}};
}

// ── Attack Declaration Phase ──────────────────────────────────────────────────

// ── Main Phase → Spell Window ──────────────────────────────────────────────────

ActionResult GameState::end_main_phase(int player_id)
{
    clear_phase_undo_on_phase_transition();
    const auto active = turn_manager.current_player();
    if (!active || *active != player_id) {
        return {false, "Only the active player may end the main phase.", {}};
    }
    const auto phase = turn_manager.current_phase;
    if (phase != TurnPhase::Main && phase != TurnPhase::SecondMain) {
        return {false, "Not in a Main Phase.", {}};
    }
    if (has_pending_move_for(player_id)) {
        return {false, "Confirm or cancel your pending move before ending the main phase.", {}};
    }

    if (!pending_spell_declarations().empty()) {
        seal_phase_action_group();
        turn_manager.current_phase = (phase == TurnPhase::Main) ? TurnPhase::SpellWindow : TurnPhase::SecondSpellWindow;
        reset_unit_phase_batch_state_for_new_phase();
        reaction_window_forfeited_.clear();
        reaction_window_priority_player_ = std::nullopt;
        reaction_window_played_this_turn_ = false;
        // Priority starts with next player after active.
        const auto& players = turn_manager.players;
        auto ait = std::find(players.begin(), players.end(), player_id);
        if (ait != players.end()) {
            auto next = ait + 1;
            if (next == players.end()) next = players.begin();
            reaction_window_priority_player_ = *next;
        }
        return {true, "Spell batch committed. Spell reaction window open.", {}};
    }

    if (phase == TurnPhase::Main) {
        // No spells queued - skip directly to AttackDeclaration.
        turn_manager.current_phase = TurnPhase::AttackDeclaration;
        phase_action_queue_.clear();
        phase_action_group_boundaries_.clear();
        attack_declared_unit_ids_.clear();
        phase_action_queue_epoch_ = 0;
        unit_phase_batch_state_.clear();
        return {true, "Main phase ended. Attack Declaration phase started.", {}};
    } else {
        // SecondMain with nothing queued - go to bonus attack phase or end turn.
        begin_bonus_attack_phase_or_end_turn();
        return {true, "Second main phase ended.", {}};
    }
}

void GameState::begin_second_main_phase()
{
    clear_phase_undo_on_phase_transition();
    phase_action_queue_.clear();
    phase_action_group_boundaries_.clear();
    attack_declared_unit_ids_.clear();
    reset_unit_phase_batch_state_for_new_phase();
    stack_manager.open_main_window(turn_manager.current_player().value_or(-1));
    turn_manager.current_phase = TurnPhase::SecondMain;
}

bool GameState::active_player_has_bonus_attacks() const
{
    const auto active = turn_manager.current_player();
    if (!active) return false;
    for (const auto& [id, ent] : board.all_entities_map) {
        if (!ent || !ent->owner.has_value() || *ent->owner != *active) continue;
        const auto* unit = dynamic_cast<const Unit*>(ent.get());
        if (unit && unit->bonus_attacks_remaining_this_turn > 0) return true;
    }
    return false;
}

void GameState::begin_bonus_attack_phase()
{
    clear_phase_undo_on_phase_transition();
    phase_action_queue_.clear();
    phase_action_group_boundaries_.clear();
    attack_declared_unit_ids_.clear();
    phase_action_queue_epoch_ = 0;
    unit_phase_batch_state_.clear();
    bonus_attack_phase_used_this_turn_ = true;
    stack_manager.open_main_window(turn_manager.current_player().value_or(-1));
    turn_manager.current_phase = TurnPhase::BonusAttackDeclaration;
}

void GameState::begin_bonus_attack_phase_or_end_turn()
{
    if (!bonus_attack_phase_used_this_turn_ && active_player_has_bonus_attacks()) {
        begin_bonus_attack_phase();
        return;
    }
    // end_current_turn() refuses to run while a reaction-window phase is still open.
    if (turn_manager.current_phase == TurnPhase::SecondSpellWindow) {
        turn_manager.current_phase = TurnPhase::SecondMain;
    }
    end_current_turn();
}





// ── Attack Declaration Phase ──────────────────────────────────────────────────

ActionResult GameState::declare_attack(int player_id, const std::string& attacker_id, int target_x, int target_y, bool ranged)
{
    const auto active = turn_manager.current_player();
    if (!active || *active != player_id) {
        return {false, "Only the active player may declare attacks.", {}};
    }
    const auto phase = turn_manager.current_phase;
    if (phase != TurnPhase::AttackDeclaration && phase != TurnPhase::BonusAttackDeclaration) {
        return {false, "Not in an Attack Declaration phase.", {}};
    }
    if (!board.all_entities_map.count(attacker_id)) {
        return {false, "Attacker '" + attacker_id + "' not found on board.", {}};
    }
    const Entity& attacker_ent = *board.all_entities_map.at(attacker_id);
    const bool base_attacker = entity_is_base(attacker_ent);
    if (!base_attacker && attack_declared_unit_ids_.count(attacker_id)) {
        return {false, "Unit '" + attacker_id + "' has already declared an attack.", {}};
    }
    // In Bonus Attack Declaration, only units with bonus attacks remaining may declare.
    if (phase == TurnPhase::BonusAttackDeclaration) {
        if (base_attacker) {
            return {false, "Player bases cannot attack during bonus attack declaration.", {}};
        }
        const auto* unit = dynamic_cast<const Unit*>(board.all_entities_map.at(attacker_id).get());
        if (!unit || unit->bonus_attacks_remaining_this_turn <= 0) {
            return {false, "Unit '" + attacker_id + "' has no bonus attacks remaining.", {}};
        }
    }
    if (!unit_may_queue_non_focus_batch_action_this_phase(attacker_id)) {
        return {false, "Unit '" + attacker_id + "' already queued an attack or ability this phase.", {}};
    }
    AttackPhaseEntry entry;
    entry.is_attack = true;
    entry.attack.attacker_id = attacker_id;
    entry.attack.target_x = target_x;
    entry.attack.target_y = target_y;
    entry.attack.ranged = ranged;
    if (!base_attacker) {
        if (auto unit = std::dynamic_pointer_cast<Unit>(board.all_entities_map.at(attacker_id))) {
            if (unit->standard_moves_remaining_this_turn > 0) {
                entry.attack.consumed_move_on_declare = true;
                entry.attack.undo_moves_remaining = unit->moves_remaining_this_turn;
                entry.attack.undo_standard_moves_remaining = unit->standard_moves_remaining_this_turn;
                consume_standard_move_if_unused(*unit);
            }
        }
    }
    append_phase_attack(std::move(entry.attack));
    attack_declared_unit_ids_.insert(attacker_id);
    note_unit_non_focus_batch_queued(attacker_id);
    return {true, "Attack declared: " + attacker_id + " → (" + std::to_string(target_x) + "," + std::to_string(target_y) + ")", {}};
}

ActionResult GameState::undeclare_attack(int player_id, const std::string& attacker_id)
{
    const auto active = turn_manager.current_player();
    if (!active || *active != player_id) {
        return {false, "Only the active player may undeclare attacks.", {}};
    }
    if (turn_manager.current_phase != TurnPhase::AttackDeclaration && turn_manager.current_phase != TurnPhase::BonusAttackDeclaration) {
        return {false, "Not in an Attack Declaration phase.", {}};
    }
    const size_t group_start = phase_action_current_group_start();
    auto it = std::find_if(phase_action_queue_.begin() + static_cast<std::ptrdiff_t>(group_start),
        phase_action_queue_.end(),
        [&](const AttackPhaseEntry& e){ return e.is_attack && e.attack.attacker_id == attacker_id; });
    if (it == phase_action_queue_.end()) {
        return {false, "No declared attack found for unit '" + attacker_id + "'.", {}};
    }
    if (it->attack.consumed_move_on_declare) {
        if (auto unit_it = board.all_entities_map.find(attacker_id);
            unit_it != board.all_entities_map.end() && unit_it->second) {
            if (auto unit = std::dynamic_pointer_cast<Unit>(unit_it->second)) {
                unit->moves_remaining_this_turn = it->attack.undo_moves_remaining;
                unit->standard_moves_remaining_this_turn = it->attack.undo_standard_moves_remaining;
            }
        }
    }
    phase_action_queue_.erase(it);
    attack_declared_unit_ids_.erase(attacker_id);
    refresh_unit_phase_batch_state_from_queue();
    return {true, "Attack declaration removed for " + attacker_id, {}};
}

ActionResult GameState::commit_attack_declaration(int player_id)
{
    clear_phase_undo_for_player(player_id);
    const auto active = turn_manager.current_player();
    if (!active || *active != player_id) {
        return {false, "Only the active player may commit attack declarations.", {}};
    }
    const auto phase = turn_manager.current_phase;
    if (phase != TurnPhase::AttackDeclaration && phase != TurnPhase::BonusAttackDeclaration) {
        return {false, "Not in an Attack Declaration phase.", {}};
    }
    const bool is_bonus = (phase == TurnPhase::BonusAttackDeclaration);
    // NB: attack_phase_queue() returns a vector by value - bind it to a single
    // local before taking begin()/end(). Calling it twice yields iterators into
    // two distinct temporaries; comparing them is UB and walks off the heap.
    const std::vector<AttackPhaseEntry> open_group = attack_phase_queue();
    const bool has_attacks = std::any_of(open_group.begin(), open_group.end(),
        [](const AttackPhaseEntry& e){ return e.is_attack; });
    if (!has_attacks) {
        seal_phase_action_group_if_non_empty();
        combat_viz_pause_.deferred_transition = DeferredPhaseTransitionAfterQueue::CommitWithoutDefenseWindow;
        combat_viz_pause_.deferred_was_bonus_defense = is_bonus;
        if (!execute_phase_action_queue(is_bonus)) {
            return {true, "Resolving batched actions - combat visualization pause.", {}};
        }
        apply_deferred_phase_transition_after_queue();
        return {true, is_bonus ? "No bonus attacks declared. Turn ended." : "No attacks declared. Second Main Phase started.", {}};
    }
    seal_phase_action_group_if_non_empty();
    turn_manager.current_phase = is_bonus ? TurnPhase::BonusDefense : TurnPhase::Defense;
    reset_unit_phase_batch_state_for_new_phase();
    reaction_window_forfeited_.clear();
    reaction_window_priority_player_ = std::nullopt;
    reaction_window_played_this_turn_ = false;
    // Priority starts with next player after active.
    const auto& players = turn_manager.players;
    auto ait = std::find(players.begin(), players.end(), player_id);
    if (ait != players.end()) {
        auto next = ait + 1;
        if (next == players.end()) next = players.begin();
        reaction_window_priority_player_ = *next;
    }
    return {true, is_bonus ? "Bonus attacks committed. Defense window open." : "Attacks committed. Defense window open.", {}};
}

// ── Spell Window ───────────────────────────────────────────────────────────────

bool GameState::can_pass_spell_window(int player_id) const
{
    const auto phase = turn_manager.current_phase;
    if (phase != TurnPhase::SpellWindow && phase != TurnPhase::SecondSpellWindow) return false;
    if (reaction_window_forfeited_.count(player_id)) return false;
    return reaction_window_priority_player_.has_value() && *reaction_window_priority_player_ == player_id;
}

ActionResult GameState::pass_spell_window(int player_id)
{
    if (!can_pass_spell_window(player_id)) {
        return {false, "It is not your turn to act in the Spell reaction window.", {}};
    }
    const auto& players = turn_manager.players;

    bool played_something = reaction_window_played_this_turn_;
    reaction_window_played_this_turn_ = false;

    if (!played_something) {
        reaction_window_forfeited_.insert(player_id);
        if (static_cast<int>(reaction_window_forfeited_.size()) >= static_cast<int>(players.size()) - 1) {
            close_spell_window();
            return {true, "Spell window closed.", {}};
        }
    } else {
        seal_phase_action_group();
    }

    auto it = std::find(players.begin(), players.end(), player_id);
    for (size_t i = 0; i < players.size(); ++i) {
        ++it;
        if (it == players.end()) it = players.begin();
        if (!reaction_window_forfeited_.count(*it)) {
            reaction_window_priority_player_ = *it;
            return {true, "Priority passed to player " + std::to_string(*it) + " in Spell window.", {}};
        }
    }
    close_spell_window();
    return {true, "Spell window closed.", {}};
}

void GameState::close_spell_window()
{
    clear_phase_undo_on_phase_transition();
    const bool was_second = (turn_manager.current_phase == TurnPhase::SecondSpellWindow);
    combat_viz_pause_.deferred_transition = DeferredPhaseTransitionAfterQueue::SpellWindowClose;
    combat_viz_pause_.deferred_was_second_spell = was_second;
    if (!execute_phase_action_queue()) {
        return;
    }
    apply_deferred_phase_transition_after_queue();
}

// ── Defense Phase ─────────────────────────────────────────────────────────────

bool GameState::can_pass_defense_window(int player_id) const
{
    const auto phase = turn_manager.current_phase;
    if (phase != TurnPhase::Defense && phase != TurnPhase::BonusDefense) return false;
    if (reaction_window_forfeited_.count(player_id)) return false;
    return reaction_window_priority_player_.has_value() && *reaction_window_priority_player_ == player_id;
}

bool GameState::has_pending_attacks_in_queue() const
{
    return !pending_attack_declarations().empty();
}

bool GameState::would_attack_commit_open_defense_window() const
{
    const auto phase = turn_manager.current_phase;
    if (phase != TurnPhase::AttackDeclaration && phase != TurnPhase::BonusAttackDeclaration) {
        return false;
    }
    const std::vector<AttackPhaseEntry> open_group = attack_phase_queue();
    return std::any_of(open_group.begin(), open_group.end(),
        [](const AttackPhaseEntry& e) { return e.is_attack; });
}

bool GameState::would_pass_close_defense_window(int player_id) const
{
    const auto phase = turn_manager.current_phase;
    if (phase != TurnPhase::Defense && phase != TurnPhase::BonusDefense) {
        return false;
    }
    if (reaction_window_forfeited_.count(player_id)) {
        return false;
    }
    if (!reaction_window_priority_player_.has_value() || *reaction_window_priority_player_ != player_id) {
        return false;
    }
    if (reaction_window_played_this_turn_) {
        return false;
    }
    const auto& players = turn_manager.players;
    const size_t forfeited_after = reaction_window_forfeited_.size() + 1;
    return static_cast<int>(forfeited_after) >= static_cast<int>(players.size()) - 1;
}

ActionResult GameState::pass_defense_window(int player_id)
{
    if (!can_pass_defense_window(player_id)) {
        return {false, "It is not your turn to act in the Defense window.", {}};
    }
    const auto& players = turn_manager.players;

    bool played_something = reaction_window_played_this_turn_;
    reaction_window_played_this_turn_ = false;

    if (!played_something) {
        reaction_window_forfeited_.insert(player_id);
        if (static_cast<int>(reaction_window_forfeited_.size()) >= static_cast<int>(players.size()) - 1) {
            close_defense_window();
            return {true, "Defense window closed.", {}};
        }
    } else {
        seal_phase_action_group();
    }

    // Advance priority to the next non-forfeited player (all players eligible, including active).
    auto it = std::find(players.begin(), players.end(), player_id);
    for (size_t i = 0; i < players.size(); ++i) {
        ++it;
        if (it == players.end()) it = players.begin();
        if (!reaction_window_forfeited_.count(*it)) {
            reaction_window_priority_player_ = *it;
            return {true, "Priority passed to player " + std::to_string(*it) + " in Defense window.", {}};
        }
    }
    close_defense_window();
    return {true, "Defense window closed.", {}};
}

void GameState::close_defense_window()
{
    clear_phase_undo_on_phase_transition();
    const bool was_bonus = (turn_manager.current_phase == TurnPhase::BonusDefense);
    combat_viz_pause_.deferred_transition = DeferredPhaseTransitionAfterQueue::DefenseWindowClose;
    combat_viz_pause_.deferred_was_bonus_defense = was_bonus;
    if (!execute_phase_action_queue(was_bonus)) {
        return;
    }
    apply_deferred_phase_transition_after_queue();
}

std::vector<StackItem> GameState::pending_spell_declarations() const
{
    std::vector<StackItem> out;
    out.reserve(phase_action_queue_.size());
    for (const auto& entry : phase_action_queue_) {
        if (!entry.is_attack) {
            out.push_back(entry.spell_item);
        }
    }
    return out;
}

std::vector<GameState::AttackPhaseEntry> GameState::attack_phase_queue() const
{
    const size_t start = phase_action_current_group_start();
    return std::vector<AttackPhaseEntry>(phase_action_queue_.begin() + static_cast<std::ptrdiff_t>(start),
        phase_action_queue_.end());
}

size_t GameState::phase_action_current_group_start() const
{
    return phase_action_group_boundaries_.empty() ? 0 : phase_action_group_boundaries_.back();
}


bool GameState::uses_phase_batching() const
{
    const auto phase = turn_manager.current_phase;
    return phase == TurnPhase::Main
        || phase == TurnPhase::SecondMain
        || phase == TurnPhase::SpellWindow
        || phase == TurnPhase::SecondSpellWindow
        || phase == TurnPhase::Defense
        || phase == TurnPhase::BonusDefense
        || phase == TurnPhase::AttackDeclaration
        || phase == TurnPhase::BonusAttackDeclaration;
}

bool GameState::allows_queued_batch_invalidation_refund() const
{
    return uses_phase_batching() && stack_batch_resolution_depth_ == 0;
}

void GameState::push_stack_batch_resolution()
{
    ++stack_batch_resolution_depth_;
}

void GameState::pop_stack_batch_resolution()
{
    if (stack_batch_resolution_depth_ > 0) {
        --stack_batch_resolution_depth_;
    }
}

void GameState::reset_unit_phase_batch_state_for_new_phase()
{
    phase_action_queue_epoch_ = phase_action_queue_.size();
    unit_phase_batch_state_.clear();
}

void GameState::refresh_unit_phase_batch_state_from_queue()
{
    unit_phase_batch_state_.clear();
    for (size_t i = phase_action_queue_epoch_; i < phase_action_queue_.size(); ++i) {
        const auto& entry = phase_action_queue_[i];
        if (entry.is_attack) {
            unit_phase_batch_state_[entry.attack.attacker_id].has_non_focus_batch = true;
            continue;
        }
        const auto& item = entry.spell_item;
        if (item.source_entity_id.empty()) {
            continue;
        }
        if (item.source_type == "focus_spell") {
            unit_phase_batch_state_[item.source_entity_id].focus_spell_count++;
        } else if (item.source_type == "ability") {
            unit_phase_batch_state_[item.source_entity_id].has_non_focus_batch = true;
        }
    }
}

void GameState::note_unit_focus_spell_queued(const std::string& entity_id)
{
    unit_phase_batch_state_[entity_id].focus_spell_count++;
}

void GameState::note_unit_non_focus_batch_queued(const std::string& entity_id)
{
    unit_phase_batch_state_[entity_id].has_non_focus_batch = true;
}

bool GameState::unit_may_move_this_phase(const std::string& entity_id) const
{
    const auto it = unit_phase_batch_state_.find(entity_id);
    if (it == unit_phase_batch_state_.end()) {
        return true;
    }
    return it->second.focus_spell_count == 0 && !it->second.has_non_focus_batch;
}

bool GameState::unit_may_queue_focus_spell_this_phase(const std::string& caster_entity_id) const
{
    (void)caster_entity_id;
    return true;
}

bool GameState::unit_may_queue_non_focus_batch_action_this_phase(const std::string& entity_id) const
{
    const auto it = unit_phase_batch_state_.find(entity_id);
    if (it == unit_phase_batch_state_.end()) {
        return true;
    }
    return !it->second.has_non_focus_batch;
}

ActionResult GameState::queue_batched_spell(StackItem item)
{
    if (item.item_id.empty()) {
        item.item_id = stack_manager.allocate_item_id();
    }
    append_phase_spell(std::move(item));
    if (turn_manager.current_phase == TurnPhase::Main
        || turn_manager.current_phase == TurnPhase::SecondMain) {
        if (const auto ap = turn_manager.current_player()) {
            stack_manager.open_main_window(*ap);
        }
    }
    return {true, "Queued for batch resolution.", {}};
}

const StackItem* GameState::find_batched_item(const std::string& item_id) const
{
    for (const auto& entry : phase_action_queue_) {
        if (!entry.is_attack && entry.spell_item.item_id == item_id) {
            return &entry.spell_item;
        }
    }
    return nullptr;
}

bool GameState::remove_batched_item_by_id(const std::string& item_id)
{
    const StackItem* found = find_batched_item(item_id);
    if (!found) {
        return false;
    }
    if (!found->multicast_cast_id.empty()) {
        const std::string& group = found->multicast_cast_id;
        const auto before = phase_action_queue_.size();
        phase_action_queue_.erase(
            std::remove_if(phase_action_queue_.begin(), phase_action_queue_.end(),
                [&](const AttackPhaseEntry& e) {
                    return !e.is_attack && e.spell_item.multicast_cast_id == group;
                }),
            phase_action_queue_.end());
        if (phase_action_queue_.size() == before) {
            return false;
        }
    } else {
        auto it = std::find_if(phase_action_queue_.begin(), phase_action_queue_.end(),
            [&](const AttackPhaseEntry& e) {
                return !e.is_attack && e.spell_item.item_id == item_id;
            });
        if (it == phase_action_queue_.end()) {
            return false;
        }
        phase_action_queue_.erase(it);
    }
    refresh_unit_phase_batch_state_from_queue();
    return true;
}

bool GameState::queued_batch_item_still_valid(const StackItem& item) const
{
    if (!item.source_entity_id.empty()) {
        const auto src_it = board.all_entities_map.find(item.source_entity_id);
        if (src_it == board.all_entities_map.end() || !src_it->second) {
            return false;
        }
    }
    return stack_queued_batch_item_still_valid(const_cast<GameState&>(*this), item);
}

void GameState::discard_queued_batch_item_undo_record(const std::string& item_id)
{
    phase_undo_stack_.erase(
        std::remove_if(phase_undo_stack_.begin(), phase_undo_stack_.end(),
            [&](const PhaseUndoEntry& e) {
                if (e.kind != PhaseUndoKind::QueuedBatchItem) {
                    return false;
                }
                if (e.batch_item_id == item_id) {
                    return true;
                }
                return std::find(e.extra_batch_item_ids.begin(), e.extra_batch_item_ids.end(), item_id)
                    != e.extra_batch_item_ids.end();
            }),
        phase_undo_stack_.end());
}

bool GameState::refund_queued_batch_item_by_id(const std::string& item_id)
{
    const StackItem* item = find_batched_item(item_id);
    if (!item) {
        return false;
    }
    EnergySpendRecord energy;
    CardInstanceId spell_card;
    bool spell_from_reserves = false;
    bool spell_had_stockpile = false;
    int spell_stockpile_remaining = 0;
    bool spell_stockpile_used_this_turn = false;
    bool spell_stockpile_double_play_used = false;
    int player_id = item->controller_id;
    bool found_undo = false;
    for (const auto& entry : phase_undo_stack_) {
        if (entry.kind != PhaseUndoKind::QueuedBatchItem) {
            continue;
        }
        const bool matches_primary = entry.batch_item_id == item_id;
        const bool matches_extra = std::find(entry.extra_batch_item_ids.begin(), entry.extra_batch_item_ids.end(), item_id)
            != entry.extra_batch_item_ids.end();
        if (!matches_primary && !matches_extra) {
            continue;
        }
        energy = entry.energy;
        spell_card = entry.spell_card;
        spell_from_reserves = entry.spell_from_reserves;
        spell_had_stockpile = entry.spell_had_stockpile;
        spell_stockpile_remaining = entry.spell_stockpile_remaining;
        spell_stockpile_used_this_turn = entry.spell_stockpile_used_this_turn;
        spell_stockpile_double_play_used = entry.spell_stockpile_double_play_used;
        player_id = entry.player_id;
        found_undo = true;
        break;
    }
    phase_undo_stack_.erase(
        std::remove_if(phase_undo_stack_.begin(), phase_undo_stack_.end(),
            [&](const PhaseUndoEntry& e) {
                if (e.kind != PhaseUndoKind::QueuedBatchItem) {
                    return false;
                }
                if (e.batch_item_id == item_id) {
                    return true;
                }
                return std::find(e.extra_batch_item_ids.begin(), e.extra_batch_item_ids.end(), item_id)
                    != e.extra_batch_item_ids.end();
            }),
        phase_undo_stack_.end());
    if (spell_card.is_valid()) {
        auto dit = players_decks.find(player_id);
        if (dit != players_decks.end()) {
            (void)restore_batched_spell_card_refund(dit->second, spell_card, spell_from_reserves, spell_had_stockpile,
                spell_stockpile_remaining, spell_stockpile_used_this_turn, spell_stockpile_double_play_used);
        }
    }
    if (!energy.empty()) {
        turn_manager.refund_energy_spend(*this, player_id, energy);
    }
    return found_undo || spell_card.is_valid() || !energy.empty();
}

bool GameState::cancel_queued_batch_item_with_refund(const std::string& item_id)
{
    if (!find_batched_item(item_id)) {
        return false;
    }
    (void)refund_queued_batch_item_by_id(item_id);
    return remove_batched_item_by_id(item_id);
}

int GameState::cancel_stale_queued_batch_items_after_entity_move(const std::string& moved_entity_id)
{
    int cancelled = 0;
    const auto phase = turn_manager.current_phase;
    if (phase == TurnPhase::AttackDeclaration || phase == TurnPhase::BonusAttackDeclaration) {
        if (const auto active = turn_manager.current_player()) {
            for (const auto& entry : phase_action_queue_) {
                if (entry.is_attack && entry.attack.attacker_id == moved_entity_id) {
                    (void)undeclare_attack(*active, moved_entity_id);
                    break;
                }
            }
        }
    }

    // Cancel + refund while batch items are still queued - not during active stack resolution.
    if (!allows_queued_batch_invalidation_refund()) {
        return cancelled;
    }

    std::vector<std::string> cancel_ids;
    cancel_ids.reserve(phase_action_queue_.size());
    for (const auto& entry : phase_action_queue_) {
        if (entry.is_attack) {
            continue;
        }
        if (!queued_batch_item_still_valid(entry.spell_item)) {
            cancel_ids.push_back(entry.spell_item.item_id);
        }
    }
    for (const auto& id : cancel_ids) {
        if (cancel_queued_batch_item_with_refund(id)) {
            ++cancelled;
        }
    }
    return cancelled;
}

void GameState::append_phase_spell(StackItem item)
{
    const std::string source_id = item.source_entity_id;
    const std::string source_type = item.source_type;
    AttackPhaseEntry entry;
    entry.is_attack = false;
    entry.spell_item = std::move(item);
    phase_action_queue_.push_back(std::move(entry));
    if (!source_id.empty()) {
        if (source_type == "focus_spell") {
            note_unit_focus_spell_queued(source_id);
        } else if (source_type == "ability" && !entry.spell_item.no_phase_batch_lock) {
            note_unit_non_focus_batch_queued(source_id);
        }
    }
}

void GameState::append_phase_attack(AttackDeclaration attack)
{
    AttackPhaseEntry entry;
    entry.is_attack = true;
    entry.attack = std::move(attack);
    phase_action_queue_.push_back(std::move(entry));
}

void GameState::seal_phase_action_group()
{
    if (!phase_action_queue_.empty()
        && (phase_action_group_boundaries_.empty()
            || phase_action_group_boundaries_.back() < phase_action_queue_.size())) {
        phase_action_group_boundaries_.push_back(phase_action_queue_.size());
        prune_sealed_phase_undo_entries();
    }
}

void GameState::seal_phase_action_group_if_non_empty()
{
    const size_t start = phase_action_current_group_start();
    if (phase_action_queue_.size() > start) {
        seal_phase_action_group();
    }
}

bool GameState::is_combat_visualization_paused() const
{
    return combat_viz_pause_.awaiting_resume || ability_viz_holding_queue_;
}

void GameState::clear_last_combat_viz_encounter_result()
{
    last_combat_viz_encounter_result_ = {};
    has_last_combat_viz_encounter_result_ = false;
}

void GameState::record_combat_viz_encounter_result(const CombatVizEncounterResult result)
{
    last_combat_viz_encounter_result_ = result;
    has_last_combat_viz_encounter_result_ = true;
}

bool GameState::try_get_last_combat_viz_encounter_result(CombatVizEncounterResult& out) const
{
    if (!has_last_combat_viz_encounter_result_) {
        return false;
    }
    out = last_combat_viz_encounter_result_;
    return true;
}

bool GameState::try_consume_passive_attack_viz_event(PassiveAttackVizEvent& out)
{
    if (passive_attack_viz_events_.empty()) {
        return false;
    }
    out = passive_attack_viz_events_.front();
    passive_attack_viz_events_.erase(passive_attack_viz_events_.begin());
    return true;
}

void GameState::log_passive_attack_viz_event(PassiveAttackVizEvent event)
{
    if (!combat_visualization_enabled_) {
        return;
    }
    passive_attack_viz_events_.push_back(std::move(event));
}

GameState::CombatVizPauseKind GameState::combat_viz_pause_kind() const
{
    return combat_viz_pause_.pause_kind;
}

bool GameState::try_get_pending_combat_visualization_attack(AttackDeclaration& out) const
{
    if (!combat_viz_pause_.awaiting_resume || combat_viz_pause_.pause_kind != CombatVizPauseKind::Attack) {
        return false;
    }
    out = combat_viz_pause_.pending_attack;
    return true;
}

bool GameState::try_get_pending_ability_resolve_viz(AbilityResolveVizPreview& out) const
{
    if (!combat_viz_pause_.awaiting_resume || combat_viz_pause_.pause_kind != CombatVizPauseKind::Ability) {
        return false;
    }
    out = combat_viz_pause_.pending_ability_viz;
    return true;
}

bool GameState::try_get_pending_combat_visualization_spell(StackItem& out) const
{
    if (!combat_viz_pause_.awaiting_resume || combat_viz_pause_.pause_kind != CombatVizPauseKind::Ability) {
        return false;
    }
    out = combat_viz_pause_.pending_spell;
    return true;
}

bool GameState::try_pause_for_blazing_ability_viz(const StackItem& item)
{
    if (!should_pause_for_ability_resolve_viz(*this, item)) {
        return false;
    }
    const auto preview = build_ability_resolve_viz_preview(*this, item);
    if (preview.blast_cells.empty()) {
        return false;
    }
    combat_viz_pause_.pending_spell = item;
    combat_viz_pause_.pending_ability_viz = preview;
    combat_viz_pause_.pause_kind = CombatVizPauseKind::Ability;
    combat_viz_pause_.awaiting_resume = true;
    combat_viz_pause_.pending_spell_outside_queue = true;
    return true;
}

void GameState::finalize_phase_action_queue_teardown()
{
    phase_action_queue_.clear();
    phase_action_group_boundaries_.clear();
    attack_declared_unit_ids_.clear();
    reset_unit_phase_batch_state_for_new_phase();
    refresh_passive_auras();
    fire_pending_reactives();
    fire_phase_pending_reactives();
    if (combat_viz_pause_.queue_batch_pushed) {
        pop_stack_batch_resolution();
    }
    const auto deferred = combat_viz_pause_.deferred_transition;
    const bool was_second_spell = combat_viz_pause_.deferred_was_second_spell;
    const bool was_bonus_defense = combat_viz_pause_.deferred_was_bonus_defense;
    combat_viz_pause_ = {};
    combat_viz_pause_.deferred_transition = deferred;
    combat_viz_pause_.deferred_was_second_spell = was_second_spell;
    combat_viz_pause_.deferred_was_bonus_defense = was_bonus_defense;
}

void GameState::apply_deferred_phase_transition_after_queue()
{
    const auto transition = combat_viz_pause_.deferred_transition;
    const bool was_second_spell = combat_viz_pause_.deferred_was_second_spell;
    const bool was_bonus_defense = combat_viz_pause_.deferred_was_bonus_defense;
    combat_viz_pause_.deferred_transition = DeferredPhaseTransitionAfterQueue::None;

    switch (transition) {
    case DeferredPhaseTransitionAfterQueue::SpellWindowClose:
        reaction_window_forfeited_.clear();
        reaction_window_priority_player_ = std::nullopt;
        reaction_window_played_this_turn_ = false;
        if (was_second_spell) {
            begin_bonus_attack_phase_or_end_turn();
        } else {
            turn_manager.current_phase = TurnPhase::AttackDeclaration;
            attack_declared_unit_ids_.clear();
            reset_unit_phase_batch_state_for_new_phase();
        }
        break;
    case DeferredPhaseTransitionAfterQueue::DefenseWindowClose:
        reaction_window_forfeited_.clear();
        reaction_window_priority_player_ = std::nullopt;
        reaction_window_played_this_turn_ = false;
        if (was_bonus_defense) {
            turn_manager.current_phase = TurnPhase::BonusAttackDeclaration;
            end_current_turn();
        } else {
            begin_second_main_phase();
        }
        break;
    case DeferredPhaseTransitionAfterQueue::CommitWithoutDefenseWindow:
        if (was_bonus_defense) {
            end_current_turn();
        } else {
            begin_second_main_phase();
        }
        break;
    case DeferredPhaseTransitionAfterQueue::None:
    default:
        break;
    }
}

ActionResult GameState::apply_paused_ability_visualization_step()
{
    const bool bCanApply = combat_viz_pause_.pause_kind == CombatVizPauseKind::Ability
        || (combat_viz_pause_.awaiting_resume
            && (combat_viz_pause_.pending_spell_outside_queue
                || (combat_viz_pause_.draining
                    && combat_viz_pause_.next_resolve_index < combat_viz_pause_.resolve_order.size())));
    if (!bCanApply) {
        return {false, "No paused ability to apply.", {}};
    }
    ability_resolve_viz_capturing_ = true;
    ability_resolve_viz_capture_buffer_.clear();
    ActionResult result{true, "Ability applied.", {}};
    if (combat_viz_pause_.pending_spell_outside_queue) {
        const StackItem item = combat_viz_pause_.pending_spell;
        combat_viz_pause_.pending_spell_outside_queue = false;
        result = stack_manager.resolve_item_direct(*this, item);
    } else if (combat_viz_pause_.draining && combat_viz_pause_.next_resolve_index < combat_viz_pause_.resolve_order.size()) {
        const size_t order_i = combat_viz_pause_.next_resolve_index;
        const size_t idx = combat_viz_pause_.resolve_order[order_i];
        const auto& entry = phase_action_queue_[idx];
        if (entry.is_attack) {
            ability_resolve_viz_capturing_ = false;
            return {false, "Paused entry is not an ability.", {}};
        }
        result = stack_manager.resolve_item_direct(*this, entry.spell_item);
        combat_viz_pause_.next_resolve_index = order_i + 1;
    } else {
        ability_resolve_viz_capturing_ = false;
        return {false, "No paused ability to apply.", {}};
    }
    ability_resolve_viz_capturing_ = false;
    last_ability_resolve_viz_hits_ = std::move(ability_resolve_viz_capture_buffer_);
    ability_resolve_viz_capture_buffer_.clear();
    combat_viz_pause_.pause_kind = CombatVizPauseKind::None;
    combat_viz_pause_.pending_spell = {};
    combat_viz_pause_.pending_ability_viz = {};
    ability_viz_holding_queue_ = true;
    combat_viz_pause_.awaiting_resume = true;
    return result;
}

ActionResult GameState::continue_after_ability_visualization()
{
    if (!ability_viz_holding_queue_) {
        return resume_combat_visualization();
    }
    ability_viz_holding_queue_ = false;
    combat_viz_pause_.awaiting_resume = false;
    if (!combat_viz_pause_.draining) {
        return {true, "Ability visualization complete.", {}};
    }
    if (!execute_phase_action_queue(combat_viz_pause_.is_bonus)) {
        return {true, "Awaiting next combat visualization step.", {}};
    }
    apply_deferred_phase_transition_after_queue();
    return {true, "Combat visualization complete.", {}};
}

bool GameState::try_consume_last_ability_resolve_viz_hits(std::vector<AbilityResolveVizHit>& out)
{
    if (last_ability_resolve_viz_hits_.empty()) {
        return false;
    }
    out = std::move(last_ability_resolve_viz_hits_);
    last_ability_resolve_viz_hits_.clear();
    return true;
}

ActionResult GameState::resume_combat_visualization()
{
    if (!combat_viz_pause_.awaiting_resume && !ability_viz_holding_queue_) {
        return {false, "No combat visualization pause to resume.", {}};
    }
    if (ability_viz_holding_queue_) {
        return continue_after_ability_visualization();
    }
    if (!combat_viz_pause_.awaiting_resume) {
        return {false, "No combat visualization pause to resume.", {}};
    }
    combat_viz_pause_.awaiting_resume = false;
    if (combat_viz_pause_.pause_kind == CombatVizPauseKind::Ability) {
        if (combat_viz_pause_.pending_spell_outside_queue) {
            const StackItem item = combat_viz_pause_.pending_spell;
            combat_viz_pause_.pending_spell = {};
            combat_viz_pause_.pending_ability_viz = {};
            combat_viz_pause_.pause_kind = CombatVizPauseKind::None;
            combat_viz_pause_.pending_spell_outside_queue = false;
            const auto result = stack_manager.resolve_item_direct(*this, item);
            return result.ok ? ActionResult{true, "Ability resolved.", {}} : result;
        }
        combat_viz_pause_.resume_next_spell = true;
        if (!execute_phase_action_queue(combat_viz_pause_.is_bonus)) {
            return {true, "Ability resolved - awaiting next combat visualization step.", {}};
        }
        apply_deferred_phase_transition_after_queue();
        return {true, "Combat visualization complete.", {}};
    }
    combat_viz_pause_.resume_next_attack = true;
    if (!execute_phase_action_queue(combat_viz_pause_.is_bonus)) {
        return {true, "Attack resolved - awaiting next combat visualization step.", {}};
    }
    apply_deferred_phase_transition_after_queue();
    return {true, "Combat visualization complete.", {}};
}

namespace {

bool queued_attack_still_resolvable(GameState& game, const GameState::AttackDeclaration& decl, bool is_bonus)
{
    const auto eit = game.board.all_entities_map.find(decl.attacker_id);
    if (eit == game.board.all_entities_map.end()) {
        return false;
    }
    const auto attacker = std::dynamic_pointer_cast<Unit>(eit->second);
    if (!attacker || attacker->current_health <= 0 || !attacker->owner.has_value()) {
        return false;
    }
    if (is_bonus) {
        if (attacker->bonus_attacks_remaining_this_turn <= 0) {
            return false;
        }
    }
    return validate_attack(game, attacker, *attacker->owner, {decl.target_x, decl.target_y}, decl.ranged).ok;
}

}  // namespace

bool GameState::execute_phase_action_queue(bool is_bonus)
{
    if (!combat_viz_pause_.draining) {
        push_stack_batch_resolution();
        combat_viz_pause_.queue_batch_pushed = true;
        seal_phase_action_group_if_non_empty();

        std::vector<size_t> group_starts;
        group_starts.push_back(0);
        for (const size_t boundary : phase_action_group_boundaries_) {
            if (boundary > 0 && boundary < phase_action_queue_.size()) {
                group_starts.push_back(boundary);
            }
        }

        combat_viz_pause_.resolve_order.clear();
        combat_viz_pause_.resolve_order.reserve(phase_action_queue_.size());
        for (int g = static_cast<int>(group_starts.size()) - 1; g >= 0; --g) {
            const size_t begin = group_starts[static_cast<size_t>(g)];
            const size_t end = (g + 1 < static_cast<int>(group_starts.size()))
                ? group_starts[static_cast<size_t>(g + 1)]
                : phase_action_queue_.size();
            for (size_t i = begin; i < end; ++i) {
                combat_viz_pause_.resolve_order.push_back(i);
            }
        }

        combat_viz_pause_.draining = true;
        combat_viz_pause_.next_resolve_index = 0;
        combat_viz_pause_.is_bonus = is_bonus;
    }

    const std::vector<size_t>& resolve_order = combat_viz_pause_.resolve_order;
    for (size_t order_i = combat_viz_pause_.next_resolve_index; order_i < resolve_order.size(); ++order_i) {
        const size_t idx = resolve_order[order_i];
        const auto& entry = phase_action_queue_[idx];
        if (!entry.is_attack) {
            const StackItem& spell_item = entry.spell_item;
            if (!queued_batch_item_still_valid(spell_item)) {
                discard_queued_batch_item_undo_record(spell_item.item_id);
                combat_viz_pause_.next_resolve_index = order_i + 1;
                continue;
            }
            if (combat_visualization_enabled_ && !combat_viz_pause_.resume_next_spell
                && should_pause_for_ability_resolve_viz(*this, entry.spell_item)) {
                const auto preview = build_ability_resolve_viz_preview(*this, entry.spell_item);
                if (!preview.blast_cells.empty()) {
                    combat_viz_pause_.pending_spell = entry.spell_item;
                    combat_viz_pause_.pending_ability_viz = preview;
                    combat_viz_pause_.pause_kind = CombatVizPauseKind::Ability;
                    combat_viz_pause_.awaiting_resume = true;
                    combat_viz_pause_.next_resolve_index = order_i;
                    return false;
                }
            }
            combat_viz_pause_.resume_next_spell = false;
            combat_viz_pause_.pause_kind = CombatVizPauseKind::None;
            combat_viz_pause_.pending_spell = {};
            combat_viz_pause_.pending_ability_viz = {};
            stack_manager.resolve_item_direct(*this, entry.spell_item);
            combat_viz_pause_.next_resolve_index = order_i + 1;
            if (pending_scan_) {
                return false;
            }
            continue;
        }

        const auto& decl = entry.attack;
        if (!queued_attack_still_resolvable(*this, decl, is_bonus)) {
            combat_viz_pause_.next_resolve_index = order_i + 1;
            continue;
        }
        if (combat_visualization_enabled_ && !combat_viz_pause_.resume_next_attack) {
            combat_viz_pause_.pending_attack = decl;
            combat_viz_pause_.pause_kind = CombatVizPauseKind::Attack;
            combat_viz_pause_.awaiting_resume = true;
            combat_viz_pause_.next_resolve_index = order_i;
            clear_last_combat_viz_encounter_result();
            return false;
        }
        combat_viz_pause_.resume_next_attack = false;

        auto eit = board.all_entities_map.find(decl.attacker_id);
        if (eit == board.all_entities_map.end()) {
            combat_viz_pause_.next_resolve_index = order_i + 1;
            continue;
        }
        auto attacker = std::dynamic_pointer_cast<Unit>(eit->second);
        if (!attacker || attacker->current_health <= 0 || !attacker->owner.has_value()) {
            combat_viz_pause_.next_resolve_index = order_i + 1;
            continue;
        }
        if (is_bonus) {
            if (attacker->bonus_attacks_remaining_this_turn <= 0) {
                combat_viz_pause_.next_resolve_index = order_i + 1;
                continue;
            }
            attacker->bonus_attacks_remaining_this_turn--;
            attacker->attacks_remaining_this_turn++;
        }
        resolve_attack(*this, attacker, *attacker->owner, {decl.target_x, decl.target_y}, decl.ranged, /*allow_counterattack=*/true);
        combat_viz_pause_.next_resolve_index = order_i + 1;
    }

    finalize_phase_action_queue_teardown();
    return true;
}

int GameState::team_of_seat(int seat) const
{
    const auto it = seat_team_id.find(seat);
    if (it == seat_team_id.end()) {
        return seat;
    }
    return it->second;
}

void GameState::set_seat_team(int seat, int team_id)
{
    seat_team_id[seat] = team_id;
}

void GameState::register_unit_deployed(const int player_id, const CardInstanceId card, const std::shared_ptr<Entity>& entity)
{
    if (!card.is_valid() || !entity) {
        return;
    }
    note_entity_placed(entity);
    auto deck_it = players_decks.find(player_id);
    if (deck_it == players_decks.end()) {
        return;
    }
    const CardInstance* inst = deck_it->second.pool.try_get(card);
    if (!inst || inst->public_id.empty()) {
        return;
    }
    entity->source_card_id = inst->public_id;
    living_tokens_by_card_id_[player_id][inst->public_id] = std::max(0, living_tokens_by_card_id_[player_id][inst->public_id]) + 1;
}

void GameState::mark_core_cracker_deployed(const std::shared_ptr<Unit>& unit)
{
    if (!unit || !entity_is_core_cracker(*unit)) {
        return;
    }
    unit->core_cracker_deploy_turn_exempt = true;
    unit->core_cracker_shutdown = false;
}

void GameState::apply_core_cracker_shutdown_at_turn_start(const int owner_id)
{
    board.for_each_entity([&](const std::shared_ptr<Entity>& ent) {
        if (!ent || !ent->owner || *ent->owner != owner_id) {
            return;
        }
        if (!entity_is_core_cracker(*ent)) {
            return;
        }
        if (ent->core_cracker_deploy_turn_exempt) {
            return;
        }
        ent->core_cracker_shutdown = true;
    });
}

void GameState::clear_core_cracker_deploy_exempt_at_turn_end(const int owner_id)
{
    board.for_each_entity([&](const std::shared_ptr<Entity>& ent) {
        if (!ent || !ent->owner || *ent->owner != owner_id) {
            return;
        }
        if (!entity_is_core_cracker(*ent)) {
            return;
        }
        ent->core_cracker_deploy_turn_exempt = false;
    });
}

void GameState::assign_monotonic_entity_id(const std::shared_ptr<Entity>& entity, const std::string& prefix)
{
    if (!entity || prefix.empty()) {
        return;
    }
    const uint64_t seq = next_entity_spawn_sequence_++;
    entity->spawn_sequence = seq;
    entity->entity_id = prefix + "_" + std::to_string(seq);
}

void GameState::note_entity_placed(const std::shared_ptr<Entity>& entity)
{
    if (!entity) {
        return;
    }
    // Ensure the entity is visible in all_entities_map regardless of whether the caller
    // also invoked board.place_entity (which normally inserts it).  This is idempotent  - 
    // a second insert for an already-present key is a no-op.  Protects against future spawn
    // paths that forget to call place_entity before note_entity_placed.
    if (!entity->entity_id.empty()) {
        board.all_entities_map.emplace(entity->entity_id, entity);
    }
    // Stamp team so grid, targeting, and reactive-passive code can use entities_allied()
    // instead of raw owner equality - critical for team games and future mind-control.
    if (entity->owner.has_value()) {
        entity->team = team_of_seat(*entity->owner);
    }
    // Auto-assign the large_unit keyword to any multi-tile entity that lacks it.
    if (entity->shape.size() > 1) {
        if (std::find(entity->keywords.begin(), entity->keywords.end(), "large_unit") == entity->keywords.end()) {
            entity->keywords.push_back("large_unit");
        }
    }
    if (entity->spawn_sequence == 0) {
        entity->spawn_sequence = next_entity_spawn_sequence_++;
        mark_passive_auras_dirty();
        return;
    }
    next_entity_spawn_sequence_ = std::max(next_entity_spawn_sequence_, entity->spawn_sequence + 1);
}

namespace {

void ensure_spawned_token_purgatory_catalog()
{
    static bool registered = false;
    if (registered) {
        return;
    }
    registered = true;

    auto register_unit_token = [](const char* key, const char* name) {
        CardDefinition def;
        def.key = key;
        def.name = name;
        def.type = "unit";
        def.energy_cost = {{EnergyType::Neutral, 0}};
        UnitCardDefinition ud;
        ud.entity_type = "unit";
        def.unit = std::move(ud);
        register_runtime_card_definition(std::move(def));
    };

    register_unit_token("spawned_token_conscript", "Conscript");
    register_unit_token("spawned_token_replicator_bot", "Replicator Bot");
    register_unit_token("spawned_token_flame_trooper", "Flame Trooper");
    register_unit_token("spawned_token_nanite_construct", "Nanite Construct");

    CardDefinition shock_wire;
    shock_wire.key = "spawned_token_shock_wire";
    shock_wire.name = "Shock Wire";
    shock_wire.type = "building";
    shock_wire.energy_cost = {{EnergyType::Neutral, 0}};
    UnitCardDefinition building;
    building.entity_type = "building";
    shock_wire.unit = std::move(building);
    register_runtime_card_definition(std::move(shock_wire));
}

std::string resolve_spawned_token_purgatory_def_key(const std::string& catalog_key)
{
    CardDefinition def;
    if (try_get_card_definition(catalog_key, def)) {
        return catalog_key;
    }
    // Runtime token defs register as "spawned_token_<key>" (ensure_spawned_token_purgatory_catalog);
    // resolve through that convention instead of special-casing individual tokens.
    const std::string token_key = "spawned_token_" + catalog_key;
    if (try_get_card_definition(token_key, def)) {
        return token_key;
    }
    return catalog_key;
}

}  // namespace

bool GameState::record_spawned_token_to_purgatory(const int player_id, const Entity& entity)
{
    const std::optional<std::string> catalog_key = try_spawned_token_purgatory_catalog_key(entity);
    if (!catalog_key) {
        return false;
    }
    auto deck_it = players_decks.find(player_id);
    if (deck_it == players_decks.end()) {
        return false;
    }
    ensure_spawned_token_purgatory_catalog();
    const std::string def_key = resolve_spawned_token_purgatory_def_key(*catalog_key);
    Deck& deck = deck_it->second;
    const CardInstanceId id = deck_allocate_instance(deck, def_key, static_cast<int>(deck.pool.size()));
    if (!id.is_valid()) {
        return false;
    }
    CardInstance& inst = deck.pool.at(id);
    inst.public_id = entity.entity_id;
    deck.send_to_purgatory(id);
    return true;
}

void GameState::return_spawned_card_to_discard_pile(const int player_id, const std::string& card_id)
{
    if (card_id.empty()) {
        return;
    }
    auto deck_it = players_decks.find(player_id);
    if (deck_it == players_decks.end()) {
        return;
    }
    Deck& deck = deck_it->second;
    const CardInstanceId card = deck.find_card_by_public_id(card_id);
    if (!card.is_valid()) {
        return;
    }
    const CardInstance& inst = deck.pool.at(card);
    if (inst.stockpile_amount > 0 && inst.stockpile_remaining > 0) {
        if (deck.contains_in_hand(card) || deck.contains_in_reserves(card) || deck.deck.end() != std::find(deck.deck.begin(), deck.deck.end(), card)) {
            return;
        }
        if (deck.contains_in_in_play(card)) {
            deck.remove_card_from_hand_and_in_play(card);
            deck.deck.push_back(card);
            return;
        }
    }
    if (card_instance_is_temporary_granted(inst)) {
        deck.move_card_to_purgatory(card);
    } else {
        deck.discard_card_to_pile(card);
    }
}

void GameState::queue_pending_death_spawn(const std::string& dying_entity_id, const int player_id, const int x,
    const int y, const int hp, const int attack)
{
    pending_death_spawns_.push_back(PendingDeathSpawn{dying_entity_id, player_id, x, y, hp, attack});
}

void GameState::flush_pending_death_spawns_for(const std::string& dying_entity_id)
{
    for (auto it = pending_death_spawns_.begin(); it != pending_death_spawns_.end();) {
        if (it->dying_entity_id != dying_entity_id) {
            ++it;
            continue;
        }
        const PendingDeathSpawn spawn = *it;
        it = pending_death_spawns_.erase(it);

        const auto sq = board.get_square(spawn.x, spawn.y);
        if (!sq || sq->occupied) {
            continue;
        }

        auto token = std::make_shared<Unit>();
        token->entity_type = "unit";
        token->unit_type = "Nanite Construct";
        token->owner = spawn.player_id;
        token->attack_type = AttackType::Melee;
        token->base_health = spawn.hp;
        token->current_health = spawn.hp;
        token->melee_damage = spawn.attack;
        token->movement = 3;
        token->shape = {{0, 0}};
        normalize_entity_shape(*token);
        sync_unit_damage_ranges_from_nominal(*token);
        assign_monotonic_entity_id(token, "vulturous_nanite_" + dying_entity_id);

        if (!board.place_entity(token, spawn.x, spawn.y)) {
            continue;
        }
        note_entity_placed(token);
        add_entity_effect(*token, "stunned", 1);
        (void)record_spawned_token_to_purgatory(spawn.player_id, *token);
        mark_passive_auras_dirty();
    }
}

void GameState::notify_unit_card_destroyed(const std::shared_ptr<Entity>& entity)
{
    if (!entity || !entity->owner) {
        return;
    }
    if (entity->source_card_id.empty()) {
        (void)record_spawned_token_to_purgatory(*entity->owner, *entity);
        return;
    }
    const int player_id = *entity->owner;
    const std::string& card_id = entity->source_card_id;
    int living_tokens = 0;
    auto player_it = living_tokens_by_card_id_.find(player_id);
    if (player_it != living_tokens_by_card_id_.end()) {
        auto count_it = player_it->second.find(card_id);
        if (count_it != player_it->second.end()) {
            count_it->second = std::max(0, count_it->second - 1);
            living_tokens = count_it->second;
            if (living_tokens == 0) {
                player_it->second.erase(count_it);
            }
        }
    }
    if (living_tokens > 0) {
        return;
    }
    return_spawned_card_to_discard_pile(player_id, card_id);
}

void GameState::destroy_board_entity(const std::shared_ptr<Entity>& entity)
{
    if (!entity) {
        return;
    }
    // Indestructible: cannot be destroyed by any means.
    if (entity_has_attribute(*entity, "indestructible")) {
        return;
    }
    // Fire death reactives before removing from the board (passives / temporary_effects still readable).
    apply_passive_reactive_on_covered_unit_died(*entity);
    apply_passive_reactive_on_self_died(*entity);
    const std::string dying_id = entity->entity_id;
    if (board.remove_entity(entity)) {
        flush_pending_death_spawns_for(dying_id);
        notify_unit_card_destroyed(entity);
    }
    mark_passive_auras_dirty();
    refresh_passive_auras();
}

void GameState::rebuild_living_tokens_from_board()
{
    living_tokens_by_card_id_.clear();
    for (const auto& [_, ent] : board.all_entities_map) {
        if (!ent || !ent->owner || ent->source_card_id.empty()) {
            continue;
        }
        const int player_id = *ent->owner;
        living_tokens_by_card_id_[player_id][ent->source_card_id] =
            std::max(0, living_tokens_by_card_id_[player_id][ent->source_card_id]) + 1;
    }
}

bool enemy_direct_target_blocked_by_stealth(const GameState& game, int actor_seat, const Entity& target)
{
    if (!entity_is_stealthed(target) || !target.owner) {
        return false;
    }
    return teams_hostile(game, actor_seat, *target.owner);
}

bool board_target_allows(const GameState& game, BoardTargetKind kind, int actor_seat, const Entity& target)
{
    if (kind == BoardTargetKind::Any) {
        return true;
    }
    if (entity_is_breakable_obstacle(target) && kind == BoardTargetKind::Enemy) {
        return !target.owner || teams_hostile(game, actor_seat, *target.owner);
    }
    // Pickups are ownerless neutrals - they can be targeted by Enemy effects (damage, fire).
    if (entity_is_pickup(target) && kind == BoardTargetKind::Enemy) {
        return true;
    }
    if (!target.owner) {
        return false;
    }
    const int to = *target.owner;
    if (kind == BoardTargetKind::Own) {
        return to == actor_seat;
    }
    if (kind == BoardTargetKind::Ally) {
        return !teams_hostile(game, actor_seat, to);
    }
    if (kind == BoardTargetKind::Enemy) {
        return teams_hostile(game, actor_seat, to);
    }
    // E3: NonSelf - any entity except the actor's own seat (used for AoE buffs that exclude caster)
    if (kind == BoardTargetKind::NonSelf) {
        return to != actor_seat;
    }
    return false;
}


// ── Phase undo stack ─────────────────────────────────────────────────────────

namespace {

void remove_temporary_effect_by_id(Entity& entity, const char* effect_id)
{
    auto& effects = entity.temporary_effects;
    effects.erase(std::remove_if(effects.begin(), effects.end(),
                      [effect_id](const TemporaryEntityEffect& effect) { return effect.effect_id == effect_id; }),
        effects.end());
}

}  // namespace

void GameState::clear_phase_undo_stack()
{
    phase_undo_stack_.clear();
}

void GameState::clear_phase_undo_for_player(const int player_id)
{
    phase_undo_stack_.erase(
        std::remove_if(phase_undo_stack_.begin(), phase_undo_stack_.end(),
            [player_id](const PhaseUndoEntry& entry) { return entry.player_id == player_id; }),
        phase_undo_stack_.end());
}

void GameState::clear_phase_undo_on_phase_transition()
{
    clear_phase_undo_stack();
}

void GameState::prune_sealed_phase_undo_entries()
{
    const size_t current = phase_action_current_group_start();
    phase_undo_stack_.erase(
        std::remove_if(phase_undo_stack_.begin(), phase_undo_stack_.end(),
            [current](const PhaseUndoEntry& entry) { return entry.phase_group_start != current; }),
        phase_undo_stack_.end());
}

bool GameState::phase_undo_entry_eligible(const PhaseUndoEntry& entry, const int player_id) const
{
    if (entry.player_id != player_id) {
        return false;
    }
    const auto phase = turn_manager.current_phase;
    if (phase == TurnPhase::SpellWindow || phase == TurnPhase::SecondSpellWindow
        || phase == TurnPhase::Defense || phase == TurnPhase::BonusDefense) {
        if (!reaction_window_priority_player_.has_value() || *reaction_window_priority_player_ != player_id) {
            return false;
        }
        return entry.phase_group_start == phase_action_current_group_start();
    }
    if (phase == TurnPhase::Main || phase == TurnPhase::SecondMain
        || phase == TurnPhase::AttackDeclaration || phase == TurnPhase::BonusAttackDeclaration) {
        const auto active = turn_manager.current_player();
        return active.has_value() && *active == player_id;
    }
    return false;
}

void GameState::push_phase_undo(PhaseUndoEntry entry)
{
    entry.phase_group_start = phase_action_current_group_start();
    phase_undo_stack_.push_back(std::move(entry));
}

bool GameState::can_undo_last_action(const int player_id) const
{
    for (int i = static_cast<int>(phase_undo_stack_.size()) - 1; i >= 0; --i) {
        if (phase_undo_entry_eligible(phase_undo_stack_[static_cast<size_t>(i)], player_id)) {
            return true;
        }
    }
    return false;
}

ActionResult GameState::apply_phase_undo_entry(const PhaseUndoEntry& entry)
{
    switch (entry.kind) {
    case PhaseUndoKind::QueuedBatchItem: {
        if (!remove_batched_item_by_id(entry.batch_item_id)) {
            return {false, "Queued action no longer in batch", {}};
        }
        if (entry.spell_card.is_valid()) {
            auto dit = players_decks.find(entry.player_id);
            if (dit == players_decks.end()) {
                return {false, "No deck for player", {}};
            }
            if (!restore_batched_spell_card_refund(dit->second, entry.spell_card, entry.spell_from_reserves,
                    entry.spell_had_stockpile, entry.spell_stockpile_remaining, entry.spell_stockpile_used_this_turn,
                    entry.spell_stockpile_double_play_used)) {
                return {false, "Could not return spell card to hand/reserves", {}};
            }
        }
        if (!entry.energy.empty()) {
            turn_manager.refund_energy_spend(*this, entry.player_id, entry.energy);
        }
        return {true, "Undid queued batch action", {}};
    }
    case PhaseUndoKind::AttackDeclaration:
        return undeclare_attack(entry.player_id, entry.unit_entity_id);
    case PhaseUndoKind::MovePreview: {
        if (entry.prior_pending) {
            pending_moves_[entry.player_id] = *entry.prior_pending;
        } else {
            pending_moves_.erase(entry.player_id);
        }
        return {true, "Undid move preview", {}};
    }
    case PhaseUndoKind::MoveRotation: {
        auto pit = pending_moves_.find(entry.player_id);
        if (pit == pending_moves_.end()) {
            return {false, "No pending move to restore rotation", {}};
        }
        pit->second.quarter_turns_cw = entry.prior_rotation;
        return {true, "Undid move rotation", {}};
    }
    case PhaseUndoKind::MoveConfirm: {
        auto it = board.all_entities_map.find(entry.unit_entity_id);
        if (it == board.all_entities_map.end() || !it->second) {
            return {false, "Unit no longer on board", {}};
        }
        auto unit = std::dynamic_pointer_cast<Unit>(it->second);
        if (!unit || !unit->position) {
            return {false, "Unit cannot be restored", {}};
        }
        board.remove_entity(unit);
        unit->shape = entry.old_shape;
        if (!board.place_entity(unit, entry.old_anchor.first, entry.old_anchor.second)) {
            return {false, "Failed to restore unit position", {}};
        }
        unit->moves_remaining_this_turn = entry.old_moves_remaining;
        unit->standard_moves_remaining_this_turn = entry.old_standard_moves_remaining;
        unit->has_moved_this_turn = entry.old_has_moved;
        return {true, "Undid confirmed move", {}};
    }
    case PhaseUndoKind::Defend: {
        auto it = board.all_entities_map.find(entry.unit_entity_id);
        if (it == board.all_entities_map.end() || !it->second) {
            return {false, "Unit no longer on board", {}};
        }
        auto unit = std::dynamic_pointer_cast<Unit>(it->second);
        if (!unit) {
            return {false, "Unit not found", {}};
        }
        remove_temporary_effect_by_id(*unit, effect_keys::kDefendStanceEffectId);
        unit->attacks_remaining_this_turn = entry.old_attacks_remaining;
        unit->has_attacked_this_turn = entry.old_has_attacked;
        unit->moves_remaining_this_turn = entry.old_moves_remaining;
        unit->standard_moves_remaining_this_turn = entry.old_standard_moves_remaining;
        if (!entry.energy.empty()) {
            turn_manager.refund_energy_spend(*this, entry.player_id, entry.energy);
        }
        return {true, "Undid defend", {}};
    }
    case PhaseUndoKind::Dash: {
        auto it = board.all_entities_map.find(entry.unit_entity_id);
        if (it == board.all_entities_map.end() || !it->second) {
            return {false, "Unit no longer on board", {}};
        }
        auto unit = std::dynamic_pointer_cast<Unit>(it->second);
        if (!unit) {
            return {false, "Unit not found", {}};
        }
        remove_temporary_effect_by_id(*unit, effect_keys::kDashMovementEffectId);
        unit->attacks_remaining_this_turn = entry.old_attacks_remaining;
        unit->has_attacked_this_turn = entry.old_has_attacked;
        if (!entry.energy.empty()) {
            turn_manager.refund_energy_spend(*this, entry.player_id, entry.energy);
        }
        return {true, "Undid dash", {}};
    }
    case PhaseUndoKind::Recover: {
        auto it = board.all_entities_map.find(entry.unit_entity_id);
        if (it == board.all_entities_map.end() || !it->second) {
            return {false, "Unit no longer on board", {}};
        }
        auto unit = std::dynamic_pointer_cast<Unit>(it->second);
        if (!unit) {
            return {false, "Unit not found", {}};
        }
        remove_temporary_effect_by_id(*unit, effect_keys::kRecoverStanceEffectId);
        remove_entity_effect(*unit, effect_keys::kRecoverStanceDamageTakenKey);
        unit->attacks_remaining_this_turn = entry.old_attacks_remaining;
        unit->has_attacked_this_turn = entry.old_has_attacked;
        unit->moves_remaining_this_turn = entry.old_moves_remaining;
        unit->standard_moves_remaining_this_turn = entry.old_standard_moves_remaining;
        if (!entry.energy.empty()) {
            turn_manager.refund_energy_spend(*this, entry.player_id, entry.energy);
        }
        return {true, "Undid recover", {}};
    }
    case PhaseUndoKind::Deploy: {
        auto it = board.all_entities_map.find(entry.unit_entity_id);
        if (it == board.all_entities_map.end() || !it->second) {
            return {false, "Deployed unit no longer on board", {}};
        }
        auto unit = std::dynamic_pointer_cast<Unit>(it->second);
        if (!unit) {
            return {false, "Deployed entity is not a unit", {}};
        }
        auto dit = players_decks.find(entry.player_id);
        if (dit == players_decks.end()) {
            return {false, "No deck for player", {}};
        }
        // Reverse register_unit_deployed: drop the living-token count for this card.
        if (const CardInstance* inst = dit->second.pool.try_get(entry.spell_card); inst && !inst->public_id.empty()) {
            auto& tokens = living_tokens_by_card_id_[entry.player_id];
            if (auto tit = tokens.find(inst->public_id); tit != tokens.end()) {
                tit->second = std::max(0, tit->second - 1);
            }
        }
        board.remove_entity(unit);
        // Stockpile units with charges left stay in hand/reserves after deploy (never enter in_play);
        // only pull the card off the board when it actually went to in_play.
        Deck& deck = dit->second;
        const bool already_live =
            std::find(deck.hand.begin(), deck.hand.end(), entry.spell_card) != deck.hand.end()
            || std::find(deck.reserves.begin(), deck.reserves.end(), entry.spell_card) != deck.reserves.end();
        if (!already_live) {
            if (!deck.return_deployed_unit_to_origin(entry.spell_card, entry.spell_from_reserves)) {
                return {false, "Could not return deployed card to hand/reserves", {}};
            }
        }
        if (entry.spell_had_stockpile) {
            CardInstance& inst = deck.pool.at(entry.spell_card);
            inst.stockpile_remaining = entry.spell_stockpile_remaining;
            inst.stockpile_used_this_turn = entry.spell_stockpile_used_this_turn;
            inst.stockpile_double_play_used_this_turn = entry.spell_stockpile_double_play_used;
        }
        if (!entry.energy.empty()) {
            turn_manager.refund_energy_spend(*this, entry.player_id, entry.energy);
        }
        return {true, "Undid deployment", {}};
    }
    }
    return {false, "Unknown undo entry", {}};
}

ActionResult GameState::undo_last_action(const int player_id)
{
    if (!can_undo_last_action(player_id)) {
        return {false, "Nothing to undo", {}};
    }
    int found = -1;
    for (int i = static_cast<int>(phase_undo_stack_.size()) - 1; i >= 0; --i) {
        if (phase_undo_entry_eligible(phase_undo_stack_[static_cast<size_t>(i)], player_id)) {
            found = i;
            break;
        }
    }
    if (found < 0) {
        return {false, "Nothing to undo", {}};
    }
    PhaseUndoEntry entry = std::move(phase_undo_stack_[static_cast<size_t>(found)]);
    phase_undo_stack_.erase(phase_undo_stack_.begin() + found);
    auto result = apply_phase_undo_entry(entry);
    if (result.ok) {
        mark_passive_auras_dirty();
        refresh_passive_auras();
    }
    return result;
}

ActionResult GameState::cancel_queued_batch_item_for_player(const int player_id, const std::string& item_id)
{
    if (!allows_queued_batch_invalidation_refund()) {
        return {false, "Cannot cancel queued actions during stack resolution", {}};
    }
    const StackItem* item = find_batched_item(item_id);
    if (!item) {
        return {false, "Queued action not found", {}};
    }
    if (item->controller_id != player_id) {
        return {false, "You may only cancel your own queued actions", {}};
    }
    if (!cancel_queued_batch_item_with_refund(item_id)) {
        return {false, "Failed to cancel queued action", {}};
    }
    mark_passive_auras_dirty();
    refresh_passive_auras();
    return {true, "Cancelled queued action (card and energy refunded)", {}};
}

void GameState::record_phase_undo_after_action(const int player_id, GameAction& action, const ActionResult& result,
    const std::map<EnergyType, int>& cost, const std::optional<EnergySpendRecord>& energy_record)
{
    (void)cost;
    if (!result.ok) {
        return;
    }
    if (auto* spell = dynamic_cast<CastSpellAction*>(&action)) {
        const auto bit = result.data.find("batch_item_id");
        if (bit == result.data.end()) {
            return;
        }
        PhaseUndoEntry entry;
        entry.player_id = player_id;
        entry.kind = PhaseUndoKind::QueuedBatchItem;
        entry.batch_item_id = bit->second;
        if (const auto mit = result.data.find("multicast_batch_item_ids"); mit != result.data.end()) {
            std::string remaining = mit->second;
            while (!remaining.empty()) {
                const auto comma = remaining.find(',');
                if (comma == std::string::npos) {
                    if (!remaining.empty()) {
                        entry.extra_batch_item_ids.push_back(remaining);
                    }
                    break;
                }
                entry.extra_batch_item_ids.push_back(remaining.substr(0, comma));
                remaining = remaining.substr(comma + 1);
            }
        }
        entry.spell_card = spell->card_id_for_undo();
        entry.spell_from_reserves = spell->played_from_reserves_for_undo();
        if (result.data.contains("undo_spell_had_stockpile")) {
            entry.spell_had_stockpile = true;
            if (const auto it = result.data.find("undo_stockpile_remaining"); it != result.data.end()) {
                entry.spell_stockpile_remaining = std::stoi(it->second);
            }
            if (const auto it = result.data.find("undo_stockpile_used_this_turn"); it != result.data.end()) {
                entry.spell_stockpile_used_this_turn = it->second == "1";
            }
            if (const auto it = result.data.find("undo_stockpile_double_play_used"); it != result.data.end()) {
                entry.spell_stockpile_double_play_used = it->second == "1";
            }
        }
        if (energy_record) {
            entry.energy = *energy_record;
        }
        push_phase_undo(std::move(entry));
        return;
    }
    if (auto* ability = dynamic_cast<ActivateAbilityAction*>(&action)) {
        (void)ability;
        const auto bit = result.data.find("batch_item_id");
        if (bit == result.data.end()) {
            return;
        }
        PhaseUndoEntry entry;
        entry.player_id = player_id;
        entry.kind = PhaseUndoKind::QueuedBatchItem;
        entry.batch_item_id = bit->second;
        if (energy_record) {
            entry.energy = *energy_record;
        }
        push_phase_undo(std::move(entry));
        return;
    }
    if (auto* attack = dynamic_cast<AttackAction*>(&action)) {
        const auto phase = turn_manager.current_phase;
        if (phase != TurnPhase::AttackDeclaration && phase != TurnPhase::BonusAttackDeclaration) {
            return;
        }
        PhaseUndoEntry entry;
        entry.player_id = player_id;
        entry.kind = PhaseUndoKind::AttackDeclaration;
        entry.unit_entity_id = attack->attacker_id_for_undo();
        push_phase_undo(std::move(entry));
        return;
    }
    if (dynamic_cast<DefendAction*>(&action)) {
        const auto bit = result.data.find("undo_unit_id");
        if (bit == result.data.end()) {
            return;
        }
        PhaseUndoEntry entry;
        entry.player_id = player_id;
        entry.kind = PhaseUndoKind::Defend;
        entry.unit_entity_id = bit->second;
        if (auto ait = result.data.find("undo_attacks_remaining"); ait != result.data.end()) {
            entry.old_attacks_remaining = std::stoi(ait->second);
        }
        if (auto mit = result.data.find("undo_moves_remaining"); mit != result.data.end()) {
            entry.old_moves_remaining = std::stoi(mit->second);
        }
        if (auto sit = result.data.find("undo_standard_moves_remaining"); sit != result.data.end()) {
            entry.old_standard_moves_remaining = std::stoi(sit->second);
        }
        if (auto hit = result.data.find("undo_has_attacked"); hit != result.data.end()) {
            entry.old_has_attacked = hit->second == "1";
        }
        if (energy_record) {
            entry.energy = *energy_record;
        }
        push_phase_undo(std::move(entry));
        return;
    }
    if (dynamic_cast<DashAction*>(&action)) {
        const auto bit = result.data.find("undo_unit_id");
        if (bit == result.data.end()) {
            return;
        }
        PhaseUndoEntry entry;
        entry.player_id = player_id;
        entry.kind = PhaseUndoKind::Dash;
        entry.unit_entity_id = bit->second;
        if (auto ait = result.data.find("undo_attacks_remaining"); ait != result.data.end()) {
            entry.old_attacks_remaining = std::stoi(ait->second);
        }
        if (auto hit = result.data.find("undo_has_attacked"); hit != result.data.end()) {
            entry.old_has_attacked = hit->second == "1";
        }
        if (energy_record) {
            entry.energy = *energy_record;
        }
        push_phase_undo(std::move(entry));
        return;
    }
    if (dynamic_cast<RecoverAction*>(&action)) {
        const auto bit = result.data.find("undo_unit_id");
        if (bit == result.data.end()) {
            return;
        }
        PhaseUndoEntry entry;
        entry.player_id = player_id;
        entry.kind = PhaseUndoKind::Recover;
        entry.unit_entity_id = bit->second;
        if (auto ait = result.data.find("undo_attacks_remaining"); ait != result.data.end()) {
            entry.old_attacks_remaining = std::stoi(ait->second);
        }
        if (auto mit = result.data.find("undo_moves_remaining"); mit != result.data.end()) {
            entry.old_moves_remaining = std::stoi(mit->second);
        }
        if (auto sit = result.data.find("undo_standard_moves_remaining"); sit != result.data.end()) {
            entry.old_standard_moves_remaining = std::stoi(sit->second);
        }
        if (auto hit = result.data.find("undo_has_attacked"); hit != result.data.end()) {
            entry.old_has_attacked = hit->second == "1";
        }
        if (energy_record) {
            entry.energy = *energy_record;
        }
        push_phase_undo(std::move(entry));
        return;
    }
    if (auto* deploy = dynamic_cast<DeployAction*>(&action)) {
        // Only reached when match_settings.allow_deployment_undo is on (see dispatch site).
        const auto eit = result.data.find("undo_deploy_entity_id");
        if (eit == result.data.end()) {
            return;
        }
        PhaseUndoEntry entry;
        entry.player_id = player_id;
        entry.kind = PhaseUndoKind::Deploy;
        entry.unit_entity_id = eit->second;
        entry.spell_card = deploy->card_id_for_undo();
        entry.spell_from_reserves = deploy->played_from_reserves_for_undo();
        if (result.data.contains("undo_spell_had_stockpile")) {
            entry.spell_had_stockpile = true;
            if (const auto it = result.data.find("undo_stockpile_remaining"); it != result.data.end()) {
                entry.spell_stockpile_remaining = std::stoi(it->second);
            }
            if (const auto it = result.data.find("undo_stockpile_used_this_turn"); it != result.data.end()) {
                entry.spell_stockpile_used_this_turn = it->second == "1";
            }
            if (const auto it = result.data.find("undo_stockpile_double_play_used"); it != result.data.end()) {
                entry.spell_stockpile_double_play_used = it->second == "1";
            }
        }
        if (energy_record) {
            entry.energy = *energy_record;
        }
        push_phase_undo(std::move(entry));
    }
}

}  // namespace tactics
