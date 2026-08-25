#pragma once

#include <string>

namespace tactics {

/** Which map a match is played on (chosen in the pre-match settings screen). */
enum class MatchMapType {
    Duel1v1,  ///< Standard 8x12 duel map (two seats).
    Team2v2,  ///< 16x8 team map with four bases (seats 1&3 vs 2&4).
};

/**
 * Pre-match settings chosen by the host / solo player before a match starts. Drives map selection,
 * which objective tiles are seeded, the deck used, and whether the going-second seats receive the
 * Field Requisition signature card. Deck choice on JOIN is handled per-peer by the network layer.
 */
struct MatchConfig {
    MatchMapType map_type{MatchMapType::Duel1v1};
    /** Objective tiles to seed (per-type toggles). */
    bool objective_scanner{true};
    bool objective_omni{true};
    bool objective_aether{true};
    /** Deck to use (deck id / filename stem); empty = the default deck. */
    std::string deck_id;
    /** When true, the going-second seats get the Field Requisition card (compensation for going second). */
    bool give_field_requisition{false};
};

}  // namespace tactics
