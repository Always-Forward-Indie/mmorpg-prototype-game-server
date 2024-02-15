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
      eventQueueGameServer_(eventQueueGameServer),
      eventQueueChunkServer_(eventQueueChunkServer),
      characterManager_(characterManager),
      eventHandler_(networkManager, chunkServerWorker, database, characterManager, logger),
      scheduler_(scheduler),
      database_(database)
{
    // Start accepting new clients connections
    networkManager_.startAccept();
}

void GameServer::mainEventLoopGS()
{
  logger_.log("Add Tasks To Scheduler...", YELLOW);

    //TODO work on this later
    //TODO - save different client data to the database in different time intervals (depend by the client data type)
    // Schedule tasks
    //scheduler_.scheduleTask({[&] { characterManager_.updateBasicCharactersData(database_, clientData_); }, 5, std::chrono::system_clock::now()}); // every 5 seconds

    try
    {
        logger_.log("Starting Game Server Event Loop...", YELLOW);
        while (true) {
            Event event;

            if (eventQueueGameServer_.pop(event)) {
                eventHandler_.dispatchEvent(event, clientData_);
            }

            // Optionally include a small delay or yield to prevent the loop from consuming too much CPU
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    catch(const std::exception& e)
    {
       logger_.logError(e.what(), RED);
    }
}

void GameServer::mainEventLoopCH()
{
  //logger_.log("Add Tasks To Scheduler...", YELLOW);

    //TODO work on this later
    //TODO - save different client data to the database in different time intervals (depend by the client data type)
    // Schedule tasks
    //scheduler_.scheduleTask({[&] { characterManager_.updateBasicCharactersData(database_, clientData_); }, 5, std::chrono::system_clock::now()}); // every 5 seconds

    try
    {
        logger_.log("Starting Chunk Server Event Loop...", YELLOW);
        while (true) {
            Event event;

            if (eventQueueChunkServer_.pop(event)) {
                eventHandler_.dispatchEvent(event, clientData_);
            }

            // Optionally include a small delay or yield to prevent the loop from consuming too much CPU
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    catch(const std::exception& e)
    {
       logger_.logError(e.what(), RED);
    }
}

void GameServer::startMainEventLoop()
{
    // Start the game server event loop in a separate thread
    event_game_server_thread_ = std::thread(&GameServer::mainEventLoopGS, this);

    // Start the chunk server event loop in a separate thread
    event_chunk_server_thread_ = std::thread(&GameServer::mainEventLoopCH, this);
}

GameServer::~GameServer()
{  
    logger_.log("Shutting down Game Server...", YELLOW);
    // Join the main event loop thread
    event_game_server_thread_.join();
    
    // Join the chunk server event loop thread
    event_chunk_server_thread_.join();
}