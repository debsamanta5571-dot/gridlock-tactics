#pragma once

#include "tactics/actions/actions.hpp"
#include "tactics/board/board.hpp"
#include "tactics/board/aether.hpp"
#include "tactics/board/scanner.hpp"
#include "tactics/board/omni_energy_tile.hpp"
#include "tactics/board/board_layout.hpp"
#include "tactics/cards/card_catalog.hpp"
#include "tactics/cards/card_runtime.hpp"
#include "tactics/cards/cards.hpp"
#include "tactics/combat/ability_resolve_viz.hpp"
#include "tactics/core/stack.hpp"
#include "tactics/core/turn_manager.hpp"
#include "tactics/sync/match_sync.hpp"
#include "tactics/energy/energy_zone.hpp"

#include <nlohmann/json_fwd.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace tactics {

/**
 * Explicit match mode. Set via `GameState::set_game_mode()` before `add_player()`.
 * When set to anything other than `Default`, this takes precedence over game_id inspection.
 */
enum class GameMode {
    Default,       ///< Infer deck type from game_id substrings (backward compat)
    Sandbox,       ///< Sandbox omni-zone deck (all catalog cards, 1 copy each)
    FootprintTest, ///< Footprint-test deck (special multi-tile units)
    TestDeck,      ///< Random test deck for unit tests
    LiveMatch,     ///< Load deck from active match deck list (explicit override)
};

/// Snapshot round-trip helpers for GameMode.
inline std::string game_mode_to_string(GameMode m)
{
    switch (m) {
        case GameMode::Sandbox:       return "sandbox";
        case GameMode::FootprintTest: return "footprint_test";
        case GameMode::TestDeck:      return "test_deck";
        case GameMode::LiveMatch:     return "live_match";
        default:                      return "default";
    }
}
inline GameMode game_mode_from_string(const std::string& s)
{
    if (s == "sandbox")        return GameMode::Sandbox;
    if (s == "footprint_test") return GameMode::FootprintTest;
    if (s == "test_deck")      return GameMode::TestDeck;
    if (s == "live_match")     return GameMode::LiveMatch;
    return GameMode::Default;
}

/** In-progress move for one seat: goal cell, resolved anchor, optional rotation before confirm. */
struct PendingMoveSelection {
    int player_id{0};
    std::string unit_entity_id;
    int goal_x{};
    int goal_y{};
    int resolved_ax{};
    int resolved_ay{};
    int quarter_turns_cw{0};
};

/** Peeked top-of-deck cards awaiting discard choices from a `scan` effect. */
struct PendingScanSelection {
    int player_id{0};
    /** Top-of-deck order: index 0 is the top card. Cards remain in the deck until discarded. */
    std::vector<CardInstanceId> peeked;
};

/** Territory enter/groundwork effects that need a player-chosen target (e.g. "enter: grant 1/1
 *  to any unit"). Queued FIFO when a territory is conquered; each resolves once a target is picked. */
struct PendingTerritoryTarget {
    int player_id{0};
    std::vector<TerritoryEffect> effects;
};

/** Optional "discard a card to draw a card" choice from a territory enter effect. */
struct PendingTerritoryLoot {
    int player_id{0};
};

/** Authoritative match: board, players, stack, turns, and objectives. */
class GameState {
public:
    /**
     * @param rng_seed If set, all shuffles/deck order are reproducible; if not, seed from `std::random_device`.
     * @throws std::invalid_argument if `width` is less than 4 or `height` cannot fit two 2-row base pads.
     * @throws std::runtime_error if the board module cannot be created.
     */
    /** Default rules match on the standard 8x12 map with top/bottom bases. */
    GameState(std::string game_id, std::optional<uint64_t> rng_seed = std::nullopt);
    GameState(std::string game_id, int width, int height, std::optional<uint64_t> rng_seed = std::nullopt);
    GameState(std::string game_id, BoardLayoutSpec layout, std::optional<uint64_t> rng_seed = std::nullopt);
    /** Full map width/height (8x12 default: 4x2 base + 8-row arena + 4x2 base). CLI uses this for 1-based addressing. */
    int board_width() const { return board_width_; }
    int board_height() const { return board_height_; }
    const std::string& game_id() const noexcept { return game_id_; }
    BoardCellBounds board_cell_bounds() const { return board.cell_bounds(); }
    const BoardLayoutSpec& board_layout() const { return layout_spec_; }
    /** True when every footprint cell at `anchor` lies in this player's deployment zone (if the layout defines one),
     *  or when a friendly Command unit on the board can sponsor this deploy (unit_cost >= 0 and Command X >= unit_cost). */
    bool can_deploy_entity_at(int player_id, const std::shared_ptr<Entity>& entity, int anchor_x, int anchor_y,
        int unit_cost = -1) const;
    bool is_deploy_zone_cell_for_player(int player_id, int x, int y) const;
    /** Add another grid module; see `GameBoard::add_modular_grid`. */
    bool add_board_module(const std::string& module_id, int width, int height, int offset_x = 0, int offset_y = 0) {
        return board.add_modular_grid(module_id, width, height, offset_x, offset_y);
    }
    bool add_board_cell_module(const std::string& module_id, const std::vector<std::pair<int, int>>& world_cells) {
        return board.add_cell_module(module_id, world_cells);
    }
    std::mt19937& rng();
    const std::mt19937& rng() const;
    /** Override deck-type selection before add_player(). Must be called before add_player(). */
    void set_game_mode(GameMode mode) { game_mode_ = mode; }
    GameMode game_mode() const { return game_mode_; }
    /** Pre-match setting: when false, the going-second seats do NOT receive the Field Requisition
     *  card at start. Transient (its effect - the injected card - is captured in the serialized deck). */
    void set_field_requisition_enabled(bool v) { field_requisition_enabled_ = v; }
    bool field_requisition_enabled() const { return field_requisition_enabled_; }
    void add_player(int player_id, const std::string& name);
    std::vector<CardInstanceId> draw_cards(int player_id, int amount = 1);
    std::optional<CardInstanceId> grant_temporary_hand_card(int player_id, const std::string& def_key,
        int hand_expires_after_owner_turn_ends);
    void expire_temporary_hand_cards_for_player(int player_id);
    std::vector<CardInstanceId> draw_unit_cards(int player_id, int amount = 1);
    std::vector<CardInstanceId> draw_spell_cards(int player_id, int amount, int max_total_cost);
    std::vector<CardInstanceId> draw_focus_spell_cards(int player_id, int amount, int max_total_cost);
    /** Move a card instance from any zone into that player's purgatory (exile-like, not drawable). */
    bool move_card_to_purgatory(int player_id, CardInstanceId id);
    bool move_card_to_purgatory_by_public_id(int player_id, const std::string& public_id);
    /** After requesting end turn with hand > kMaxHandSize: discard chosen cards (1-based index) until legal. */
    ActionResult discard_hand_card_at(int player_id, int idx_1based);
    /** True between "end turn" with oversized hand and completing discards. */
    bool IsAwaitingHandDiscard() const { return pending_discard_player_.has_value(); }
    /** Active player who must discard after end turn with hand size strictly greater than `kMaxHandSize`. */
    std::optional<int> PendingDiscardPlayerId() const { return pending_discard_player_; }
    bool IsPendingDiscardForPlayer(int player_id) const {
        return pending_discard_player_.has_value() && *pending_discard_player_ == player_id;
    }
    /** True while a `scan` effect waits for the controller to discard from peeked deck cards. */
    bool IsAwaitingScan() const { return pending_scan_.has_value(); }
    std::optional<int> PendingScanPlayerId() const {
        return pending_scan_ ? std::optional<int>{pending_scan_->player_id} : std::nullopt;
    }
    bool IsPendingScanForPlayer(int player_id) const {
        return pending_scan_.has_value() && pending_scan_->player_id == player_id;
    }
    const std::vector<CardInstanceId>* pending_scan_peeked_for(int player_id) const;
    /** Discard one peeked card (1-based index into the current peek list). */
    ActionResult scan_discard_at(int player_id, int idx_1based);
    /** Keep all remaining peeked cards on top of the deck and continue resolution. */
    ActionResult scan_finish(int player_id);
    void complete_deferred_turn_draw_if_ready();

    // ── Conquering Territories: pending enter/groundwork target ────────────────────
    /** True while a placed territory has a targeted enter/groundwork effect awaiting a target. */
    bool IsAwaitingTerritoryTarget() const { return pending_territory_target_.has_value(); }
    std::optional<int> PendingTerritoryTargetPlayerId() const {
        return pending_territory_target_ ? std::optional<int>{pending_territory_target_->player_id} : std::nullopt;
    }
    bool IsPendingTerritoryTargetForPlayer(int player_id) const {
        return pending_territory_target_.has_value() && pending_territory_target_->player_id == player_id;
    }
    /** Effect key of the front pending territory target (for a UI prompt); empty when none. */
    std::string pending_territory_target_effect_key(int player_id) const;
    /** Front pending enter/groundwork effect, or null when none. */
    const TerritoryEffect* pending_territory_front_effect(int player_id) const;
    /** Resolve the front pending territory effect against the chosen target, then advance the queue. */
    ActionResult resolve_territory_target(int player_id, const std::map<std::string, int>& targets,
        const std::string& target_entity_id = {});
    /** Skip the front pending territory effect (it fizzles) - always available so play never stalls. */
    ActionResult skip_territory_target(int player_id);
    /** True while a territory enter effect awaits an optional discard-to-draw choice. */
    bool IsAwaitingTerritoryLoot() const { return pending_territory_loot_.has_value(); }
    std::optional<int> PendingTerritoryLootPlayerId() const {
        return pending_territory_loot_ ? std::optional<int>{pending_territory_loot_->player_id} : std::nullopt;
    }
    bool IsPendingTerritoryLootForPlayer(int player_id) const {
        return pending_territory_loot_.has_value() && pending_territory_loot_->player_id == player_id;
    }
    /** Discard one hand card (1-based index) to draw one card; opens only when loot is pending. */
    ActionResult territory_loot_discard_at(int player_id, int idx_1based);
    /** Decline the optional discard-to-draw and continue. */
    ActionResult territory_loot_skip(int player_id);
    void start_game();
    /** Place 4x2 bases on the bottom two rows (P1) and top two rows (P2) when absent. */
    void ensure_player_bases();
    /** Replace an existing seat's deck (and territory deck + opening hand) from a deck list - used when
     *  a networked client sends its chosen deck on join. No-op if the seat has not been added. */
    void set_player_deck_from_list(int player_id, const DeckListDefinition& deck_list);
    /** Re-apply the default 8x12 jigsaw if nominal size is standard but geometry is stale (e.g. solid 96-tile rect). */
    void repair_standard_duel_board_geometry_if_needed();
    ActionResult choose_energy_zone(int player_id, int choice_index);
    ActionResult skip_energy_zone(int player_id);
    /** Conquering Territories: resolve a freshly-placed territory - evaluate `groundwork` against
     *  the owner's previously-conquered territory, set the depleted/use state, fire enter and
     *  matched groundwork effects, and record this as the new last-conquered territory. */
    /** Returns false when the territory is destroyed on enter (e.g. groundwork not met). */
    bool on_territory_conquered(int player_id, EnergyZone& placed);
    /** Activate one of a placed territory's "use land" abilities (0-based indices). Spends the
     *  shared per-turn use and the ability cost, credits any produced energy, and resolves its
     *  effect (with an optional target unit for effects that require one). */
    ActionResult use_land(int player_id, int territory_index, int ability_index,
        const std::map<std::string, int>& targets = {}, const std::string& target_entity_id = {});
    ActionResult perform_action(int player_id, GameAction& action);
    bool end_current_turn();
    /** True when `player_id` may pass in the current phase (main, reaction windows). */
    bool can_pass_priority(int player_id) const;
    ActionResult pass_priority(int player_id);
    /** True when player spells/abilities use the phase batch queue (normal play). */
    bool uses_phase_batching() const;
    /** True when movement may cancel+refund stale queued batch items (not during stack resolve). */
    bool allows_queued_batch_invalidation_refund() const;
    void push_stack_batch_resolution();
    void pop_stack_batch_resolution();
    ActionResult queue_batched_spell(StackItem item);
    const StackItem* find_batched_item(const std::string& item_id) const;
    bool remove_batched_item_by_id(const std::string& item_id);
    /** Per-phase unit batch: one queued non-focus attack/ability per unit; focus stack unlimited; focus+attack/ability coexist; move blocked after any queue; burst is instant (exempt). Locks reset on phase transitions and after batch resolve. */
    bool unit_may_move_this_phase(const std::string& entity_id) const;
    bool unit_may_queue_focus_spell_this_phase(const std::string& caster_entity_id) const;
    bool unit_may_queue_non_focus_batch_action_this_phase(const std::string& entity_id) const;
    /** True when `player_id` may pop and reverse their most recent undoable action this phase. */
    bool can_undo_last_action(int player_id) const;
    /** Reverse the latest undoable action for `player_id` (LIFO per player). */
    ActionResult undo_last_action(int player_id);
    /** Cancel a queued batch item owned by `player_id` (card/energy/stockpile refund). */
    ActionResult cancel_queued_batch_item_for_player(int player_id, const std::string& item_id);
    void clear_phase_undo_stack();
    void clear_phase_undo_for_player(int player_id);

    // ── Match settings ────────────────────────────────────────────────────────
    /**
     * Per-match, synced rules/QoL toggles. Part of the authoritative match state (serialized in
     * match_snapshot), so all networked clients agree. Add new toggles here; default off keeps
     * existing matches behaving as before.
     */
    struct MatchSettings {
        /** When true, a deployment may be undone like a batched action (off = deploy seals the turn). */
        bool allow_deployment_undo{false};
    };
    MatchSettings match_settings{};
    bool allow_deployment_undo() const { return match_settings.allow_deployment_undo; }
    void set_allow_deployment_undo(const bool v) { match_settings.allow_deployment_undo = v; }
    /** Independent aether clusters - each tracks cells, scaling, and per-round fire state. */
    std::vector<AetherClusterState> aether_clusters_{};
    /** Independent scanner tiles - each tracks home seat and per-round team fire state. */
    std::vector<ScannerClusterState> scanner_clusters_{};
    /** Independent omni-energy tiles - each tracks home seat and per-round team fire state. */
    std::vector<OmniEnergyClusterState> omni_energy_clusters_{};

    // ── Main Phase ───────────────────────────────────────────────────────────
    /**
     * End the Main Phase and open SpellWindow (if spells queued) or AttackDeclaration
     * (from Main), or the end-of-turn path (from SecondMain).
     * Use `end_current_turn()` from Main Phase to skip the attack phase entirely.
     */
    ActionResult end_main_phase(int player_id);
    /** True when player_id may pass the spell reaction window. */
    bool can_pass_spell_window(int player_id) const;
    /** Pass / forfeit in the spell reaction window. */
    ActionResult pass_spell_window(int player_id);

    // ── Attack Declaration Phase ─────────────────────────────────────────────
    /** Queue an attack for resolution after the Defense window closes. */
    ActionResult declare_attack(int player_id, const std::string& attacker_id, int target_x, int target_y, bool ranged);
    /** Remove a previously declared attack (only while still in AttackDeclaration phase). */
    ActionResult undeclare_attack(int player_id, const std::string& attacker_id);
    /** Lock declarations and open the Defense window (or end turn if queue is empty). */
    ActionResult commit_attack_declaration(int player_id);

    // ── Defense Phase ────────────────────────────────────────────────────────
    /**
     * Pass / forfeit the defense window for `player_id`.
     * First pass with nothing played = forfeit (skipped on future rounds).
     * When forfeited_count >= total_players - 1 the window closes automatically.
     */
    ActionResult pass_defense_window(int player_id);
    /** True when `player_id` may pass the defense window. */
    bool can_pass_defense_window(int player_id) const;
    /** True when the unified phase batch queue contains at least one attack entry. */
    bool has_pending_attacks_in_queue() const;
    /** True when `attack_commit` would open Defense/BonusDefense (attacks in the open batch group). */
    bool would_attack_commit_open_defense_window() const;
    /** True when a forfeit pass with nothing played would close the defense window. */
    bool would_pass_close_defense_window(int player_id) const;

    /** A single entry in the unified phase batch queue (attack or spell/ability). */
    struct AttackDeclaration {
        std::string attacker_id;
        int target_x{0};
        int target_y{0};
        bool ranged{false};
        /** True when declare consumed an unused standard move (restored on undeclare). */
        bool consumed_move_on_declare{false};
        int undo_moves_remaining{0};
        int undo_standard_moves_remaining{0};
    };

    // ── Combat visualization pause (Unreal combat flash) ───────────────────────
    /** When true, the phase batch queue pauses before each attack until resumed. */
    bool combat_visualization_enabled() const { return combat_visualization_enabled_; }
    void set_combat_visualization_enabled(bool enabled) { combat_visualization_enabled_ = enabled; }
    /** True while an attack or ability resolve is frozen awaiting `resume_combat_visualization()`. */
    bool is_combat_visualization_paused() const;
    enum class CombatVizPauseKind : std::uint8_t { None, Attack, Ability };
    CombatVizPauseKind combat_viz_pause_kind() const;
    /** Attack declaration for the encounter currently paused (before `resolve_attack`). */
    bool try_get_pending_combat_visualization_attack(AttackDeclaration& out) const;
    /** Ability preview for the stack item currently paused (before `resolve_item_direct`). */
    bool try_get_pending_ability_resolve_viz(AbilityResolveVizPreview& out) const;
    /** Paused spell/ability stack item (Blazing resolve viz, etc.). */
    bool try_get_pending_combat_visualization_spell(StackItem& out) const;
    /** Resolve the paused attack/ability and continue the batch queue (pauses again if more follow). */
    ActionResult resume_combat_visualization();
    /** Mid-animation: apply the paused ability, capture actual per-unit damage/heal, hold the queue. */
    ActionResult apply_paused_ability_visualization_step();
    /** After ability damage labels finish, continue the batch queue. */
    ActionResult continue_after_ability_visualization();
    bool is_ability_viz_holding_queue() const { return ability_viz_holding_queue_; }
    /** Pop hits recorded during the most recent `apply_paused_ability_visualization_step()`. */
    bool try_consume_last_ability_resolve_viz_hits(std::vector<AbilityResolveVizHit>& out);
    void note_ability_damage_dealt(const std::string& entity_id);
    /** Increment lifetime ability-damage total for Exalted gates when resolving an ability stack item. */
    void note_exalted_ability_damage_dealt(int damage);
    bool try_pause_for_blazing_ability_viz(const StackItem& item);

    /** Resolved primary-attack exchange stats for combat viz (floating damage, crit flags). */
    struct CombatVizEncounterResult {
        bool attack_was_crit{false};
        bool counter_was_crit{false};
        int attack_damage{0};
        int counter_damage{0};
    };
    void clear_last_combat_viz_encounter_result();
    void record_combat_viz_encounter_result(CombatVizEncounterResult result);
    bool try_get_last_combat_viz_encounter_result(CombatVizEncounterResult& out) const;

    // ── Passive attack visualization events (mortar shots etc.) ───────────────
    /** Logged by passive effects (mortar shots) when combat_visualization_enabled_ is true. */
    struct PassiveAttackVizEvent {
        std::string source_entity_id;   // building/entity that fired
        std::string target_entity_id;   // primary target hit
        int damage_to_primary{0};       // HP lost by primary target (0 if it died outright or was immune)
        std::string event_type;         // e.g. "mortar_shot"
    };
    /** True if any passive attack viz events are queued and not yet consumed. */
    bool has_pending_passive_attack_viz_events() const { return !passive_attack_viz_events_.empty(); }
    /** Pop and return the oldest queued passive attack viz event; returns false if empty. */
    bool try_consume_passive_attack_viz_event(PassiveAttackVizEvent& out);
    /** Log a passive attack viz event (called from effect handlers; no-op when viz disabled). */
    void log_passive_attack_viz_event(PassiveAttackVizEvent event);
    /** Clear all queued passive attack viz events (e.g. when combat viz is turned off). */
    void clear_passive_attack_viz_events() { passive_attack_viz_events_.clear(); }

    struct AttackPhaseEntry {
        bool is_attack{true};
        AttackDeclaration attack;   // valid when is_attack
        StackItem spell_item;       // valid when !is_attack
    };
    /** Spell/ability entries in the unified phase batch queue (flattened view). */
    std::vector<StackItem> pending_spell_declarations() const;
    /** Current open batch during Attack Declaration (unsealed group only). */
    std::vector<AttackPhaseEntry> attack_phase_queue() const;
    /** Full unified queue for snapshot / display. */
    const std::vector<AttackPhaseEntry>& phase_action_queue() const { return phase_action_queue_; }
    const std::vector<size_t>& phase_action_group_boundaries() const { return phase_action_group_boundaries_; }
    /** All attack entries in the unified phase batch queue (including sealed groups). */
    std::vector<AttackDeclaration> pending_attack_declarations() const {
        std::vector<AttackDeclaration> out;
        for (const auto& e : phase_action_queue_) {
            if (e.is_attack) {
                out.push_back(e.attack);
            }
        }
        return out;
    }
    const std::unordered_set<int>& reaction_window_forfeited() const { return reaction_window_forfeited_; }
    std::optional<int> reaction_window_priority_player() const { return reaction_window_priority_player_; }
    /** Recompute transient passive auras from living entities on the board. */
    void refresh_passive_auras();
    /** Mark auras stale so the next `refresh_passive_auras()` recomputes (no-op if already dirty). */
    void mark_passive_auras_dirty();
    /** Consume one use of an activated ability (after stack resolve). */
    void mark_ability_used_this_turn(const std::shared_ptr<Entity>& entity, const std::string& ability_key);
    /** Reset per-turn ability uses and barrage cast counts for one entity. */
    void refresh_ability_uses_for_entity(const std::shared_ptr<Entity>& entity);
    uint64_t bump_network_snap_seq();
    uint64_t network_snap_seq() const { return network_snap_seq_; }
    /** Host: record one committed CLI line; returns monotonic command sequence. */
    uint64_t record_authority_command(int seat, std::string line_utf8);
    uint64_t match_command_seq() const;
    const std::vector<MatchCommandEntry>& command_journal() const { return command_journal_; }
    void clear_command_journal();
    /** Attach a synced temporary effect and preserve marked damage across max-health changes. */
    void add_temporary_effect(const std::shared_ptr<Entity>& target, TemporaryEntityEffect effect);
    /** Decrement and remove temporary effects for the given turn event. */
    void expire_temporary_effects_for_turn(int player_id, const std::string& expire_on);
    /** Remove Vulnerable stacks applied via apply_vulnerable_turn_end when the active turn ends. */
    void expire_vulnerable_turn_end_for_turn(int turn_owner_id);
    void expire_active_turn_buffs_for_turn(int turn_owner_id);
    /** True when `player_id` has a living Ancient Frog with Reservoir storage room. */
    bool player_has_ancient_frog_storage_consumers(int player_id) const;
    /** Begin per-turn Ancient Frog storage split for `turn_owner_id` (owner_turn_end passive wave). */
    void begin_owner_turn_end_energy_storage(int turn_owner_id);
    /**
     * Ancient Frog share of this turn's drained float for `entity_id`.
     * Drains once per turn owner per wave; splits evenly among frogs with storage room
     * (turn-order rank gets +1 remainder pips).
     */
    int energy_storage_share_for_unit(int controller_id, const std::string& entity_id);

    /** Wave 1 of end-of-turn: heal all owned entities by their regen value (oldest-first). */
    void process_end_of_turn_regen(int player_id);
    /** Wave 2.5 of end-of-turn: apply effects from tile overlays (gas cloud etc.) to all owned entities (oldest-first). */
    void process_tile_overlay_effects(int player_id);
    /** Wave 3 of end-of-turn: resolve poison/fire/bleed/overload DOT for all owned entities (oldest-first). */
    void process_end_of_turn_dot(int player_id);
    /** Decay stealth and evasive stacks at the start of a player's turn. */
    void process_start_of_turn_status_effects(int player_id);
    /** Apply bleed damage after voluntary movement. */
    void process_bleed_after_movement(const std::shared_ptr<Entity>& entity);

    void mark_core_cracker_deployed(const std::shared_ptr<Unit>& unit);
    void apply_core_cracker_shutdown_at_turn_start(int owner_id);
    void clear_core_cracker_deploy_exempt_at_turn_end(int owner_id);

    /**
     * Apply overload stacks to a unit/structure; triggers explosion at threshold.
     * Returns a short log suffix (empty if nothing applied).
     */
    std::string apply_overload_stacks(const std::shared_ptr<Entity>& target, int amount,
                                      std::optional<int> applier_player_id = std::nullopt);

    /** When source dealt damage, apply overload/jammed from on-damage primers on source. */
    void apply_on_damage_dealt_primer_statuses(const Entity& source, const std::shared_ptr<Entity>& victim, int damage_dealt,
        bool from_basic_attack = false);

    void begin_stack_effect_resolution(const StackItem* item = nullptr);
    void end_stack_effect_resolution();
    bool is_resolving_ability_stack_effect() const;
    bool should_emit_ability_damage_popup() const;
    void log_ability_damage_popup(const Entity& target, int amount, bool is_heal);
    void enqueue_ability_damage_popup_event(AbilityResolveVizHit hit);
    const StackItem* resolving_stack_item_ptr() const;
    /** @deprecated Use log_ability_damage_popup; still fills viz capture when capturing. */
    void try_record_ability_resolve_viz_hit(const Entity& target, int amount, bool is_heal);
    bool has_pending_ability_damage_popups() const { return !ability_damage_popup_events_.empty(); }
    void backfill_pending_ability_damage_popup_labels(const std::string& label);
    bool has_ability_damage_popup_for_entity(const std::string& entity_id) const;
    bool try_consume_ability_damage_popup_event(AbilityResolveVizHit& out);
    void consume_next_ability_on_hit_primers_if_needed(const std::string& entity_id);

    /** Passive reactive rewards when source deals damage (e.g. Resonance Tithe). Source must still be alive. */
    void apply_passive_reactive_on_damage_dealt(const Entity& source, const Entity& victim, int damage_dealt);

    /** Passive reactives on victim when it takes damage (e.g. Shock Retaliation overload on melee attackers). */
    void apply_passive_reactive_on_damage_taken(const Entity& victim, Entity& attacker, bool from_melee, int damage_taken);

    /** Fire "covered_unit_died" reactive passives on any unit that was covering the dying entity. Call before removing the entity from the board. */
    void apply_passive_reactive_on_covered_unit_died(const Entity& dying_entity);
    /** Fires passives on the dying unit with reactive_trigger == "self_died" (before board removal). */
    void apply_passive_reactive_on_self_died(const Entity& dying_entity);
    /** Queue a post-removal death spawn (e.g. Vulturous Nanites token). */
    void queue_pending_death_spawn(const std::string& dying_entity_id, int player_id, int x, int y, int hp,
        int attack);

    /** Passive reactives on killer when it destroys a hostile enemy unit (e.g. Sanglante's Bloodlust). */
    void apply_passive_reactive_on_enemy_unit_killed(const Entity& killer, const Entity& victim);

    /**
     * Board-wide scan: for every entity with reactive_trigger == "ally_took_damage_nearby",
     * fires if `victim` is an ally within the passive's aura_range (Chebyshev) and the damage
     * came from a hostile source.  Used by Lady Concordia's Promise passive.
     */
    void apply_passive_reactive_on_ally_took_damage(const Entity& victim, const Entity& attacker, int damage);

    /** Spellbound: reactive passives on this player's units when they successfully play a spell. */
    void apply_passive_reactive_on_owner_spell_played(
        int player_id, std::optional<EffectSpeed> spell_speed = std::nullopt);

    /**
     * Reactive passives on allied entities when a damaging spell is queued (e.g. Mana Pylon).
     * `batch_item_id` identifies the queued spell copy to receive per-cast ability_damage_bonus.
     */
    void apply_passive_reactive_on_allied_damaging_spell_played(
        int caster_player_id, const std::string& effect_key, const std::string& batch_item_id);

    /** Adjust queued phase-batch spell damage bonus (returns false if item not found). */
    bool adjust_batched_item_ability_damage_bonus(const std::string& item_id, int delta);

    /**
     * Fire all pending deferred reactives, then clear the list.
     * Each reactive only fires if its source is still alive on the board.
     * Call after the full exchange resolves (post-counterattack for attacks,
     * post-handler for ability stack items). Skips passives with
     * `reactive_string_payload.source_survive_until == "phase_resolution"`.
     */
    void fire_pending_reactives();

    /**
     * Fire phase-deferred reactives (e.g. tithe passives) after the batched phase queue
     * fully resolves. Each reactive only fires if its source is still alive.
     */
    void fire_phase_pending_reactives();

    /** Queue a deferred reactive for fire_pending_reactives(). Called by apply_damage_packet. */
    void queue_pending_reactive(const std::string& source_id, const Entity& victim_snapshot, int damage_dealt);

    /** Queue a reactive that requires the source to survive until phase batch resolution completes. */
    void queue_phase_pending_reactive(const std::string& source_id, const Entity& victim_snapshot, int damage_dealt);

    /** True when any passive on this unit defers until phase resolution (`source_survive_until`). */
    bool entity_has_phase_survive_damage_reactive(const Entity& source) const;

    /** True while `execute_phase_action_queue()` is draining the batched phase queue. */
    bool is_draining_phase_action_queue() const;

    bool has_pending_move_for(int player_id) const;
    std::optional<PendingMoveSelection> get_pending_move_for(int player_id) const;
    /** Board unit, or a copy at pending move destination when this unit has a move preview. */
    std::shared_ptr<Unit> unit_at_validation_pose(const std::shared_ptr<Unit>& unit) const;
    void clear_pending_move_for(int player_id);
    /** Keeps an in-progress move preview when re-selecting that unit; clears preview when selecting another. */
    void reconcile_pending_move_for_unit_selection(int player_id, const std::string& unit_entity_id);
    ActionResult apply_move_preview(int player_id, const std::shared_ptr<Unit>& unit, int goal_x, int goal_y);
    ActionResult apply_pending_move_rotation_delta(int player_id, int delta_quarters_cw);
    ActionResult confirm_pending_move(int player_id);
    /** Apply terrain effects after forced movement/pushes. Removes non-flying units that fall into void. */
    ActionResult resolve_terrain_after_forced_movement(const std::shared_ptr<Entity>& entity);

    /** Remove an entity from the board and return its unit card to discard when appropriate. */
    void destroy_board_entity(const std::shared_ptr<Entity>& entity);
    void register_unit_deployed(int player_id, CardInstanceId card, const std::shared_ptr<Entity>& entity);
    /** Assign a unique `entity_id` suffix via `next_entity_spawn_sequence_` (never reuses map size). */
    void assign_monotonic_entity_id(const std::shared_ptr<Entity>& entity, const std::string& prefix);
    /** Assigns spawn_sequence for passive action ordering when an entity enters the board. */
    void note_entity_placed(const std::shared_ptr<Entity>& entity);

    /** Full authoritative match JSON for multiplayer (UTF-8). Includes board, decks, hands, zones, stack, turn. */
    std::string build_match_snapshot_utf8() const;
    /** Restore rules state from host snapshot. Requires matching board dimensions (subsystem recreates GameState if needed). */
    bool apply_match_snapshot_utf8(const std::string& utf8, std::string& err);
    /** Same restore from a pre-parsed snapshot DOM (skips the JSON parse - hot in bot search cloning). */
    bool apply_match_snapshot_json(const nlohmann::json& root, std::string& err);

    GameBoard board;
    TurnManager turn_manager;
    StackManager stack_manager;
    std::unordered_map<int, Deck> players_decks;
    std::unordered_map<int, std::vector<CardInstanceId>*> players_hands;
    std::unordered_map<int, std::vector<EnergyZone>> players_energy_zones;
    std::unordered_map<int, EnergyZoneDeck> players_energy_zones_decks;
    /** Conquering Territories: the last territory each player conquered, for `groundwork` matching. */
    std::unordered_map<int, ConqueredTerritoryMemory> last_conquered_territory;
    /** Seat → team id. Missing keys behave as `team_of_seat(seat)==seat` (legacy FFA). */
    std::unordered_map<int, int> seat_team_id;

    int team_of_seat(int seat) const;
    void set_seat_team(int seat, int team_id);

private:
    bool passive_auras_dirty_{true};
    GameMode game_mode_{GameMode::Default};
    bool field_requisition_enabled_{false};
    void register_default_effects();
    void apply_layout_spec(const BoardLayoutSpec& layout);
    void notify_unit_card_destroyed(const std::shared_ptr<Entity>& entity);
    bool record_spawned_token_to_purgatory(int player_id, const Entity& entity);
    void return_spawned_card_to_discard_pile(int player_id, const std::string& card_id);
    void rebuild_living_tokens_from_board();

    /** Per player: how many board tokens are still alive for each deployed card_id. */
    std::unordered_map<int, std::unordered_map<std::string, int>> living_tokens_by_card_id_;

    void flush_pending_death_spawns_for(const std::string& dying_entity_id);
    struct PendingDeathSpawn {
        std::string dying_entity_id;
        int player_id{0};
        int x{0};
        int y{0};
        int hp{0};
        int attack{0};
    };
    std::vector<PendingDeathSpawn> pending_death_spawns_;
    struct VulnerableTurnEndEntry {
        int turn_owner_id{0};
        std::string entity_id;
        int amount{0};
    };

    struct ActiveTurnBuffEntry {
        int turn_owner_id{0};
        std::string entity_id;
        std::string effect_id;
    };

    struct EnergyStorageTurnSplit {
        int turn_owner_id{-1};
        bool drained{false};
        std::unordered_map<std::string, int> share_by_entity_id;
    };
    std::optional<EnergyStorageTurnSplit> energy_storage_turn_split_;

    struct PendingReactive {
        std::string source_id;
        Entity victim_snapshot;
        int damage_dealt{0};
    };
    std::vector<PendingReactive> pending_reactives_;
    std::vector<PendingReactive> phase_pending_reactives_;
    std::vector<VulnerableTurnEndEntry> vulnerable_turn_end_pending_;
    std::vector<ActiveTurnBuffEntry> active_turn_buff_pending_;
    std::unordered_set<std::string> ability_damage_dealt_this_stack_resolution_;
    int stack_batch_resolution_depth_{0};

    // ── Spell / Attack / Defense phase state ─────────────────────────────────
    /** Unified batch queue: FIFO within a group, LIFO across groups on resolve. */
    std::vector<AttackPhaseEntry> phase_action_queue_;
    std::vector<size_t> phase_action_group_boundaries_;
    std::unordered_set<std::string> attack_declared_unit_ids_;
    struct UnitPhaseBatchState {
        int focus_spell_count{0};
        bool has_non_focus_batch{false};
    };
    /** Tracks unit commitments for entries queued since the current phase began. */
    std::unordered_map<std::string, UnitPhaseBatchState> unit_phase_batch_state_;
    size_t phase_action_queue_epoch_{0};
    void reset_unit_phase_batch_state_for_new_phase();
    void refresh_unit_phase_batch_state_from_queue();
    void note_unit_focus_spell_queued(const std::string& entity_id);
    void note_unit_non_focus_batch_queued(const std::string& entity_id);
    /**
     * Shared reaction-window state used for the Defense phase.
     * Only one reaction window is ever open at a time so reuse is safe.
     */
    std::unordered_set<int> reaction_window_forfeited_;
    std::optional<int> reaction_window_priority_player_;
    bool reaction_window_played_this_turn_{false};
    /** True once BonusAttackDeclaration has been entered this turn; prevents re-entry. */
    bool bonus_attack_phase_used_this_turn_{false};

    enum class DeferredPhaseTransitionAfterQueue : std::uint8_t {
        None,
        SpellWindowClose,
        DefenseWindowClose,
        CommitWithoutDefenseWindow,
    };
    struct CombatVisualizationPauseState {
        bool draining{false};
        bool awaiting_resume{false};
        bool resume_next_attack{false};
        bool resume_next_spell{false};
        CombatVizPauseKind pause_kind{CombatVizPauseKind::None};
        size_t next_resolve_index{0};
        std::vector<size_t> resolve_order;
        bool is_bonus{false};
        bool queue_batch_pushed{false};
        bool pending_spell_outside_queue{false};
        AttackDeclaration pending_attack{};
        StackItem pending_spell{};
        AbilityResolveVizPreview pending_ability_viz{};
        DeferredPhaseTransitionAfterQueue deferred_transition{DeferredPhaseTransitionAfterQueue::None};
        bool deferred_was_second_spell{false};
        bool deferred_was_bonus_defense{false};
    };
    CombatVisualizationPauseState combat_viz_pause_{};
    bool combat_visualization_enabled_{true};
    bool ability_resolve_viz_capturing_{false};
    bool ability_viz_holding_queue_{false};
    std::vector<AbilityResolveVizHit> ability_resolve_viz_capture_buffer_{};
    std::vector<AbilityResolveVizHit> last_ability_resolve_viz_hits_{};
    std::vector<AbilityResolveVizHit> ability_damage_popup_events_{};
    std::optional<StackItem> resolving_stack_item_{};
    CombatVizEncounterResult last_combat_viz_encounter_result_{};
    bool has_last_combat_viz_encounter_result_{false};
    std::vector<PassiveAttackVizEvent> passive_attack_viz_events_;

    /** Transition to the Second Main Phase (called after Defense or empty Attack commit). */
    void begin_second_main_phase();
    /** Transition to Bonus Attack Declaration, or end the turn if no bonus attacks remain. */
    void begin_bonus_attack_phase_or_end_turn();
    /** Open the Bonus Attack Declaration phase (active player has bonus attacks remaining). */
    void begin_bonus_attack_phase();
    /** Returns true if the active player has any units with bonus attacks remaining this turn. */
    bool active_player_has_bonus_attacks() const;
    void close_spell_window();
    void close_defense_window();
    size_t phase_action_current_group_start() const;
    void append_phase_spell(StackItem item);
    void append_phase_attack(AttackDeclaration attack);
    void seal_phase_action_group();
    void seal_phase_action_group_if_non_empty();
    /** @return true when the queue fully drained; false when paused for combat visualization. */
    bool execute_phase_action_queue(bool is_bonus = false);
    void finalize_phase_action_queue_teardown();
    void apply_deferred_phase_transition_after_queue();
    void try_resume_paused_phase_queue_after_scan();

    enum class PhaseUndoKind : std::uint8_t {
        QueuedBatchItem,
        AttackDeclaration,
        MovePreview,
        MoveRotation,
        MoveConfirm,
        Defend,
        Dash,
        Recover,
        Deploy,
    };
    struct PhaseUndoEntry {
        int player_id{0};
        /** Open `phase_action_group` index when this entry was recorded (reaction-window batch scope). */
        size_t phase_group_start{0};
        PhaseUndoKind kind{};
        std::string batch_item_id;
        /** Additional batch ids from the same Multicast cast (undo/cancel removes all). */
        std::vector<std::string> extra_batch_item_ids{};
        CardInstanceId spell_card{};
        bool spell_from_reserves{false};
        bool spell_had_stockpile{false};
        int spell_stockpile_remaining{0};
        bool spell_stockpile_used_this_turn{false};
        bool spell_stockpile_double_play_used{false};
        EnergySpendRecord energy{};
        std::string unit_entity_id;
        int attack_target_x{0};
        int attack_target_y{0};
        bool attack_ranged{false};
        std::optional<PendingMoveSelection> prior_pending;
        int prior_rotation{0};
        std::pair<int, int> old_anchor{};
        std::vector<std::pair<int, int>> old_shape;
        int old_moves_remaining{0};
        int old_standard_moves_remaining{0};
        bool old_has_moved{false};
        int old_attacks_remaining{0};
        bool old_has_attacked{false};
    };
    std::vector<PhaseUndoEntry> phase_undo_stack_;
    void push_phase_undo(PhaseUndoEntry entry);
    ActionResult apply_phase_undo_entry(const PhaseUndoEntry& entry);
    void record_phase_undo_after_action(int player_id, GameAction& action, const ActionResult& result,
        const std::map<EnergyType, int>& cost, const std::optional<EnergySpendRecord>& energy_record);
    void clear_phase_undo_on_phase_transition();
    bool phase_undo_entry_eligible(const PhaseUndoEntry& entry, int player_id) const;
    void prune_sealed_phase_undo_entries();
    /** Drop queued spells/abilities (with energy/card refund) that no longer resolve after board movement. */
    int cancel_stale_queued_batch_items_after_entity_move(const std::string& moved_entity_id);
    bool queued_batch_item_still_valid(const StackItem& item) const;
    /** Drop undo record for a fizzled batch item (no card/energy refund). Used during batch drain. */
    void discard_queued_batch_item_undo_record(const std::string& item_id);
    /** Refund card/energy from the undo record; does not remove the queue entry. Pre-resolution only. */
    bool refund_queued_batch_item_by_id(const std::string& item_id);
    bool cancel_queued_batch_item_with_refund(const std::string& item_id);

    std::optional<int> pending_discard_player_{};
    std::optional<PendingScanSelection> pending_scan_{};
    std::optional<PendingTerritoryTarget> pending_territory_target_{};
    std::optional<PendingTerritoryLoot> pending_territory_loot_{};
    std::unordered_map<int, PendingMoveSelection> pending_moves_{};
    uint64_t network_snap_seq_{0};
    uint64_t match_next_command_seq_{1};
    std::vector<MatchCommandEntry> command_journal_;
    std::string game_id_;
    BoardLayoutSpec layout_spec_{};
    int board_width_{kStandardBoardWidth};
    int board_height_{kStandardBoardHeight};
    std::mt19937 rng_{};
    uint64_t next_entity_spawn_sequence_{1};
};

inline bool teams_hostile(const GameState& game, int seat_a, int seat_b)
{
	return game.team_of_seat(seat_a) != game.team_of_seat(seat_b);
}

/** Sum of Conduit keyword values on all living units and structures owned by `owner_seat`. */
int owner_conduit_total(const GameState& game, int owner_seat);

bool board_target_allows(const GameState& game, BoardTargetKind kind, int actor_seat, const Entity& target);

/** True when an enemy cannot directly target this entity (attacks / single-target abilities) because of stealth. */
bool enemy_direct_target_blocked_by_stealth(const GameState& game, int actor_seat, const Entity& target);

inline BoardTargetKind spell_board_target_kind(const CardDefinition& def)
{
    return definition_spell_board_target_kind(def);
}

/**
 * F6: Snapshot version changelog.
 * v1 - initial format; single deck list per player, no stack serialization
 * v2 - added stack_manager state, per-player energy zone decks, pending_moves dict (replaces single pending_move)
 * v3 - flat aether fields (`aether_damage_next`, `aether_last_sole_control_team`, `aether_teams_fired_this_round`)
 * v4 - `aether_clusters[]` per-cluster state (migrates v3 center cluster automatically)
 * v5 - `scanner_clusters[]` per-tile state (standard duel seeds when absent on load)
 * v6 - `omni_energy_clusters[]` per-tile state (standard duel seeds when absent on load)
 * When adding new fields: bump kMatchSnapshotVersion, add a migration branch in apply_match_snapshot_utf8,
 * and document the change here.
 */
inline constexpr int kMatchSnapshotVersion = 6;
/**
 * Network wire version changelog.
 * v1 - initial wire protocol
 * v2 - added snap_seq to "snap" frames
 * v3 - added command journal fields
 * v4 - cli auth digest is HMAC-SHA256 over "v2|nonce|seat|ctr|line" (welcome carries auth_nonce; cli carries ctr); cmd frames unsigned
 */
inline constexpr int kNetworkWireVersion = 4;

/** Wraps `build_match_snapshot_utf8()` output as a typed wire frame `{ "t":"snap", "v":4, "snap_seq":N, "payload": {...} }`. */
std::string wrap_match_snapshot_for_network_utf8(const std::string& snapshot_inner_utf8, uint64_t snap_seq);

/** Parse header + body and allocate a new `GameState` restored from a host snapshot. */
bool load_game_from_snapshot_utf8(std::unique_ptr<GameState>& out, const std::string& utf8, std::string& err);

/** Same as `load_game_from_snapshot_utf8` but from a pre-parsed inner snapshot DOM  - 
 *  bot search clones per simulation, so skipping the JSON re-parse matters. */
bool load_game_from_snapshot_json(std::unique_ptr<GameState>& out, const nlohmann::json& root, std::string& err);

/** Replace `game_slot` from wire JSON (`{ "t":"snap", "payload": ... }` or raw inner snapshot). */
bool replace_game_from_wire_utf8(std::unique_ptr<GameState>& game_slot, const std::string& wire_utf8, std::string& err);

/** Parse snap envelope metadata without replacing game state. */
bool peek_wire_snap_seq_utf8(const std::string& wire_utf8, std::optional<uint64_t>& out_snap_seq, std::string& err);

/** Unwrap `{ "t":"snap", ... }` to inner snapshot JSON; raw inner snapshots pass through. */
bool unwrap_snap_wire_utf8_for_replace(const std::string& wire, std::string& inner_snapshot_out, std::optional<uint64_t>& out_snap_seq,
                                       std::string& err);

/** Patch `base_inner_utf8` with RFC 6902 `delta_json_utf8` and apply to `game`. */
bool apply_match_snapshot_delta_utf8(GameState& game, const std::string& base_inner_utf8, const std::string& delta_json_utf8,
    std::string& err);

/** Apply `{ "t":"snap_delta", ... }` by patching `base_inner_utf8` (must match `base_seq`). */
bool apply_snap_delta_wire_utf8(GameState& game, const std::string& wire_utf8, const std::string& base_inner_utf8, uint64_t base_seq,
    std::string& err);

}  // namespace tactics
