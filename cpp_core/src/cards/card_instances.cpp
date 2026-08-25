#include "tactics/cards/card_instances.hpp"

#include <algorithm>
#include <stdexcept>

namespace tactics {

CardInstanceId CardInstancePool::emplace(CardDefId definition_id, std::string public_id, int stockpile_amount)
{
    CardInstance inst;
    inst.id = CardInstanceId{next_id_++};
    inst.definition_id = definition_id;
    inst.public_id = std::move(public_id);
    inst.stockpile_amount = std::max(0, stockpile_amount);
    inst.stockpile_remaining = inst.stockpile_amount;
    inst.stockpile_used_this_turn = false;
    const std::size_t index = instances_.size();
    instances_.push_back(std::move(inst));
    id_to_index_[instances_.back().id.value] = index;
    return instances_.back().id;
}

CardInstanceId CardInstancePool::import_instance(CardInstance inst)
{
    if (!inst.id.is_valid()) {
        return {};
    }
    const std::size_t index = instances_.size();
    instances_.push_back(std::move(inst));
    id_to_index_[instances_.back().id.value] = index;
    next_id_ = std::max(next_id_, instances_.back().id.value + 1);
    return instances_.back().id;
}

const CardInstance* CardInstancePool::try_get(const CardInstanceId id) const
{
    if (!id.is_valid()) {
        return nullptr;
    }
    const auto it = id_to_index_.find(id.value);
    if (it == id_to_index_.end()) {
        return nullptr;
    }
    return &instances_[it->second];
}

CardInstance* CardInstancePool::try_get(const CardInstanceId id)
{
    return const_cast<CardInstance*>(static_cast<const CardInstancePool*>(this)->try_get(id));
}

const CardInstance& CardInstancePool::at(const CardInstanceId id) const
{
    const CardInstance* inst = try_get(id);
    if (!inst) {
        throw std::out_of_range("CardInstancePool::at invalid id");
    }
    return *inst;
}

CardInstance& CardInstancePool::at(const CardInstanceId id)
{
    return const_cast<CardInstance&>(static_cast<const CardInstancePool*>(this)->at(id));
}

void CardInstancePool::clear()
{
    instances_.clear();
    id_to_index_.clear();
    next_id_ = 1;
}

}  // namespace tactics
