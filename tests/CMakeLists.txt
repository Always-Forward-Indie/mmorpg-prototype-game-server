# Game-server unit tests (gtest). Run inside the dev container:
#   ctest --test-dir /usr/src/app/build --output-on-failure
find_package(GTest QUIET)
if(GTest_FOUND)
    # Drop unreferenced sections so DB-adjacent .cpps link without a live DB.
    add_compile_options(-ffunction-sections -fdata-sections)
    add_link_options(-Wl,--gc-sections)
    add_executable(unit_tests
        test_chunk_manager.cpp
        test_event_queue.cpp
        test_client_manager.cpp
        test_game_characters.cpp
        test_threadpool.cpp
        test_game_items.cpp
        test_game_npcs.cpp
        test_game_mobs.cpp
        test_game_spawns.cpp
        test_game_config.cpp
        test_random_utils.cpp
        ../src/services/ChunkManager.cpp
        ../src/services/ClientManager.cpp
        ../src/services/CharacterManager.cpp
        ../src/services/ClassSpawnZoneManager.cpp
        ../src/services/ItemManager.cpp
        ../src/services/NPCManager.cpp
        ../src/services/MobManager.cpp
        ../src/services/SpawnZoneManager.cpp
        ../src/services/ClassSpawnZoneManager.cpp
        ../src/services/GameConfigService.cpp
        ../src/utils/ThreadPool.cpp
        ../src/events/Event.cpp
        ../src/events/EventQueue.cpp
        ../src/utils/Logger.cpp
    )
    target_link_libraries(unit_tests PRIVATE GTest::gtest_main spdlog::spdlog_header_only)
    add_test(NAME unit COMMAND unit_tests)
else()
    message(WARNING "GTest not found - unit tests skipped (install libgtest-dev, build & install)")
endif()
