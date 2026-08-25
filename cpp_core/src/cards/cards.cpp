#include "tactics/cards/cards.hpp"

#include "tactics/cards/ability_catalog.hpp"
#include "tactics/cards/card_catalog.hpp"
#include "tactics/cards/card_runtime.hpp"
#include "tactics/cards/focus_spell.hpp"
#include "tactics/common/effect_keys.hpp"

#include <algorithm>

namespace tactics {

namespace {

bool zone_contains(const std::vector<CardInstanceId>& zone, const CardInstanceId id)
{
    return id.is_valid() && std::find(zone.begin(), zone.end(), id) != zone.end();
}

void erase_from_zone(std::vector<CardInstanceId>& zone, const CardInstanceId id)
{
    zone.erase(std::remove(zone.begin(), zone.end(), id), zone.end());
}

bool definition_is_spell_type(const CardDefinition& def) { return def.type == "spell"; }

struct CardHandSortKey {
    int energy_cost{0};
    std::string name;
};

CardHandSortKey hand_sort_key_for_instance(const Deck& deck, const CardInstanceId id)
{
    CardHandSortKey key;
    const CardInstance* inst = deck.pool.try_get(id);
    if (!inst || !inst->definition_id.is_valid()) {
        return key;
    }
    CardDefinition def;
    if (!try_get_card_definition(inst->definition_id, def)) {
        return key;
    }
    key.energy_cost = definition_total_energy_cost(def);
    key.name = def.name;
    return key;
}

}  // namespace

void Deck::add_card(const CardInstanceId id)
{
    if (id.is_valid()) {
        deck.push_back(id);
    }
}

void Deck::shuffle(std::mt19937& rng) { std::shuffle(deck.begin(), deck.end(), rng); }

std::vector<CardInstanceId> Deck::draw(const int amount, std::mt19937& rng)
{
    // The discard pile is permanent: dead units and discarded cards stay there and are NOT
    // recycled into the deck. When the deck runs out, draws simply yield nothing.
    (void)rng;
    std::vector<CardInstanceId> out;
    for (int i = 0; i < amount; ++i) {
        if (deck.empty()) {
            continue;
        }
        const CardInstanceId drawn = deck.front();
        deck.erase(deck.begin());
        if (CardInstance* inst = pool.try_get(drawn)) {
            if (inst->stockpile_amount > 0 && inst->stockpile_remaining <= 0) {
                inst->stockpile_remaining = inst->stockpile_amount;
            }
            inst->stockpile_used_this_turn = false;
        }
        hand.push_back(drawn);
        out.push_back(drawn);
    }
    return out;
}


namespace {

void note_drawn_instance(Deck& deck, const CardInstanceId id)
{
    if (CardInstance* inst = deck.pool.try_get(id)) {
        if (inst->stockpile_amount > 0 && inst->stockpile_remaining <= 0) {
            inst->stockpile_remaining = inst->stockpile_amount;
        }
        inst->stockpile_used_this_turn = false;
    }
}

template <typename Predicate>
std::vector<CardInstanceId> draw_matching_preserving_deck_order(
    Deck& deck, const int amount, std::mt19937& rng, Predicate&& predicate)
{
    std::vector<CardInstanceId> out;
    if (amount <= 0) {
        return out;
    }

    (void)rng;  // discard is never recycled, so no reshuffle is needed
    int safety = 0;
    const int max_passes = static_cast<int>(deck.deck.size() + deck.discard.size()) + amount + 8;
    while (static_cast<int>(out.size()) < amount && safety < max_passes) {
        ++safety;

        std::optional<std::size_t> pick;
        for (std::size_t i = 0; i < deck.deck.size(); ++i) {
            if (predicate(deck.deck[i])) {
                pick = i;
                break;
            }
        }
        if (!pick) {
            // No matching card left in the deck - the discard pile is permanent and is not
            // recycled, so stop rather than reshuffling discards back in.
            break;
        }

        const CardInstanceId drawn = deck.deck[*pick];
        deck.deck.erase(deck.deck.begin() + static_cast<std::ptrdiff_t>(*pick));
        note_drawn_instance(deck, drawn);
        deck.hand.push_back(drawn);
        out.push_back(drawn);
    }

    return out;
}

}  // namespace

std::vector<CardInstanceId> Deck::draw_unit_cards(const int amount, std::mt19937& rng)
{
    return draw_matching_preserving_deck_order(*this, amount, rng, [this](const CardInstanceId id) -> bool {
        const CardInstance* inst = pool.try_get(id);
        if (!inst || !inst->definition_id.is_valid()) {
            return false;
        }
        CardDefinition def;
        if (!try_get_card_definition(inst->definition_id, def)) {
            return false;
        }
        return def.type == "unit";
    });
}

std::vector<CardInstanceId> Deck::draw_spell_cards(const int amount, const int max_total_cost, std::mt19937& rng)
{
    return draw_matching_preserving_deck_order(*this, amount, rng,
        [this, max_total_cost](const CardInstanceId id) -> bool {
            const CardInstance* inst = pool.try_get(id);
            if (!inst || !inst->definition_id.is_valid()) {
                return false;
            }
            CardDefinition def;
            if (!try_get_card_definition(inst->definition_id, def)) {
                return false;
            }
            if (def.type != "spell") {
                return false;
            }
            return definition_total_energy_cost(def) <= max_total_cost;
        });
}

std::vector<CardInstanceId> Deck::draw_focus_spell_cards(const int amount, const int max_total_cost, std::mt19937& rng)
{
    return draw_matching_preserving_deck_order(*this, amount, rng,
        [this, max_total_cost](const CardInstanceId id) -> bool {
            const CardInstance* inst = pool.try_get(id);
            if (!inst || !inst->definition_id.is_valid()) {
                return false;
            }
            CardDefinition def;
            if (!try_get_card_definition(inst->definition_id, def)) {
                return false;
            }
            if (def.type != "spell" || !spell_is_focus(def)) {
                return false;
            }
            return definition_total_energy_cost(def) <= max_total_cost;
        });
}

void Deck::sort_hand_by_energy_cost()
{
    std::stable_sort(hand.begin(), hand.end(), [this](const CardInstanceId a, const CardInstanceId b) {
        const CardHandSortKey ka = hand_sort_key_for_instance(*this, a);
        const CardHandSortKey kb = hand_sort_key_for_instance(*this, b);
        if (ka.energy_cost != kb.energy_cost) {
            return ka.energy_cost < kb.energy_cost;
        }
        return ka.name < kb.name;
    });
}

bool Deck::contains_in_hand(const CardInstanceId id) const { return zone_contains(hand, id); }

bool Deck::contains_in_in_play(const CardInstanceId id) const { return zone_contains(in_play, id); }

bool Deck::contains_in_purgatory(const CardInstanceId id) const { return zone_contains(purgatory, id); }

CardInstanceId Deck::find_card_by_public_id(const std::string& public_id) const
{
    if (public_id.empty()) {
        return {};
    }
    const auto find_in = [&](const std::vector<CardInstanceId>& zone) -> CardInstanceId {
        for (const CardInstanceId cid : zone) {
            if (const CardInstance* inst = pool.try_get(cid)) {
                if (inst->public_id == public_id) {
                    return cid;
                }
            }
        }
        return {};
    };
    if (CardInstanceId c = find_in(hand); c.is_valid()) {
        return c;
    }
    if (CardInstanceId c = find_in(in_play); c.is_valid()) {
        return c;
    }
    if (CardInstanceId c = find_in(deck); c.is_valid()) {
        return c;
    }
    if (CardInstanceId c = find_in(discard); c.is_valid()) {
        return c;
    }
    if (CardInstanceId c = find_in(reserves); c.is_valid()) {
        return c;
    }
    if (CardInstanceId c = find_in(purgatory); c.is_valid()) {
        return c;
    }
    return {};
}

void Deck::remove_card_from_hand_and_in_play(const CardInstanceId id)
{
    if (!id.is_valid()) {
        return;
    }
    erase_from_zone(hand, id);
    erase_from_zone(in_play, id);
    erase_from_zone(reserves, id);
}

void Deck::send_to_discard_pile(const CardInstanceId id)
{
    if (!id.is_valid()) {
        return;
    }
    for (const CardInstanceId existing : discard) {
        if (existing == id) {
            return;
        }
    }
    discard.push_back(id);
}

void Deck::send_played_spell_to_used_pile(const CardInstanceId id)
{
    if (!id.is_valid()) {
        return;
    }
    if (const CardInstance* inst = pool.try_get(id)) {
        if (card_instance_is_temporary_granted(*inst)) {
            move_card_to_purgatory(id);
            return;
        }
    }
    send_to_discard_pile(id);
}

void Deck::send_to_purgatory(const CardInstanceId id)
{
    if (!id.is_valid()) {
        return;
    }
    for (const CardInstanceId existing : purgatory) {
        if (existing == id) {
            return;
        }
    }
    purgatory.push_back(id);
}

void Deck::move_card_to_purgatory(const CardInstanceId id)
{
    if (!id.is_valid()) {
        return;
    }
    remove_card_from_hand_and_in_play(id);
    erase_from_zone(deck, id);
    erase_from_zone(discard, id);
    send_to_purgatory(id);
}

bool Deck::return_batched_spell_from_discard_to_hand(const CardInstanceId id)
{
    if (!id.is_valid()) {
        return false;
    }
    const auto from_zone = [&](std::vector<CardInstanceId>& zone) -> bool {
        const auto it = std::find(zone.begin(), zone.end(), id);
        if (it == zone.end()) {
            return false;
        }
        zone.erase(it);
        hand.push_back(id);
        return true;
    };
    if (from_zone(discard)) {
        return true;
    }
    return from_zone(purgatory);
}

bool Deck::return_batched_spell_from_discard_to_reserves(const CardInstanceId id)
{
    if (!id.is_valid()) {
        return false;
    }
    const auto from_zone = [&](std::vector<CardInstanceId>& zone) -> bool {
        const auto it = std::find(zone.begin(), zone.end(), id);
        if (it == zone.end()) {
            return false;
        }
        zone.erase(it);
        reserves.push_back(id);
        return true;
    };
    if (from_zone(discard)) {
        return true;
    }
    return from_zone(purgatory);
}

bool Deck::return_batched_spell_from_in_play_to_reserves(const CardInstanceId id)
{
    if (!id.is_valid()) {
        return false;
    }
    const auto it = std::find(in_play.begin(), in_play.end(), id);
    if (it == in_play.end()) {
        return false;
    }
    in_play.erase(it);
    reserves.push_back(id);
    return true;
}

bool Deck::return_deployed_unit_to_origin(const CardInstanceId id, const bool to_reserves)
{
    if (!id.is_valid()) {
        return false;
    }
    const auto it = std::find(in_play.begin(), in_play.end(), id);
    if (it == in_play.end()) {
        return false;
    }
    in_play.erase(it);
    if (to_reserves) {
        reserves.push_back(id);
    } else {
        hand.push_back(id);
    }
    return true;
}

void Deck::discard_card_to_pile(const CardInstanceId id)
{
    if (!id.is_valid()) {
        return;
    }
    remove_card_from_hand_and_in_play(id);
    erase_from_zone(deck, id);
    send_to_discard_pile(id);
}

bool Deck::can_play_card_now(const CardInstanceId id, std::string* reason) const
{
    if (!id.is_valid() || !contains_in_hand(id)) {
        if (reason) {
            *reason = "Card is not in your hand";
        }
        return false;
    }
    const CardInstance& inst = pool.at(id);
    if (inst.stockpile_amount > 0) {
        if (inst.stockpile_remaining <= 0) {
            if (reason) {
                *reason = "Stockpile is depleted";
            }
            return false;
        }
        if (inst.stockpile_used_this_turn) {
            // Allow a second play this turn only if Second Wave (or similar) is active
            // AND the card hasn't already consumed its second play.
            if (!stockpile_double_play_active || inst.stockpile_double_play_used_this_turn) {
                if (reason) {
                    *reason = "Already used this turn";
                }
                return false;
            }
        }
    }
    return true;
}

bool Deck::contains_in_reserves(const CardInstanceId id) const { return zone_contains(reserves, id); }

bool Deck::can_play_card_from_reserves(const CardInstanceId id, std::string* reason) const
{
    if (!id.is_valid() || !contains_in_reserves(id)) {
        if (reason) {
            *reason = "Card is not in your reserves";
        }
        return false;
    }
    const CardInstance& inst = pool.at(id);
    if (inst.stockpile_amount > 0) {
        if (inst.stockpile_remaining <= 0) {
            if (reason) {
                *reason = "Stockpile is depleted";
            }
            return false;
        }
        if (inst.stockpile_used_this_turn) {
            if (!stockpile_double_play_active || inst.stockpile_double_play_used_this_turn) {
                if (reason) {
                    *reason = "Already used this turn";
                }
                return false;
            }
        }
    }
    return true;
}

bool Deck::play_card_from_reserves(const CardInstanceId id)
{
    if (!can_play_card_from_reserves(id, nullptr)) {
        return false;
    }
    const auto it = std::find(reserves.begin(), reserves.end(), id);
    if (it == reserves.end()) {
        return false;
    }
    const CardDefinition* def = try_get_card_definition_ptr(pool.at(id).definition_id);
    if (!def) {
        return false;
    }
    CardInstance& inst = pool.at(id);
    if (definition_is_spell_type(*def)) {
        reserves.erase(it);
        if (inst.stockpile_amount > 0) {
            if (inst.stockpile_used_this_turn) {
                inst.stockpile_double_play_used_this_turn = true;
            }
            inst.stockpile_remaining = std::max(0, inst.stockpile_remaining - 1);
            inst.stockpile_used_this_turn = true;
            if (inst.stockpile_remaining > 0) {
                reserves.push_back(id);
                return true;
            }
        }
        send_played_spell_to_used_pile(id);
        return true;
    }
    if (inst.stockpile_amount > 0) {
        if (inst.stockpile_used_this_turn) {
            // This is the second play - consume the double-play slot.
            inst.stockpile_double_play_used_this_turn = true;
        }
        inst.stockpile_remaining = std::max(0, inst.stockpile_remaining - 1);
        inst.stockpile_used_this_turn = true;
        if (inst.stockpile_remaining > 0) {
            return true;
        }
    }
    reserves.erase(it);  // consume the reserve copy on deploy (non-stockpile units/buildings must leave reserves)
    in_play.push_back(id);
    return true;
}

bool Deck::play_card(const CardInstanceId id)
{
    if (!can_play_card_now(id, nullptr)) {
        return false;
    }
    const auto it = std::find(hand.begin(), hand.end(), id);
    if (it == hand.end()) {
        return false;
    }
    const CardDefinition* def = try_get_card_definition_ptr(pool.at(id).definition_id);
    if (!def) {
        return false;
    }
    CardInstance& inst = pool.at(id);
    if (definition_is_spell_type(*def)) {
        hand.erase(it);
        if (inst.stockpile_amount > 0) {
            if (inst.stockpile_used_this_turn) {
                inst.stockpile_double_play_used_this_turn = true;
            }
            inst.stockpile_remaining = std::max(0, inst.stockpile_remaining - 1);
            inst.stockpile_used_this_turn = true;
            if (inst.stockpile_remaining > 0) {
                hand.push_back(id);
                return true;
            }
        }
        send_played_spell_to_used_pile(id);
        return true;
    }
    if (inst.stockpile_amount > 0) {
        if (inst.stockpile_used_this_turn) {
            // This is the second play - consume the double-play slot.
            inst.stockpile_double_play_used_this_turn = true;
        }
        inst.stockpile_remaining = std::max(0, inst.stockpile_remaining - 1);
        inst.stockpile_used_this_turn = true;
        if (inst.stockpile_remaining > 0) {
            return true;
        }
    }
    hand.erase(it);  // consume the hand copy on deploy (non-stockpile units/buildings must leave hand)
    in_play.push_back(id);
    return true;
}


bool Deck::commit_unit_played_from_deck(const CardInstanceId id)
{
    if (!id.is_valid()) {
        return false;
    }
    erase_from_zone(deck, id);
    CardInstance& inst = pool.at(id);
    if (inst.stockpile_amount > 0) {
        inst.stockpile_remaining = std::max(0, inst.stockpile_remaining - 1);
        inst.stockpile_used_this_turn = true;
        if (inst.stockpile_remaining > 0) {
            deck.push_back(id);
            return true;
        }
    }
    in_play.push_back(id);
    return true;
}


void Deck::refresh_turn_limited_card_attributes()
{
    stockpile_double_play_active = false;
    for (const CardInstanceId cid : hand) {
        if (CardInstance* inst = pool.try_get(cid)) {
            inst->stockpile_used_this_turn = false;
            inst->stockpile_double_play_used_this_turn = false;
        }
    }
    for (const CardInstanceId cid : reserves) {
        if (CardInstance* inst = pool.try_get(cid)) {
            inst->stockpile_used_this_turn = false;
            inst->stockpile_double_play_used_this_turn = false;
        }
    }
}

void Deck::discard_down_to(const std::size_t max_hand)
{
    // Signature/token cards flagged `ignores_hand_limit` never count toward the limit and are
    // never discarded here. Count only limit-bearing cards and discard the newest of those.
    const auto counts_toward_limit = [&](const CardInstanceId cid) -> bool {
        if (const CardInstance* inst = pool.try_get(cid)) {
            if (const CardDefinition* def = try_get_card_definition_ptr(inst->definition_id)) {
                return !def->ignores_hand_limit;
            }
        }
        return true;
    };
    const auto limit_count = [&]() -> std::size_t {
        std::size_t n = 0;
        for (const CardInstanceId cid : hand) {
            if (counts_toward_limit(cid)) {
                ++n;
            }
        }
        return n;
    };
    while (limit_count() > max_hand) {
        std::size_t idx = hand.size();
        for (std::size_t i = hand.size(); i-- > 0;) {
            if (counts_toward_limit(hand[i])) {
                idx = i;
                break;
            }
        }
        if (idx >= hand.size()) {
            break;
        }
        const CardInstanceId cid = hand[idx];
        if (!cid.is_valid()) {
            hand.erase(hand.begin() + static_cast<std::ptrdiff_t>(idx));
            continue;
        }
        discard_card_to_pile(cid);
    }
}

bool Deck::discard_hand_card_at_1based(const int idx_1based)
{
    if (idx_1based < 1 || idx_1based > static_cast<int>(hand.size())) {
        return false;
    }
    const size_t i = static_cast<size_t>(idx_1based - 1);
    const CardInstanceId cid = hand[i];
    if (!cid.is_valid()) {
        hand.erase(hand.begin() + static_cast<std::ptrdiff_t>(i));
        return true;
    }
    discard_card_to_pile(cid);
    return true;
}

std::shared_ptr<Unit> UnitCard::create_unit(const int owner, const std::string& entity_id) const
{
    CardInstance scratch;
    scratch.public_id = card_id;
    scratch.definition_id = try_card_def_id_for_key(definition_key);
    CardDefinition def;
    if (!try_get_card_definition(definition_key, def)) {
        auto u = std::make_shared<Unit>(template_unit);
        u->entity_id = entity_id;
        u->owner = owner;
        u->source_card_id = card_id;
        normalize_entity_shape(*u);
        return u;
    }
    return create_unit_from_definition(def, scratch, owner, entity_id);
}

Deck create_starter_deck(std::mt19937& rng) { return create_starter_deck_from_catalog(rng); }

Deck create_test_deck(std::mt19937& rng) { return create_test_deck_from_catalog(rng); }

Deck create_footprint_test_deck()
{
    Deck d;
    auto add_unit = [&](const char* key, const char* display_name, std::vector<std::pair<int, int>> shape,
                        const char* extra_kw = nullptr) {
        CardDefinition def;
        def.key = key;
        def.name = display_name;
        def.type = "unit";
        def.energy_cost = {{EnergyType::Neutral, 1}};
        UnitCardDefinition ud;
        ud.entity_type = "unit";
        ud.unit_type = "TestFootprint";
        ud.attack_type = AttackType::Melee;
        ud.melee_damage = 1;
        ud.movement = 6;
        ud.shape = std::move(shape);
        if (extra_kw) {
            ud.keywords.push_back({extra_kw, std::nullopt});
        }
        def.unit = std::move(ud);
        register_runtime_card_definition(std::move(def));
        d.add_card(deck_allocate_instance(d, key, 0));
    };
    add_unit("ftest_1", "Test footprint 1 tile", {{0, 0}});
    add_unit("ftest_2_domino", "Test footprint 2 tiles (line)", {{0, 0}, {1, 0}}, "reach");
    add_unit("ftest_3_line", "Test footprint 3 tiles (line)", {{0, 0}, {1, 0}, {2, 0}});
    add_unit("ftest_3_L", "Test footprint 3 tiles (L)", {{0, 0}, {1, 0}, {0, 1}}, "flying");
    add_unit("ftest_4_block", "Test footprint 4 tiles (2x2)", {{0, 0}, {1, 0}, {0, 1}, {1, 1}});
    add_unit("ftest_5_line", "Test footprint 5 tiles (line)", {{0, 0}, {1, 0}, {2, 0}, {3, 0}, {4, 0}});
    add_unit("ftest_haste", "Test Haste", {{0, 0}}, "haste");
    add_unit("ftest_surge", "Test Surge", {{0, 0}}, "surge");
    add_unit("ftest_charge", "Test Charge", {{0, 0}}, "charge");
    return d;
}

}  // namespace tactics
