#include "tactics/core/passive_action_order.hpp"

#include "tactics/attributes/attributes.hpp"
#include "tactics/combat/combat_resolver.hpp"
#include "tactics/common/effect_keys.hpp"
#include "tactics/core/game_state.hpp"
#include "tactics/board/board.hpp"

#include <algorithm>
#include <functional>
#include <optional>

namespace tactics {
namespace {

bool passive_has_automated_action(const PassiveAbilitySpec& passive)
{
    return !passive.trigger_timing.empty() && !passive.automated_effect_key.empty();
}

BoardTargetKind automated_target_kind(const PassiveAbilitySpec& passive)
{
    if (passive.automated_board_target_kind.has_value()) {
        return *passive.automated_board_target_kind;
    }
    return effect_board_target_kind(passive.automated_effect_key);
}

std::shared_ptr<Entity> pick_automated_target(GameState& game, const Entity& source, BoardTargetKind kind)
{
    if (!source.owner) {
        return nullptr;
    }
    std::vector<std::shared_ptr<Entity>> candidates;
    for (const auto& entity : all_living_entities_oldest_first(game.board)) {
        if (!entity || entity->entity_id == source.entity_id) {
            continue;
        }
        if (!board_target_allows(game, kind, *source.owner, *entity)) {
            continue;
        }
        candidates.push_back(entity);
    }
    if (candidates.empty()) {
        return nullptr;
    }
    sort_entities_oldest_first(candidates);
    return candidates.front();
}

int payload_amount(const PassiveAbilitySpec& passive, const std::string& key, int default_value = 0)
{
    const auto it = passive.automated_effect_payload.find(key);
    if (it == passive.automated_effect_payload.end()) {
        return default_value;
    }
    return it->second;
}

}  // namespace

bool entity_participates_in_passive_action_order(const Entity& e)
{
    // Opt-out flag: layout entities (obstacles, bases set in board_layout) can disable participation
    if (!e.participates_in_passive_order) {
        return false;
    }
    if (entity_is_base(e) || e.entity_type == "obstacle" || entity_is_low_cover(e)) {
        return false;
    }
    return e.entity_type == "unit" || entity_is_building(e) || entity_is_breakable_obstacle(e);
}

bool entity_spawn_older(const Entity& a, const Entity& b)
{
    if (a.spawn_sequence != b.spawn_sequence) {
        if (a.spawn_sequence == 0) {
            return b.spawn_sequence != 0;
        }
        if (b.spawn_sequence == 0) {
            return false;
        }
        return a.spawn_sequence < b.spawn_sequence;
    }
    return a.entity_id < b.entity_id;
}

void sort_entities_oldest_first(std::vector<std::shared_ptr<Entity>>& entities)
{
    std::sort(entities.begin(), entities.end(), [](const std::shared_ptr<Entity>& a, const std::shared_ptr<Entity>& b) {
        if (!a || !b) {
            return static_cast<bool>(a) < static_cast<bool>(b);
        }
        return entity_spawn_older(*a, *b);
    });
}

std::vector<std::shared_ptr<Entity>> living_entities_for_owner_oldest_first(const GameBoard& board, const int owner_id)
{
    // Use for_each_entity to avoid building a temporary copy of all_entities_map
    std::vector<std::shared_ptr<Entity>> out;
    board.for_each_entity([&](const std::shared_ptr<Entity>& entity) {
        if (!entity || !entity->owner || *entity->owner != owner_id) return;
        if (entity->current_health <= 0) return;
        out.push_back(entity);
    });
    sort_entities_oldest_first(out);
    return out;
}

std::vector<std::shared_ptr<Entity>> all_living_entities_oldest_first(const GameBoard& board)
{
    // Use for_each_entity to avoid building a temporary copy of all_entities_map
    std::vector<std::shared_ptr<Entity>> out;
    board.for_each_entity([&](const std::shared_ptr<Entity>& entity) {
        if (!entity || entity->current_health <= 0) return;
        out.push_back(entity);
    });
    sort_entities_oldest_first(out);
    return out;
}

std::unordered_map<std::string, int> compute_passive_action_order_ranks(const GameBoard& board)
{
    std::unordered_map<std::string, int> ranks;
    const auto entities = all_living_entities_oldest_first(board);
    for (int index = 0; index < static_cast<int>(entities.size()); ++index) {
        const auto ent = entities[static_cast<std::size_t>(index)];
        if (!ent) {
            continue;
        }
        if (!entity_participates_in_passive_action_order(*ent)) {
            continue;
        }
        ranks[ent->entity_id] = static_cast<int>(ranks.size()) + 1;
    }
    return ranks;
}

int passive_action_order_rank(const GameBoard& board, const std::string& entity_id)
{
    if (entity_id.empty()) {
        return 0;
    }
    // NOTE: This rebuilds the full rank map for a single query - O(N log N).
    // When you need ranks for multiple entities in the same frame, call
    // compute_passive_action_order_ranks() once and look up each entity in the result.
    const auto ranks = compute_passive_action_order_ranks(board);
    const auto it = ranks.find(entity_id);
    return it == ranks.end() ? 0 : it->second;
}

bool entity_turn_order_badge_at_cell(const Entity& entity, const int world_x, const int world_y)
{
    // If snapshot sync didn't preserve `occupied_positions` and `position`, we still want to show the badge
    // on the specific cell UE queried via `board.entity_at(WorldX, WorldY)`.
    if (entity.occupied_positions.empty() && !entity.position) {
        return true;
    }

    std::optional<std::pair<int, int>> badge_cell;
    auto consider = [&](const int x, const int y) {
        if (!badge_cell || y < badge_cell->second || (y == badge_cell->second && x < badge_cell->first)) {
            badge_cell = {x, y};
        }
    };
    if (!entity.occupied_positions.empty()) {
        for (const auto& [x, y] : entity.occupied_positions) {
            consider(x, y);
        }
    } else if (entity.position) {
        consider(entity.position->first, entity.position->second);
    }
    return badge_cell && badge_cell->first == world_x && badge_cell->second == world_y;
}

bool resolve_automated_passive_action(GameState& game, const std::shared_ptr<Entity>& source, const PassiveAbilitySpec& passive)
{
    if (!source || source->current_health <= 0 || !game.board.all_entities_map.contains(source->entity_id)) {
        return false;
    }
    if (entity_is_silenced(*source) || entity_immune_to_all_effects(*source)) {
        return false;
    }
    if (!source->owner) {
        return false;
    }
    const std::string& effect_key = passive.automated_effect_key;

    // Fast-path for effects that need a picked target (deal_damage, heal): resolve target here,
    // then fire through the stack as a Burst so any registered handler runs.
    const bool needs_target = (effect_key == "deal_damage" || effect_key == "heal");
    if (needs_target) {
        const int amount = payload_amount(passive, "amount", 1);
        if (amount <= 0) return false;
        auto target = pick_automated_target(game, *source, automated_target_kind(passive));
        if (!target) return false;

        StackItem item;
        item.source_type = "passive";
        item.source_name = passive.name.empty() ? passive.key : passive.name;
        item.source_entity_id = source->entity_id;
        item.controller_id = *source->owner;
        item.effect_key = effect_key;
        item.speed = EffectSpeed::Blazing;
        item.payload = passive.automated_effect_payload;
        item.payload[effect_keys::kPayloadAmount] = amount;
        item.targets[effect_keys::kCellX] = target->position ? target->position->first : 0;
        item.targets[effect_keys::kCellY] = target->position ? target->position->second : 0;
        item.target_entity_id = target->entity_id;
        item.board_target_kind = automated_target_kind(passive);
        if (effect_key == "deal_damage") {
            item.pierces_damage_prevention = true;
        }
        const auto result = game.stack_manager.add_item(game, std::move(item));
        return result.ok;
    }

    // For no-target effects (gain_neutral, draw_cards, etc.), fire directly through the stack.
    // To add new automated passive effects, register a handler via stack_manager.register_effect().
    if (game.stack_manager.has_effect_handler(effect_key)) {
        StackItem item;
        item.source_type = "passive";
        item.source_name = passive.name.empty() ? passive.key : passive.name;
        item.source_entity_id = source->entity_id;
        item.controller_id = *source->owner;
        item.effect_key = effect_key;
        item.speed = EffectSpeed::Blazing;
        item.payload = passive.automated_effect_payload;
        item.string_payload = passive.automated_string_payload;
        item.board_target_kind = automated_target_kind(passive);
        const auto result = game.stack_manager.add_item(game, std::move(item));
        return result.ok;
    }

    return false;
}

void process_automated_passive_actions(GameState& game, const int player_id, const std::string& timing)
{
    if (timing.empty()) {
        return;
    }
    if (timing == kPassiveActionTimingOwnerTurnEnd
            && game.player_has_ancient_frog_storage_consumers(player_id)) {
        game.begin_owner_turn_end_energy_storage(player_id);
    }
    const auto sources = living_entities_for_owner_oldest_first(game.board, player_id);
    for (const auto& source : sources) {
        if (!source || !game.board.all_entities_map.contains(source->entity_id)) {
            continue;
        }
        // Collect only the matching passives, then fire them in sort_order (stable, lowest first).
        // This lets a passive with a higher sort_order (e.g. Starforged Feast = 100) always fire
        // after lower-priority passives (e.g. Macrowave = 0) on the same unit.
        std::vector<std::reference_wrapper<const PassiveAbilitySpec>> to_fire;
        for (const PassiveAbilitySpec& passive : source->passive_abilities) {
            if (passive_has_automated_action(passive) && passive.trigger_timing == timing) {
                to_fire.emplace_back(passive);
            }
        }
        std::stable_sort(to_fire.begin(), to_fire.end(),
            [](const std::reference_wrapper<const PassiveAbilitySpec>& a,
               const std::reference_wrapper<const PassiveAbilitySpec>& b) {
                return a.get().sort_order < b.get().sort_order;
            });
        for (const auto& passive_ref : to_fire) {
            if (!game.board.all_entities_map.contains(source->entity_id)) break; // source died
            resolve_automated_passive_action(game, source, passive_ref.get());
        }
    }
}

}  // namespace tactics
