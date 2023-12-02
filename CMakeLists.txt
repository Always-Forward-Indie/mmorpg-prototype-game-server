cmake_minimum_required(VERSION 3.10)

project(MMOGameServer)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
# Set compiler flags for debug mode (need to remove when compiling for production)
set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=thread -g")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=thread")


# Define your project's source files
set(SOURCE_FILES
    src/main.cpp
    src/game_server/GameServer.cpp
    src/game_server/Authenticator.cpp
    src/game_server/ChunkServerWorker.cpp
    src/game_server/CharacterManager.cpp
    src/network/NetworkManager.cpp
    src/events/Event.cpp
    src/events/EventQueue.cpp
    src/events/EventHandler.cpp
    src/data/ClientData.cpp
    src/utils/JSONParser.cpp
    src/utils/Logger.cpp
    src/utils/Database.cpp
    src/utils/Config.cpp
    # ... other source files
)

# Define your project's header files
set(HEADER_FILES

    include/game_server/GameServer.hpp
    include/game_server/Authenticator.hpp
    include/game_server/ChunkServerWorker.hpp
    include/game_server/CharacterManager.hpp
    include/network/NetworkManager.hpp
    include/events/Event.hpp
    include/events/EventQueue.hpp
    include/events/EventHandler.hpp
    include/data/DataStructs.hpp
    include/data/ClientData.hpp
    include/utils/JSONParser.hpp
    include/utils/ResponseBuilder.hpp
    include/utils/Logger.hpp
    include/utils/TerminalColors.hpp
    include/utils/Database.hpp
    include/utils/Config.hpp

    # ... other header files
)

# Include directories for your project's headers
include_directories(include)

# Create the executable
add_executable(${PROJECT_NAME} ${SOURCE_FILES} ${HEADER_FILES})
target_link_libraries(${PROJECT_NAME} pqxx)
target_link_libraries(${PROJECT_NAME} pq)