cmake_minimum_required(VERSION 3.20)
# tactics_core sources are also compiled into Unreal via shims:
#   TacticsGameUnreal/Source/TacticsCore/Private/CppCoreStub_*.cpp (includes match_snapshot.cpp)
#   (includes master_cli_dispatch.cpp when present in this list)
# Keep that list in sync when adding/removing files below (or run scripts/generate_cpp_core_stubs.py).
# UObject code that calls tactics::* must live in the TacticsCore module (same DLL as these shims).
project(tactics_core LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(TACTICS_NET_USE_OPENSSL "Enable in-process TLS/WSS path in tactics_net_server" OFF)

add_library(tactics_core
    src/attributes/attributes.cpp
    src/effects/effect_registry.cpp
    src/effects/effect_traits.cpp
    src/effects/status_effect_catalog.cpp
    src/energy/energy_zone.cpp
    src/board/grid.cpp
    src/board/movement_policy.cpp
    src/board/push_displacement.cpp
    src/board/tile_modifiers.cpp
    src/board/trench.cpp
    src/board/aether.cpp
    src/board/scanner.cpp
    src/board/omni_energy_tile.cpp
    src/board/board_display.cpp
    src/board/board.cpp
    src/board/board_layout.cpp
    src/combat/combat_resolver.cpp
    src/combat/soul_steal.cpp
    src/combat/taunt.cpp
    src/combat/frenzy.cpp
    src/combat/low_cover.cpp
    src/combat/evasive.cpp
    src/combat/aoe_shapes.cpp
    src/combat/directional_area.cpp
    src/combat/ability_resolve_viz.cpp
    src/cards/ability_catalog.cpp
    src/cards/effect_definitions.cpp
    src/cards/effect_definitions_io.cpp
    src/cards/passive_catalog.cpp
    src/cards/card_catalog.cpp
    src/cards/card_search_index.cpp
    src/cards/unit_types.cpp
    src/cards/focus_spell.cpp
    src/cards/card_instances.cpp
    src/cards/card_runtime.cpp
    src/cards/cards.cpp
    src/content/glossary_copy.cpp
    src/content/card_glossary.cpp
    src/content/project_content.cpp
    src/core/turn_manager.cpp
    src/core/passive_action_order.cpp
    src/core/stack.cpp
    src/core/board_target_policy.cpp
    src/core/stack_targets.cpp
    src/actions/actions.cpp
    src/actions/board_targeting.cpp
    src/actions/move_resolution.cpp
    src/core/game_state.cpp
    src/sync/match_snapshot.cpp
    src/sync/match_sync.cpp
    src/sync/match_auth.cpp
    src/apps/master_cli_dispatch.cpp
    src/apps/sandbox_match.cpp
    src/bot/bot_action.cpp
    src/bot/bot_match_outcome.cpp
    src/bot/bot_observation.cpp
    src/bot/legal_action_generator.cpp
    src/bot/action_executor.cpp
    src/bot/bot_policy.cpp
    src/bot/bot_economy.cpp
    src/bot/bot_game_clone.cpp
    src/bot/bot_evaluator.cpp
    src/bot/bot_sim_step.cpp
    src/bot/mcts_policy.cpp
    src/bot/bot_match_driver.cpp
    src/bot/bot_step.cpp
)
# Keep passive_catalog.cpp in sync with the authoritative JSON file before every build.
# Run scripts/sync_passive_catalog.py manually whenever you edit passive_catalog.json.
find_program(PYTHON_EXEC NAMES python3 python)
if (PYTHON_EXEC)
    add_custom_target(sync_passive_catalog
        COMMAND "${PYTHON_EXEC}" "${CMAKE_SOURCE_DIR}/../scripts/sync_passive_catalog.py"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/.."
        COMMENT "Syncing kBuiltinPassiveCatalogJson from passive_catalog.json"
    )
    add_dependencies(tactics_core sync_passive_catalog)

    add_custom_target(sync_ability_catalog
        COMMAND "${PYTHON_EXEC}" "${CMAKE_SOURCE_DIR}/../scripts/sync_ability_catalog.py"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/.."
        COMMENT "Syncing kBuiltinAbilityCatalogJson from ability_catalog.json"
    )
    add_dependencies(tactics_core sync_ability_catalog)

    add_custom_target(sync_glossary_copy
        COMMAND "${PYTHON_EXEC}" "${CMAKE_SOURCE_DIR}/../scripts/sync_glossary_copy.py"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/.."
        COMMENT "Syncing kBuiltinGlossaryCopyJson and authoring-guide tables from glossary_copy.json"
    )
    add_dependencies(tactics_core sync_glossary_copy)
endif()

target_include_directories(tactics_core PUBLIC include "${CMAKE_CURRENT_SOURCE_DIR}/third_party")
if (MSVC)
    target_compile_options(tactics_core PUBLIC /EHsc)
    # Use embedded debug info (/Z7) so the standalone build doesn't need
    # a shared PDB file that Unreal Editor may have locked.
    target_compile_options(tactics_core PUBLIC "$<$<CONFIG:Debug>:/Z7>")
    target_link_options(tactics_core PUBLIC "$<$<CONFIG:Debug>:/DEBUG:FULL>")
endif()

add_executable(tactics_core_cli src/main.cpp)
target_link_libraries(tactics_core_cli PRIVATE tactics_core)

add_executable(tactics_master_cli src/master_cli.cpp)
target_link_libraries(tactics_master_cli PRIVATE tactics_core)

add_executable(bot_match src/apps/bot_match.cpp)
target_link_libraries(bot_match PRIVATE tactics_core)

add_executable(tactics_net_server src/apps/tactics_net_server.cpp)
target_link_libraries(tactics_net_server PRIVATE tactics_core)
target_include_directories(tactics_net_server PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/third_party")
if (WIN32)
	target_link_libraries(tactics_net_server PRIVATE ws2_32 advapi32)
	if (TACTICS_NET_USE_OPENSSL)
		target_compile_definitions(tactics_net_server PRIVATE TACTICS_NET_USE_OPENSSL=1)
		if (OPENSSL_INCLUDE_DIR AND OPENSSL_SSL_LIBRARY AND OPENSSL_CRYPTO_LIBRARY)
			target_include_directories(tactics_net_server PRIVATE "${OPENSSL_INCLUDE_DIR}")
			target_link_libraries(tactics_net_server PRIVATE "${OPENSSL_SSL_LIBRARY}" "${OPENSSL_CRYPTO_LIBRARY}" crypt32)
		else()
			find_package(OpenSSL REQUIRED)
			target_link_libraries(tactics_net_server PRIVATE OpenSSL::SSL OpenSSL::Crypto crypt32)
		endif()
	endif()
else()
	find_package(OpenSSL REQUIRED)
	target_link_libraries(tactics_net_server PRIVATE OpenSSL::Crypto)
	if (TACTICS_NET_USE_OPENSSL)
		target_compile_definitions(tactics_net_server PRIVATE TACTICS_NET_USE_OPENSSL=1)
		target_link_libraries(tactics_net_server PRIVATE OpenSSL::SSL)
	endif()
endif()

if (WIN32)
    target_link_libraries(tactics_core PRIVATE advapi32)
    target_sources(tactics_master_cli PRIVATE src/apps/native_plot_win.cpp)
endif()

include(CTest)
enable_testing()

add_executable(aether_bot_test tests/aether_bot_test.cpp)
target_link_libraries(aether_bot_test PRIVATE tactics_core)
add_test(NAME aether_bot_test COMMAND aether_bot_test)
