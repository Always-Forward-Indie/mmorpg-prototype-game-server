#include "game_server/GameServer.hpp"

GameServer::GameServer(EventQueue& eventQueue, 
NetworkManager& networkManager, 
ChunkServerWorker& chunkServerWorker, 
Logger& logger, Database& database)
    : networkManager_(networkManager),
      clientData_(),
      logger_(logger),
      eventQueue_(eventQueue),
      eventHandler_(networkManager, chunkServerWorker, database, logger)
{
    // Start accepting new clients connections
    networkManager_.startAccept();
}

void GameServer::mainEventLoop() {
    logger_.log("Starting Main Event Loop...", YELLOW);

    while (true) {
        Event event;
        if (eventQueue_.pop(event)) {
            eventHandler_.dispatchEvent(event, clientData_);
        }

        // Optionally include a small delay or yield to prevent the loop from consuming too much CPU
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

void GameServer::startMainEventLoop()
{
    // Start the main event loop in a new thread
    event_thread_ = std::thread(&GameServer::mainEventLoop, this);
}