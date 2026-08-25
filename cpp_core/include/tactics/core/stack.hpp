#pragma once

#include "tactics/common/types.hpp"

#include <functional>
#include <map>
#include <optional>
#include <unordered_map>
#include <vector>

namespace tactics {

class GameState;

/** One spell or ability waiting to resolve on the shared stack. */
struct StackItem {
    std::string item_id;
    std::string source_type;
    std::string source_name;
    std::string source_entity_id{};
    /** Set for activated abilities; consumed when the stack item resolves. */
    std::string source_ability_key{};
    int controller_id{0};
    std::string effect_key;
    EffectSpeed speed{EffectSpeed::Channeled};
    std::map<std::string, int> payload{};
    /** String-valued payload fields (e.g. "shape" for directional_damage). */
    std::map<std::string, std::string> string_payload{};
    /** Float-valued payload fields (e.g. movement multipliers). Use payload_float() for type-safe access. */
    std::map<std::string, float> float_payload{};
    std::map<std::string, int> targets{};
    std::string target_entity_id{};
    std::string target_stack_item_id{};
    BoardTargetKind board_target_kind{BoardTargetKind::Enemy};
    bool pierces_damage_prevention{false};
    /** When true, HP removed by this item's damage effect heals `source_entity_id` (ability keyword or unit lifesteal). */
    bool heals_on_damage_dealt{false};
    /** When true, post-mitigation damage dealt heals an allied base (`soul_steal_heal_base_entity_id`). */
    bool heals_allied_base_on_damage_dealt{false};
    std::string soul_steal_heal_base_entity_id;
    /** When true, resolving this item spends one attack action from `source_entity_id` (unit). */
    bool consumes_attack_action{false};
    /** Board target must match at least one type (OR); empty = no restriction. */
    std::vector<std::string> require_target_unit_types{};
    std::vector<std::string> bonus_damage_unit_types{};
    int bonus_damage_amount{0};
    /**
     * Extra flat damage added to all damaging ability effects in this stack item.
     * Populated at dispatch time from the source entity's `aura_bonus_ability_damage`.
     * Unlike `bonus_damage_amount`, this always applies regardless of target unit type.
     */
    int ability_damage_bonus{0};
    /** When true, only playable on the controller's turn (including Fast/Burst). */
    bool played_from_reserves{false};
    /**
     * Chain: after the primary effect resolves, BFS flood-fill through connected entities
     * (8-directional adjacency) that satisfy `board_target_kind` and apply the same effect to each.
     * Each entity is hit at most once. Chain copies are never added to the stack - they resolve
     * immediately (burst-style). Does not re-spend the attack budget or mark ability-used again.
     */
    bool chain{false};
    /** When true, this item will not lock the source unit's per-phase batch slot. */
    bool no_phase_batch_lock{false};
    /** Shared id for all stack copies queued by one Multicast spell cast (undo/cancel as a group). */
    std::string multicast_cast_id{};
    /** Total deploy/cast energy of a batched spell (fixed cost + chosen X). Used by copy_allied_spell. */
    int batched_spell_total_cost{0};
};

/** Type-safe int payload read with fallback. */
inline int payload_int(const StackItem& item, const std::string& key, int fallback = 0)
{
    const auto it = item.payload.find(key);
    return it == item.payload.end() ? fallback : it->second;
}

/** Type-safe float payload read; falls back to int payload (cast), then to `fallback`. */
inline float payload_float(const StackItem& item, const std::string& key, float fallback = 0.0f)
{
    const auto fit = item.float_payload.find(key);
    if (fit != item.float_payload.end()) return fit->second;
    const auto iit = item.payload.find(key);
    if (iit != item.payload.end()) return static_cast<float>(iit->second);
    return fallback;
}

/** Type-safe string payload read with fallback. */
inline const std::string& payload_string(const StackItem& item, const std::string& key)
{
    static const std::string kEmpty;
    const auto it = item.string_payload.find(key);
    return it == item.string_payload.end() ? kEmpty : it->second;
}

class StackManager {
public:
    using EffectHandler = std::function<ActionResult(GameState&, const StackItem&)>;
    /** Registers or replaces the handler for `key`. @return true if `key` was new, false if an existing handler was replaced. */
    [[nodiscard]] bool register_effect(const std::string& key, EffectHandler handler);
    bool has_effect_handler(const std::string& key) const;
    std::vector<std::string> registered_effect_keys() const;
    ActionResult add_item(GameState& game, const std::string& source_type, const std::string& source_name, int controller_id,
                          const std::string& effect_key, EffectSpeed speed, std::map<std::string, int> payload, std::map<std::string, int> targets,
                          BoardTargetKind board_target_kind = BoardTargetKind::Enemy, std::string target_entity_id = {});
    ActionResult add_item(GameState& game, const std::string& source_type, const std::string& source_name, int controller_id,
                          const std::string& effect_key, EffectSpeed speed, std::map<std::string, int> payload, std::map<std::string, int> targets,
                          BoardTargetKind board_target_kind, std::string target_entity_id, std::string target_stack_item_id);
    ActionResult add_item(GameState& game, StackItem item);
    /** Allocate a unique id for phase batch queue items. */
    std::string allocate_item_id();
    /** Legacy stack priority helpers (unused for player actions). */
    bool can_pass_priority(const GameState& game, int player_id) const;
    ActionResult pass_priority(GameState& game, int player_id);
    /**
     * Legacy: drain `stack` LIFO. Player actions use `GameState::execute_phase_action_queue()` instead.



     */
    void resolve_all(GameState& game);
    /**
     * Execute a single StackItem directly, without pushing to `stack`.
     * Used by `execute_phase_action_queue()` for batched spells/abilities and Burst effects.
     * and to execute queued attack declarations after the Defense window closes.
     * The item is NOT pushed onto `stack` - it resolves immediately.
     */
    ActionResult resolve_item_direct(GameState& game, const StackItem& item);
    bool has_stack_item(const std::string& item_id) const;
    const StackItem* find_stack_item(const std::string& item_id) const;
    bool remove_stack_item_by_id(const std::string& item_id);
    void open_main_window(int active_player);
    void clear_for_new_turn(std::optional<int> active_player);
    /** Seal the current player's group in a reaction window (call when they pass after playing). */
    void seal_reaction_group();
    /** Legacy main-window marker (empty optional if unset). Reaction windows use `GameState::reaction_window_priority_player_`. */
    std::optional<int> priority_player_id() const { return priority_player_; }
    std::vector<StackItem> stack;

    /** Multiplayer snapshot: capture internal priority/pass/counter with public stack copy. */
    void capture_network_snapshot(std::vector<StackItem>& out_stack, std::optional<int>& out_priority, int& out_passes, int& out_counter) const;
    void restore_network_snapshot(std::vector<StackItem> items, std::optional<int> priority, int passes, int counter);

private:
    ActionResult resolve_top(GameState& game);
    std::optional<int> next_player(const std::vector<int>& players, int current);
    std::unordered_map<std::string, EffectHandler> handlers_;
    std::optional<int> priority_player_{};
    int passes_{0};
    int counter_{0};
    // Group boundaries for reaction windows: each entry is the stack index where a new group starts.
    // Groups resolve LIFO; items within each group resolve FIFO.
    std::vector<size_t> reaction_group_boundaries_;
};

}  // namespace tactics
