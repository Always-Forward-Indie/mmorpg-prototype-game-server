cmake_minimum_required(VERSION 3.10)

project(MMOGameServer)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_BUILD_TYPE Debug)

# Define your project's source files
set(SOURCE_FILES
    src/main.cpp
    src/game_server/GameServer.cpp
    src/game_server/Authenticator.cpp
    src/game_server/ClientData.cpp
    src/game_server/ChunkServerWorker.cpp
    src/game_server/CharacterManager.cpp
    src/helpers/Logger.cpp
    src/helpers/Database.cpp
    src/helpers/Config.cpp
    # ... other source files
)

# Define your project's header files
set(HEADER_FILES
    include/game_server/GameServer.hpp
    include/game_server/Authenticator.hpp
    include/game_server/ClientData.hpp
    include/game_server/ChunkServerWorker.hpp
    include/game_server/CharacterManager.hpp
    include/helpers/Logger.hpp
    include/helpers/TerminalColors.hpp
    include/helpers/Database.hpp
    include/helpers/Config.hpp

    # ... other header files
)

# Include directories for your project's headers
include_directories(include)

# Create the executable
add_executable(${PROJECT_NAME} ${SOURCE_FILES} ${HEADER_FILES})
target_link_libraries(${PROJECT_NAME} pqxx)
target_link_libraries(${PROJECT_NAME} pq)