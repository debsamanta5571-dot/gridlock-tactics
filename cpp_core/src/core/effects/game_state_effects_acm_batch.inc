// game_state_effects_acm_batch.inc — shard of register_default_effects()
    static_cast<void>(stack_manager.register_effect("grant_explosive_graft",
        [](GameState& game, const StackItem& item) {
            const auto resolved = resolve_stack_board_target(game, item, "grant_explosive_graft");
            if (!resolved.status.ok || resolved.status.message.find("(fizzled)") != std::string::npos) {
                return resolved.status;
            }
            if (!resolved.target || resolved.target->entity_type != "unit") {
                return ActionResult{false, "grant_explosive_graft: requires an allied unit", {}};
            }
            add_entity_effect(*resolved.target, "stunned", 1);
            TemporaryEntityEffect eff;
            eff.effect_id = "explosive_graft";
            eff.source_id = item.source_entity_id;
            eff.name = "Explosive Graft";
            eff.rules_text = "Next time an enemy damages this unit, all surrounding units take 5 damage and 2 overload.";
            eff.expire_on = "";
            game.add_temporary_effect(resolved.target, std::move(eff));
            return ActionResult{true, resolved.target->entity_id + " stunned with Explosive Graft marker", {}};
        }));

    // Focused Lethal Draw: deal `amount` damage; if target dies, controller draws 1 card.
    static_cast<void>(stack_manager.register_effect("focused_lethal_draw",
        [](GameState& game, const StackItem& item) {
            const auto resolved = resolve_stack_board_target(game, item, "focused_lethal_draw");
            if (!resolved.status.ok || resolved.status.message.find("(fizzled)") != std::string::npos) {
                return resolved.status;
            }
            if (!resolved.target || resolved.target->entity_type != "unit") {
                return ActionResult{false, "focused_lethal_draw: requires a unit target", {}};
            }
            int amount = 4;
            if (const auto it = item.payload.find(effect_keys::kPayloadAmount); it != item.payload.end()) {
                amount = it->second;
            }
            amount += item.ability_damage_bonus;
            const std::string target_id = resolved.target->entity_id;
            DamagePacket pkt;
            pkt.source = game.board.all_entities_map.count(item.source_entity_id)
                ? game.board.all_entities_map.at(item.source_entity_id) : nullptr;
            pkt.target = resolved.target;
            pkt.amount = amount;
            pkt.rolled_amount = amount;
            pkt.from_basic_attack = false;
            pkt.source_label = "1G-GY";
            apply_damage_packet(game, pkt);
            bool killed = !game.board.all_entities_map.contains(target_id)
                || game.board.all_entities_map.at(target_id)->current_health <= 0;
            if (killed) {
                game.draw_cards(item.controller_id, 1);
                return ActionResult{true,
                    "1G-GY dealt " + std::to_string(amount) + " damage (lethal) — drew 1 card", {}};
            }
            return ActionResult{true,
                "1G-GY dealt " + std::to_string(amount) + " damage", {}};
        }));

