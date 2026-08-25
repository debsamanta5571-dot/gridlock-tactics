#include "tactics/core/stack.hpp"

#include "tactics/board/adjacency.hpp"
#include "tactics/combat/combat_resolver.hpp"
#include "tactics/core/board_target_policy.hpp"
#include "tactics/common/effect_keys.hpp"
#include "tactics/core/game_state.hpp"
#include "tactics/effects/effect_traits.hpp"
#include "tactics/entities/entity.hpp"

#include <algorithm>
#include <set>
#include <string>

namespace tactics {

namespace {

void apply_owner_conduit_spell_bonus(const GameState& game, StackItem& item)
{
    if (item.source_type != "spell" && item.source_type != "focus_spell") {
        return;
    }
    if (!effect_key_deals_damage(item.effect_key)) {
        return;
    }
    item.ability_damage_bonus += owner_conduit_total(game, item.controller_id);
}

void mark_resolved_stack_item(GameState& game, const StackItem& item) {
    if (item.source_type == "ability" && !item.source_ability_key.empty() && !item.source_entity_id.empty()) {
        const auto src_it = game.board.all_entities_map.find(item.source_entity_id);
        if (src_it != game.board.all_entities_map.end() && src_it->second) {
            const auto& entity = src_it->second;
            const AbilitySpec* resolved = nullptr;
            for (const auto& a : entity->activated_abilities) {
                if (a.key == item.source_ability_key) {
                    resolved = &a;
                    break;
                }
            }
            if (resolved) {
                entity_consume_ability_use(*entity, *resolved);
                if (resolved->barrage) {
                    entity->barrage_cast_counts_this_turn[item.source_ability_key]++;
                }
            }
        }
    }
    consume_attack_action_from_stack_item(game, item);
    if (item.source_type == "ability" && !item.source_entity_id.empty()) {
        game.consume_next_ability_on_hit_primers_if_needed(item.source_entity_id);
    }
}

}  // namespace

bool StackManager::register_effect(const std::string& key, EffectHandler handler) {
    const bool existed = handlers_.contains(key);
    handlers_[key] = std::move(handler);
    return !existed;
}

bool StackManager::has_effect_handler(const std::string& key) const
{
    return handlers_.contains(key);
}

std::vector<std::string> StackManager::registered_effect_keys() const
{
    std::vector<std::string> keys;
    keys.reserve(handlers_.size());
    for (const auto& [k, _] : handlers_) {
        keys.push_back(k);
    }
    std::sort(keys.begin(), keys.end());
    return keys;
}

void StackManager::open_main_window(int active_player) { priority_player_ = active_player; passes_ = 0; }
void StackManager::clear_for_new_turn(std::optional<int> active_player) {
    stack.clear();
    reaction_group_boundaries_.clear();
    priority_player_ = active_player;
    passes_ = 0;
}

std::optional<int> StackManager::next_player(const std::vector<int>& players, int current) {
    auto it = std::find(players.begin(), players.end(), current);
    if (it == players.end()) return std::nullopt;
    size_t idx = static_cast<size_t>(std::distance(players.begin(), it));
    return players[(idx + 1) % players.size()];
}

ActionResult StackManager::add_item(GameState& game, const std::string& source_type, const std::string& source_name, int controller_id,
                                    const std::string& effect_key, EffectSpeed speed, std::map<std::string, int> payload, std::map<std::string, int> targets,
                                    BoardTargetKind board_target_kind, std::string target_entity_id) {
    return add_item(game, source_type, source_name, controller_id, effect_key, speed, std::move(payload), std::move(targets), board_target_kind,
        std::move(target_entity_id), {});
}

ActionResult StackManager::add_item(GameState& game, const std::string& source_type, const std::string& source_name, int controller_id,
                                    const std::string& effect_key, EffectSpeed speed, std::map<std::string, int> payload, std::map<std::string, int> targets,
                                    BoardTargetKind board_target_kind, std::string target_entity_id, std::string target_stack_item_id) {
    StackItem item;
    item.item_id = "stack_" + std::to_string(counter_ + 1);
    item.source_type = source_type;
    item.source_name = source_name;
    item.controller_id = controller_id;
    item.effect_key = effect_key;
    item.speed = speed;
    item.payload = std::move(payload);
    item.targets = std::move(targets);
    item.board_target_kind = board_target_kind;
    item.target_entity_id = std::move(target_entity_id);
    item.target_stack_item_id = std::move(target_stack_item_id);
    return add_item(game, std::move(item));
}

std::string StackManager::allocate_item_id()
{
    return "batch_" + std::to_string(++counter_);
}

ActionResult StackManager::add_item(GameState& game, StackItem item) {
    if (item.played_from_reserves) {
        const auto ap = game.turn_manager.current_player();
        if (!ap || *ap != item.controller_id) {
            return {false, "Reserves cards can only be played on your turn.", {}};
        }
    }
    if (item.item_id.empty()) {
        item.item_id = allocate_item_id();
    }
    if (item.speed == EffectSpeed::Blazing) {
        game.clear_phase_undo_for_player(item.controller_id);
        auto h = handlers_.find(item.effect_key);
        if (h == handlers_.end()) {
            return {false, "No stack effect handler is registered for '" + item.effect_key + "' (check spelling / data files).", {}};
        }
        if (game.try_pause_for_blazing_ability_viz(item)) {
            const std::string label = item.source_name.empty() ? item.effect_key : item.source_name;
            return {true, "[BURST] " + label + " - awaiting visualization", {}};
        }
        auto result = resolve_item_direct(game, item);
        if (result.ok) {
            mark_resolved_stack_item(game, item);
        }
        if (!game.is_draining_phase_action_queue()) {
            game.fire_pending_reactives();
            game.fire_phase_pending_reactives();
        }
        result.message = "[BURST] " + result.message;
        return result;
    }
    if (game.uses_phase_batching()) {
        const std::string batch_id = item.item_id;
        auto result = game.queue_batched_spell(std::move(item));
        if (result.ok) {
            result.data["batch_item_id"] = batch_id;
        }
        return result;
    }
    return resolve_item_direct(game, item);
}

bool StackManager::has_stack_item(const std::string& item_id) const {
    return std::any_of(stack.begin(), stack.end(), [&](const StackItem& item) { return item.item_id == item_id; });
}

const StackItem* StackManager::find_stack_item(const std::string& item_id) const {
    const auto it = std::find_if(stack.begin(), stack.end(), [&](const StackItem& item) { return item.item_id == item_id; });
    return it == stack.end() ? nullptr : &*it;
}

bool StackManager::remove_stack_item_by_id(const std::string& item_id) {
    const auto it = std::find_if(stack.begin(), stack.end(), [&](const StackItem& item) { return item.item_id == item_id; });
    if (it == stack.end()) {
        return false;
    }
    stack.erase(it);
    return true;
}

ActionResult StackManager::resolve_top(GameState& game) {
    if (stack.empty()) return {false, "Stack is empty", {}};
    game.push_stack_batch_resolution();
    auto item = stack.back();
    stack.pop_back();
    game.begin_stack_effect_resolution(&item);
    apply_owner_conduit_spell_bonus(game, item);
    auto h = handlers_.find(item.effect_key);
    if (h == handlers_.end()) {
        game.end_stack_effect_resolution();
        game.pop_stack_batch_resolution();
        return {false, "No stack effect handler is registered for '" + item.effect_key + "' (check spelling / data files).", {}};
    }
    auto res = h->second(game, item);
    if (res.ok) {
        mark_resolved_stack_item(game, item);
    }

    // Chain: after primary resolution, BFS through all entities connected by 8-directional
    // adjacency that satisfy `board_target_kind` and apply the same effect to each.
    // Chain copies resolve immediately (burst-style) and never touch the attack budget again.
    if (res.ok && item.chain && !item.target_entity_id.empty()) {
        const auto init_it = game.board.all_entities_map.find(item.target_entity_id);
        if (init_it != game.board.all_entities_map.end() && init_it->second && init_it->second->position) {
            std::set<std::string> visited;
            std::vector<std::shared_ptr<Entity>> frontier;
            visited.insert(init_it->second->entity_id);
            frontier.push_back(init_it->second);

            for (std::size_t qi = 0; qi < frontier.size(); ++qi) {
                for (const auto& [cx, cy] : entity_surrounding_cells(*frontier[qi])) {
                    auto neighbor = game.board.entity_at(cx, cy);
                    if (!neighbor) continue;
                    if (visited.count(neighbor->entity_id)) continue;
                    if (!neighbor->position) continue;
                    // Chain propagates only through units and structures (not bases or pure obstacles).
                    if (neighbor->entity_type != "unit" && !entity_is_structure(*neighbor)) continue;
                    // Must satisfy the original targeting constraints.
                    if (!board_target_allows(game, item.board_target_kind, item.controller_id, *neighbor)) continue;
                    if (!board_target_entity_allowed_for_effect(*neighbor, item.board_target_kind, item.effect_key)) {
                        continue;
                    }
                    // Look up the owning shared_ptr for the frontier and lifetime guarantee.
                    const auto nb_it = game.board.all_entities_map.find(neighbor->entity_id);
                    if (nb_it == game.board.all_entities_map.end() || !nb_it->second) continue;
                    visited.insert(nb_it->second->entity_id);
                    frontier.push_back(nb_it->second);
                    // Apply the same effect to this entity - burst-style, does not re-spend attack budget.
                    StackItem chain_item = item;
                    chain_item.chain = false;           // prevent recursive re-chaining
                    chain_item.consumes_attack_action = false;
                    chain_item.target_entity_id = nb_it->second->entity_id;
                    chain_item.targets[effect_keys::kCellX] = neighbor->position->first;
                    chain_item.targets[effect_keys::kCellY] = neighbor->position->second;
                    h->second(game, chain_item);
                }
            }
        }
    }

    // Survive-the-exchange rule: fire any deferred reactives (e.g. tithe) now that the
    // full stack item and any synchronous reactions it caused have resolved.
    game.fire_pending_reactives();
    auto ap = game.turn_manager.current_player();
    if (ap) priority_player_ = *ap;
    res.message = "Resolved " + item.source_name + ": " + res.message;
    game.end_stack_effect_resolution();
    game.pop_stack_batch_resolution();
    return res;
}

void StackManager::capture_network_snapshot(std::vector<StackItem>& out_stack, std::optional<int>& out_priority, int& out_passes,
                                            int& out_counter) const {
    out_stack = stack;
    out_priority = priority_player_;
    out_passes = passes_;
    out_counter = counter_;
}

void StackManager::restore_network_snapshot(std::vector<StackItem> items, std::optional<int> priority, int passes, int counter) {
    stack = std::move(items);
    priority_player_ = priority;
    passes_ = passes;
    counter_ = counter;
}

bool StackManager::can_pass_priority(const GameState& game, const int player_id) const
{
    (void)game;
    (void)player_id;
    return false;
}

ActionResult StackManager::pass_priority(GameState& game, int player_id) {
    if (!can_pass_priority(game, player_id)) {
        if (stack.empty()) {
            return {false, "Priority passing is disabled - all effects batch until the reaction window closes.", {}};
        }
        if (!priority_player_) {
            return {false, "No player currently has priority.", {}};
        }
        if (*priority_player_ != player_id) {
            return {false,
                    "Stack: player " + std::to_string(player_id) + " cannot pass now - priority is with player " +
                        std::to_string(*priority_player_) + ".",
                    {}};
        }
        return {false, "Player " + std::to_string(player_id) + " is not in this match.", {}};
    }
    const auto& players = game.turn_manager.players;
    /** If the next seat to receive priority would be whoever controls the top stack item, all other players have declined - resolve now (caster never needs to pass on their own spell). */
    if (!stack.empty()) {
        const int top_controller = stack.back().controller_id;
        auto np = next_player(players, player_id);
        if (np && *np == top_controller) {
            passes_ = 0;
            return resolve_top(game);
        }
    }
    ++passes_;
    if (passes_ < static_cast<int>(players.size())) {
        priority_player_ = next_player(players, player_id);
        return {true, "Player " + std::to_string(player_id) + " passed priority", {}};
    }
    passes_ = 0;
    if (!stack.empty()) return resolve_top(game);
    auto ap = game.turn_manager.current_player();
    if (ap) priority_player_ = *ap;
    return {true, "All players passed. Stack empty.", {}};
}

ActionResult StackManager::resolve_item_direct(GameState& game, const StackItem& item)
{
    game.push_stack_batch_resolution();
    game.begin_stack_effect_resolution(&item);
    StackItem resolved_item = item;
    apply_owner_conduit_spell_bonus(game, resolved_item);
    auto h = handlers_.find(resolved_item.effect_key);
    if (h == handlers_.end()) {
        game.end_stack_effect_resolution();
        game.pop_stack_batch_resolution();
        return {false, "No handler for effect_key '" + resolved_item.effect_key + "'", {}};
    }
    auto res = h->second(game, resolved_item);
    if (res.ok) {
        mark_resolved_stack_item(game, resolved_item);
    }
    game.end_stack_effect_resolution();
    game.pop_stack_batch_resolution();
    return res;
}

void StackManager::seal_reaction_group()
{
    // Only record a boundary if the current group has items (avoid empty groups).
    if (!stack.empty() && (reaction_group_boundaries_.empty() || reaction_group_boundaries_.back() < stack.size())) {
        reaction_group_boundaries_.push_back(stack.size());
    }
}

void StackManager::resolve_all(GameState& game)
{
    // Build group start indices from boundaries.
    // Boundaries mark where each NEW group begins; items before the first boundary are group 0.
    // Groups resolve LIFO (newest first); items within each group resolve FIFO.
    std::vector<size_t> group_starts;
    group_starts.push_back(0);
    for (size_t b : reaction_group_boundaries_) {
        if (b > 0 && b < stack.size()) group_starts.push_back(b);
    }

    // Collect items in resolution order into a separate list, then clear the stack.
    std::vector<StackItem> ordered;
    ordered.reserve(stack.size());
    for (int g = static_cast<int>(group_starts.size()) - 1; g >= 0; --g) {
        size_t begin = group_starts[g];
        size_t end = (g + 1 < static_cast<int>(group_starts.size())) ? group_starts[g + 1] : stack.size();
        for (size_t i = begin; i < end; ++i) {
            ordered.push_back(std::move(stack[i]));
        }
    }
    stack.clear();
    reaction_group_boundaries_.clear();

    game.push_stack_batch_resolution();
    // Resolve each item using the same logic as resolve_top (including chain propagation).
    for (auto& item : ordered) {
        game.begin_stack_effect_resolution(&item);
        apply_owner_conduit_spell_bonus(game, item);
        auto h = handlers_.find(item.effect_key);
        if (h == handlers_.end()) {
            game.end_stack_effect_resolution();
            continue;
        }
        auto res = h->second(game, item);
        if (res.ok) {
            mark_resolved_stack_item(game, item);
            // Chain propagation (mirrors resolve_top chain logic).
            if (item.chain && !item.target_entity_id.empty()) {
                const auto init_it = game.board.all_entities_map.find(item.target_entity_id);
                if (init_it != game.board.all_entities_map.end() && init_it->second && init_it->second->position) {
                    std::set<std::string> visited;
                    std::vector<std::shared_ptr<Entity>> frontier;
                    visited.insert(init_it->second->entity_id);
                    frontier.push_back(init_it->second);
                    for (std::size_t qi = 0; qi < frontier.size(); ++qi) {
                        for (const auto& [cx, cy] : entity_surrounding_cells(*frontier[qi])) {
                            auto neighbor = game.board.entity_at(cx, cy);
                            if (!neighbor || visited.count(neighbor->entity_id)) continue;
                            if (!neighbor->position) continue;
                            if (neighbor->entity_type != "unit" && !entity_is_structure(*neighbor)) continue;
                            if (!board_target_allows(game, item.board_target_kind, item.controller_id, *neighbor)) continue;
                            if (!board_target_entity_allowed_for_effect(*neighbor, item.board_target_kind, item.effect_key)) {
                                continue;
                            }
                            const auto nb_it = game.board.all_entities_map.find(neighbor->entity_id);
                            if (nb_it == game.board.all_entities_map.end() || !nb_it->second) continue;
                            visited.insert(nb_it->second->entity_id);
                            frontier.push_back(nb_it->second);
                            StackItem chain_item = item;
                            chain_item.chain = false;
                            chain_item.consumes_attack_action = false;
                            chain_item.target_entity_id = nb_it->second->entity_id;
                            chain_item.targets[effect_keys::kCellX] = neighbor->position->first;
                            chain_item.targets[effect_keys::kCellY] = neighbor->position->second;
                            h->second(game, chain_item);
                        }
                    }
                }
            }
        }
        game.fire_pending_reactives();
        game.end_stack_effect_resolution();
    }
    game.pop_stack_batch_resolution();
    passes_ = 0;
}

}  // namespace tactics
