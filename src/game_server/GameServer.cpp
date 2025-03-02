#include "game_server/GameServer.hpp"
#include <unordered_set>

GameServer::GameServer(ClientData &clientData,
                        EventHandler &eventHandler,
                        EventQueue &eventQueueGameServer,
                        EventQueue &eventQueueChunkServer,
                        EventQueue &eventQueueGameServerPing,
                        Scheduler &scheduler,
                        ChunkServerWorker &chunkServerWorker,
                        Database &database,
                        CharacterManager &characterManager,
                        Logger &logger)
    :
      clientData_(clientData),
      logger_(logger),
      eventQueueGameServer_(eventQueueGameServer),
      eventQueueChunkServer_(eventQueueChunkServer),
      eventQueueGameServerPing_(eventQueueGameServerPing),
      characterManager_(characterManager),
      eventHandler_(eventHandler),
      scheduler_(scheduler),
      database_(database),
      mobManager_(database, logger),
      spawnZoneManager_(mobManager_, database, logger)
{
    
}

void GameServer::mainEventLoopGS()
{
    logger_.log("Add Tasks To Game Server Scheduler...", YELLOW);
    constexpr int BATCH_SIZE = 10;

     // TODO work on this later
    // TODO - save different client data to the database in different time intervals (depend by the client data type)
    
    // Task for spawning mobs in the zone
    Task spawnMobInZoneTask(
        [&]
        {
            spawnZoneManager_.spawnMobsInZone(1);
            SpawnZoneStruct spawnZone = spawnZoneManager_.getMobSpawnZoneByID(1);

            auto connectedClients = clientData_.getClientsDataMap();
            for (const auto &client : connectedClients)
            {
                if (client.second.clientId == 0)
                    continue;

                auto clientSocket = client.second.socket;
                Event spawnMobsInZoneEvent(Event::SPAWN_MOBS_IN_ZONE, client.second.clientId, spawnZone, clientSocket);
                eventQueueGameServer_.push(spawnMobsInZoneEvent);
            }
        },
        15,
        std::chrono::system_clock::now(),
        1 // unique task ID
    );

    scheduler_.scheduleTask(spawnMobInZoneTask); 

    // Task for moving mobs in the zone
    Task moveMobInZoneTask(
        [&]
        {
            spawnZoneManager_.moveMobsInZone(1);
            std::vector<MobDataStruct> mobsList = spawnZoneManager_.getMobsInZone(1);

            auto connectedClients = clientData_.getClientsDataMap();
            for (const auto &client : connectedClients)
            {
                if (client.second.clientId == 0)
                    continue;

                auto clientSocket = client.second.socket;
                Event moveMobsInZoneEvent(Event::ZONE_MOVE_MOBS, client.second.clientId, mobsList, clientSocket);
                eventQueueGameServer_.push(moveMobsInZoneEvent);
            }
        },
        3,
        std::chrono::system_clock::now(),
        2 // unique task ID
    );

    scheduler_.scheduleTask(moveMobInZoneTask); 

    try
    {
        logger_.log("Starting Game Server Event Loop...", YELLOW);
        while (running_)
        {
            std::vector<Event> eventsBatch;
            if (eventQueueGameServer_.popBatch(eventsBatch, BATCH_SIZE))
            {
                processBatch(eventsBatch);
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    catch (const std::exception &e)
    {
        logger_.logError(e.what(), RED);
    }
}



void GameServer::mainEventLoopCH()
{
    constexpr int BATCH_SIZE = 10;
    logger_.log("Add Tasks To Chunk Server Scheduler...", YELLOW);

    try
    {
        logger_.log("Starting Chunk Server Event Loop...", YELLOW);
        while (running_)
        {
            std::vector<Event> eventsBatch;
            if (eventQueueChunkServer_.popBatch(eventsBatch, BATCH_SIZE))
            {
                processBatch(eventsBatch);
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    catch (const std::exception &e)
    {
        logger_.logError(e.what(), RED);
    }
}

void GameServer::mainEventLoopPing()
{
    constexpr int BATCH_SIZE = 1; // Ping обрабатывай сразу

    logger_.log("Starting Ping Event Loop...", YELLOW);

    try
    {
        while (running_)
        {
            std::vector<Event> pingEvents;
            if (eventQueueGameServerPing_.popBatch(pingEvents, BATCH_SIZE))
            {
                processPingBatch(pingEvents);
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    catch (const std::exception &e)
    {
        logger_.logError("Ping Event Loop Error: " + std::string(e.what()), RED);
    }
}

void GameServer::processPingBatch(const std::vector<Event>& pingEvents)
{
    for (const auto& event : pingEvents)
    {
        threadPool_.enqueueTask([this, event] {
            try
            {
                eventHandler_.dispatchEvent(event, clientData_);
            }
            catch (const std::exception &e)
            {
                logger_.logError("Error processing PING_EVENT: " + std::string(e.what()));
            }
        });
    }

    eventCondition.notify_all();
}

void GameServer::processBatch(const std::vector<Event>& eventsBatch)
{
    std::vector<Event> priorityEvents;
    std::vector<Event> normalEvents;

    // Separate ping events from other events
    for (const auto& event : eventsBatch)
    {
        // if (event.PING_CLIENT == Event::PING_CLIENT)
        //     priorityEvents.push_back(event);
        // else
            normalEvents.push_back(event);
    }

    // Process priority ping events first
    for (const auto& event : priorityEvents)
    {
        threadPool_.enqueueTask([this, event] {
            try
            {
                eventHandler_.dispatchEvent(event, clientData_);
            }
            catch (const std::exception &e)
            {
                logger_.logError("Error processing priority dispatchEvent: " + std::string(e.what()));
            }
        });
    }

    // Process normal events
    for (const auto& event : normalEvents)
    {
        threadPool_.enqueueTask([this, event] {
            try
            {
                eventHandler_.dispatchEvent(event, clientData_);
            }
            catch (const std::exception &e)
            {
                logger_.logError("Error in normal dispatchEvent: " + std::string(e.what()));
            }
        });
    }

    eventCondition.notify_all();
}



void GameServer::startMainEventLoop()
{
    if (event_game_server_thread_.joinable() || event_chunk_server_thread_.joinable())
    {
        logger_.log("Game server event loops are already running!", RED);
        return;
    }

    event_game_server_thread_ = std::thread(&GameServer::mainEventLoopGS, this);
    event_chunk_server_thread_ = std::thread(&GameServer::mainEventLoopCH, this);
    event_ping_thread_ = std::thread(&GameServer::mainEventLoopPing, this);
}

void GameServer::stop()
{
    running_ = false;
    scheduler_.stop();
    eventCondition.notify_all();
}

GameServer::~GameServer()
{
    logger_.log("Shutting down Game Server...", YELLOW);
    
    stop();  

    if (event_game_server_thread_.joinable())
        event_game_server_thread_.join();

    if (event_chunk_server_thread_.joinable())
        event_chunk_server_thread_.join();

    if (event_ping_thread_.joinable())
        event_ping_thread_.join();
}
