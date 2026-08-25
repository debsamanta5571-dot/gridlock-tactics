#pragma once

#include "tactics/cards/card_instances.hpp"
#include "tactics/common/types.hpp"
#include "tactics/effects/effect_registry.hpp"
#include "tactics/entities/entity.hpp"

#include <algorithm>
#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <vector>

namespace tactics {

/** Opening hand after shuffle; drawn in `GameState::add_player`. */
inline constexpr int kOpeningHandSize = 5;
/** Maximum cards retained after turn end; excess discards at end of turn (may exceed during turn). */
inline constexpr int kMaxHandSize = 10;
/** Maximum cards in the reserves side pool (set at game start only). */
inline constexpr int kMaxReservesSize = 5;
/** Standard constructed main library size. */
inline constexpr int kMaxMainDeckCards = 40;
/** Maximum copies of the same card definition in the main library or reserves. */
inline constexpr int kMaxCopiesPerCard = 3;
/** Energy zone pool size at deck build (tournament-legal lists must include exactly this many). */
inline constexpr int kMaxZoneDeckSize = 20;
/** Max copies of a non-basic (unique) territory in a zone deck; basic territories have no per-type cap. */
inline constexpr int kMaxCopiesPerUniqueTerritory = 3;

/** Where a deploy/cast action takes its card from. */
enum class CardPlayZone { Hand, Reserves };

/** Legacy full card blob (snapshot v1 import only). Prefer catalog + `CardInstance`. */
struct Card {
    std::string definition_key;
    std::string card_id;
    std::string name;
    std::string card_type;
    std::string rules_text;
    std::string art_id;
    std::vector<std::string> tags{};
    std::vector<std::string> unit_types{};
    std::map<EnergyType, int> energy_cost;
    std::vector<std::string> keywords{};
    int stockpile_amount{0};
    int stockpile_remaining{0};
    bool stockpile_used_this_turn{false};
    /** True once a second play has been consumed this turn (only meaningful when the owning
     *  Deck has stockpile_double_play_active set). Reset by refresh_turn_limited_card_attributes. */
    bool stockpile_double_play_used_this_turn{false};
    virtual ~Card() = default;
};

struct UnitCard : Card {
    Unit template_unit;
    std::shared_ptr<Unit> create_unit(int owner, const std::string& entity_id) const;
};

struct SpellCard : Card {
    EffectSpeed speed{EffectSpeed::Channeled};
    std::string effect_key{"generic_effect"};
    std::map<std::string, int> effect_payload;
    std::optional<BoardTargetKind> board_target_kind{};
    std::optional<bool> requires_mandatory_board_cell{};
    int focus_range{0};
    std::vector<std::string> require_target_unit_types{};
    std::vector<std::string> bonus_damage_unit_types{};
    int bonus_damage_amount{0};
};

using CardPtr = std::shared_ptr<Card>;

inline bool card_has_attribute(const Card& c, const std::string& key)
{
    return std::find(c.keywords.begin(), c.keywords.end(), key) != c.keywords.end();
}

inline void add_card_attribute(Card& c, const std::string& key)
{
    if (!card_has_attribute(c, key)) {
        c.keywords.push_back(key);
    }
}

inline void set_card_stockpile(Card& c, int amount)
{
    c.stockpile_amount = std::max(0, amount);
    c.stockpile_remaining = c.stockpile_amount;
    c.stockpile_used_this_turn = false;
    c.stockpile_double_play_used_this_turn = false;
    if (c.stockpile_amount > 0) {
        add_card_attribute(c, "stockpile");
    }
}

struct Deck {
    CardInstancePool pool;
    std::vector<CardInstanceId> deck;
    std::vector<CardInstanceId> hand;
    std::vector<CardInstanceId> discard;
    /** Removed-from-game zone (exile-like): not drawable and not in discard recycle. */
    std::vector<CardInstanceId> purgatory;
    std::vector<CardInstanceId> in_play;
    std::vector<CardInstanceId> reserves;
    /** When true, stockpile cards in this deck may be played up to twice this turn instead of
     *  once. Set by effects such as Second Wave; cleared by refresh_turn_limited_card_attributes. */
    bool stockpile_double_play_active{false};

    void add_card(CardInstanceId id);
    void shuffle(std::mt19937& rng);
    std::vector<CardInstanceId> draw(int amount, std::mt19937& rng);
    /** Draw up to `amount` cards whose definition type is `"unit"`. Ineligible cards stay in deck order. */
    std::vector<CardInstanceId> draw_unit_cards(int amount, std::mt19937& rng);
    /** Draw up to `amount` spell cards with total energy cost <= `max_total_cost`. Ineligible cards stay in deck order. */
    std::vector<CardInstanceId> draw_spell_cards(int amount, int max_total_cost, std::mt19937& rng);
    /** Draw up to `amount` focus spell cards with total energy cost <= `max_total_cost`. */
    std::vector<CardInstanceId> draw_focus_spell_cards(int amount, int max_total_cost, std::mt19937& rng);
    bool play_card(CardInstanceId id);
    bool can_play_card_now(CardInstanceId id, std::string* reason = nullptr) const;
    bool contains_in_reserves(CardInstanceId id) const;
    bool can_play_card_from_reserves(CardInstanceId id, std::string* reason = nullptr) const;
    bool play_card_from_reserves(CardInstanceId id);
    void refresh_turn_limited_card_attributes();
    bool contains_in_hand(CardInstanceId id) const;
    bool contains_in_in_play(CardInstanceId id) const;
    bool contains_in_purgatory(CardInstanceId id) const;
    CardInstanceId find_card_by_public_id(const std::string& public_id) const;
    void remove_card_from_hand_and_in_play(CardInstanceId id);
    void discard_down_to(std::size_t max_hand);
    bool discard_hand_card_at_1based(int idx_1based);
    void discard_card_to_pile(CardInstanceId id);
    void send_to_discard_pile(CardInstanceId id);
    /** Spell just played: discard, or purgatory when `card_instance_is_temporary_granted`. */
    void send_played_spell_to_used_pile(CardInstanceId id);
    void send_to_purgatory(CardInstanceId id);
    /** Strip from all other zones, then add to purgatory. */
    void move_card_to_purgatory(CardInstanceId id);
    bool return_batched_spell_from_discard_to_hand(CardInstanceId id);
    bool return_batched_spell_from_discard_to_reserves(CardInstanceId id);
    bool return_batched_spell_from_in_play_to_reserves(CardInstanceId id);
    /** Undo a deploy: pull a unit instance out of in_play back to hand (or reserves). */
    bool return_deployed_unit_to_origin(CardInstanceId id, bool to_reserves);

    /** Stable sort: ascending total energy cost, then card name. */
    void sort_hand_by_energy_cost();

    /** Spell deploy: remove from deck zone, consume stockpile, return to deck if charges remain else in_play. */
    bool commit_unit_played_from_deck(CardInstanceId id);
};


Deck create_starter_deck(std::mt19937& rng);
Deck create_test_deck(std::mt19937& rng);
Deck create_footprint_test_deck();

}  // namespace tactics
