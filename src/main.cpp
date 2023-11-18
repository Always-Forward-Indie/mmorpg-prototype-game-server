#include "helpers/Config.hpp"
#include <iostream>
#include "game_server/GameServer.hpp"
#include "game_server/ChunkServerWorker.hpp"
#include "helpers/Logger.hpp"

int main() {
    try {
        // Initialize Config
        Config config;
        // Get configs for connections to servers from config.json
        auto configs = config.parseConfig("/home/shardanov/mmorpg-prototype-game-server/build/config.json");
        
        // Initialize Logger
        Logger logger;

        // Initialize ChunkServerWorker
        ChunkServerWorker chunkServerWorker(configs, logger);

        // Start ChunkServerWorker IO Context in a separate thread
        chunkServerWorker.startIOEventLoop(); 

        // Initialize GameServer
        GameServer gameServer(chunkServerWorker, configs, logger);

        //Start Game Server IO Context as the main thread
        gameServer.startIOEventLoop();

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;  // Indicate an error exit status
    }
}