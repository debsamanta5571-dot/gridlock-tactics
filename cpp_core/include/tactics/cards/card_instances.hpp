#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace tactics {

/** Stable index into the loaded card catalog (`CardCatalog::definitions`). */
struct CardDefId {
    uint32_t value{UINT32_MAX};
    bool is_valid() const { return value != UINT32_MAX; }
    friend bool operator==(const CardDefId& a, const CardDefId& b) { return a.value == b.value; }
    friend bool operator!=(const CardDefId& a, const CardDefId& b) { return !(a == b); }
};

inline constexpr CardDefId kInvalidCardDefId{UINT32_MAX};

/** Per-match card copy identity (zones store these, not full card objects). */
struct CardInstanceId {
    uint32_t value{0};
    bool is_valid() const { return value != 0; }
    friend bool operator==(const CardInstanceId& a, const CardInstanceId& b) { return a.value == b.value; }
    friend bool operator!=(const CardInstanceId& a, const CardInstanceId& b) { return !(a == b); }
};

/** Mutable per-copy state; static card data lives in `CardDefinition`. */
struct CardInstance {
    CardInstanceId id;
    CardDefId definition_id{kInvalidCardDefId};
    /** Stable string for entity `source_card_id` and legacy snapshots (`definition_key_copyIndex`). */
    std::string public_id;
    int stockpile_amount{0};
    int stockpile_remaining{0};
    bool stockpile_used_this_turn{false};
    bool stockpile_double_play_used_this_turn{false};
    /** Temporary granted cards: decremented each owner turn-end while in hand; purgatory at 0 or when cast/deployed. */
    int hand_expires_after_owner_turn_ends{0};
};

inline bool card_instance_is_temporary_granted(const CardInstance& inst)
{
    return inst.hand_expires_after_owner_turn_ends > 0;
}

/** Dense storage for all card copies owned by one player's deck zones. */
class CardInstancePool {
public:
    CardInstanceId emplace(CardDefId definition_id, std::string public_id, int stockpile_amount);

    /** Restore a snapshot instance with a fixed id (for match load). */
    CardInstanceId import_instance(CardInstance inst);

    const CardInstance* try_get(CardInstanceId id) const;
    CardInstance* try_get(CardInstanceId id);
    const CardInstance& at(CardInstanceId id) const;
    CardInstance& at(CardInstanceId id);

    const std::vector<CardInstance>& all() const { return instances_; }
    std::size_t size() const { return instances_.size(); }
    void clear();

private:
    std::vector<CardInstance> instances_;
    std::unordered_map<uint32_t, std::size_t> id_to_index_;
    uint32_t next_id_{1};
};

}  // namespace tactics

namespace std {

template <>
struct hash<tactics::CardDefId> {
    size_t operator()(const tactics::CardDefId& id) const noexcept
    {
        return hash<uint32_t>{}(id.value);
    }
};

}  // namespace std
