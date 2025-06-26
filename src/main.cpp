#include <iostream>
#include <csignal>
#include <atomic>
#include <thread>
#include "utils/Config.hpp"
#include "utils/Logger.hpp"
#include "utils/TimeConverter.hpp"
#include "game_server/GameServer.hpp"
#include "network/NetworkManager.hpp"
#include "utils/Database.hpp"
#include "utils/Scheduler.hpp"
#include "services/CharacterManager.hpp"
#include "services/GameServices.hpp"

std::atomic<bool> running(true);

void signalHandler(int signal) {
    running = false;
}

int main() {
    try {
        // Register signal handlers
        std::signal(SIGINT, signalHandler);
        std::signal(SIGTERM, signalHandler);

        // Initialize Config
        Config config;
        // Initialize Logger
        Logger logger;
        // Get configs for connections to servers from config.json
        auto configs = config.parseConfig("config.json");

        // Initialize EventQueue
        EventQueue eventQueueChunkServer;
        EventQueue eventQueueGameServer;
        EventQueue eventQueueGameServerPing;

        // Initialize Scheduler
        Scheduler scheduler;

        // Initialize Database
        Database database(configs, logger);

        // Initialize CharacterManager
        CharacterManager characterManager(logger);

        // Initialize GameServices
        GameServices gameServices(database, logger);

        // Initialize NetworkManager
        NetworkManager networkManager(eventQueueGameServer, eventQueueGameServerPing, configs, logger);

        // Event Handler
        EventHandler eventHandler(networkManager, gameServices);

        // Initialize GameServer
        GameServer gameServer(gameServices, eventHandler, eventQueueGameServer, eventQueueChunkServer, eventQueueGameServerPing, scheduler);

        // Set the GameServer object in the NetworkManager
        networkManager.setGameServer(&gameServer);

        // Start accepting connections
        networkManager.startAccept();

        // Start the IO Networking event loop in the main thread
        networkManager.startIOEventLoop();

        // Start Game Server main event loop in a separate thread
        gameServer.startMainEventLoop();

        // Start Scheduler loop in a separate thread
       scheduler.start();

        // wait for the signal to stop the server
        while (running) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        std::cout << "Shutting down gracefully..." << std::endl;

        return 0;
    } catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
