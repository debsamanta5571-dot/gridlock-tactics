#pragma once

#include "tactics/core/game_state.hpp"

#include <optional>

namespace tactics::bot {

/** True when `seat` still has a living player base on the board. */
bool player_has_living_base(const GameState& game, int seat);

/** True once every player's deck has been empty for `kSuddenDeathRounds` full rounds  - 
 *  the match then ends on combined base HP. */
bool sudden_death_timeout(const GameState& game);

/** Combined living base HP for every seat on `team` (sudden-death tiebreak metric). */
int team_base_hp(const GameState& game, int team);

/** Full rounds remaining before sudden-death resolution; negative when decks are not all empty. */
int rounds_until_sudden_death(const GameState& game);

/** True once every deck is empty and `seat`'s team trails the leading hostile team on base HP. */
bool trailing_on_sudden_death_base_hp(const GameState& game, int seat);

/** Policy/evaluator urgency magnitude in ~[0, 280]; zero when not trailing or decks not empty. */
int sudden_death_base_hp_urgency(const GameState& game, int seat);

/** True when at most one team still has any living base, OR sudden death has timed out. */
bool is_match_over(const GameState& game);

/** Winning seat when `is_match_over`; nullopt on draw or in-progress. */
std::optional<int> winner_seat(const GameState& game);

}  // namespace tactics::bot