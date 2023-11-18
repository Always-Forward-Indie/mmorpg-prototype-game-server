#include "helpers/Config.hpp"
#include <iostream>
#include "game_server/GameServer.hpp"
#include "game_server/ChunkServerWorker.hpp"
#include "helpers/Logger.hpp"

int main() {
    try {
        boost::asio::io_context io_context;
        Config config;
        auto configs = config.parseConfig("config.json");
        short port = std::get<1>(configs).port;
        std::string ip = std::get<1>(configs).host;
        short maxClients = std::get<1>(configs).max_clients;

        Logger logger;
        ChunkServerWorker chunkServerWorker;

        // Create a new thread and run the ChunkServerWorker's io_context in that thread
        std::thread chunkServerThread([&chunkServerWorker]() {
            chunkServerWorker.startIOThread(); // Replace run() with your ChunkServerWorker's method to start its io_context
        });
        

        GameServer gameServer(io_context, ip, port, maxClients, chunkServerWorker, logger);

        //Start Game Server IO Context
        io_context.run();  // Start the event loop

        // Join the ChunkServerWorker thread to ensure it's properly cleaned up
        chunkServerThread.join();

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;  // Indicate an error exit status
    }
}