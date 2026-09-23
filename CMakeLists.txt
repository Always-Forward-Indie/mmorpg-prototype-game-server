cmake_minimum_required(VERSION 3.16)

project(MMOGameServer)

# C++20 (was 17): invoke_result, concepts-ready.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options(-Wall -Wextra -Wswitch -Wimplicit-fallthrough)

# Build type is set via -DCMAKE_BUILD_TYPE=Debug|Release (defaults to none if omitted)
# AddressSanitizer is active only in Debug builds (mirrors chunk-server).

set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fsanitize=address -fno-omit-frame-pointer -g")
set(CMAKE_LINKER_FLAGS_DEBUG "${CMAKE_LINKER_FLAGS_DEBUG} -fsanitize=address")

# Define your project's source files
set(SOURCE_FILES
    src/main.cpp
    src/game_server/GameServer.cpp
    src/services/ClientManager.cpp  
    src/services/ChunkManager.cpp
    src/services/CharacterManager.cpp
    src/services/SpawnZoneManager.cpp
    src/services/SpawnManager.cpp
    src/services/ClassSpawnZoneManager.cpp
    src/services/NPCManager.cpp
    src/services/MobManager.cpp
    src/services/ItemManager.cpp
    src/services/DialogueQuestManager.cpp
    src/services/GameConfigService.cpp
    src/network/NetworkManager.cpp
    src/network/ClientSession.cpp
    src/events/Event.cpp
    src/events/EventQueue.cpp
    src/events/EventHandler.cpp
    src/events/EventDispatcher.cpp
    src/events/Outbox.cpp
    src/events/handlers/BaseEventHandler.cpp
    src/events/handlers/ItemEventHandler.cpp
    src/events/handlers/EffectsEventHandler.cpp
    src/events/handlers/SocialEventHandler.cpp
    src/events/handlers/SkillsEventHandler.cpp
    src/events/handlers/ProgressionEventHandler.cpp
    src/events/handlers/WorldEventHandler.cpp
    src/events/handlers/CatalogEventHandler.cpp
    src/events/handlers/InventoryEventHandler.cpp
    src/events/handlers/CharacterEventHandler.cpp
    src/events/handlers/DialogueQuestEventHandler.cpp
    src/events/handlers/VendorEventHandler.cpp
    src/utils/Scheduler.cpp
    src/utils/ThreadPool.cpp
    src/utils/JSONParser.cpp
    src/utils/TimeConverter.cpp
    src/utils/Generators.cpp
    src/utils/Logger.cpp
    src/utils/Database.cpp
    src/utils/DatabasePool.cpp
    src/utils/QueryRegistry.cpp
    src/utils/Config.cpp
    src/utils/TimeUtils.cpp
    src/utils/TimestampUtils.cpp
    src/handlers/MessageHandler.cpp
    # ... other source files
)

# Define your project's header files
set(HEADER_FILES
    include/game_server/GameServer.hpp
    include/services/ClientManager.hpp
    include/services/ChunkManager.hpp
    include/services/GameServices.hpp
    include/services/CharacterManager.hpp
    include/services/SpawnZoneManager.hpp
    include/services/SpawnManager.hpp
    include/services/ClassSpawnZoneManager.hpp
    include/services/MobManager.hpp
    include/services/ItemManager.hpp
    include/services/NPCManager.hpp
    include/services/DialogueQuestManager.hpp
    include/services/GameConfigService.hpp
    include/network/NetworkManager.hpp
    include/network/ClientSession.hpp
    include/events/Event.hpp
    include/events/EventQueue.hpp
    include/events/EventHandler.hpp
    include/events/EventDispatcher.hpp
    include/events/Outbox.hpp
    include/events/handlers/BaseEventHandler.hpp
    include/events/handlers/ItemEventHandler.hpp
    include/events/handlers/EffectsEventHandler.hpp
    include/events/handlers/SocialEventHandler.hpp
    include/events/handlers/SkillsEventHandler.hpp
    include/events/handlers/ProgressionEventHandler.hpp
    include/events/handlers/WorldEventHandler.hpp
    include/events/handlers/CatalogEventHandler.hpp
    include/events/handlers/InventoryEventHandler.hpp
    include/events/handlers/CharacterEventHandler.hpp
    include/events/handlers/DialogueQuestEventHandler.hpp
    include/events/handlers/VendorEventHandler.hpp
    include/data/DataStructs.hpp
    include/data/SkillStructs.hpp
    include/data/SpecialStructs.hpp
    include/utils/Scheduler.hpp
    include/utils/ThreadPool.hpp
    include/utils/JSONParser.hpp
    include/utils/ProtocolLimits.hpp
    include/utils/ResponseBuilder.hpp
    include/utils/TimeConverter.hpp
    include/utils/Generators.hpp
    include/utils/RandomUtils.hpp
    include/utils/Logger.hpp
    include/utils/TerminalColors.hpp
    include/utils/Database.hpp
    include/utils/DatabasePool.hpp
    include/utils/QueryRegistry.hpp
    include/utils/Config.hpp
    include/utils/TimeUtils.hpp
    include/utils/TimestampUtils.hpp
    include/handlers/MessageHandler.hpp
    # ... other header files
)

# Include directories for your project's headers
include_directories(include)

# Ensure libpqxx is correctly detected
find_package(PkgConfig REQUIRED)
pkg_check_modules(libpqxx REQUIRED IMPORTED_TARGET libpqxx)

# spdlog
find_package(spdlog REQUIRED)

# Create the executable
add_executable(${PROJECT_NAME} ${SOURCE_FILES} ${HEADER_FILES})

target_link_libraries(${PROJECT_NAME} PkgConfig::libpqxx spdlog::spdlog_header_only)

# Unit tests (gtest). tests/CMakeLists.txt skips gracefully when GTest is
# absent (e.g. prod image without libgtest-dev).
enable_testing()
add_subdirectory(tests)