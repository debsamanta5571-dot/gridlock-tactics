#include "tactics/combat/ability_resolve_viz.hpp"
#include "tactics/core/stack.hpp"

#include "tactics/actions/board_targeting.hpp"
#include "tactics/board/adjacency.hpp"
#include "tactics/combat/aoe_shapes.hpp"
#include "tactics/combat/directional_area.hpp"
#include "tactics/common/effect_keys.hpp"
#include "tactics/core/game_state.hpp"
#include "tactics/core/stack_targets.hpp"
#include "tactics/effects/effect_registry.hpp"
#include "tactics/effects/effect_traits.hpp"
#include "tactics/entities/entity.hpp"

#include <algorithm>
#include <unordered_set>

namespace tactics {
namespace {

int wave_depth_from_caster(const int ax, const int ay, const int cx, const int cy, const int dir_x, const int dir_y)
{
    const int dx = cx - ax;
    const int dy = cy - ay;
    if (dir_x != 0 && dir_y != 0) {
        return std::max(std::abs(dx), std::abs(dy));
    }
    if (dir_x > 0) {
        return dx;
    }
    if (dir_x < 0) {
        return -dx;
    }
    if (dir_y > 0) {
        return dy;
    }
    if (dir_y < 0) {
        return -dy;
    }
    return 0;
}

int chebyshev_ring_from_center(const int center_x, const int center_y, const int x, const int y)
{
    return std::max(std::abs(x - center_x), std::abs(y - center_y));
}

void sort_blast_cells_wave_order(std::vector<AbilityResolveVizBlastCell>& cells)
{
    std::stable_sort(cells.begin(), cells.end(),
        [](const AbilityResolveVizBlastCell& a, const AbilityResolveVizBlastCell& b) {
            if (a.wave_depth != b.wave_depth) {
                return a.wave_depth < b.wave_depth;
            }
            if (a.grid_y != b.grid_y) {
                return a.grid_y < b.grid_y;
            }
            return a.grid_x < b.grid_x;
        });
}

std::optional<std::pair<int, int>> entity_anchor(const Entity& ent)
{
    if (ent.position) {
        return *ent.position;
    }
    if (!ent.occupied_positions.empty()) {
        return ent.occupied_positions.front();
    }
    return std::nullopt;
}

std::vector<std::pair<int, int>> entity_footprint_cells(const Entity& entity)
{
    if (!entity.position) {
        return {};
    }
    const auto [ax, ay] = *entity.position;
    std::vector<std::pair<int, int>> cells;
    for (const auto& [dx, dy] : entity_shape_offsets(entity)) {
        cells.emplace_back(ax + dx, ay + dy);
    }
    return cells;
}

bool stack_item_has_board_cell_targets(const StackItem& item)
{
    return item.targets.count(effect_keys::kCellX) > 0 && item.targets.count(effect_keys::kCellY) > 0;
}

bool is_viz_eligible_stack_source(const std::string& source_type)
{
    return source_type == "ability" || source_type == "spell" || source_type == "focus_spell";
}

bool effect_skips_resolve_viz(const std::string& effect_key)
{
    static const std::unordered_set<std::string> kSkip = {
        "missile_storm",
        "cross_shot",
        "x_shot",
        "alternating_cross_x_shot",
        "draw_cards",
        "draw_spell_cards",
        "draw_cards_for_player",
        "draw_cards_owner",
        "scan",
        "gain_neutral",
        "gain_orange",
        "counter_spell",
        "copy_allied_spell",
        "mobilize",
        "second_wave",
        "mortar_barrage",
    };
    return kSkip.count(effect_key) > 0;
}

bool effect_uses_caster_surrounding_viz(const std::string& effect_key)
{
    static const std::unordered_set<std::string> kSurrounding = {
        "heal_surrounding_allies",
        "magus_charge_surrounding_burst",
        "grant_relentless_aura",
        "grant_movement_aura",
        "grant_damage_aura",
        "spawn_conscripts_adjacent",
        "spawn_flame_trooper_adjacent",
        "deal_damage_to_all_enemies_nearby",
    };
    return kSurrounding.count(effect_key) > 0;
}

bool effect_uses_caster_adjacent_viz(const std::string& effect_key)
{
    return effect_key == "thundering_vale";
}

bool effect_uses_caster_footprint_viz(const std::string& effect_key)
{
    if (effect_skips_resolve_viz(effect_key)) {
        return false;
    }
    if (effect_uses_caster_surrounding_viz(effect_key)) {
        return false;
    }
    const TargetDefinition target = target_definition_for_effect_key(effect_key);
    return target.domain == TargetDomain::None;
}

bool effect_uses_friendly_resolve_viz_color(const std::string& effect_key)
{
    static const std::unordered_set<std::string> kFriendlyBuffKeys = {
        "grant_keyword_mirror_passive",
    };
    if (kFriendlyBuffKeys.count(effect_key) > 0) {
        return true;
    }
    static const std::unordered_set<std::string> kHealKeys = {
        "heal",
        "heal_boosted",
        "heal_and_triage",
        "heal_self",
        "heal_surrounding_allies",
        "repair_structure_adjacent",
        "healing_flight",
        "cleanse",
        "triage",
    };
    if (kHealKeys.count(effect_key) > 0) {
        return true;
    }
    if (effect_key_deals_damage(effect_key)) {
        return false;
    }
    const BoardTargetKind kind = effect_board_target_kind(effect_key);
    if (kind == BoardTargetKind::Ally) {
        return true;
    }
    if (kind == BoardTargetKind::Own) {
        return true;
    }
    if (effect_uses_caster_surrounding_viz(effect_key)) {
        return effect_key != "deal_damage_to_all_enemies_nearby";
    }
    return false;
}

void init_preview_metadata(AbilityResolveVizPreview& preview, const StackItem& item)
{
    preview.source_entity_id = item.source_entity_id;
    preview.ability_name = item.source_name.empty() ? item.effect_key : item.source_name;
    preview.effect_key = item.effect_key;
    preview.friendly_effect = effect_uses_friendly_resolve_viz_color(item.effect_key);
}

AbilityResolveVizPreview build_directional_damage_preview(GameState& game, const StackItem& item, const Entity& source)
{
    AbilityResolveVizPreview preview;
    init_preview_metadata(preview, item);
    preview.directional_wave = true;

    const int max_range = payload_int(item, "max_range", 4);
    const bool allow_diagonals = payload_int(item, "cardinal_only", 0) == 0;
    const auto aim_vr = validate_directional_area_damage_ability_target(
        game, item.controller_id, source, item.targets, max_range, allow_diagonals);
    if (!aim_vr.ok) {
        return preview;
    }
    const int tx = item.targets.at(effect_keys::kCellX);
    const int ty = item.targets.at(effect_keys::kCellY);
    const std::string shape_key = payload_string(item, "shape").empty() ? "rectangle" : payload_string(item, "shape");
    const auto raw_cells = preview_directional_damage_blast_cells(game, source, tx, ty, shape_key, item.payload);
    const auto anchor = entity_anchor(source);
    if (!anchor || raw_cells.empty()) {
        return preview;
    }
    const auto [ax, ay] = *anchor;
    const auto [dir_x, dir_y] = snap_octilinear_direction(ax, ay, tx, ty);
    preview.blast_cells.reserve(raw_cells.size());
    for (const auto& [cx, cy] : raw_cells) {
        preview.blast_cells.push_back(
            {cx, cy, wave_depth_from_caster(ax, ay, cx, cy, dir_x, dir_y)});
    }
    sort_blast_cells_wave_order(preview.blast_cells);
    return preview;
}

AbilityResolveVizPreview build_directional_effect_preview(GameState& game, const StackItem& item, const Entity& source)
{
    AbilityResolveVizPreview preview;
    init_preview_metadata(preview, item);
    preview.directional_wave = true;

    const auto cx_it = item.targets.find(effect_keys::kCellX);
    const auto cy_it = item.targets.find(effect_keys::kCellY);
    if (cx_it == item.targets.end() || cy_it == item.targets.end()) {
        return preview;
    }
    const int tx = cx_it->second;
    const int ty = cy_it->second;
    const auto raw_cells = preview_directional_effect_blast_cells(
        game, source, tx, ty, item.effect_key, item.payload, item.string_payload);
    const auto anchor = entity_anchor(source);
    if (!anchor || raw_cells.empty()) {
        return preview;
    }
    const auto [ax, ay] = *anchor;
    const auto [dir_x, dir_y] = snap_octilinear_direction(ax, ay, tx, ty);
    preview.blast_cells.reserve(raw_cells.size());
    for (const auto& [cx, cy] : raw_cells) {
        preview.blast_cells.push_back(
            {cx, cy, wave_depth_from_caster(ax, ay, cx, cy, dir_x, dir_y)});
    }
    sort_blast_cells_wave_order(preview.blast_cells);
    return preview;
}

AbilityResolveVizPreview build_piercing_shot_preview(GameState& game, const StackItem& item, const Entity& source)
{
    AbilityResolveVizPreview preview;
    init_preview_metadata(preview, item);
    preview.directional_wave = true;

    const int max_range = payload_int(item, "max_range", 8);
    const std::map<std::string, int> targets = {{effect_keys::kCellX, item.targets.at(effect_keys::kCellX)},
        {effect_keys::kCellY, item.targets.at(effect_keys::kCellY)}};
    const auto aim_vr = validate_directional_area_damage_ability_target(game, item.controller_id, source, targets, max_range, true);
    if (!aim_vr.ok) {
        return preview;
    }
    const int tx = item.targets.at(effect_keys::kCellX);
    const int ty = item.targets.at(effect_keys::kCellY);
    const auto anchor = entity_anchor(source);
    if (!anchor) {
        return preview;
    }
    const auto [ax, ay] = *anchor;
    const auto [dir_x, dir_y] = snap_octilinear_direction(ax, ay, tx, ty);
    for (int dist = 1; dist <= max_range; ++dist) {
        const int x = ax + dist * dir_x;
        const int y = ay + dist * dir_y;
        if (!game.board.get_square(x, y)) {
            break;
        }
        preview.blast_cells.push_back({x, y, dist});
    }
    return preview;
}

AbilityResolveVizPreview build_ring_ordered_cell_preview(
    const StackItem& item, const int center_x, const int center_y, const std::vector<std::pair<int, int>>& raw_cells)
{
    AbilityResolveVizPreview preview;
    init_preview_metadata(preview, item);
    preview.blast_cells.reserve(raw_cells.size());
    for (const auto& [x, y] : raw_cells) {
        preview.blast_cells.push_back({x, y, chebyshev_ring_from_center(center_x, center_y, x, y)});
    }
    sort_blast_cells_wave_order(preview.blast_cells);
    return preview;
}

AbilityResolveVizPreview build_lobbed_aoe_center_preview(const StackItem& item)
{
    const auto cx_it = item.targets.find(effect_keys::kCellX);
    const auto cy_it = item.targets.find(effect_keys::kCellY);
    if (cx_it == item.targets.end() || cy_it == item.targets.end()) {
        return {};
    }
    const int cx = cx_it->second;
    const int cy = cy_it->second;
    return build_ring_ordered_cell_preview(item, cx, cy, preview_lobbed_aoe_blast_cells(cx, cy, item.payload));
}

AbilityResolveVizPreview build_named_shape_cell_preview(
    GameState& game, const StackItem& item, const Entity* source, const std::string& effect_key)
{
    if (!stack_item_has_board_cell_targets(item)) {
        return {};
    }
    const int cx = item.targets.at(effect_keys::kCellX);
    const int cy = item.targets.at(effect_keys::kCellY);
    const auto raw_cells = preview_effect_aoe_blast_cells(
        game, source, cx, cy, effect_key, item.payload, item.string_payload);
    if (raw_cells.empty()) {
        return {};
    }
    return build_ring_ordered_cell_preview(item, cx, cy, raw_cells);
}

AbilityResolveVizPreview build_high_explosive_preview(GameState& game, const StackItem& item)
{
    AbilityResolveVizPreview preview;
    init_preview_metadata(preview, item);

    const auto resolved = resolve_stack_board_target(game, item, item.effect_key);
    if (!resolved.status.ok || !resolved.target) {
        return preview;
    }
    int gx = 0;
    int gy = 0;
    if (resolved.target->position) {
        gx = resolved.target->position->first;
        gy = resolved.target->position->second;
    } else if (!resolved.target->occupied_positions.empty()) {
        gx = resolved.target->occupied_positions.front().first;
        gy = resolved.target->occupied_positions.front().second;
    }
    const auto raw_cells = preview_lobbed_aoe_blast_cells(gx, gy, item.payload);
    preview.blast_cells.reserve(raw_cells.size());
    for (const auto& [x, y] : raw_cells) {
        preview.blast_cells.push_back({x, y, chebyshev_ring_from_center(gx, gy, x, y)});
    }
    sort_blast_cells_wave_order(preview.blast_cells);
    return preview;
}

AbilityResolveVizPreview build_board_target_preview(GameState& game, const StackItem& item)
{
    AbilityResolveVizPreview preview;
    init_preview_metadata(preview, item);

    const auto resolved = resolve_stack_board_target(game, item, item.effect_key);
    if (!resolved.status.ok || !resolved.target) {
        return preview;
    }
    int gx = 0;
    int gy = 0;
    if (resolved.target->position) {
        gx = resolved.target->position->first;
        gy = resolved.target->position->second;
    } else if (!resolved.target->occupied_positions.empty()) {
        gx = resolved.target->occupied_positions.front().first;
        gy = resolved.target->occupied_positions.front().second;
    }
    preview.blast_cells.push_back({gx, gy, 0});
    return preview;
}

AbilityResolveVizPreview build_cell_target_aoe_preview(GameState& game, const StackItem& item, const Entity* source)
{
    AbilityResolveVizPreview preview;
    init_preview_metadata(preview, item);

    const int cx = item.targets.at(effect_keys::kCellX);
    const int cy = item.targets.at(effect_keys::kCellY);
    const auto raw_cells = preview_effect_aoe_blast_cells(
        game, source, cx, cy, item.effect_key, item.payload, item.string_payload);
    if (raw_cells.empty()) {
        return preview;
    }
    if (source && effect_key_uses_directional_aim(item.effect_key)) {
        preview.directional_wave = true;
        const auto anchor = entity_anchor(*source);
        if (!anchor) {
            return preview;
        }
        const auto [ax, ay] = *anchor;
        const auto [dir_x, dir_y] = snap_octilinear_direction(ax, ay, cx, cy);
        preview.blast_cells.reserve(raw_cells.size());
        for (const auto& [x, y] : raw_cells) {
            preview.blast_cells.push_back({x, y, wave_depth_from_caster(ax, ay, x, y, dir_x, dir_y)});
        }
        sort_blast_cells_wave_order(preview.blast_cells);
        return preview;
    }
    preview.blast_cells.reserve(raw_cells.size());
    for (const auto& [x, y] : raw_cells) {
        preview.blast_cells.push_back({x, y, chebyshev_ring_from_center(cx, cy, x, y)});
    }
    sort_blast_cells_wave_order(preview.blast_cells);
    return preview;
}

AbilityResolveVizPreview build_caster_surrounding_preview(const StackItem& item, const Entity& source)
{
    AbilityResolveVizPreview preview;
    init_preview_metadata(preview, item);

    const auto raw_cells = entity_surrounding_cells(source);
    preview.blast_cells.reserve(raw_cells.size());
    for (const auto& [x, y] : raw_cells) {
        preview.blast_cells.push_back({x, y, 0});
    }
    return preview;
}

AbilityResolveVizPreview build_caster_adjacent_preview(const StackItem& item, const Entity& source)
{
    AbilityResolveVizPreview preview;
    init_preview_metadata(preview, item);

    const auto raw_cells = entity_adjacent_cells(source);
    preview.blast_cells.reserve(raw_cells.size());
    for (const auto& [x, y] : raw_cells) {
        preview.blast_cells.push_back({x, y, 0});
    }
    return preview;
}

AbilityResolveVizPreview build_caster_footprint_preview(const StackItem& item, const Entity& source)
{
    AbilityResolveVizPreview preview;
    init_preview_metadata(preview, item);

    const auto raw_cells = entity_footprint_cells(source);
    preview.blast_cells.reserve(raw_cells.size());
    for (const auto& [x, y] : raw_cells) {
        preview.blast_cells.push_back({x, y, 0});
    }
    return preview;
}

const Entity* find_source_entity(const GameState& game, const StackItem& item)
{
    if (item.source_entity_id.empty()) {
        return nullptr;
    }
    const auto src_it = game.board.all_entities_map.find(item.source_entity_id);
    if (src_it == game.board.all_entities_map.end() || !src_it->second) {
        return nullptr;
    }
    return src_it->second.get();
}

}  // namespace

bool should_pause_for_ability_resolve_viz(const GameState& game, const StackItem& item)
{
    if (!game.combat_visualization_enabled() || !is_viz_eligible_stack_source(item.source_type)) {
        return false;
    }
    if (effect_skips_resolve_viz(item.effect_key)) {
        return false;
    }
    const TargetDefinition target = target_definition_for_effect_key(item.effect_key);
    if (target.domain == TargetDomain::StackItem || target.domain == TargetDomain::PlayerSeat) {
        return false;
    }
    if (effect_key_uses_directional_aim(item.effect_key)) {
        return true;
    }
    if (effect_key_uses_lobbed_aoe_center(item.effect_key)) {
        return true;
    }
    if (item.effect_key == "high_explosive_round" || item.effect_key == "aoe_damage_square") {
        return true;
    }
    if (effect_requires_board_target(item.effect_key)) {
        return true;
    }
    if (stack_item_has_board_cell_targets(item) && effect_supports_aoe_blast_preview(item.effect_key)) {
        return true;
    }
    if (!item.source_entity_id.empty() && effect_uses_caster_surrounding_viz(item.effect_key)) {
        return true;
    }
    if (!item.source_entity_id.empty() && effect_uses_caster_adjacent_viz(item.effect_key)) {
        return true;
    }
    if (!item.source_entity_id.empty() && effect_uses_caster_footprint_viz(item.effect_key)) {
        return true;
    }
    const EffectTraits traits = effect_traits_for_key(item.effect_key);
    if (traits.deals_damage && stack_item_has_board_cell_targets(item)) {
        return true;
    }
    static const std::unordered_set<std::string> kHealKeys = {
        "heal",
        "heal_boosted",
        "heal_and_triage",
        "repair_structure_adjacent",
    };
    return kHealKeys.count(item.effect_key) > 0;
}

AbilityResolveVizPreview build_ability_resolve_viz_preview(GameState& game, const StackItem& item)
{
    const Entity* source = find_source_entity(game, item);

    if (item.effect_key == "aoe_damage_square") {
        return build_lobbed_aoe_center_preview(item);
    }
    if (item.effect_key == "high_explosive_round") {
        return build_high_explosive_preview(game, item);
    }
    static const std::unordered_set<std::string> kNamedShapeCellEffects = {
        "scorching_sphere",
        "gas_grenade",
        "gas_strike",
        "shocking_stimulus_aoe",
    };
    if (kNamedShapeCellEffects.count(item.effect_key) > 0) {
        const auto shape_preview = build_named_shape_cell_preview(game, item, source, item.effect_key);
        if (!shape_preview.blast_cells.empty()) {
            return shape_preview;
        }
    }
    if (source) {
        if (item.effect_key == "directional_damage") {
            return build_directional_damage_preview(game, item, *source);
        }
        if (item.effect_key == "piercing_shot") {
            return build_piercing_shot_preview(game, item, *source);
        }
        if (effect_key_uses_directional_aim(item.effect_key)) {
            return build_directional_effect_preview(game, item, *source);
        }
    }
    if (effect_key_uses_lobbed_aoe_center(item.effect_key) && stack_item_has_board_cell_targets(item)) {
        return build_lobbed_aoe_center_preview(item);
    }
    if (stack_item_has_board_cell_targets(item)) {
        const auto aoe_preview = build_cell_target_aoe_preview(game, item, source);
        if (!aoe_preview.blast_cells.empty()) {
            return aoe_preview;
        }
    }
    if (effect_requires_board_target(item.effect_key)) {
        return build_board_target_preview(game, item);
    }
    if (source && effect_uses_caster_surrounding_viz(item.effect_key)) {
        return build_caster_surrounding_preview(item, *source);
    }
    if (source && effect_uses_caster_adjacent_viz(item.effect_key)) {
        return build_caster_adjacent_preview(item, *source);
    }
    if (source && effect_uses_caster_footprint_viz(item.effect_key)) {
        return build_caster_footprint_preview(item, *source);
    }
    if (source && effect_key_uses_directional_aim(item.effect_key)) {
        return build_directional_effect_preview(game, item, *source);
    }
    return {};
}

void emit_ability_resolve_result_label_popups_if_needed(GameState& game)
{
    if (!game.combat_visualization_enabled() || !game.should_emit_ability_damage_popup()) {
        return;
    }
    const StackItem* item = game.resolving_stack_item_ptr();
    if (!item) {
        return;
    }
    const AbilityResolveVizPreview preview = build_ability_resolve_viz_preview(game, *item);
    if (preview.blast_cells.empty()) {
        return;
    }
    const std::string label = preview.ability_name.empty() ? item->effect_key : preview.ability_name;
    if (label.empty()) {
        return;
    }
    game.backfill_pending_ability_damage_popup_labels(label);
    std::unordered_set<std::string> seen_entities;
    for (const AbilityResolveVizBlastCell& cell : preview.blast_cells) {
        const auto entity = game.board.entity_at(cell.grid_x, cell.grid_y);
        if (!entity || entity->current_health <= 0) {
            continue;
        }
        if (!seen_entities.insert(entity->entity_id).second) {
            continue;
        }
        if (game.has_ability_damage_popup_for_entity(entity->entity_id)) {
            continue;
        }
        AbilityResolveVizHit hit;
        hit.entity_id = entity->entity_id;
        hit.grid_x = cell.grid_x;
        hit.grid_y = cell.grid_y;
        hit.amount = 0;
        hit.is_heal = preview.friendly_effect;
        hit.event_label = label;
        game.enqueue_ability_damage_popup_event(hit);
    }
}

}  // namespace tactics