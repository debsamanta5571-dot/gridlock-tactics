#include "tactics/actions/board_targeting.hpp"

#include "tactics/cards/focus_spell.hpp"

#include "tactics/actions/actions.hpp"
#include "tactics/board/push_displacement.hpp"
#include "tactics/cards/ability_catalog.hpp"
#include "tactics/combat/aoe_shapes.hpp"
#include "tactics/combat/combat_resolver.hpp"
#include "tactics/combat/directional_area.hpp"
#include "tactics/combat/taunt.hpp"
#include "tactics/core/board_target_policy.hpp"
#include "tactics/core/game_state.hpp"
#include "tactics/cards/card_catalog.hpp"
#include "tactics/cards/card_runtime.hpp"
#include "tactics/cards/unit_types.hpp"
#include "tactics/effects/effect_traits.hpp"
#include "tactics/entities/entity.hpp"

#include <set>
#include <algorithm>

namespace tactics {
namespace {

void append_footprint_cells(const Entity& ent, std::vector<std::pair<int, int>>& cells)
{
    cells = ent.occupied_positions;
    if (cells.empty() && ent.position) {
        const auto [ax, ay] = *ent.position;
        for (const auto& [dx, dy] : entity_shape_offsets(ent)) {
            cells.push_back({ax + dx, ay + dy});
        }
    }
}

void append_unique(std::vector<std::pair<int, int>>& out, const std::pair<int, int>& cell)
{
    if (std::find(out.begin(), out.end(), cell) == out.end()) {
        out.push_back(cell);
    }
}

bool ability_probe_valid(GameState& game, const std::shared_ptr<Unit>& actor, const int player_id, const std::string& ability_key,
                         const int wx, const int wy)
{
    ActivateAbilityAction probe(actor, player_id, ability_key, {{effect_keys::kCellX, wx}, {effect_keys::kCellY, wy}});
    const auto cost = probe.get_cost(game);
    if (!cost.empty() && !game.turn_manager.can_afford(game, player_id, cost)) {
        return false;
    }
    return probe.validate(game).ok;
}

bool push_direction_entity_target_valid(GameState& game, const int player_id, const Entity& ent,
    const std::string& effect_key, const BoardTargetKind board_target_kind,
    const std::vector<std::string>& require_target_unit_types)
{
    if (!board_target_allows(game, board_target_kind, player_id, ent)) {
        return false;
    }
    if (!board_target_entity_allowed_for_effect(ent, board_target_kind, effect_key)) {
        return false;
    }
    if (!entity_satisfies_unit_type_filter(ent, require_target_unit_types)) {
        return false;
    }
    if (!taunt_allows_board_target(game, nullptr, player_id, ent)) {
        return false;
    }
    if (enemy_direct_target_blocked_by_stealth(game, player_id, ent)) {
        return false;
    }
    return true;
}

const Entity* resolve_validation_actor(const GameState& game, const Entity& caster)
{
    if (const auto caster_it = game.board.all_entities_map.find(caster.entity_id); caster_it != game.board.all_entities_map.end()) {
        if (const auto caster_unit = std::dynamic_pointer_cast<Unit>(caster_it->second)) {
            if (const std::shared_ptr<Unit> pose = game.unit_at_validation_pose(caster_unit)) {
                return pose.get();
            }
        }
    }
    return &caster;
}

bool unit_can_cast_focus_spell(const GameState& game, const int player_id, const Unit& caster, const std::string& effect_key,
    const int focus_range, const BoardTargetKind board_target_kind, const std::map<std::string, int>& payload,
    const std::map<std::string, std::string>& string_payload)
{
    if (entity_is_stunned(caster) || entity_is_jammed(caster)) {
        return false;
    }
    const Entity* actor = resolve_validation_actor(game, caster);
    if (!actor) {
        return false;
    }
    if (effect_uses_directional_aim(effect_key)) {
        if (effect_key_is_movement_landing(effect_key)) {
            int max_range = 4;
            if (const auto it = payload.find("max_range"); it != payload.end()) {
                max_range = it->second;
            }
            const int min_range = [&payload]() {
                const auto it = payload.find("min_range");
                return it != payload.end() ? it->second : 1;
            }();
            const bool allow_diagonals = [&payload]() {
                const auto it = payload.find("cardinal_only");
                return !(it != payload.end() && it->second != 0);
            }();
            return !directional_movement_landing_cells(game, *actor, min_range, max_range, allow_diagonals).empty();
        }
        return !directional_aim_indicator_cells(game, *actor, payload).empty();
    }
    if (!effect_requires_entity_at_target_cell(effect_key) && !effect_key_targets_empty_cell(effect_key)) {
        int range_max = focus_range > 0 ? focus_range : 4;
        if (const auto it = payload.find("max_range"); it != payload.end()) {
            range_max = it->second;
        }
        return !gather_lobbed_aoe_center_cells(game, *actor, range_max, payload).other_cells.empty();
    }
    BoardTargetHighlightCells probe_out;
    const Entity* taunt_actor = actor;
    game.board.for_each_entity([&](const std::shared_ptr<Entity>& ent_ptr) {
        if (!ent_ptr || !board_target_allows(game, board_target_kind, player_id, *ent_ptr)) {
            return;
        }
        if (!board_target_entity_allowed_for_effect(*ent_ptr, board_target_kind, effect_key)) {
            return;
        }
        if (!taunt_allows_board_target(game, taunt_actor, player_id, *ent_ptr)) {
            return;
        }
        if (entity_is_stunned(*ent_ptr)) {
            return;
        }
        std::vector<std::pair<int, int>> target_cells;
        append_footprint_cells(*ent_ptr, target_cells);
        for (const auto& [wx, wy] : target_cells) {
            if (focus_range > 0) {
                if (min_chebyshev_entity_to_cell(*actor, wx, wy) > focus_range) {
                    continue;
                }
                if (!entity_has_line_of_sight_to_cell(game, *actor, {wx, wy})) {
                    continue;
                }
            }
            const bool hostile = ent_ptr->owner.has_value() && teams_hostile(game, player_id, *ent_ptr->owner);
            auto& bucket = hostile ? probe_out.enemy_cells : probe_out.other_cells;
            append_unique(bucket, {wx, wy});
        }
    });
    return !probe_out.enemy_cells.empty() || !probe_out.other_cells.empty();
}

}  // namespace

bool spell_probe_valid(GameState& game, const CardInstanceId card_id, const int player_id,
    const std::shared_ptr<Entity>& focus_caster, const CardPlayZone zone, const int wx, const int wy)
{
    if (!card_id.is_valid()) {
        return false;
    }
    CastSpellAction probe(card_id, player_id, {{effect_keys::kCellX, wx}, {effect_keys::kCellY, wy}}, focus_caster, zone);
    const auto cost = probe.get_cost(game);
    if (!cost.empty() && !game.turn_manager.can_afford(game, player_id, cost)) {
        return false;
    }
    return probe.validate(game).ok;
}

BoardTargetHighlightCells gather_directional_effect_board_target_cells(
    GameState& game, const std::shared_ptr<Unit>& actor, const std::string& effect_key,
    const std::map<std::string, int>& payload, const std::map<std::string, std::string>& string_payload,
    DirectionalTargetCellProbe probe)
{
    BoardTargetHighlightCells out;
    if (!actor || !effect_uses_directional_aim(effect_key)) {
        return out;
    }
    static_cast<void>(string_payload);

    const std::shared_ptr<Unit> pose = game.unit_at_validation_pose(actor);
    const std::shared_ptr<Unit> ability_actor = pose ? pose : actor;

    std::vector<std::pair<int, int>> candidates;
    if (effect_key_is_movement_landing(effect_key)) {
        int max_range = 4;
        if (const auto it = payload.find("max_range"); it != payload.end()) {
            max_range = it->second;
        }
        const bool allow_diagonals = [&payload]() {
            const auto it = payload.find("cardinal_only");
            return !(it != payload.end() && it->second != 0);
        }();
        const int min_range = [&payload]() {
            const auto it = payload.find("min_range");
            return it != payload.end() ? it->second : 1;
        }();
        candidates = directional_movement_landing_cells(game, *ability_actor, min_range, max_range, allow_diagonals);
    } else {
        candidates = directional_aim_indicator_cells(game, *ability_actor, payload);
    }

    for (const auto& [wx, wy] : candidates) {
        if (probe && !probe(wx, wy)) {
            continue;
        }
        append_unique(out.other_cells, {wx, wy});
    }
    return out;
}

BoardTargetHighlightCells gather_ability_board_target_cells(GameState& game, const std::shared_ptr<Unit>& actor, const int player_id,
                                                            const std::string& ability_key_utf8)
{
    BoardTargetHighlightCells out;
    if (!actor || ability_key_utf8.empty()) {
        return out;
    }

    AbilitySpec ability;
    bool found_on_unit = false;
    for (const AbilitySpec& on_unit : actor->activated_abilities) {
        if (on_unit.key == ability_key_utf8) {
            ability = on_unit;
            found_on_unit = true;
            break;
        }
    }
    if (!found_on_unit && !try_get_ability_from_catalog(ability_key_utf8, ability)) {
        return out;
    }

    const std::shared_ptr<Unit> pose = game.unit_at_validation_pose(actor);
    const std::shared_ptr<Unit> ability_actor = pose ? pose : actor;

    if (effect_uses_directional_aim(ability.effect_key)) {
        const auto probe = [&](const int wx, const int wy) {
            return ability_probe_valid(game, ability_actor, player_id, ability_key_utf8, wx, wy);
        };
        return gather_directional_effect_board_target_cells(
            game, ability_actor, ability.effect_key, ability.effect_payload, ability.effect_string_payload, probe);
    }

    if (!effect_requires_entity_at_target_cell(ability.effect_key) && !effect_key_targets_empty_cell(ability.effect_key)) {
        int range_max = ability.range_max > 0 ? ability.range_max : 4;
        if (const auto it = ability.effect_payload.find("max_range"); it != ability.effect_payload.end()) {
            range_max = it->second;
        }
        const auto probe = [&](const int wx, const int wy) {
            return ability_probe_valid(game, ability_actor, player_id, ability_key_utf8, wx, wy);
        };
        const auto gathered =
            gather_lobbed_aoe_center_cells(game, *ability_actor, range_max, ability.effect_payload, probe);
        for (const auto& cell : gathered.other_cells) {
            append_unique(out.other_cells, cell);
        }
        return out;
    }

    if (effect_key_targets_empty_cell(ability.effect_key)) {
        if (!ability_actor->position) {
            return out;
        }
        const int range_max = ability.range_max > 0 ? ability.range_max : 4;
        const auto [ax, ay] = *ability_actor->position;
        for (int dy = -range_max; dy <= range_max; ++dy) {
            for (int dx = -range_max; dx <= range_max; ++dx) {
                const int cx = ax + dx;
                const int cy = ay + dy;
                if (!game.board.get_square(cx, cy)) {
                    continue;
                }
                if (game.board.entity_at(cx, cy)) {
                    continue;
                }
                if (min_chebyshev_entity_to_cell(*ability_actor, cx, cy) > range_max) {
                    continue;
                }
                if (ability.effect_key == "spawn_shock_wire_adjacent"
                        && min_chebyshev_entity_to_cell(*ability_actor, cx, cy) < 1) {
                    continue;
                }
                if (!ability_probe_valid(game, ability_actor, player_id, ability_key_utf8, cx, cy)) {
                    continue;
                }
                append_unique(out.other_cells, {cx, cy});
            }
        }
        return out;
    }

    if (!ability_requires_board_target(ability)) {
        return out;
    }

    const BoardTargetKind kind = ability_board_target_kind(ability);
    const Entity* taunt_actor = ability_actor.get();
    game.board.for_each_entity([&](const std::shared_ptr<Entity>& ent_ptr) {
        if (!ent_ptr || !board_target_allows(game, kind, player_id, *ent_ptr)) {
            return;
        }
        if (!board_target_entity_allowed_for_effect(*ent_ptr, kind, ability.effect_key)) {
            return;
        }
        if (!taunt_allows_board_target(game, taunt_actor, player_id, *ent_ptr)) {
            return;
        }
        std::vector<std::pair<int, int>> target_cells;
        append_footprint_cells(*ent_ptr, target_cells);
        const bool hostile = ent_ptr->owner.has_value() && teams_hostile(game, player_id, *ent_ptr->owner);
        auto& bucket = hostile ? out.enemy_cells : out.other_cells;
        for (const auto& [wx, wy] : target_cells) {
            if (!ability_probe_valid(game, ability_actor, player_id, ability_key_utf8, wx, wy)) {
                continue;
            }
            append_unique(bucket, {wx, wy});
        }
    });
    return out;
}

BoardTargetHighlightCells gather_spell_board_target_cells(
    GameState& game, const std::shared_ptr<Unit>& focus_caster, const int player_id, const std::string& effect_key,
    const int focus_range, const BoardTargetKind board_target_kind, const std::map<std::string, int>& payload,
    const std::map<std::string, std::string>& string_payload, LobbedAoeCenterCellProbe probe)
{
    BoardTargetHighlightCells out;
    if (effect_key.empty()) {
        return out;
    }

    std::shared_ptr<Unit> spell_actor;
    const Entity* range_los_actor = nullptr;
    if (focus_caster) {
        spell_actor = game.unit_at_validation_pose(focus_caster);
        if (!spell_actor) {
            spell_actor = focus_caster;
        }
        range_los_actor = spell_actor.get();
    }

    if (effect_uses_directional_aim(effect_key)) {
        if (!spell_actor) {
            return out;
        }
        DirectionalTargetCellProbe dir_probe;
        if (probe) {
            dir_probe = [probe](const int wx, const int wy) { return probe(wx, wy); };
        }
        return gather_directional_effect_board_target_cells(game, spell_actor, effect_key, payload, string_payload, dir_probe);
    }

    if (!effect_requires_entity_at_target_cell(effect_key) && !effect_key_targets_empty_cell(effect_key)) {
        int range_max = focus_range > 0 ? focus_range : 4;
        if (const auto it = payload.find("max_range"); it != payload.end()) {
            range_max = it->second;
        }
        if (range_los_actor) {
            return gather_lobbed_aoe_center_cells(game, *range_los_actor, range_max, payload, probe);
        }
        const BoardCellBounds bounds = game.board.cell_bounds();
        if (bounds.empty()) {
            return out;
        }
        for (int wy = bounds.min_y; wy <= bounds.max_y; ++wy) {
            for (int wx = bounds.min_x; wx <= bounds.max_x; ++wx) {
                if (!game.board.get_square(wx, wy)) {
                    continue;
                }
                if (probe && !probe(wx, wy)) {
                    continue;
                }
                append_unique(out.other_cells, {wx, wy});
            }
        }
        return out;
    }

    if (!effect_requires_board_target(effect_key)) {
        return out;
    }

    int max_deploy_cost = -1;
    if (const auto mit = payload.find("max_deploy_cost"); mit != payload.end()) {
        max_deploy_cost = mit->second;
    }
    // A focus spell/ability is cast THROUGH a unit; that unit (the caster) is not a valid target of
    // its own effect unless the `may_target_self` flag is set (the `{RANGE_SELF}`/`{ADJACENT_SELF}` tag).
    const bool may_target_self = [&]() {
        const auto it = payload.find("may_target_self");
        return it != payload.end() && it->second != 0;
    }();
    const Entity* taunt_actor = range_los_actor;
    game.board.for_each_entity([&](const std::shared_ptr<Entity>& ent_ptr) {
        if (!ent_ptr || !board_target_allows(game, board_target_kind, player_id, *ent_ptr)) {
            return;
        }
        if (!may_target_self && range_los_actor && ent_ptr->entity_id == range_los_actor->entity_id) {
            return;  // exclude the casting unit itself
        }
        if (!board_target_entity_allowed_for_effect(*ent_ptr, board_target_kind, effect_key)) {
            return;
        }
        if (max_deploy_cost >= 0 && !entity_satisfies_max_deploy_cost(*ent_ptr, max_deploy_cost)) {
            return;
        }
        if (!taunt_allows_board_target(game, taunt_actor, player_id, *ent_ptr)) {
            return;
        }
        std::vector<std::pair<int, int>> target_cells;
        append_footprint_cells(*ent_ptr, target_cells);
        const bool hostile = ent_ptr->owner.has_value() && teams_hostile(game, player_id, *ent_ptr->owner);
        auto& bucket = hostile ? out.enemy_cells : out.other_cells;
        for (const auto& [wx, wy] : target_cells) {
            if (range_los_actor && focus_range > 0) {
                if (min_chebyshev_entity_to_cell(*range_los_actor, wx, wy) > focus_range) {
                    continue;
                }
                if (!entity_has_line_of_sight_to_cell(game, *range_los_actor, {wx, wy})) {
                    continue;
                }
            }
            if (probe && !probe(wx, wy)) {
                continue;
            }
            append_unique(bucket, {wx, wy});
        }
    });
    return out;
}

BoardTargetHighlightCells gather_forced_damage_spell_focus_caster_cells(GameState& game, const int player_id)
{
    BoardTargetHighlightCells out;
    game.board.for_each_entity([&](const std::shared_ptr<Entity>& ent_ptr) {
        if (!ent_ptr || !ent_ptr->owner || *ent_ptr->owner != player_id || ent_ptr->current_health <= 0) {
            return;
        }
        if (!entity_valid_focus_spell_caster(*ent_ptr)) {
            return;
        }
        if (!entity_forced_damage_spell_focus_range(*ent_ptr)) {
            return;
        }
        if (entity_is_stunned(*ent_ptr) || entity_is_jammed(*ent_ptr)) {
            return;
        }
        std::vector<std::pair<int, int>> cells;
        append_footprint_cells(*ent_ptr, cells);
        for (const auto& cell : cells) {
            append_unique(out.other_cells, cell);
        }
    });
    return out;
}

BoardTargetHighlightCells gather_focus_caster_highlight_cells(
    GameState& game, const int player_id, const std::string& effect_key, const int focus_range,
    const BoardTargetKind board_target_kind, const std::map<std::string, int>& payload,
    const std::map<std::string, std::string>& string_payload)
{
    BoardTargetHighlightCells out;
    game.board.for_each_entity([&](const std::shared_ptr<Entity>& ent_ptr) {
        const auto caster_unit = std::dynamic_pointer_cast<Unit>(ent_ptr);
        if (!caster_unit || !caster_unit->owner || *caster_unit->owner != player_id) {
            return;
        }
        if (!entity_valid_focus_spell_caster(*caster_unit)) {
            return;
        }
        if (!unit_can_cast_focus_spell(game, player_id, *caster_unit, effect_key, focus_range, board_target_kind, payload,
                string_payload)) {
            return;
        }
        std::vector<std::pair<int, int>> cells;
        append_footprint_cells(*caster_unit, cells);
        for (const auto& cell : cells) {
            append_unique(out.other_cells, cell);
        }
    });
    return out;
}

std::vector<std::pair<int, int>> preview_effect_aoe_blast_cells(
    const GameState& game, const Entity* actor, const int aim_x, const int aim_y, const std::string& effect_key,
    const std::map<std::string, int>& payload, const std::map<std::string, std::string>& string_payload)
{
    if (effect_key == "scorching_sphere") {
        static_cast<void>(game);
        static_cast<void>(actor);
        static_cast<void>(payload);
        static_cast<void>(string_payload);
        return scorching_sphere_cells(aim_x, aim_y);
    }
    if (effect_key == "gas_strike") {
        static_cast<void>(game);
        static_cast<void>(actor);
        static_cast<void>(payload);
        static_cast<void>(string_payload);
        return gas_strike_cross_cells(aim_x, aim_y);
    }
    if (effect_key == "gas_grenade") {
        static_cast<void>(game);
        static_cast<void>(payload);
        static_cast<void>(string_payload);
        if (!actor) {
            return {};
        }
        return cleave_pattern_cells(*actor, aim_x, aim_y);
    }
    if (effect_key == "shocking_stimulus_aoe") {
        static_cast<void>(game);
        static_cast<void>(actor);
        static_cast<void>(payload);
        static_cast<void>(string_payload);
        return shocking_stimulus_aoe_cells(aim_x, aim_y);
    }
    if (!effect_supports_aoe_blast_preview(effect_key)) {
        return {};
    }
    if (effect_key_uses_push_direction_aim(effect_key)) {
        static_cast<void>(game);
        static_cast<void>(string_payload);
        return {};
    }
    if (effect_uses_directional_aim(effect_key)) {
        if (!actor) {
            return {};
        }
        return preview_directional_effect_blast_cells(game, *actor, aim_x, aim_y, effect_key, payload, string_payload);
    }
    // Rectangular W×H area effects (payload carries width/height), e.g. randomize_stats_area (The
    // Starforged Aberration). Match the resolver's own anchoring EXACTLY - cells
    // (aim + dx - W/2, aim + dy - H/2) for dx∈[0,W), dy∈[0,H) - so the highlight equals the cells that
    // are actually affected, rather than the radius blob the lobbed fallback would draw.
    if (const auto wit = payload.find("width"), hit = payload.find("height");
        wit != payload.end() && hit != payload.end()) {
        const int W = std::max(1, wit->second);
        const int H = std::max(1, hit->second);
        std::vector<std::pair<int, int>> out;
        out.reserve(static_cast<std::size_t>(W * H));
        for (int dy = 0; dy < H; ++dy) {
            for (int dx = 0; dx < W; ++dx) {
                out.push_back({aim_x + dx - W / 2, aim_y + dy - H / 2});
            }
        }
        return out;
    }
    if (effect_key_uses_lobbed_aoe_center(effect_key) || !effect_requires_entity_at_target_cell(effect_key)) {
        return preview_lobbed_aoe_blast_cells(aim_x, aim_y, payload);
    }
    return {};
}

std::vector<std::pair<int, int>> preview_lobbed_aoe_blast_cells(
    const int center_x, const int center_y, const std::map<std::string, int>& payload)
{
    int radius = 1;
    if (const auto it = payload.find("radius"); it != payload.end()) {
        radius = std::max(0, it->second);
    }
    std::vector<std::pair<int, int>> out;
    out.reserve(static_cast<std::size_t>((2 * radius + 1) * (2 * radius + 1)));
    for (int dx = -radius; dx <= radius; ++dx) {
        for (int dy = -radius; dy <= radius; ++dy) {
            out.push_back({center_x + dx, center_y + dy});
        }
    }
    return out;
}

BoardTargetHighlightCells gather_lobbed_aoe_center_cells(
    const GameState& game, const Entity& caster, const int max_range, const std::map<std::string, int>& payload,
    LobbedAoeCenterCellProbe probe)
{
    static_cast<void>(payload);
    BoardTargetHighlightCells out;
    const BoardCellBounds bounds = game.board.cell_bounds();
    if (bounds.empty()) {
        return out;
    }
    for (int wy = bounds.min_y; wy <= bounds.max_y; ++wy) {
        for (int wx = bounds.min_x; wx <= bounds.max_x; ++wx) {
            if (!game.board.get_square(wx, wy)) {
                continue;
            }
            if (max_range > 0 && min_chebyshev_entity_to_cell(caster, wx, wy) > max_range) {
                continue;
            }
            if (!entity_has_line_of_sight_to_cell(game, caster, {wx, wy})) {
                continue;
            }
            if (probe && !probe(wx, wy)) {
                continue;
            }
            append_unique(out.other_cells, {wx, wy});
        }
    }
    return out;
}

BoardTargetHighlightCells gather_push_direction_spell_entity_cells(
    GameState& game, const int player_id, const std::string& effect_key, const BoardTargetKind board_target_kind,
    const std::vector<std::string>& require_target_unit_types)
{
    BoardTargetHighlightCells out;
    game.board.for_each_entity([&](const std::shared_ptr<Entity>& ent_ptr) {
        if (!ent_ptr || !push_direction_entity_target_valid(
                game, player_id, *ent_ptr, effect_key, board_target_kind, require_target_unit_types)) {
            return;
        }
        std::vector<std::pair<int, int>> target_cells;
        append_footprint_cells(*ent_ptr, target_cells);
        const bool hostile = ent_ptr->owner.has_value() && teams_hostile(game, player_id, *ent_ptr->owner);
        auto& bucket = hostile ? out.enemy_cells : out.other_cells;
        for (const auto& cell : target_cells) {
            append_unique(bucket, cell);
        }
    });
    return out;
}

BoardTargetHighlightCells gather_push_direction_indicator_cells_for_target(
    const GameState& game, const int target_x, const int target_y, const std::map<std::string, int>& payload)
{
    BoardTargetHighlightCells out;
    const auto target = game.board.entity_at(target_x, target_y);
    if (!target) {
        return out;
    }
    for (const auto& cell : push_direction_aim_indicator_cells(game, *target, payload)) {
        append_unique(out.other_cells, cell);
    }
    return out;
}

ActionResult validate_push_direction_spell_target(
    GameState& game, const int player_id, const std::string& effect_key, const BoardTargetKind board_target_kind,
    const std::vector<std::string>& require_target_unit_types, const std::map<std::string, int>& payload,
    const std::map<std::string, int>& targets)
{
    const auto xit = targets.find(effect_keys::kCellX);
    const auto yit = targets.find(effect_keys::kCellY);
    const auto axit = targets.find(effect_keys::kAimX);
    const auto ayit = targets.find(effect_keys::kAimY);
    if (xit == targets.end() || yit == targets.end()) {
        return {false, "Push spell requires a unit target cell", {}};
    }
    if (axit == targets.end() || ayit == targets.end()) {
        return {false, "Push spell requires a direction cell (aim_x/aim_y)", {}};
    }
    const auto target = game.board.entity_at(xit->second, yit->second);
    if (!target) {
        return {false, "Push target is not a board entity", {}};
    }
    if (!board_target_allows(game, board_target_kind, player_id, *target)) {
        return {false, "Push target is not a legal target", {}};
    }
    if (!board_target_entity_allowed_for_effect(*target, board_target_kind, effect_key)) {
        return {false, "Push target entity type is not valid for this effect", {}};
    }
    if (!entity_satisfies_unit_type_filter(*target, require_target_unit_types)) {
        return {false, target->entity_id + " does not have a required unit type", {}};
    }
    if (!taunt_allows_board_target(game, nullptr, player_id, *target)) {
        return {false, "Taunt: must target a directly adjacent enemy taunt unit", {}};
    }
    if (enemy_direct_target_blocked_by_stealth(game, player_id, *target)) {
        return {false, target->entity_id + " is stealthed", {}};
    }
    const auto indicators = push_direction_aim_indicator_cells(game, *target, payload);
    const auto aim_cell = std::make_pair(axit->second, ayit->second);
    if (std::find(indicators.begin(), indicators.end(), aim_cell) == indicators.end()) {
        return {false, "Push direction cell is not adjacent to the target", {}};
    }
    const auto dir = push_direction_from_aim_cell(*target, axit->second, ayit->second);
    if (dir.first == 0 && dir.second == 0) {
        return {false, "Push direction is zero", {}};
    }
    const bool cardinal_only = [&payload]() {
        const auto it = payload.find("cardinal_only");
        return it != payload.end() && it->second != 0;
    }();
    if (cardinal_only && dir.first != 0 && dir.second != 0) {
        return {false, "Push spell requires a cardinal (orthogonal) direction", {}};
    }
    return {true, "", {}};
}

std::vector<std::pair<int, int>> preview_push_direction_spell_path_cells(
    const GameState& game, const int target_x, const int target_y, const int aim_x, const int aim_y,
    const std::map<std::string, int>& payload)
{
    const auto target = game.board.entity_at(target_x, target_y);
    if (!target) {
        return {};
    }
    const auto dir = push_direction_from_aim_cell(*target, aim_x, aim_y);
    if (dir.first == 0 && dir.second == 0) {
        return {};
    }
    const int distance = [&payload]() {
        const auto it = payload.find(effect_keys::kPayloadAmount);
        return it != payload.end() ? std::max(1, it->second) : 1;
    }();
    return preview_push_displacement_path_cells(*target, dir.first, dir.second, distance);
}

bool ability_board_target_includes_caster_self(
    GameState& game, const std::shared_ptr<Unit>& actor, const int player_id, const std::string& ability_key_utf8)
{
    if (!actor) {
        return false;
    }
    const BoardTargetHighlightCells cells = gather_ability_board_target_cells(game, actor, player_id, ability_key_utf8);
    std::vector<std::pair<int, int>> footprint;
    append_footprint_cells(*actor, footprint);
    auto contains = [](const std::vector<std::pair<int, int>>& list, const int wx, const int wy) {
        return std::any_of(list.begin(), list.end(), [wx, wy](const std::pair<int, int>& p) {
            return p.first == wx && p.second == wy;
        });
    };
    for (const auto& [wx, wy] : footprint) {
        if (contains(cells.other_cells, wx, wy) || contains(cells.enemy_cells, wx, wy)) {
            return true;
        }
    }
    return false;
}

}  // namespace tactics