#pragma once

#include "tactics/core/game_state.hpp"

#include <optional>
#include <string>
#include <vector>

namespace tactics {
struct CardDefinition;
}

namespace tactics::bot {

/** Position value in [-1, 1] from `root_player`'s perspective (+1 = winning). */
class IBotEvaluator {
public:
    virtual ~IBotEvaluator() = default;
    virtual double evaluate(const GameState& game, int root_player) const = 0;
};

/** Default linear heuristic used by MCTS when no trained weights are loaded. */
class HeuristicEvaluator final : public IBotEvaluator {
public:
    double evaluate(const GameState& game, int root_player) const override;
};

// ── Learnable value function ──────────────────────────────────────────────────
// The position evaluator is a *linear model over engineered features*:
//     value = clamp( dot(features, weights), -1, 1 ).
// The default weights reproduce the hand-tuned heuristic exactly, so behaviour is
// unchanged until a trained weight file overrides them. This is the seam for machine
// learning: self-play logs (feature vector, game outcome) pairs, an offline trainer
// fits the weights, and `load_bot_value_weights` (or the TACTICS_BOT_WEIGHTS env var)
// swaps them in without a recompile.

/** Names of the value features, in the fixed order used by features/weights. */
const std::vector<std::string>& bot_value_feature_names();

/** The engineered feature vector for a position, from `root_player`'s perspective. */
std::vector<double> bot_value_features(const GameState& game, int root_player);

/** Current model weights (lazily initialised to the hand-tuned defaults, or to the file
 *  named by the TACTICS_BOT_WEIGHTS env var if set). */
const std::vector<double>& bot_value_weights();

/** Replace the weights in memory (ignored unless the size matches the feature count). */
void set_bot_value_weights(std::vector<double> weights);

/** Load weights from a text file - one value per line (bare number, or "name value";
 *  '#' starts a comment). Returns false (and keeps the current weights) if the count does
 *  not match the feature vector. */
bool load_bot_value_weights(const std::string& path);

/** Terminal outcome for `root_player`, or nullopt if the match is still in progress. */
std::optional<double> terminal_value_for_player(const GameState& game, int root_player);

/** Nominal offensive output of a unit - the better of its melee / ranged damage. */
int unit_nominal_attack(const Unit& u);

/** "Invest now, pay later" value of *playing* a card: engine passives (energy
 *  generation, spawners, allied auras, automated/reactive effects) it will bring to
 *  the board. 0 for a vanilla body or a spell. Lets the deploy scorer value ramp/engine
 *  cards that do nothing the turn they land. */
double card_engine_future_value(const CardDefinition& def);

/** How much a board asset (unit or structure) is worth: survivability (HP) + offense
 *  + reach/mobility + keyword premia + activated-ability optionality + passive-engine
 *  value + pending damage boost. Shared by the position evaluator and the policy's
 *  attack-target scoring so both agree on "a Sentinel is not a token." */
double piece_value(const Entity& ent);

/** Net objective-tile control (scanner / omni-energy / aether) from `root_player`'s
 *  perspective: the per-turn hold value our team currently extracts minus what hostile
 *  teams extract. A positional "key squares" term - objectives generate card advantage
 *  (scanner scan), energy ramp (omni), or escalating base damage (aether). Adding a new
 *  objective type is one entry in the internal enumerator. */
double objective_control_value(const GameState& game, int root_player);

/** Value (in the same hold-value units as `objective_control_value`) to `seat` of moving
 *  control-eligible `mover` onto cell (x,y): capturing an enemy scanner/omni tile,
 *  capturing or contesting the aether center, or denying an enemy off our own tile. 0 when
 *  the cell is not an objective the move helps. Aether damages every occupant on the next
 *  burst - units that would die are never placed there (capture value 0). */
double objective_cell_capture_value(const GameState& game, int seat, const Entity& mover, int x, int y);

/** Chebyshev distance from a cell to the nearest friendly base (999 if none). */
int distance_cell_to_own_base(const GameState& game, int player_id, int x, int y);

/** True when a hostile can imminently strike one of our bases (within threat range + 1). */
bool enemy_threatens_own_base(const GameState& game, int player_id, const Entity& enemy);

/** Summed incoming threat when our base is in danger; 0 when safe. Drives defensive recalls. */
int own_base_defense_need(const GameState& game, int player_id);

/** Best piece value among hostiles a base turret could lethal with temporary +damage/+range (0 if none). */
double base_turret_buff_kill_value(const GameState& game, int player_id, int bonus_damage, int bonus_range);

}  // namespace tactics::bot