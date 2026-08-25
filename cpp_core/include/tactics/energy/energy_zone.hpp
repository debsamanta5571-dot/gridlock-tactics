#pragma once

#include "tactics/common/types.hpp"

#include <map>
#include <optional>
#include <random>
#include <string>
#include <vector>

namespace tactics {

/**
 * Conquering Territories expansion. A territory (formerly a plain energy zone) can carry
 * activated "use land" abilities, entrance effects, groundwork triggers, and a depleted
 * state - all data-driven and resolved through the shared effect_key pipeline, so authoring
 * a new territory is JSON, not code.
 */

/** One resolvable territory effect (used by enter effects, groundwork, and use-land abilities).
 *  Reuses the same effect_key resolution as spells/abilities. `requires_target` => the player
 *  picks a target unit when it resolves (targeting flow identical to targeted abilities). */
struct TerritoryEffect {
    std::string effect_key;
    std::map<std::string, int> payload{};
    std::map<std::string, std::string> string_payload{};
    bool requires_target{false};
    BoardTargetKind board_target_kind{BoardTargetKind::Any};
};

/** A "use land" activated ability. Costs energy, optionally produces energy (float or flux),
 *  and optionally resolves an effect. A territory has ONE shared use per turn across all its
 *  abilities (`land_use_available`), refreshing at the start of the owner's turn. */
struct TerritoryAbility {
    std::string name;
    std::map<EnergyType, int> cost{};
    /** Energy this ability grants when used (credited as float, or as flux when `produces_flux`). */
    std::map<EnergyType, int> energy_produced{};
    bool produces_flux{false};
    /** When true, this land is removed from the owner's row after the ability resolves. */
    bool sacrifice_self{false};
    /** Optional effect resolved on use (scan, grant stat, etc.). Empty effect_key = energy only. */
    TerritoryEffect effect{};

    /** A "special" land ability resolves a non-energy effect (grant, scan, boost, …). These are
     *  channeled speed - usable only on the controller's own main phase, like a channeled spell.
     *  Pure energy-generating abilities (no effect) are blazing: usable any time the player
     *  holds priority and they resolve immediately. */
    bool is_special_ability() const { return !effect.effect_key.empty(); }
    /** Printed speed token (`CHANNELED` / `BLAZING`). Special (has an effect) = channeled;
     *  energy/flux taps = blazing. */
    const char* speed_token() const { return is_special_ability() ? "CHANNELED" : "BLAZING"; }

    /** True when this ability is a plain, unconditional energy source the game may auto-tap to pay for
     *  cards/abilities: it produces (unrestricted) float energy, costs nothing to activate, resolves no
     *  effect, and does not sacrifice the land. Flux abilities are excluded (flux goes to the restricted
     *  spell/ability pool, so it must stay a manual "use land"). */
    bool is_auto_tap_energy() const
    {
        return !energy_produced.empty() && !produces_flux && cost.empty() && effect.effect_key.empty()
            && !sacrifice_self;
    }
};

/** `groundwork <color>`: an on-entrance trigger that fires only if the PREVIOUS territory the
 *  owner conquered was a **basic** territory of `color`. `ignore_depleted` is the special
 *  variant (the territory enters ready instead of depleted); `effect` fires the payload. */
struct GroundworkTrigger {
    EnergyType color{EnergyType::Neutral};
    bool ignore_depleted{false};
    /** When true and the groundwork condition is NOT met, the territory is destroyed on enter. */
    bool destroy_if_unmet{false};
    /** Optional effect to resolve when the groundwork condition is met. */
    std::optional<TerritoryEffect> effect{};
};

struct EnergyZone {
    std::string zone_id;
    std::string name;
    /** UI art slug under `card_art/` (see `ZoneListEntry::art_id`). */
    std::string art_id;
    /** Passive per-turn energy (legacy zones auto-tap this; new territories usually leave it
     *  empty and generate via `land_abilities` instead). */
    std::map<EnergyType, int> energy_produced;
    bool is_tapped{false};

    // ── Conquering Territories fields ───────────────────────────────────────────
    /** Territory color; set for basic territories and colored lands (drives groundwork). */
    std::optional<EnergyType> color{};
    /** True for a "basic <color> territory" - what groundwork matches against. */
    bool is_basic{false};
    /** `depleted` keyword: the land's use starts spent on the turn it is conquered. */
    bool enters_depleted{false};
    /** Runtime: currently depleted (use spent). Cleared at the owner's next turn start. */
    bool depleted{false};
    /** Shared per-turn use charge across all `land_abilities` (0 = none left / no abilities). */
    int land_use_available{0};
    /** Fired once when the territory is conquered (placed). */
    std::vector<TerritoryEffect> enter_effects{};
    /** Evaluated on placement against the owner's previously-conquered territory. */
    std::vector<GroundworkTrigger> groundwork{};
    /** "Use land" abilities; sharing the single per-turn use. */
    std::vector<TerritoryAbility> land_abilities{};

    void untap() { is_tapped = false; }
    std::map<EnergyType, int> tap(EnergyType chosen);

    /** True when this territory offers an activatable "use land" ability. */
    bool has_land_abilities() const { return !land_abilities.empty(); }
    /** True when any land ability resolves an effect - used to preserve these lands (tap plain energy
     *  lands first) when auto-paying. */
    bool has_special_land_ability() const
    {
        for (const auto& ab : land_abilities) {
            if (ab.is_special_ability()) {
                return true;
            }
        }
        return false;
    }
    /** Index of an energy-only land ability the game may auto-tap for payment (requires the shared use
     *  to be available), or -1 if none. */
    int auto_tap_ability_index() const
    {
        if (land_use_available <= 0) {
            return -1;
        }
        for (std::size_t i = 0; i < land_abilities.size(); ++i) {
            if (land_abilities[i].is_auto_tap_energy()) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }
    /** Energy this placed land can auto-pay with right now (legacy `energy_produced` + unused auto-tap). */
    std::map<EnergyType, int> available_auto_energy() const
    {
        std::map<EnergyType, int> out;
        if (!is_tapped) {
            for (const auto& [etype, amount] : energy_produced) {
                if (amount > 0) {
                    out[etype] = amount;
                }
            }
        }
        const int ai = auto_tap_ability_index();
        if (ai >= 0) {
            for (const auto& [etype, amount] : land_abilities[static_cast<std::size_t>(ai)].energy_produced) {
                if (amount > 0) {
                    out[etype] += amount;
                }
            }
        }
        return out;
    }
    /** Energy this land would auto-pay with once conquered and ready (ignores depleted / tapped). */
    std::map<EnergyType, int> potential_auto_energy() const
    {
        std::map<EnergyType, int> out = energy_produced;
        for (const auto& ab : land_abilities) {
            if (!ab.is_auto_tap_energy()) {
                continue;
            }
            for (const auto& [etype, amount] : ab.energy_produced) {
                if (amount > 0) {
                    out[etype] += amount;
                }
            }
            break;
        }
        return out;
    }
    /** Restore the shared use (and clear depletion) at the owner's turn start. */
    void refresh_land_use()
    {
        depleted = false;
        land_use_available = has_land_abilities() ? 1 : 0;
    }
};

/** Per-player memory of the last territory conquered, for `groundwork` matching. */
struct ConqueredTerritoryMemory {
    bool has_value{false};
    bool was_basic{false};
    EnergyType color{EnergyType::Neutral};
};

struct DeckListDefinition;
struct ZoneListEntry;

/** Materialize one runtime territory from a deck-list zone entry and a unique instance id. */
EnergyZone energy_zone_from_list_entry(const ZoneListEntry& entry, const std::string& instance_id);

struct EnergyZoneDeck {
    std::vector<EnergyZone> deck;
    /** Satisfies `std::unordered_map::operator[]`; deck is empty until replaced by `EnergyZoneDeck(rng)`. */
    EnergyZoneDeck() = default;
    explicit EnergyZoneDeck(std::mt19937& rng);
    static EnergyZoneDeck from_deck_list(const DeckListDefinition& deck_list, std::mt19937& rng);
    std::vector<EnergyZone> draw(int amount = 3);
    void shuffle_back(const std::vector<EnergyZone>& zones, std::mt19937& rng);
};

}  // namespace tactics
