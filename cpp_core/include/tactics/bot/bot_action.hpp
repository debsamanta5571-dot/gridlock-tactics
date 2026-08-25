#pragma once

#include "tactics/cards/card_instances.hpp"
#include "tactics/cards/cards.hpp"
#include "tactics/common/types.hpp"
#include "tactics/core/game_state.hpp"

#include <map>
#include <string>
#include <vector>

namespace tactics::bot {

enum class BotActionKind {
    ChooseEnergyZone,
    SkipEnergyZone,
    Deploy,
    DeployReserve,
    MovePreview,
    MoveConfirm,
    MoveCancel,
    MoveRotate,
    CastSpell,
    CastSpellReserve,
    ActivateAbility,
    DeclareAttack,
    AttackUndeclare,
    CommitAttackDeclaration,
    EndMainPhase,
    PassPriority,
    Defend,
    Dash,
    Recover,
    Undo,
    BatchCancel,
    DiscardHandCard,
    ScanDiscard,
    ScanFinish,
    SkipTerritoryTarget,
    ResolveTerritoryTarget,
    SkipTerritoryLoot,
    TerritoryLootDiscard,
    UseLand,
    ResumeCombatViz,
};

struct BotAction {
    BotActionKind kind{};
    int player_id{0};
    CardInstanceId card_id{};
    CardPlayZone play_zone{CardPlayZone::Hand};
    std::string entity_id;
    /** Focus-spell casting unit (entity_id). */
    std::string focus_caster_entity_id;
    /** Stack-item id for stack-target spells/abilities. */
    std::string stack_target_id;
    int x{0};
    int y{0};
    bool ranged{false};
    int energy_zone_index_1based{0};
    /** 1-based "use land" ability index on the territory in `energy_zone_index_1based`. */
    int land_ability_index_1based{0};
    int hand_index_1based{0};
    int quarter_turns_cw{1};
    std::map<std::string, int> spell_targets;
    std::string ability_key;
    int spell_x_amount{0};
    /** Modal spells: chosen mode index into the card's `modes` (-1 = not a modal cast). */
    int spell_mode{-1};
    std::vector<std::map<std::string, int>> multicast_spell_targets;
};

const char* bot_action_kind_name(BotActionKind kind);

/** True when two bot actions refer to the same concrete play (for fallback dedup). */
bool bot_actions_equivalent(const BotAction& a, const BotAction& b);

/** Human-readable detail for verbose match logs (card names, cells, units). */
std::string format_bot_action_detail(const tactics::GameState& game, const BotAction& action);

}  // namespace tactics::bot