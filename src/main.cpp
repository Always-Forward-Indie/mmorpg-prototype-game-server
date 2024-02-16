#include <iostream>
#include <csignal>
#include <atomic>
#include "utils/Config.hpp"
#include "utils/Logger.hpp"
#include "game_server/GameServer.hpp"
#include "network/ChunkServerWorker.hpp"
#include "network/NetworkManager.hpp"
#include "utils/Database.hpp"
#include "utils/Scheduler.hpp"
#include "services/CharacterManager.hpp"

std::atomic<bool> running(true);

void signalHandler(int signal) {
    running = false;
}

int main() {
    try {
        // Initialize Config
        Config config;
        // Initialize Logger
        Logger logger;
        // Get configs for connections to servers from config.json
        auto configs = config.parseConfig("config.json");

        // Initialize EventQueue
        EventQueue eventQueueChunkServer;
        EventQueue eventQueueGameServer;

        // Initialize ClientData
        ClientData clientData;

        // Initialize Scheduler
        Scheduler scheduler;

        // Initialize CharacterManager
        CharacterManager characterManager(logger);

        // Initialize Database
        Database database(configs, logger);

        // Initialize NetworkManager
        NetworkManager networkManager(eventQueueGameServer, configs, logger);

        // Initialize ChunkServerWorker
        ChunkServerWorker chunkServerWorker(eventQueueChunkServer, networkManager, configs, logger);

        // Initialize GameServer
        GameServer gameServer(clientData, eventQueueGameServer, eventQueueChunkServer, scheduler, networkManager, chunkServerWorker, database, characterManager, logger);

        //Start the IO Networking event loop in the main thread
        networkManager.startIOEventLoop();

        // Start ChunkServerWorker IO Context in a separate thread
        chunkServerWorker.startIOEventLoop(); 

        //Start Game Server main event loop  in a separate thread
        gameServer.startMainEventLoop();

        //Start Scheduler loop in a separate thread
       // scheduler.start();

        //TODO fix issue where the server does not stop on Ctrl+C
        // Temporary commented out the signal handler
        // Register signal handler for graceful shutdown
        //signal(SIGINT, signalHandler);

        // while (running) {
        //     std::this_thread::sleep_for(std::chrono::seconds(1));
        // }

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;  // Indicate an error exit status
    }
}