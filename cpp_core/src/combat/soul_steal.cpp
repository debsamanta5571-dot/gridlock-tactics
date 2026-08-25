#include "tactics/combat/soul_steal.hpp"

#include "tactics/common/effect_keys.hpp"
#include "tactics/core/game_state.hpp"
#include "tactics/entities/entity.hpp"

namespace tactics {

std::vector<std::shared_ptr<Entity>> allied_bases_for_controller(const GameState& game, const int controller_id)
{
    std::vector<std::shared_ptr<Entity>> out;
    for (const auto& [_, ent] : game.board.all_entities_map) {
        if (!ent || !entity_is_base(*ent) || !ent->owner) {
            continue;
        }
        if (!teams_hostile(game, controller_id, *ent->owner)) {
            out.push_back(ent);
        }
    }
    return out;
}

ActionResult validate_soul_steal_heal_base_target(const GameState& game, const int controller_id,
                                                  const std::map<std::string, int>& targets, const bool needs_soul_steal)
{
    if (!needs_soul_steal) {
        return {true, "Soul Steal not required", {}};
    }
    const std::vector<std::shared_ptr<Entity>> bases = allied_bases_for_controller(game, controller_id);
    if (bases.empty()) {
        return {false, "Soul Steal: no allied base on the board", {}};
    }
    if (bases.size() == 1) {
        return {true, "Soul Steal: only one allied base", {}};
    }
    const auto xit = targets.find(effect_keys::kHealBaseX);
    const auto yit = targets.find(effect_keys::kHealBaseY);
    const bool has_cell = xit != targets.end() && yit != targets.end();
    if ((xit != targets.end()) != (yit != targets.end())) {
        return {false, "Soul Steal base target must include both heal_base_x and heal_base_y", {}};
    }
    if (!has_cell) {
        return {false, "Soul Steal: choose which allied base to heal (heal_base_x heal_base_y)", {}};
    }
    auto ent = game.board.entity_at(xit->second, yit->second);
    if (!ent || !entity_is_base(*ent) || !ent->owner) {
        return {false, "Soul Steal: no allied base at the chosen cell", {}};
    }
    if (teams_hostile(game, controller_id, *ent->owner)) {
        return {false, "Soul Steal: chosen base is not allied", {}};
    }
    return {true, "Soul Steal base target allowed", {}};
}

std::shared_ptr<Entity> resolve_soul_steal_heal_base(const GameState& game, const int controller_id,
                                                     const std::map<std::string, int>& targets)
{
    const std::vector<std::shared_ptr<Entity>> bases = allied_bases_for_controller(game, controller_id);
    if (bases.empty()) {
        return nullptr;
    }
    if (bases.size() == 1) {
        return bases.front();
    }
    const auto xit = targets.find(effect_keys::kHealBaseX);
    const auto yit = targets.find(effect_keys::kHealBaseY);
    if (xit == targets.end() || yit == targets.end()) {
        return nullptr;
    }
    auto ent = game.board.entity_at(xit->second, yit->second);
    if (!ent || !entity_is_base(*ent) || !ent->owner || teams_hostile(game, controller_id, *ent->owner)) {
        return nullptr;
    }
    return ent;
}

}  // namespace tactics
