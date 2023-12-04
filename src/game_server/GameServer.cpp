#include "game_server/GameServer.hpp"

GameServer::GameServer(EventQueue& eventQueue, 
Scheduler& scheduler,
NetworkManager& networkManager, 
ChunkServerWorker& chunkServerWorker, 
Database& database,
CharacterManager& characterManager,
Logger& logger) 
    : networkManager_(networkManager),
      clientData_(),
      logger_(logger),
      eventQueue_(eventQueue),
      characterManager_(characterManager),
      eventHandler_(networkManager, chunkServerWorker, database, characterManager, logger),
      scheduler_(scheduler),
      database_(database)
{
    // Start accepting new clients connections
    networkManager_.startAccept();
}

void GameServer::mainEventLoop() {
    //TODO - save different client data to the database in different time intervals (depend by the client data type)
    // maybe it should be like scheduled task
    // also maybe use a separate thread for this as background service
    logger_.log("Add Tasks To Scheduler...", YELLOW);
    // Schedule tasks
    scheduler_.scheduleTask({[&] { characterManager_.updateCharactersData(database_, clientData_); }, 5, std::chrono::system_clock::now()}); // every 5 seconds
    //scheduler_.scheduleTask({savePlayerPosition, 15, std::chrono::system_clock::now()}); // every 15 seconds

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