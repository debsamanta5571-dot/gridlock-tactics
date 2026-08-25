#include "tactics/board/push_displacement.hpp"

#include "tactics/attributes/attributes.hpp"
#include "tactics/board/adjacency.hpp"
#include "tactics/board/board_layout.hpp"
#include "tactics/board/grid.hpp"
#include "tactics/board/movement_policy.hpp"
#include "tactics/board/tile_modifiers.hpp"
#include "tactics/combat/directional_area.hpp"
#include "tactics/core/game_state.hpp"
#include "tactics/core/stack.hpp"

#include <set>
#include <sstream>
#include <vector>

namespace tactics {
namespace {

struct PushStepBlock {
    enum class Kind { Wall, Entity };
    Kind kind{Kind::Wall};
    std::shared_ptr<Entity> blocker;
};

std::vector<std::pair<int, int>> footprint_cells_at_anchor(const Entity& entity, int anchor_x, int anchor_y)
{
    std::vector<std::pair<int, int>> cells;
    for (const auto& [dx, dy] : entity_shape_offsets(entity)) {
        cells.push_back({anchor_x + dx, anchor_y + dy});
    }
    return cells;
}

std::optional<PushStepBlock> probe_push_step_block(
    GameBoard& board, const BoardLayoutSpec& layout, const Entity& mover, int next_ax, int next_ay)
{
    std::shared_ptr<Entity> blocker;
    for (const auto& [cx, cy] : footprint_cells_at_anchor(mover, next_ax, next_ay)) {
        const auto sq = board.get_square(cx, cy);
        if (!sq) {
            if (off_board_treatment_at(layout, cx, cy) == OffBoardTileTreatment::Void) {
                continue;
            }
            PushStepBlock out;
            out.kind = PushStepBlock::Kind::Wall;
            return out;
        }
        if (!sq->occupied || !sq->entity || sq->entity.get() == &mover) {
            continue;
        }
        if (entity_is_pickup(*sq->entity)) {
            continue;
        }
        if (!blocker) {
            blocker = sq->entity;
        } else if (blocker != sq->entity) {
            PushStepBlock out;
            out.kind = PushStepBlock::Kind::Entity;
            out.blocker = blocker;
            return out;
        }
    }
    if (blocker) {
        PushStepBlock out;
        out.kind = PushStepBlock::Kind::Entity;
        out.blocker = std::move(blocker);
        return out;
    }
    return std::nullopt;
}

bool footprint_would_fall_into_void(
    GameBoard& board, const BoardLayoutSpec& layout, const Entity& entity, int anchor_x, int anchor_y)
{
    std::vector<const GridSquare*> occupied;
    for (const auto& [cx, cy] : footprint_cells_at_anchor(entity, anchor_x, anchor_y)) {
        const auto sq = board.get_square(cx, cy);
        if (!sq) {
            if (off_board_treatment_at(layout, cx, cy) == OffBoardTileTreatment::Void) {
                return true;
            }
            continue;
        }
        occupied.push_back(sq.get());
    }
    if (occupied.empty()) {
        return true;
    }
    return entity_should_fall_into_void(entity, occupied);
}

void remove_pickups_under_footprint(GameBoard& board, const Entity& entity, int anchor_x, int anchor_y,
    std::vector<std::shared_ptr<Entity>>& collected_pickups)
{
    for (const auto& [cx, cy] : footprint_cells_at_anchor(entity, anchor_x, anchor_y)) {
        const auto sq = board.get_square(cx, cy);
        if (!sq || !sq->occupied || !sq->entity || !entity_is_pickup(*sq->entity)) {
            continue;
        }
        collected_pickups.push_back(sq->entity);
        board.remove_entity(sq->entity);
    }
}

void apply_push_step_tile_effects(GameState& game, Entity& entity, int from_ax, int from_ay, int to_ax, int to_ay)
{
    const auto get_square = [&game](int x, int y) { return game.board.get_square(x, y); };
    const int terrain_damage = footprint_step_terrain_damage(get_square, entity, from_ax, from_ay, to_ax, to_ay);
    if (terrain_damage > 0) {
        apply_incoming_damage(entity, terrain_damage);
    }

    const auto from_cells = footprint_cells_at_anchor(entity, from_ax, from_ay);
    const auto to_cells = footprint_cells_at_anchor(entity, to_ax, to_ay);
    std::set<std::pair<int, int>> entered;
    for (const auto& cell : to_cells) {
        bool was_on = false;
        for (const auto& prior : from_cells) {
            if (prior == cell) {
                was_on = true;
                break;
            }
        }
        if (!was_on) {
            entered.insert(cell);
        }
    }
    if (std::abs(to_ax - from_ax) + std::abs(to_ay - from_ay) == 2) {
        const auto side_a = footprint_cells_at_anchor(entity, to_ax, from_ay);
        const auto side_b = footprint_cells_at_anchor(entity, from_ax, to_ay);
        const auto add_side = [&](const std::vector<std::pair<int, int>>& side_cells) {
            for (const auto& cell : side_cells) {
                bool on_from = false;
                bool on_to = false;
                for (const auto& prior : from_cells) {
                    if (prior == cell) {
                        on_from = true;
                        break;
                    }
                }
                for (const auto& next : to_cells) {
                    if (next == cell) {
                        on_to = true;
                        break;
                    }
                }
                if (!on_from && !on_to) {
                    entered.insert(cell);
                }
            }
        };
        add_side(side_a);
        add_side(side_b);
    }

    for (const auto& [cx, cy] : entered) {
        const auto sq = game.board.get_square(cx, cy);
        if (!sq) {
            continue;
        }
        const auto* ov = square_overlay_modifier(*sq);
        if (ov && ov->name == kGasCloudOverlayName) {
            add_entity_effect(entity, "poison", 1, "push_gas");
        }
    }
}

void fire_collected_pickup_effects(GameState& game, const std::shared_ptr<Entity>& collector,
    const std::vector<std::shared_ptr<Entity>>& pickups)
{
    if (!collector || !collector->owner) {
        return;
    }
    for (const auto& pickup : pickups) {
        if (!pickup || pickup->pickup_effect_key.empty()
                || !game.stack_manager.has_effect_handler(pickup->pickup_effect_key)) {
            continue;
        }
        StackItem pk_item;
        pk_item.source_type = "pickup";
        pk_item.source_name = pickup->entity_id;
        pk_item.source_entity_id = collector->entity_id;
        pk_item.controller_id = *collector->owner;
        pk_item.effect_key = pickup->pickup_effect_key;
        pk_item.speed = EffectSpeed::Blazing;
        pk_item.payload = pickup->pickup_payload;
        game.stack_manager.add_item(game, std::move(pk_item));
    }
}

void apply_push_collision_thorns(GameState& game, const std::shared_ptr<Entity>& victim,
    const std::shared_ptr<Entity>& other, const int damage_taken)
{
    if (damage_taken <= 0 || !victim || !other) {
        return;
    }
    const int thorns = thorns_value(*victim);
    if (thorns <= 0 || !game.board.all_entities_map.contains(other->entity_id)) {
        return;
    }
    apply_incoming_damage(*other, thorns, /*pierce=*/true, DamageType::Physical);
    if (other->current_health <= 0) {
        game.destroy_board_entity(other);
    }
}

void apply_collision_damage(GameState& game, const std::shared_ptr<Entity>& mover,
    const std::shared_ptr<Entity>& blocker, int damage)
{
    if (damage <= 0 || !mover) {
        return;
    }
    const int mover_damage = apply_incoming_damage(*mover, damage);
    if (blocker) {
        apply_push_collision_thorns(game, mover, blocker, mover_damage);
    }
    if (mover->current_health <= 0) {
        game.destroy_board_entity(mover);
        return;
    }
    if (!blocker) {
        return;
    }
    const int blocker_damage = apply_incoming_damage(*blocker, damage);
    apply_push_collision_thorns(game, blocker, mover, blocker_damage);
    if (mover->current_health <= 0) {
        game.destroy_board_entity(mover);
        return;
    }
    if (blocker->current_health <= 0) {
        game.destroy_board_entity(blocker);
    }
}

}  // namespace

std::optional<std::pair<int, int>> entity_primary_anchor(const Entity& entity)
{
    if (entity.position) {
        return *entity.position;
    }
    if (!entity.occupied_positions.empty()) {
        return entity.occupied_positions.front();
    }
    return std::nullopt;
}

std::pair<int, int> push_direction_away_from_source(const Entity& source, const Entity& target)
{
    int sx = 0;
    int sy = 0;
    if (source.position) {
        sx = source.position->first;
        sy = source.position->second;
    } else if (!source.occupied_positions.empty()) {
        sx = source.occupied_positions.front().first;
        sy = source.occupied_positions.front().second;
    }
    int tx = 0;
    int ty = 0;
    if (target.position) {
        tx = target.position->first;
        ty = target.position->second;
    } else if (!target.occupied_positions.empty()) {
        tx = target.occupied_positions.front().first;
        ty = target.occupied_positions.front().second;
    }
    return snap_octilinear_direction(sx, sy, tx, ty);
}

std::pair<int, int> push_direction_away_from_controller_base(
    const GameState& game, const int controller_id, const Entity& target)
{
    const auto target_anchor = entity_primary_anchor(target);
    if (!target_anchor) {
        return {0, 0};
    }
    const auto [tx, ty] = *target_anchor;
    const int bx = player_base_anchor_x(game.board_width());
    const int by = player_base_anchor_y(game.board_height(), controller_id);
    return snap_octilinear_direction(bx, by, tx, ty);
}

std::vector<std::pair<int, int>> push_direction_aim_indicator_cells(
    const GameState& game, const Entity& target, const std::map<std::string, int>& payload)
{
    const auto anchor = entity_primary_anchor(target);
    if (!anchor) {
        return {};
    }
    const auto [ax, ay] = *anchor;
    const bool cardinal_only = [&payload]() {
        const auto it = payload.find("cardinal_only");
        return it != payload.end() && it->second != 0;
    }();
    std::vector<std::pair<int, int>> out;
    const auto append_indicator = [&](const int dx, const int dy) {
        const int x = ax + dx;
        const int y = ay + dy;
        if (game.board.get_square(x, y)) {
            out.push_back({x, y});
        }
    };
    if (cardinal_only) {
        out.reserve(4);
        for (const auto [dx, dy] : kAdjacentOffsets) {
            append_indicator(dx, dy);
        }
    } else {
        out.reserve(8);
        for (const auto [dx, dy] : kSurroundingOffsets) {
            append_indicator(dx, dy);
        }
    }
    return out;
}

std::pair<int, int> push_direction_from_aim_cell(const Entity& target, const int aim_x, const int aim_y)
{
    const auto anchor = entity_primary_anchor(target);
    if (!anchor) {
        return {0, 0};
    }
    const auto [tx, ty] = *anchor;
    return snap_octilinear_direction(tx, ty, aim_x, aim_y);
}

std::vector<std::pair<int, int>> preview_push_displacement_path_cells(
    const Entity& target, const int dir_x, const int dir_y, const int distance)
{
    std::vector<std::pair<int, int>> out;
    const auto anchor = entity_primary_anchor(target);
    if (!anchor || distance <= 0 || (dir_x == 0 && dir_y == 0)) {
        return out;
    }
    const auto [ax, ay] = *anchor;
    out.reserve(static_cast<size_t>(distance));
    for (int step = 1; step <= distance; ++step) {
        out.push_back({ax + step * dir_x, ay + step * dir_y});
    }
    return out;
}

PushDisplacementResult resolve_push_displacement(
    GameState& game,
    const std::shared_ptr<Entity>& entity,
    const int dir_x,
    const int dir_y,
    const int distance,
    PushDisplacementOptions options)
{
    PushDisplacementResult result;
    if (!entity || !entity->position || entity->current_health <= 0) {
        result.ok = false;
        result.message = "push: no valid entity";
        return result;
    }
    if (distance <= 0) {
        result.message = "push: distance must be positive";
        return result;
    }
    if (dir_x == 0 && dir_y == 0) {
        result.ok = false;
        result.message = "push: direction is zero";
        return result;
    }
    if (!options.bypass_immovable && entity_is_immovable(*entity)) {
        result.ok = true;
        result.message = entity->entity_id + " is Immovable - push fizzled";
        return result;
    }

    const int start_ax = entity->position->first;
    const int start_ay = entity->position->second;
    int current_ax = start_ax;
    int current_ay = start_ay;

    if (!game.board.remove_entity(entity)) {
        result.ok = false;
        result.message = "push: failed to lift entity from board";
        return result;
    }

    const BoardLayoutSpec& layout = game.board_layout();
    std::vector<std::shared_ptr<Entity>> step_pickups;
    for (int step = 1; step <= distance; ++step) {
        const int next_ax = current_ax + dir_x;
        const int next_ay = current_ay + dir_y;
        const int remaining_tiles = distance - step + 1;

        if (const auto block = probe_push_step_block(game.board, layout, *entity, next_ax, next_ay)) {
            if (result.tiles_moved == 0) {
                game.board.place_entity(entity, start_ax, start_ay);
            }
            result.collision = true;
            result.collision_damage = 1 + remaining_tiles;
            result.collision_blocker = block->blocker;
            const std::shared_ptr<Entity> blocker_ent =
                block->kind == PushStepBlock::Kind::Entity ? block->blocker : nullptr;
            apply_collision_damage(game, entity, blocker_ent, result.collision_damage);
            std::ostringstream msg;
            msg << entity->entity_id << " hit ";
            if (blocker_ent) {
                msg << blocker_ent->entity_id;
            } else {
                msg << "a wall";
            }
            msg << " for " << result.collision_damage << " collision damage";
            result.message = msg.str();
            return result;
        }

        remove_pickups_under_footprint(game.board, *entity, next_ax, next_ay, step_pickups);
        apply_push_step_tile_effects(game, *entity, current_ax, current_ay, next_ax, next_ay);
        if (entity->current_health <= 0) {
            game.destroy_board_entity(entity);
            result.message = entity->entity_id + " destroyed by terrain during push";
            return result;
        }

        if (footprint_would_fall_into_void(game.board, layout, *entity, next_ax, next_ay)) {
            result.tiles_moved = step;
            result.fell_into_void = true;
            const std::string id = entity->entity_id;
            game.destroy_board_entity(entity);
            result.message = id + " fell into the void";
            return result;
        }

        if (!game.board.place_entity(entity, next_ax, next_ay)) {
            game.board.place_entity(entity, current_ax, current_ay);
            result.ok = false;
            result.message = "push: failed to place entity";
            return result;
        }

        current_ax = next_ax;
        current_ay = next_ay;
        result.tiles_moved = step;
    }

    if (result.tiles_moved > 0 && game.board.all_entities_map.contains(entity->entity_id)) {
        game.resolve_terrain_after_forced_movement(entity);
        fire_collected_pickup_effects(game, entity, step_pickups);
        result.message = "Pushed " + entity->entity_id + " " + std::to_string(result.tiles_moved) + " tile(s)";
    } else if (result.tiles_moved == 0) {
        game.board.place_entity(entity, start_ax, start_ay);
        result.message = entity->entity_id + " did not move";
    }

    return result;
}

}  // namespace tactics