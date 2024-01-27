#include "game_server/GameServer.hpp"

GameServer::GameServer(ClientData &clientData,
EventQueue& eventQueueGameServer, 
EventQueue& eventQueueChunkServer, 
Scheduler& scheduler,
NetworkManager& networkManager, 
ChunkServerWorker& chunkServerWorker, 
Database& database,
CharacterManager& characterManager,
Logger& logger) 
    : networkManager_(networkManager),
      clientData_(clientData),
      logger_(logger),
      eventQueueChunkServer_(eventQueueGameServer),
      eventQueueGameServer_(eventQueueChunkServer),
      characterManager_(characterManager),
      eventHandler_(networkManager, chunkServerWorker, database, characterManager, logger),
      scheduler_(scheduler),
      database_(database)
{
    // Start accepting new clients connections
    networkManager_.startAccept();
}

void GameServer::mainEventLoop() {
    logger_.log("Add Tasks To Scheduler...", YELLOW);

    //TODO work on this later
    //TODO - save different client data to the database in different time intervals (depend by the client data type)
    // Schedule tasks
    //scheduler_.scheduleTask({[&] { characterManager_.updateBasicCharactersData(database_, clientData_); }, 5, std::chrono::system_clock::now()}); // every 5 seconds

    logger_.log("Starting Event Loops...", YELLOW);
    while (true) {
        Event eventChunk;
        Event eventGame;

        if (eventQueueGameServer_.pop(eventGame)) {
            eventHandler_.dispatchEvent(eventGame, clientData_);
        }

        if (eventQueueChunkServer_.pop(eventChunk)) {
            eventHandler_.dispatchEvent(eventChunk, clientData_);
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