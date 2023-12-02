#include <iostream>
#include "utils/Config.hpp"
#include "utils/Logger.hpp"
#include "game_server/GameServer.hpp"
#include "game_server/ChunkServerWorker.hpp"
#include "network/NetworkManager.hpp"
#include "utils/Database.hpp"


int main() {
    try {
        // Initialize Config
        Config config;
        // Initialize Logger
        Logger logger;
        // Get configs for connections to servers from config.json
        auto configs = config.parseConfig("/home/shardanov/mmorpg-prototype-game-server/build/config.json");

        // Initialize EventQueue
        EventQueue eventQueue;

        // Initialize Database
        Database database(configs, logger);

        // Initialize NetworkManager
        NetworkManager networkManager(eventQueue, configs, logger);

        // Initialize ChunkServerWorker
        ChunkServerWorker chunkServerWorker(eventQueue, networkManager, configs, logger);

        // Initialize GameServer
        GameServer gameServer(eventQueue, networkManager, chunkServerWorker, logger, database);

        // Start ChunkServerWorker IO Context in a separate thread
        chunkServerWorker.startIOEventLoop(); 

        //Start Game Server main event loop
        gameServer.startMainEventLoop();

        //Start the IO Networking event loop
        networkManager.startIOEventLoop();

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;  // Indicate an error exit status
    }
}