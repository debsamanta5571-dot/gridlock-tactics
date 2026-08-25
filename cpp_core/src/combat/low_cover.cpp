#include "tactics/combat/low_cover.hpp"

#include "tactics/attributes/attributes.hpp"
#include "tactics/combat/combat_resolver.hpp"
#include "tactics/common/effect_keys.hpp"
#include "tactics/core/game_state.hpp"
#include "tactics/core/stack.hpp"



#include <algorithm>

#include <climits>

#include <cstdlib>

#include <optional>

#include <vector>



namespace tactics {

namespace {

std::optional<AbilitySpec> find_ability_on_entity(const Entity& entity, const std::string& key)
{
    for (const AbilitySpec& ability : entity.activated_abilities) {
        if (ability.key == key) {
            return ability;
        }
    }
    return std::nullopt;
}




std::vector<std::pair<int, int>> footprint_cells_for_entity(const Entity& entity)

{

    if (!entity.occupied_positions.empty()) {

        return entity.occupied_positions;

    }

    if (!entity.position) {

        return {};

    }

    std::vector<std::pair<int, int>> out;

    const auto [ax, ay] = *entity.position;

    for (const auto& [dx, dy] : entity_shape_offsets(entity)) {

        out.push_back({ax + dx, ay + dy});

    }

    return out;

}



int min_chebyshev_between_cells(int ax, int ay, int bx, int by)

{

    return std::max(std::abs(ax - bx), std::abs(ay - by));

}



int lowest_path_index_for_entity(const std::vector<std::pair<int, int>>& path, const Entity& entity)

{

    int best = static_cast<int>(path.size());

    for (int i = 0; i < static_cast<int>(path.size()); ++i) {

        if (min_chebyshev_entity_to_cell(entity, path[i].first, path[i].second) == 0) {

            best = std::min(best, i);

        }

    }

    return best;

}



int min_chebyshev_entity_to_shooter_cell(const Entity& entity, int shooter_x, int shooter_y)

{

    int best = INT_MAX;

    for (const auto& [dx, dy] : footprint_cells_for_entity(entity)) {

        best = std::min(best, min_chebyshev_between_cells(shooter_x, shooter_y, dx, dy));

    }

    return best == INT_MAX ? 0 : best;

}



struct CanonicalAttackLos {

    bool valid{false};

    int shooter_x{0};

    int shooter_y{0};

    std::vector<std::pair<int, int>> path;

};



/** One clear supercover path from the attacker footprint cell closest to the target (matches attack validation). */

CanonicalAttackLos canonical_attack_los(const GameState& game, const Entity& attacker, std::pair<int, int> target_cell)

{

    CanonicalAttackLos best;

    int best_shooter_dist = INT_MAX;

    int best_path_len = INT_MAX;

    for (const auto& [sx, sy] : footprint_cells_for_entity(attacker)) {

        const LOSResult los = game.board.line_of_sight({sx, sy}, target_cell);

        if (!los.query_valid || !los.clear || los.path.size() < 2) {

            continue;

        }

        const int shooter_dist = min_chebyshev_between_cells(sx, sy, target_cell.first, target_cell.second);

        const int path_len = static_cast<int>(los.path.size());

        if (shooter_dist < best_shooter_dist || (shooter_dist == best_shooter_dist && path_len < best_path_len)) {

            best_shooter_dist = shooter_dist;

            best_path_len = path_len;

            best.valid = true;

            best.shooter_x = sx;

            best.shooter_y = sy;

            best.path = los.path;

        }

    }

    return best;

}



bool cover_already_listed(const std::vector<std::shared_ptr<Entity>>& covers, const Entity& candidate)

{

    for (const auto& cover : covers) {

        if (cover.get() == &candidate) {

            return true;

        }

    }

    return false;

}



void collect_covers_on_los_path(const GameState& game, int shooter_x, int shooter_y, const std::vector<std::pair<int, int>>& path,

    const Entity& defender, std::vector<std::shared_ptr<Entity>>& out)

{

    if (path.size() < 2) {

        return;

    }

    const int defender_index = lowest_path_index_for_entity(path, defender);

    const bool defender_on_path = defender_index < static_cast<int>(path.size());

    const int shooter_to_defender = min_chebyshev_entity_to_shooter_cell(defender, shooter_x, shooter_y);

    for (int i = 1; i < static_cast<int>(path.size()) - 1; ++i) {

        if (defender_on_path && i >= defender_index) {

            break;

        }

        const auto& [cx, cy] = path[i];

        const auto sq = game.board.get_square(cx, cy);

        if (!sq || !sq->entity || !entity_is_low_cover(*sq->entity) || sq->entity->current_health <= 0) {

            continue;

        }

        if (min_chebyshev_entity_to_cell(defender, cx, cy) > 1) {

            continue;

        }

        if (!defender_on_path) {

            const int shooter_to_cover = min_chebyshev_between_cells(shooter_x, shooter_y, cx, cy);

            if (shooter_to_cover <= 0 || shooter_to_cover >= shooter_to_defender) {

                continue;

            }

        }

        if (!cover_already_listed(out, *sq->entity)) {

            out.push_back(sq->entity);

        }

    }

}



}  // namespace



std::vector<std::shared_ptr<Entity>> find_all_low_cover_for_ranged_evasion(const GameState& game, const Entity& attacker,

    std::pair<int, int> target_cell, const Entity& defender)

{

    std::vector<std::shared_ptr<Entity>> eligible;

    if (defender.entity_type != "unit" || defender.current_health <= 0) {

        return eligible;

    }



    const CanonicalAttackLos los = canonical_attack_los(game, attacker, target_cell);

    if (!los.valid) {

        return eligible;

    }

    collect_covers_on_los_path(game, los.shooter_x, los.shooter_y, los.path, defender, eligible);

    return eligible;

}



std::shared_ptr<Entity> find_low_cover_for_ranged_evasion(const GameState& game, const Entity& attacker,

    std::pair<int, int> target_cell, const Entity& defender)

{

    const auto eligible = find_all_low_cover_for_ranged_evasion(game, attacker, target_cell, defender);

    return eligible.empty() ? nullptr : eligible.front();

}



bool roll_low_cover_evasion(std::mt19937& rng, const int chance_percent)

{

    const int clamped = std::clamp(chance_percent, 0, 100);

    if (clamped <= 0) {

        return false;

    }

    if (clamped >= 100) {

        return true;

    }

    std::uniform_int_distribution<int> dist(1, 100);

    return dist(rng) <= clamped;

}



bool stack_item_uses_ranged_low_cover_evasion(const StackItem& item, const Entity& source, std::optional<AbilitySpec>& ability_out)
{
    ability_out.reset();
    if (item.effect_key != "deal_damage") {
        return false;
    }
    const auto xit = item.targets.find(effect_keys::kCellX);
    const auto yit = item.targets.find(effect_keys::kCellY);
    if (xit == item.targets.end() || yit == item.targets.end()) {
        return false;
    }
    if (item.source_type == "ability") {
        if (item.source_ability_key.empty()) {
            return false;
        }
        const auto spec = find_ability_on_entity(source, item.source_ability_key);
        if (!spec || !ability_uses_ranged_targeting(*spec)) {
            return false;
        }
        ability_out = *spec;
        return true;
    }
    if (item.source_type == "focus_spell") {
        return true;
    }
    return false;
}

bool maybe_redirect_ranged_attack_to_low_cover(GameState& game, const Entity& attacker, const std::pair<int, int> target_cell,

    const Entity& intended_target, DamagePacket& packet, const AbilitySpec* ability)

{

    if (packet.amount <= 0 || !packet.target) {

        return false;

    }

    if (damage_source_ignores_low_cover_evasion(attacker, ability)) {

        return false;

    }

    const std::vector<std::shared_ptr<Entity>> eligible =

        find_all_low_cover_for_ranged_evasion(game, attacker, target_cell, intended_target);

    if (eligible.empty()) {

        return false;

    }

    if (!roll_low_cover_evasion(game.rng(), kLowCoverRangedEvasionPercent)) {

        return false;

    }

    std::uniform_int_distribution<std::size_t> pick(0, eligible.size() - 1);

    packet.target = eligible[pick(game.rng())];

    return true;

}



}  // namespace tactics

