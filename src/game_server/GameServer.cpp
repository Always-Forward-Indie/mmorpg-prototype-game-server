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
      database_(database),
      mobManager_(database, logger),
      spawnZoneManager_(mobManager_, database, logger)
{
    // Start accepting new clients connections
    networkManager_.startAccept();
}

void GameServer::mainEventLoopGS()
{
    logger_.log("Add Tasks To Game Server Scheduler...", YELLOW);
    constexpr int BATCH_SIZE = 10; // Process up to 10 events at a time
    //TODO work on this later
    //TODO - save different client data to the database in different time intervals (depend by the client data type)
    // Schedule tasks
    //scheduler_.scheduleTask({[&] { characterManager_.updateBasicCharactersData(database_, clientData_); }, 5, std::chrono::system_clock::now()}); // every 5 seconds

    // schedule mob spawn zone tasks
    scheduler_.scheduleTask({[&] { 
            // spawn mobs in the zone
            //std::vector<MobDataStruct> mobsList = 
            spawnZoneManager_.spawnMobsInZone(1);

            // get spawn zone data by id
            SpawnZoneStruct spawnZone = spawnZoneManager_.getMobSpawnZoneByID(1);

            //get all the connected clients from clientData_ and push the event with client id
            auto connectedClients = clientData_.getClientsDataMap();
               
                for (const auto& client : connectedClients)
                {
                    if(client.second.clientId == 0)
                    {
                        continue;
                    }
                    
                    auto clientSocket = client.second.socket;
                    Event spawnMobsInZoneEvent(Event::SPAWN_MOBS_IN_ZONE, client.second.clientId, spawnZone, clientSocket);
                    eventQueueGameServer_.push(spawnMobsInZoneEvent);
                }
        
        }, 30, std::chrono::system_clock::now()}); // every 30 seconds

    try
    {
        logger_.log("Starting Game Server Event Loop...", YELLOW);
        while (true) {
            // Create an event object to store the next event
            // Event event;

            // // Pop the next event from the queue
            // if (eventQueueGameServer_.pop(event)) {
            //     // Dispatch the event from the event handler
            //     eventHandler_.dispatchEvent(event, clientData_);
            // }

            std::vector<Event> eventsBatch;
            if (eventQueueGameServer_.popBatch(eventsBatch, BATCH_SIZE)) {
                processBatch(eventsBatch);
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
     constexpr int BATCH_SIZE = 10;
    logger_.log("Add Tasks To Chunk Server Scheduler...", YELLOW);

    //TODO work on this later
    //TODO - save different client data to the database in different time intervals (depend by the client data type)
    // Schedule tasks
    //scheduler_.scheduleTask({[&] { characterManager_.updateBasicCharactersData(database_, clientData_); }, 5, std::chrono::system_clock::now()}); // every 5 seconds

    try
    {
        logger_.log("Starting Chunk Server Event Loop...", YELLOW);
        while (true) {
            // Event event;

            // if (eventQueueChunkServer_.pop(event)) {
            //     eventHandler_.dispatchEvent(event, clientData_);
            // }

            std::vector<Event> eventsBatch;
            if (eventQueueChunkServer_.popBatch(eventsBatch, BATCH_SIZE)) {
                processBatch(eventsBatch);
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

// Implement asynchronous processing of event batches
void GameServer::processBatch(const std::vector<Event>& eventsBatch) {
    // Asynchronously process events in the batch
    // for (const auto& event : eventsBatch) {
    //     // Asynchronously process each event using std::async or std::thread
    //     std::async(std::launch::async | std::launch::deferred, [&]() {
    //         eventHandler_.dispatchEvent(event, clientData_);
    //     });
    // }

        std::vector<std::future<void>> futures;

    for (const auto& event : eventsBatch) {
        // Launch asynchronous task and save the future
        auto future = std::async(std::launch::async | std::launch::deferred, [&]() {
            eventHandler_.dispatchEvent(event, clientData_);
        });
        futures.push_back(std::move(future));
    }

    // Wait for all tasks to complete
    for (auto& future : futures) {
        future.wait();
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