#pragma once

#include "tactics/cards/card_catalog.hpp"
#include "tactics/cards/card_instances.hpp"
#include "tactics/cards/cards.hpp"
#include "tactics/common/types.hpp"
#include "tactics/entities/entity.hpp"

#include <optional>
#include <utility>
#include <vector>

namespace tactics {

class GameState;

/** One rules action a player can take (validate, then execute). */
class GameAction {
public:
    virtual ~GameAction() = default;
    ActionType action_type{ActionType::Special};
    int player_id{0};
    virtual bool requires_active_player() const { return true; }
    virtual std::map<EnergyType, int> get_cost(const GameState& game) const { (void)game; return {}; }
    virtual ActionResult validate(GameState& game) = 0;
    virtual ActionResult execute(GameState& game) = 0;
    virtual bool allows_during_own_pending_move() const { return false; }
};

/** Starts a unit move: pick a goal cell before confirm. */
class MovePreviewAction : public GameAction {
public:
    MovePreviewAction(std::shared_ptr<Unit> unit, int player_id, std::pair<int, int> goal_cell);
    bool allows_during_own_pending_move() const override { return true; }
    ActionResult validate(GameState& game) override;
    ActionResult execute(GameState& game) override;

private:
    std::shared_ptr<Unit> unit_;
    std::pair<int, int> goal_;
};

class MovePendingRotateAction : public GameAction {
public:
    explicit MovePendingRotateAction(int player_id, int delta_quarters_cw);
    bool allows_during_own_pending_move() const override { return true; }
    ActionResult validate(GameState& game) override;
    ActionResult execute(GameState& game) override;

private:
    int delta_quarters_cw_{0};
};

class MoveConfirmAction : public GameAction {
public:
    explicit MoveConfirmAction(int player_id);
    bool allows_during_own_pending_move() const override { return true; }
    ActionResult validate(GameState& game) override;
    ActionResult execute(GameState& game) override;
};

class MoveCancelAction : public GameAction {
public:
    explicit MoveCancelAction(int player_id);
    bool allows_during_own_pending_move() const override { return true; }
    ActionResult validate(GameState& game) override;
    ActionResult execute(GameState& game) override;
};

class AttackAction : public GameAction {
public:
    AttackAction(std::shared_ptr<Unit> actor, int player_id, std::pair<int, int> target, bool ranged,
        std::optional<std::pair<int, int>> soul_steal_heal_base_cell = std::nullopt);
    std::map<EnergyType, int> get_cost(const GameState& game) const override { (void)game; return {}; }
    ActionResult validate(GameState& game) override;
    ActionResult execute(GameState& game) override;
    std::string attacker_id_for_undo() const { return actor_ ? actor_->entity_id : std::string{}; }

private:
    std::shared_ptr<Unit> actor_;
    std::pair<int, int> target_;
    bool ranged_{false};
    std::optional<std::pair<int, int>> soul_steal_heal_base_cell_;
};

class DefendAction : public GameAction {
public:
    DefendAction(std::shared_ptr<Unit> actor, int player_id);
    ActionResult validate(GameState& game) override;
    ActionResult execute(GameState& game) override;

private:
    std::shared_ptr<Unit> actor_;
};

class DashAction : public GameAction {
public:
    DashAction(std::shared_ptr<Unit> actor, int player_id);
    bool allows_during_own_pending_move() const override { return true; }
    ActionResult validate(GameState& game) override;
    ActionResult execute(GameState& game) override;

private:
    std::shared_ptr<Unit> actor_;
};

class RecoverAction : public GameAction {
public:
    RecoverAction(std::shared_ptr<Unit> actor, int player_id);
    ActionResult validate(GameState& game) override;
    ActionResult execute(GameState& game) override;

private:
    std::shared_ptr<Unit> actor_;
};

class DeployAction : public GameAction {
public:
    DeployAction(CardInstanceId card, int player_id, std::pair<int, int> target, CardPlayZone zone = CardPlayZone::Hand);
    std::map<EnergyType, int> get_cost(const GameState& game) const override;
    ActionResult validate(GameState& game) override;
    ActionResult execute(GameState& game) override;

    // Accessors for the phase-undo system (reverse a deploy when match settings allow it).
    CardInstanceId card_id_for_undo() const { return card_id_; }
    bool played_from_reserves_for_undo() const { return zone_ == CardPlayZone::Reserves; }

private:
    CardInstanceId card_id_;
    std::pair<int, int> target_;
    CardPlayZone zone_{CardPlayZone::Hand};
};

class CastSpellAction : public GameAction {
public:
    CastSpellAction(CardInstanceId card, int player_id, std::map<std::string, int> targets, CardPlayZone zone = CardPlayZone::Hand);
    CastSpellAction(CardInstanceId card, int player_id, std::map<std::string, int> targets, std::string stack_target_id,
        CardPlayZone zone = CardPlayZone::Hand);
    CastSpellAction(CardInstanceId card, int player_id, std::map<std::string, int> targets, std::shared_ptr<Entity> focus_caster,
        CardPlayZone zone = CardPlayZone::Hand);
    CastSpellAction(CardInstanceId card, int player_id, std::map<std::string, int> targets, std::string stack_target_id,
        std::shared_ptr<Entity> focus_caster, CardPlayZone zone = CardPlayZone::Hand);
    bool allows_during_own_pending_move() const override { return true; }
    bool requires_active_player() const override { return zone_ == CardPlayZone::Reserves; }
    std::map<EnergyType, int> get_cost(const GameState& game) const override;
    ActionResult validate(GameState& game) override;
    ActionResult execute(GameState& game) override;
    CardInstanceId card_id_for_undo() const { return card_id_; }
    bool played_from_reserves_for_undo() const { return zone_ == CardPlayZone::Reserves; }
    /** Set the chosen X value for variable-cost spells before calling perform_action. */
    void set_x_amount(int x) { x_amount_ = std::max(0, x); }
    int x_amount() const { return x_amount_; }
    /** Modal spells ("choose one of N"): the selected mode index into the card's `modes`.
     *  -1 = none chosen (required before a modal card can be cast). */
    void set_mode_index(int m) { mode_index_ = m; }
    int mode_index() const { return mode_index_; }
    /** Multicast spells: one target map per copy (size must equal multicast amount when required). */
    void set_multicast_targets(std::vector<std::map<std::string, int>> targets);

private:
    std::vector<std::map<std::string, int>> resolved_multicast_target_maps(const CardDefinition& def) const;
    ActionResult validate_spell_target_map(GameState& game, const CardDefinition& def, const SpellCardDefinition& spell,
        const std::map<std::string, int>& targets) const;

    CardInstanceId card_id_;
    std::map<std::string, int> targets_;
    std::vector<std::map<std::string, int>> multicast_targets_;
    std::string stack_target_id_;
    std::shared_ptr<Entity> focus_caster_;
    CardPlayZone zone_{CardPlayZone::Hand};
    int x_amount_{0};
    int mode_index_{-1};
};

class ActivateAbilityAction : public GameAction {
public:
    ActivateAbilityAction(std::shared_ptr<Entity> actor, int player_id, std::string ability_key, std::map<std::string, int> targets);
    ActivateAbilityAction(std::shared_ptr<Entity> actor, int player_id, std::string ability_key, std::map<std::string, int> targets,
        std::string stack_target_id);
    bool requires_active_player() const override { return false; }
    void set_x_amount(int x) { x_amount_ = std::max(0, x); }
    std::map<EnergyType, int> get_cost(const GameState& game) const override;
    ActionResult validate(GameState& game) override;
    ActionResult execute(GameState& game) override;

private:
    std::shared_ptr<Entity> actor_;
    std::string ability_key_;
    std::map<std::string, int> targets_;
    std::string stack_target_id_;
    std::optional<AbilitySpec> ability_;
    int x_amount_{0};
};

}  // namespace tactics