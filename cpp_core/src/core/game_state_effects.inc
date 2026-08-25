// game_state_effects.inc — orchestrates stack effect handler registration shards.
// #included into game_state.cpp at namespace tactics scope (not a separate compile unit).

void GameState::register_default_effects() {
    ensure_builtin_effect_registry_loaded();
    #include "effects/game_state_effects_combat.inc"
    #include "effects/game_state_effects_heal_buff.inc"
    #include "effects/game_state_effects_aoe_status.inc"
    #include "effects/game_state_effects_draw_energy.inc"
    #include "effects/game_state_effects_mortar_focus.inc"
    #include "effects/game_state_effects_spawn_terrain.inc"
    #include "effects/game_state_effects_movement.inc"
    #include "effects/game_state_effects_player_style.inc"
    #include "effects/game_state_effects_acm_batch.inc"
    verify_effect_stack_handler_parity(stack_manager);
}
