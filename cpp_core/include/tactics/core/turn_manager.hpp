#pragma once

#include "tactics/board/board.hpp"
#include "tactics/common/types.hpp"
#include "tactics/energy/energy_zone.hpp"

#include <optional>
#include <unordered_map>

namespace tactics {

/** Which energy zone was tapped and what color it produced. */
struct EnergyZoneTapRecord {
    int zone_index{-1};
    EnergyType produced_type{EnergyType::Neutral};
};

/** Captures how energy was spent so `refund_energy_spend` can reverse a single action payment. */
struct EnergySpendRecord {
    std::map<std::string, std::map<EnergyType, int>> tagged_spend;
    std::map<EnergyType, int> unrestricted_spend;
    std::vector<EnergyZoneTapRecord> zone_taps;
    bool empty() const
    {
        if (!zone_taps.empty()) {
            return false;
        }
        for (const auto& [_, per_type] : tagged_spend) {
            for (const auto& [__, amount] : per_type) {
                if (amount > 0) {
                    return false;
                }
            }
        }
        for (const auto& [_, amount] : unrestricted_spend) {
            if (amount > 0) {
                return false;
            }
        }
        return true;
    }
};

class GameState;

class TurnManager {
public:
    explicit TurnManager(GameBoard& board);
    void add_player(int player_id);
    void start_turn(GameState& game);
    ActionResult choose_energy_zone(GameState& game, int player_id, int choice_index);
    ActionResult skip_energy_zone(GameState& game, int player_id);
    bool can_afford(const GameState& game, int player_id, const std::map<EnergyType, int>& cost,
                    ActionType action_type = ActionType::Special) const;
    void spend_energy(GameState& game, int player_id, const std::map<EnergyType, int>& cost,
                      ActionType action_type = ActionType::Special);
    std::optional<EnergySpendRecord> spend_energy_recorded(GameState& game, int player_id,
        const std::map<EnergyType, int>& cost, ActionType action_type = ActionType::Special);
    void refund_energy_spend(GameState& game, int player_id, const EnergySpendRecord& record);
    bool end_turn(GameState& game);
    std::optional<int> current_player() const;

    GameBoard& board_;
    std::vector<int> players;
    int current_player_index{0};
    int round_number{1};
    /** Number of extra full rounds granted once every player's deck is empty, after which
     *  the match ends on combined base HP (a draw if tied). */
    static constexpr int kSuddenDeathRounds = 5;
    /** Round at which every player's draw deck first became empty; -1 until then. Set once
     *  in `advance()` and never reset (decks don't refill - discard is permanent). */
    int all_decks_empty_since_round{-1};
    /** Unrestricted floating energy - usable for any action type; cleared when the active turn ends. */
    std::unordered_map<int, std::map<EnergyType, int>> player_energy;
    /**
     * Per-player deploy discount (in total energy) applied to every DeployAction this turn.
     * When > 0, DeployAction::get_cost reduces the card's cost by this many energy points,
     * consuming neutral energy first then colored energy (never below 0 per type).
     * Reset to 0 at the start of each player's turn.
     */
    std::unordered_map<int, int> deploy_discount_per_unit;
    /**
     * Tagged floating energy pools. Each tag maps to per-player energy amounts.
     * Cleared when the active turn ends (all seats, after owner_turn_end passives - same as player_energy).
     * The tag controls which action types may spend from it (see kTaggedPoolRestrictions in
     * turn_manager.cpp). Built-in tags:
     *   "spell_ability" - Spell + Ability only
     *   "spell_only"    - Spell only
     *   "unit_deploy"   - Deploy only
     * Add new tags to kTaggedPoolRestrictions to create more restriction types.
     *
     * Usage: player_tagged_float[tag][player_id][EnergyType]
     */
    std::unordered_map<std::string, std::unordered_map<int, std::map<EnergyType, int>>> player_tagged_float;
    /** Lifetime flux energy generated per player (sum of all energy credited to `spell_ability`). */
    std::unordered_map<int, int> player_flux_energy_generated_total;
    /** Lifetime overload stacks applied by each player (effective stacks after resistance). */
    std::unordered_map<int, int> player_overload_applied_total;
    /** Lifetime damage dealt via activated abilities per player (actual HP lost after mitigation). */
    std::unordered_map<int, int> player_ability_damage_dealt_total;
    TurnPhase current_phase{TurnPhase::Energy};
    std::unordered_map<int, std::vector<EnergyZone>> pending_energy_choices;
    /** First `start_turn` of the match skips the draw step (opening hand only). */
    bool skip_first_turn_draw_{true};

    bool skip_first_turn_draw_for_snapshot() const { return skip_first_turn_draw_; }
    void set_skip_first_turn_draw_for_snapshot(bool v) { skip_first_turn_draw_ = v; }
    /** Turn draw deferred until an opening scan selection (e.g. scanner tile) completes. */
    std::optional<int> deferred_turn_draw_seat_{};

    std::optional<int> deferred_turn_draw_seat_for_snapshot() const { return deferred_turn_draw_seat_; }
    void set_deferred_turn_draw_seat_for_snapshot(std::optional<int> seat) { deferred_turn_draw_seat_ = std::move(seat); }
};

/** Credit tagged floating energy; increments lifetime flux total when `tag == "spell_ability"`. */
void credit_tagged_float(TurnManager& tm, const std::string& tag, int player_id, EnergyType etype, int amount);
int player_flux_energy_generated_total(const TurnManager& tm, int player_id);
int player_overload_applied_total(const TurnManager& tm, int player_id);
int player_ability_damage_dealt_total(const TurnManager& tm, int player_id);

}  // namespace tactics
