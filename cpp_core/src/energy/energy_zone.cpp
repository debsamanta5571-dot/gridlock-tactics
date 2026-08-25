#include "tactics/energy/energy_zone.hpp"

#include "tactics/cards/card_catalog.hpp"
#include "tactics/cards/cards.hpp"

#include <algorithm>

namespace tactics {

EnergyZone energy_zone_from_list_entry(const ZoneListEntry& entry, const std::string& instance_id)
{
    EnergyZone zone;
    zone.zone_id = instance_id;
    zone.name = entry.name;
    zone.art_id = entry.art_id;
    zone.energy_produced = entry.energy_produced;
    zone.color = entry.color;
    zone.is_basic = entry.is_basic;
    zone.enters_depleted = entry.enters_depleted;
    zone.enter_effects = entry.enter_effects;
    zone.groundwork = entry.groundwork;
    zone.land_abilities = entry.land_abilities;
    return zone;
}

std::map<EnergyType, int> EnergyZone::tap(EnergyType chosen) {
    // Legacy passive zone: produce from `energy_produced` and mark tapped for the turn.
    if (!is_tapped) {
        auto it = energy_produced.find(chosen);
        if (it != energy_produced.end() && it->second > 0) {
            is_tapped = true;
            return {{chosen, it->second}};
        }
    }
    // Conquering Territories: auto-tap an energy-only land ability, spending the land's shared use.
    const int ai = auto_tap_ability_index();
    if (ai >= 0) {
        const auto& produced = land_abilities[static_cast<std::size_t>(ai)].energy_produced;
        auto it = produced.find(chosen);
        if (it != produced.end() && it->second > 0) {
            land_use_available = 0;
            depleted = true;
            return {{chosen, it->second}};
        }
    }
    return {};
}

EnergyZoneDeck::EnergyZoneDeck(std::mt19937& rng) {
    for (int i = 0; i < kMaxZoneDeckSize; ++i) {
        EnergyZone zone;
        zone.zone_id = "asteria_" + std::to_string(i);
        zone.name = "Asteria";
        zone.art_id = "territories/asteria";
        zone.color = EnergyType::Orange;
        zone.is_basic = true;
        TerritoryAbility mine;
        mine.name = "Mine";
        mine.energy_produced = {{EnergyType::Orange, 1}};
        zone.land_abilities.push_back(std::move(mine));
        deck.push_back(std::move(zone));
    }
    std::shuffle(deck.begin(), deck.end(), rng);
}

EnergyZoneDeck EnergyZoneDeck::from_deck_list(const DeckListDefinition& deck_list, std::mt19937& rng) {
    EnergyZoneDeck out;
    if (deck_list.zones.empty()) {
        return EnergyZoneDeck(rng);
    }
    int copy_index = 0;
    for (const auto& entry : deck_list.zones) {
        for (int i = 0; i < entry.copies; ++i) {
            const std::string suffix = entry.copies > 1 ? "_" + std::to_string(copy_index++) : "";
            out.deck.push_back(energy_zone_from_list_entry(entry, entry.zone_id + suffix));
        }
    }
    std::shuffle(out.deck.begin(), out.deck.end(), rng);
    return out;
}

std::vector<EnergyZone> EnergyZoneDeck::draw(int amount) {
    std::vector<EnergyZone> out;
    for (int i = 0; i < amount && !deck.empty(); ++i) {
        out.push_back(deck.front());
        deck.erase(deck.begin());
    }
    return out;
}

void EnergyZoneDeck::shuffle_back(const std::vector<EnergyZone>& zones, std::mt19937& rng) {
    deck.insert(deck.end(), zones.begin(), zones.end());
    std::shuffle(deck.begin(), deck.end(), rng);
}

}  // namespace tactics
