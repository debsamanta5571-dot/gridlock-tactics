// game_state_effects_draw_energy.inc — shard of register_default_effects()
    static_cast<void>(stack_manager.register_effect("apply_silenced_owner_turn_end", [](GameState& game, const StackItem& item) {
        const auto resolved = resolve_stack_board_target(game, item, "apply_silenced_owner_turn_end");
        if (!resolved.status.ok || resolved.status.message.find("(fizzled)") != std::string::npos) {
            return resolved.status;
        }
        int amount = 1;
        if (const auto it = item.payload.find(effect_keys::kPayloadAmount); it != item.payload.end()) {
            amount = it->second;
        }
        amount = std::max(0, amount);
        if (!add_entity_effect(*resolved.target, "silenced", amount, effect_keys::kSilencedOwnerTurnEndSource)) {
            return ActionResult{true, resolved.target->entity_id + " is immune to silenced (fizzled)", {}};
        }
        return ActionResult{true, "Applied " + std::to_string(amount)
            + " timed silenced stack(s) to " + resolved.target->entity_id + " until owner's turn end", {}};
    }));
    register_status_effect("apply_stealth", "stealth", "apply_stealth");
    register_status_effect("apply_jammed", "jammed", "apply_jammed");
    register_status_effect("apply_vulnerable", "vulnerable", "apply_vulnerable");

    static_cast<void>(stack_manager.register_effect("apply_vulnerable_turn_end", [](GameState& game, const StackItem& item) {
        const auto resolved = resolve_stack_board_target(game, item, "apply_vulnerable_turn_end");
        if (!resolved.status.ok || resolved.status.message.find("(fizzled)") != std::string::npos) {
            return resolved.status;
        }
        int amount = 1;
        if (const auto it = item.payload.find(effect_keys::kPayloadAmount); it != item.payload.end()) {
            amount = it->second;
        }
        amount = std::max(0, amount);
        if (!add_entity_effect(*resolved.target, "vulnerable", amount)) {
            return ActionResult{true, resolved.target->entity_id + " is immune to vulnerable (fizzled)", {}};
        }
        const auto turn_owner = game.turn_manager.current_player();
        if (!turn_owner) {
            return ActionResult{false, "apply_vulnerable_turn_end: no active turn", {}};
        }
        game.vulnerable_turn_end_pending_.push_back(
            VulnerableTurnEndEntry{*turn_owner, resolved.target->entity_id, amount});
        return ActionResult{true, "Applied " + std::to_string(amount)
            + " Vulnerable to " + resolved.target->entity_id + " until end of this turn", {}};
    }));

    register_status_effect("apply_rooted", "rooted", "apply_rooted");
    register_status_effect("apply_stunned", "stunned", "apply_stunned");

    // Overload: stacks never decay mid-turn. At end of the affected entity's owner's turn,
    // >= kOverloadExplosionThreshold stacks triggers explosion (see process_end_of_turn_dot).
    static_cast<void>(stack_manager.register_effect("apply_overload", [](GameState& game, const StackItem& item) {
        const auto resolved = resolve_stack_board_target(game, item, "apply_overload");
        if (!resolved.status.ok || resolved.status.message.find("(fizzled)") != std::string::npos) {
            return resolved.status;
        }
        int amount = 1;
        if (const auto it = item.payload.find(effect_keys::kPayloadAmount); it != item.payload.end()) {
            amount = it->second;
        }
        amount = std::max(0, amount);
        const std::string msg = game.apply_overload_stacks(resolved.target, amount);
        return ActionResult{true, msg.empty() ? "Applied overload (fizzled)" : msg, {}};
    }));

    const auto register_on_damage_status_adjacent = [](
                                                        GameState& game, const StackItem& item, const char* display_name,
                                                        const char* temp_effect_id, const char* temp_name, const char* temp_rules) {
        const auto resolved = resolve_stack_board_target(game, item, display_name);
        if (!resolved.status.ok || resolved.status.message.find("(fizzled)") != std::string::npos) {
            return resolved.status;
        }
        if (!resolved.target || (resolved.target->entity_type != "unit" && !entity_is_structure(*resolved.target))) {
            return ActionResult{false, std::string(display_name) + " requires an allied unit or structure", {}};
        }
        const auto src_it = game.board.all_entities_map.find(item.source_entity_id);
        if (src_it == game.board.all_entities_map.end() || !src_it->second) {
            return ActionResult{false, "Source unit is no longer on the board", {}};
        }
        const bool is_self = resolved.target->entity_id == src_it->second->entity_id;
        if (!is_self) {
            if (!resolved.target->owner || !src_it->second->owner ||
                teams_hostile(game, *resolved.target->owner, *src_it->second->owner)) {
                return ActionResult{false, std::string(display_name) + " must target an allied unit or structure", {}};
            }
        }
        auto& effects = resolved.target->temporary_effects;
        // On-damage primers are mutually exclusive: applying one removes all existing primers.
        effects.erase(std::remove_if(effects.begin(), effects.end(),
                          [](const TemporaryEntityEffect& effect) {
                              return effect.effect_id == effect_keys::kOnDamageApplyOverloadEffectId
                                  || effect.effect_id == effect_keys::kOnDamageApplyJammedEffectId
                                  || effect.effect_id == effect_keys::kMedicalOverrideEffectId;
                          }),
            effects.end());
        TemporaryEntityEffect primer;
        primer.effect_id = temp_effect_id;
        primer.source_id = item.item_id;
        primer.name = temp_name;
        primer.rules_text = temp_rules;
        primer.expire_on = "owner_turn_end";
        primer.remaining_turns = 1;
        TemporaryEntityEffect primer_for_mirror = primer;  // copy before move
        game.add_temporary_effect(resolved.target, std::move(primer));
        propagate_temporary_buff_to_surrounding_mirror_passives(game, *resolved.target, std::move(primer_for_mirror));
        return ActionResult{true,
            std::string(temp_name) + " on " + resolved.target->entity_id + " until end of its owner's turn",
            {}};
    };

    const auto register_next_ability_on_hit_primer_self =
        [](GameState& game, const StackItem& item, const char* display_name, const char* temp_effect_id,
            const char* temp_name, const char* temp_rules) -> ActionResult {
            if (item.source_entity_id.empty()) {
                return ActionResult{false, std::string(display_name) + " requires a source unit", {}};
            }
            const auto src_it = game.board.all_entities_map.find(item.source_entity_id);
            if (src_it == game.board.all_entities_map.end() || !src_it->second) {
                return ActionResult{false, "Source unit is no longer on the board", {}};
            }
            // Next-ability primers stack: only refresh the primer being granted.
            strip_on_damage_primer_by_id(*src_it->second, temp_effect_id);
            TemporaryEntityEffect primer;
            primer.effect_id = temp_effect_id;
            primer.source_id = item.item_id;
            primer.name = temp_name;
            primer.rules_text = temp_rules;
            primer.expire_on = "owner_turn_end";
            primer.remaining_turns = 1;
            game.add_temporary_effect(src_it->second, std::move(primer));
            return ActionResult{true,
                std::string(temp_name) + " primed on " + src_it->second->entity_id + " for its next damaging ability",
                {}};
        };

    static_cast<void>(stack_manager.register_effect("grant_next_ability_movement_reduction_self",
        [register_next_ability_on_hit_primer_self](GameState& game, const StackItem& item) {
            return register_next_ability_on_hit_primer_self(game, item, "grant_next_ability_movement_reduction_self",
                effect_keys::kOnDamageApplyMovementReductionNextAbilityEffectId, "Drag Line",
                "This unit's next damaging ability reduces 1 movement to each unit it damages until end of that unit's owner's turn.");
        }));

    static_cast<void>(stack_manager.register_effect("grant_next_ability_rooted_self",
        [register_next_ability_on_hit_primer_self](GameState& game, const StackItem& item) {
            return register_next_ability_on_hit_primer_self(game, item, "grant_next_ability_rooted_self",
                effect_keys::kOnDamageApplyRootedNextAbilityEffectId, "Anchor Line",
                "This unit's next damaging ability roots each unit it damages.");
        }));

    // Ally-targeted versions: prime a chosen allied unit (or self) instead of always self.
    const auto register_next_ability_on_hit_primer_ally =
        [](GameState& game, const StackItem& item, const char* display_name, const char* temp_effect_id,
            const char* temp_name, const char* temp_rules) -> ActionResult {
            const auto resolved = resolve_stack_board_target(game, item, display_name);
            if (!resolved.status.ok || resolved.status.message.find("(fizzled)") != std::string::npos) {
                return resolved.status;
            }
            if (!resolved.target || resolved.target->entity_type != "unit") {
                return ActionResult{false, std::string(display_name) + " requires an allied unit target", {}};
            }
            const auto src_it = game.board.all_entities_map.find(item.source_entity_id);
            if (src_it == game.board.all_entities_map.end() || !src_it->second) {
                return ActionResult{false, "Source unit is no longer on the board", {}};
            }
            const bool is_self = resolved.target->entity_id == src_it->second->entity_id;
            if (!is_self) {
                if (!resolved.target->owner || !src_it->second->owner ||
                    teams_hostile(game, *resolved.target->owner, *src_it->second->owner)) {
                    return ActionResult{false, std::string(display_name) + " must target an allied unit or self", {}};
                }
            }
            strip_on_damage_primer_by_id(*resolved.target, temp_effect_id);
            TemporaryEntityEffect primer;
            primer.effect_id = temp_effect_id;
            primer.source_id = item.item_id;
            primer.name = temp_name;
            primer.rules_text = temp_rules;
            primer.expire_on = "owner_turn_end";
            primer.remaining_turns = 1;
            game.add_temporary_effect(resolved.target, std::move(primer));
            return ActionResult{true,
                std::string(temp_name) + " primed on " + resolved.target->entity_id + " for its next damaging ability",
                {}};
        };

    static_cast<void>(stack_manager.register_effect("grant_next_ability_movement_reduction_ally",
        [register_next_ability_on_hit_primer_ally](GameState& game, const StackItem& item) {
            return register_next_ability_on_hit_primer_ally(game, item, "grant_next_ability_movement_reduction_ally",
                effect_keys::kOnDamageApplyMovementReductionNextAbilityEffectId, "Drag Line",
                "This unit's next damaging ability reduces 1 movement to each unit it damages until end of that unit's owner's turn.");
        }));

    static_cast<void>(stack_manager.register_effect("grant_next_ability_rooted_ally",
        [register_next_ability_on_hit_primer_ally](GameState& game, const StackItem& item) {
            return register_next_ability_on_hit_primer_ally(game, item, "grant_next_ability_rooted_ally",
                effect_keys::kOnDamageApplyRootedNextAbilityEffectId, "Anchor Line",
                "This unit's next damaging ability roots each unit it damages.");
        }));

    static_cast<void>(stack_manager.register_effect("grant_on_damage_apply_overload_adjacent",
        [register_next_ability_on_hit_primer_ally](GameState& game, const StackItem& item) {
            return register_next_ability_on_hit_primer_ally(game, item, "grant_on_damage_apply_overload_adjacent",
                effect_keys::kOnDamageApplyOverloadNextAbilityEffectId, "Static Charge",
                "This unit's next attack or ability applies 1 overload to each entity it damages.");
        }));

    static_cast<void>(stack_manager.register_effect("grant_on_damage_apply_jammed_adjacent",
        [register_next_ability_on_hit_primer_ally](GameState& game, const StackItem& item) {
            return register_next_ability_on_hit_primer_ally(game, item, "grant_on_damage_apply_jammed_adjacent",
                effect_keys::kOnDamageApplyJammedNextAbilityEffectId, "Jamming Array",
                "This unit's next attack or ability applies 1 jammed to each entity it damages.");
        }));

    // Disregarded Rangers Serrated Bolt: bearer applies bleed to victims of their next attack or ability.
    static_cast<void>(stack_manager.register_effect("grant_next_bleed_self",
        [register_next_ability_on_hit_primer_self](GameState& game, const StackItem& item) {
            return register_next_ability_on_hit_primer_self(game, item, "grant_next_bleed_self",
                effect_keys::kOnDamageApplyBleedNextAbilityEffectId, "Serrated Bolt",
                "This unit's next attack or ability applies bleed to each unit it damages.");
        }));

    // Medical Override: when the buffed unit's deal_damage ability hits an ally, that ally is
    // healed for the damage amount instead of being damaged. Enemies are still damaged normally.
    // Mutually exclusive with the other on-damage primers.
    static_cast<void>(stack_manager.register_effect("grant_medical_override",
        [register_on_damage_status_adjacent](GameState& game, const StackItem& item) {
            return register_on_damage_status_adjacent(game, item, "grant_medical_override",
                effect_keys::kMedicalOverrideEffectId, "Medical Override",
                "This unit's damaging abilities heal allied targets for the damage amount instead of damaging them.");
        }));

    static_cast<void>(stack_manager.register_effect("remove_silenced", [](GameState& game, const StackItem& item) {
        const auto resolved = resolve_stack_board_target(game, item, "remove_silenced");
        if (!resolved.status.ok || resolved.status.message.find("(fizzled)") != std::string::npos) {
            return resolved.status;
        }
        int amount = 1;
        if (const auto it = item.payload.find(effect_keys::kPayloadAmount); it != item.payload.end()) {
            amount = it->second;
        }
        amount = std::max(0, amount);
        const int before = entity_effect_amount(*resolved.target, "silenced");
        reduce_entity_effect(*resolved.target, "silenced", amount);
        const int removed = before - entity_effect_amount(*resolved.target, "silenced");
        return ActionResult{true, "Removed " + std::to_string(removed) + " silenced stack(s) from " + resolved.target->entity_id, {}};
    }));

    static_cast<void>(stack_manager.register_effect("draw_cards", [](GameState& game, const StackItem& item) {
        if (game.players_decks.find(item.controller_id) == game.players_decks.end()) {
            return ActionResult{false, "draw_cards: unknown controller player", {}};
        }
        int amount = 1;
        if (auto it = item.payload.find(effect_keys::kPayloadAmount); it != item.payload.end()) amount = it->second;
        if (amount < 0) return ActionResult{false, "draw_cards: negative amount", {}};
        auto drawn = game.draw_cards(item.controller_id, amount);
        return ActionResult{true, "Player " + std::to_string(item.controller_id) + " drew " + std::to_string(drawn.size()) + " card(s)", {}};
    }));
    static_cast<void>(stack_manager.register_effect("draw_cards_for_player", [](GameState& game, const StackItem& item) {
        const auto pit = item.targets.find(effect_keys::kTargetPlayerSeat);
        if (pit == item.targets.end()) {
            return ActionResult{false, "draw_cards_for_player: requires target_player", {}};
        }
        const int target_seat = pit->second;
        if (game.players_decks.find(target_seat) == game.players_decks.end()) {
            return ActionResult{false, "draw_cards_for_player: unknown target player", {}};
        }
        int amount = 3;
        if (const auto it = item.payload.find(effect_keys::kPayloadAmount); it != item.payload.end()) {
            amount = it->second;
        }
        if (amount < 0) {
            return ActionResult{false, "draw_cards_for_player: negative amount", {}};
        }
        const auto drawn = game.draw_cards(target_seat, amount);
        return ActionResult{true, "Player " + std::to_string(target_seat) + " drew " + std::to_string(drawn.size())
            + " card(s)", {}};
    }));
    static_cast<void>(stack_manager.register_effect("move_to_purgatory", [](GameState& game, const StackItem& item) {
        const auto pit = item.targets.find(effect_keys::kTargetPlayerSeat);
        if (pit == item.targets.end()) {
            return ActionResult{false, "move_to_purgatory: requires target_player", {}};
        }
        const int target_seat = pit->second;
        const auto key_it = item.string_payload.find("card_public_id");
        if (key_it == item.string_payload.end() || key_it->second.empty()) {
            return ActionResult{false, "move_to_purgatory: requires card_public_id", {}};
        }
        if (!game.move_card_to_purgatory_by_public_id(target_seat, key_it->second)) {
            return ActionResult{false, "move_to_purgatory: card not found for player " + std::to_string(target_seat), {}};
        }
        return ActionResult{true, "Moved " + key_it->second + " to P" + std::to_string(target_seat) + " purgatory", {}};
    }));
    static_cast<void>(stack_manager.register_effect("draw_unit_cards", [](GameState& game, const StackItem& item) {
        if (game.players_decks.find(item.controller_id) == game.players_decks.end()) {
            return ActionResult{false, "draw_unit_cards: unknown controller player", {}};
        }
        int amount = 1;
        if (auto it = item.payload.find(effect_keys::kPayloadAmount); it != item.payload.end()) amount = it->second;
        if (amount < 0) return ActionResult{false, "draw_unit_cards: negative amount", {}};
        auto drawn = game.draw_unit_cards(item.controller_id, amount);
        return ActionResult{true, "Player " + std::to_string(item.controller_id) + " drew " + std::to_string(drawn.size())
            + " unit card(s)", {}};
    }));
    static_cast<void>(stack_manager.register_effect("draw_spell_cards", [](GameState& game, const StackItem& item) {
        if (game.players_decks.find(item.controller_id) == game.players_decks.end()) {
            return ActionResult{false, "draw_spell_cards: unknown controller player", {}};
        }
        int amount = 1;
        if (auto it = item.payload.find(effect_keys::kPayloadAmount); it != item.payload.end()) {
            amount = it->second;
        }
        int max_total_cost = INT_MAX;
        if (auto it = item.payload.find("max_total_cost"); it != item.payload.end()) {
            max_total_cost = it->second;
        }
        if (amount < 0) {
            return ActionResult{false, "draw_spell_cards: negative amount", {}};
        }
        if (max_total_cost < 0) {
            return ActionResult{false, "draw_spell_cards: negative max_total_cost", {}};
        }
        auto drawn = game.draw_spell_cards(item.controller_id, amount, max_total_cost);
        if (drawn.empty()) {
            return ActionResult{false, "No spell in deck costing " + std::to_string(max_total_cost) + " or less", {}};
        }
        return ActionResult{true, "Player " + std::to_string(item.controller_id) + " drew " + std::to_string(drawn.size())
            + " spell card(s)", {}};
    }));

    static_cast<void>(stack_manager.register_effect("draw_focus_spell_cards", [](GameState& game, const StackItem& item) {
        if (game.players_decks.find(item.controller_id) == game.players_decks.end()) {
            return ActionResult{false, "draw_focus_spell_cards: unknown controller player", {}};
        }
        int amount = 1;
        if (auto it = item.payload.find(effect_keys::kPayloadAmount); it != item.payload.end()) {
            amount = it->second;
        }
        int max_total_cost = INT_MAX;
        if (auto it = item.payload.find("max_total_cost"); it != item.payload.end()) {
            max_total_cost = it->second;
        }
        if (amount < 0) {
            return ActionResult{false, "draw_focus_spell_cards: negative amount", {}};
        }
        if (max_total_cost < 0) {
            return ActionResult{false, "draw_focus_spell_cards: negative max_total_cost", {}};
        }
        auto drawn = game.draw_focus_spell_cards(item.controller_id, amount, max_total_cost);
        if (drawn.empty()) {
            return ActionResult{false, "No focus spell in deck costing " + std::to_string(max_total_cost) + " or less", {}};
        }
        return ActionResult{true, "Player " + std::to_string(item.controller_id) + " drew " + std::to_string(drawn.size())
            + " focus spell card(s)", {}};
    }));

    static_cast<void>(stack_manager.register_effect("optional_discard_draw", [](GameState& game, const StackItem& item) {
        if (game.pending_territory_loot_) {
            return ActionResult{false, "optional_discard_draw: already awaiting choice", {}};
        }
        const auto deck_it = game.players_decks.find(item.controller_id);
        if (deck_it == game.players_decks.end()) {
            return ActionResult{false, "optional_discard_draw: unknown controller player", {}};
        }
        if (deck_it->second.hand.empty()) {
            return ActionResult{true, "optional_discard_draw: empty hand (fizzled)", {}};
        }
        PendingTerritoryLoot pending;
        pending.player_id = item.controller_id;
        game.pending_territory_loot_ = std::move(pending);
        return ActionResult{true, "Player " + std::to_string(item.controller_id)
            + " may discard a card to draw a card", {}};
    }));

    static_cast<void>(stack_manager.register_effect("scan", [](GameState& game, const StackItem& item) {
        if (game.pending_scan_) {
            return ActionResult{false, "scan: already awaiting selection", {}};
        }
        if (game.pending_discard_player_) {
            return ActionResult{false, "scan: finish end-of-turn discard first", {}};
        }
        const auto deck_it = game.players_decks.find(item.controller_id);
        if (deck_it == game.players_decks.end()) {
            return ActionResult{false, "scan: unknown controller player", {}};
        }
        int amount = 1;
        if (const auto it = item.payload.find(effect_keys::kPayloadAmount); it != item.payload.end()) {
            amount = it->second;
        }
        if (amount < 0) {
            return ActionResult{false, "scan: negative amount", {}};
        }
        const int peek_count = std::min(amount, static_cast<int>(deck_it->second.deck.size()));
        if (peek_count <= 0) {
            return ActionResult{true, "scan: deck empty (fizzled)", {}};
        }
        PendingScanSelection pending;
        pending.player_id = item.controller_id;
        pending.peeked.reserve(static_cast<size_t>(peek_count));
        for (int i = 0; i < peek_count; ++i) {
            pending.peeked.push_back(deck_it->second.deck[static_cast<size_t>(i)]);
        }
        game.pending_scan_ = std::move(pending);
        return ActionResult{true, "Player " + std::to_string(item.controller_id) + " is scanning "
            + std::to_string(peek_count) + " card(s)", {}};
    }));

    static_cast<void>(stack_manager.register_effect("gain_neutral", [](GameState& game, const StackItem& item) {
        int amount = 1;
        if (auto it = item.payload.find(effect_keys::kPayloadAmount); it != item.payload.end()) amount = it->second;
        amount = std::max(0, amount);
        auto itp = game.turn_manager.player_energy.find(item.controller_id);
        if (itp == game.turn_manager.player_energy.end()) {
            return ActionResult{false, "gain_neutral: unknown controller player", {}};
        }
        itp->second[EnergyType::Neutral] += amount;
        return ActionResult{true, "Player " + std::to_string(item.controller_id) + " gained " + std::to_string(amount) + " neutral floating energy", {}};
    }));

    // Grant N Omni flux energy to the caster (default 1; payload "amount"). Flux lives in the
    // tagged "spell_ability" float pool and is recorded in the flux-generated total (the same
    // mechanism as Sentinel's Tithe via credit_tagged_float).
    static_cast<void>(stack_manager.register_effect("gain_omni_flux", [](GameState& game, const StackItem& item) {
        int amount = 1;
        if (auto it = item.payload.find(effect_keys::kPayloadAmount); it != item.payload.end()) amount = it->second;
        amount = std::max(0, amount);
        game.turn_manager.player_tagged_float["spell_ability"][item.controller_id][EnergyType::Omni] += amount;
        game.turn_manager.player_flux_energy_generated_total[item.controller_id] += amount;
        return ActionResult{true,
            "Player " + std::to_string(item.controller_id) + " gained " + std::to_string(amount) + " Omni flux energy", {}};
    }));

    static_cast<void>(stack_manager.register_effect("gain_orange", [](GameState& game, const StackItem& item) {
        int amount = 1;
        if (auto it = item.payload.find(effect_keys::kPayloadAmount); it != item.payload.end()) {
            amount = it->second;
        }
        amount = std::max(0, amount);
        if (amount <= 0) return ActionResult{true, "", {}};
        // If the item specifies a tagged pool, route there; otherwise deposit into unrestricted pool.
        const auto pool_it = item.string_payload.find("pool");
        if (pool_it != item.string_payload.end() && !pool_it->second.empty()) {
            credit_tagged_float(game.turn_manager, pool_it->second, item.controller_id, EnergyType::Orange, amount);
            return ActionResult{true, "Player " + std::to_string(item.controller_id) + " gained " + std::to_string(amount) + " orange (" + pool_it->second + ")", {}};
        }
        auto itp = game.turn_manager.player_energy.find(item.controller_id);
        if (itp == game.turn_manager.player_energy.end()) {
            return ActionResult{false, "gain_orange: unknown controller player", {}};
        }
        itp->second[EnergyType::Orange] += amount;
        return ActionResult{true, "Player " + std::to_string(item.controller_id) + " gained " + std::to_string(amount) + " orange floating energy", {}};
    }));

    static_cast<void>(stack_manager.register_effect("gain_turquoise", [](GameState& game, const StackItem& item) {
        int amount = 1;
        if (auto it = item.payload.find(effect_keys::kPayloadAmount); it != item.payload.end()) {
            amount = it->second;
        }
        amount = std::max(0, amount);
        if (amount <= 0) {
            return ActionResult{true, "", {}};
        }
        const auto pool_it = item.string_payload.find("pool");
        if (pool_it != item.string_payload.end() && !pool_it->second.empty()) {
            credit_tagged_float(game.turn_manager, pool_it->second, item.controller_id, EnergyType::Turquoise, amount);
            return ActionResult{true, "Player " + std::to_string(item.controller_id) + " gained " + std::to_string(amount)
                + " turquoise (" + pool_it->second + ")", {}};
        }
        auto itp = game.turn_manager.player_energy.find(item.controller_id);
        if (itp == game.turn_manager.player_energy.end()) {
            return ActionResult{false, "gain_turquoise: unknown controller player", {}};
        }
        itp->second[EnergyType::Turquoise] += amount;
        return ActionResult{true, "Player " + std::to_string(item.controller_id) + " gained " + std::to_string(amount)
            + " turquoise floating energy", {}};
    }));

    static_cast<void>(stack_manager.register_effect("gain_turquoise_if_enemy_within", [](GameState& game, const StackItem& item) {
        if (item.source_entity_id.empty()) {
            return ActionResult{false, "gain_turquoise_if_enemy_within requires a source unit", {}};
        }
        const auto src_it = game.board.all_entities_map.find(item.source_entity_id);
        if (src_it == game.board.all_entities_map.end() || !src_it->second) {
            return ActionResult{false, "Source unit is no longer on the board", {}};
        }
        int range = 2;
        if (auto it = item.payload.find("range"); it != item.payload.end()) {
            range = it->second;
        }
        if (!hostile_unit_within_chebyshev(game, *src_it->second, range)) {
            return ActionResult{true, "No enemy unit within range — no turquoise gained", {}};
        }
        StackItem gain = item;
        gain.effect_key = "gain_turquoise";
        return game.stack_manager.resolve_item_direct(game, gain);
    }));

    static_cast<void>(stack_manager.register_effect("consume_spell_orange_for_growth", [](GameState& game, const StackItem& item) {
        if (item.source_entity_id.empty()) {
            return ActionResult{false, "consume_spell_orange_for_growth requires a source unit", {}};
        }
        const auto src_it = game.board.all_entities_map.find(item.source_entity_id);
        if (src_it == game.board.all_entities_map.end() || !src_it->second) {
            return ActionResult{false, "Source unit is no longer on the board", {}};
        }
        auto unit = std::dynamic_pointer_cast<Unit>(src_it->second);
        if (!unit) {
            return ActionResult{false, "Source is not a unit", {}};
        }
        // Drain the spell_ability tagged orange pool for this controller.
        auto& pool_for_player = game.turn_manager.player_tagged_float["spell_ability"][item.controller_id];
        const auto oit = pool_for_player.find(EnergyType::Orange);
        const int consumed = (oit != pool_for_player.end()) ? oit->second : 0;
        if (oit != pool_for_player.end()) {
            oit->second = 0;
        }
        if (consumed <= 0) {
            return ActionResult{true, "No floating orange energy to consume — no growth", {}};
        }
        // Apply permanent growth: +consumed max HP and +consumed damage.
        unit->base_health += consumed;
        unit->current_health += consumed;
        sync_unit_damage_ranges_from_nominal(*unit);
        unit->melee_damage_min += consumed;
        unit->melee_damage_max += consumed;
        unit->melee_damage += consumed;
        if (unit->is_ranged || unit->attack_type == AttackType::Ranged) {
            unit->ranged_damage_min += consumed;
            unit->ranged_damage_max += consumed;
            unit->ranged_damage += consumed;
        }
        game.refresh_passive_auras();
        return ActionResult{true,
            unit->entity_id + " consumed " + std::to_string(consumed)
            + " spell-ability orange, gaining +" + std::to_string(consumed)
            + " max HP and +" + std::to_string(consumed) + " damage permanently",
            {}};
    }));

    static_cast<void>(stack_manager.register_effect("consume_floating_energy_for_storage",
        [](GameState& game, const StackItem& item) {
        if (item.source_entity_id.empty()) {
            return ActionResult{false, "consume_floating_energy_for_storage requires a source unit", {}};
        }
        const auto src_it = game.board.all_entities_map.find(item.source_entity_id);
        if (src_it == game.board.all_entities_map.end() || !src_it->second
                || src_it->second->current_health <= 0) {
            return ActionResult{false, "Source unit is no longer on the board", {}};
        }
        auto unit = std::dynamic_pointer_cast<Unit>(src_it->second);
        if (!unit) {
            return ActionResult{false, "Source is not a unit", {}};
        }
        int max_storage = 16;
        if (const auto it = item.payload.find("max_storage"); it != item.payload.end()) {
            max_storage = std::max(0, it->second);
        }
        const int current = entity_effect_amount(*unit, "ancient_frog_stored_energy");
        const int room = std::max(0, max_storage - current);
        if (room <= 0) {
            return ActionResult{true, unit->entity_id + " storage full (" + std::to_string(max_storage) + ")", {}};
        }

        if (!entity_has_ancient_frog_store_passive(*unit)) {
            return ActionResult{false, "consume_floating_energy_for_storage is Ancient Frog only", {}};
        }
        const auto cp = game.turn_manager.current_player();
        if (!cp || *cp != item.controller_id) {
            return ActionResult{true, "Ancient Frog Reservoir only stores at owner turn end", {}};
        }

        const int share = game.energy_storage_share_for_unit(item.controller_id, item.source_entity_id);
        int gained = std::min(share, room);
        if (gained <= 0) {
            return ActionResult{true, "No floating energy to store", {}};
        }
        add_entity_effect(*unit, "ancient_frog_stored_energy", gained, item.source_entity_id);
        return ActionResult{true,
            unit->entity_id + " stored " + std::to_string(gained) + " energy ("
            + std::to_string(current + gained) + "/" + std::to_string(max_storage) + ")",
            {}};
    }));

    // Mortar barrage: end-of-turn automated effect fired from a building passive.
    // Selects a random enemy unit in [min_range, max_range] (Chebyshev, deadzone excluded),
    // then deals `damage` and applies `overload` stacks to it and all entities on adjacent cells.
    // Payload: damage (default 3), overload (default 1), min_range (default 2), max_range (default 5).
    // Reads entity effects set by Final Barrage to fire multiple times then self-destruct.
    static_cast<void>(stack_manager.register_effect("mortar_barrage", [](GameState& game, const StackItem& item) {
        const auto src_it = game.board.all_entities_map.find(item.source_entity_id);
        if (src_it == game.board.all_entities_map.end() || !src_it->second) {
            return ActionResult{false, "mortar_barrage: source not found", {}};
        }
        const auto& src = src_it->second;

        // If Final Barrage is primed, skip the normal shot — detonation fires at owner_turn_start.
        if (entity_effect_amount(*src, "final_barrage_charges") > 0) {
            return ActionResult{true, "Mortar barrage: Final Barrage primed, holding fire", {}};
        }

        const int damage    = payload_int(item, "damage",    3);
        const int overload  = payload_int(item, "overload",  1);
        const int min_range = payload_int(item, "min_range", 2);
        const int max_range = payload_int(item, "max_range", 4);

        if (!fire_mortar_shot(game, *src, item.controller_id, damage, overload, min_range, max_range)) {
            return ActionResult{true, "Mortar barrage: no targets in range", {}};
        }
        return ActionResult{true, "Mortar barrage fired", {}};
    }));

    // Final Barrage detonation: fires at owner_turn_start after the mortar was primed by the
    // 'final_barrage' activation ability. Reads final_barrage_charges from entity effects;
    // no-ops silently when the mortar was never primed this cycle.
    // Barrage params (damage, overload, min_range, max_range) come from the passive payload.
    // Self-destruct params come from entity effects set by the activation ability.
    static_cast<void>(stack_manager.register_effect("final_barrage_detonate", [](GameState& game, const StackItem& item) {
        const auto src_it = game.board.all_entities_map.find(item.source_entity_id);
        if (src_it == game.board.all_entities_map.end() || !src_it->second) {
            return ActionResult{false, "final_barrage_detonate: source not found", {}};
        }
        const auto& src = src_it->second;

        const int charges = entity_effect_amount(*src, "final_barrage_charges");
        if (charges == 0) {
            return ActionResult{true, "Final Barrage: not primed, no-op", {}};
        }
        reduce_entity_effect(*src, "final_barrage_charges", charges);

        const int damage    = payload_int(item, "damage",    3);
        const int overload  = payload_int(item, "overload",  1);
        const int min_range = payload_int(item, "min_range", 2);
        const int max_range = payload_int(item, "max_range", 4);

        int shots_fired = 0;
        for (int i = 0; i < charges; ++i) {
            if (fire_mortar_shot(game, *src, item.controller_id, damage, overload, min_range, max_range)) {
                ++shots_fired;
            }
        }

        // Self-destruct after all shots fire: blast the mortar's own 8-way neighbors, then remove it.
        if (game.board.all_entities_map.contains(src->entity_id)) {
            const int bd = std::max(1, entity_effect_amount(*src, "final_barrage_blast_damage"));
            const int bo = std::max(1, entity_effect_amount(*src, "final_barrage_blast_overload"));
            reduce_entity_effect(*src, "final_barrage_blast_damage",  bd);
            reduce_entity_effect(*src, "final_barrage_blast_overload", bo);

            std::vector<std::shared_ptr<Entity>> adjacent;
            std::set<std::string> seen_adj;
            for (const auto& [cx, cy] : entity_surrounding_cells(*src)) {
                auto neighbor = game.board.entity_at(cx, cy);
                if (neighbor && seen_adj.insert(neighbor->entity_id).second) {
                    if (neighbor->entity_type == "unit" || entity_is_structure(*neighbor) || entity_is_base(*neighbor)) {
                        adjacent.push_back(neighbor);
                    }
                }
            }
            const std::optional<int> applier = src->owner ? std::optional<int>(*src->owner) : std::nullopt;
            apply_mortar_blast_to_victims(game, adjacent, bd, bo, applier);
            game.destroy_board_entity(src);
            game.refresh_passive_auras();
        }
        return ActionResult{true, "Final Barrage: " + std::to_string(shots_fired) + " shots fired, mortar destroyed", {}};
    }));

    // Final Barrage activation: prime the mortar to fire 6 times at end of turn then self-destruct.
    // Payload: charges (default 6), self_destruct_damage (default 3), self_destruct_overload (default 1).
    static_cast<void>(stack_manager.register_effect("final_barrage", [](GameState& game, const StackItem& item) {
        const auto src_it = game.board.all_entities_map.find(item.source_entity_id);
        if (src_it == game.board.all_entities_map.end() || !src_it->second) {
            return ActionResult{false, "final_barrage: source not found", {}};
        }
        auto& src = *src_it->second;
        const int charges  = payload_int(item, "charges",              6);
        const int blast_dmg = payload_int(item, "self_destruct_damage", 3);
        const int blast_ovl = payload_int(item, "self_destruct_overload", 1);
        add_entity_effect(src, "final_barrage_charges",       charges,  item.item_id);
        add_entity_effect(src, "final_barrage_blast_damage",  blast_dmg, item.item_id);
        add_entity_effect(src, "final_barrage_blast_overload", blast_ovl, item.item_id);
        return ActionResult{true, "Final Barrage primed: will fire " + std::to_string(charges) + " times this turn end", {}};
    }));

    static_cast<void>(stack_manager.register_effect("counter_spell", [](GameState& game, const StackItem& item) {
        if (item.target_stack_item_id.empty()) {
            return ActionResult{false, "counter_spell requires a stack target", {}};
        }
        const StackItem* target = game.find_batched_item(item.target_stack_item_id);
        if (!target) {
            return ActionResult{false, "Counter target is no longer in the batch queue", {}};
        }
        if (target->source_type != "spell" && target->source_type != "ability") {
            return ActionResult{false, "Counterspell can only counter spells and abilities", {}};
        }
        if (!game.remove_batched_item_by_id(item.target_stack_item_id)) {
            return ActionResult{false, "Failed to remove countered batch item", {}};
        }
        return ActionResult{true, "Countered " + item.target_stack_item_id, {}};
    }));

    static_cast<void>(stack_manager.register_effect("copy_allied_spell", [](GameState& game, const StackItem& item) {
        (void)game;
        (void)item;
        return ActionResult{true, "copy_allied_spell resolves at ability activation (no-op)", {}};
    }));

