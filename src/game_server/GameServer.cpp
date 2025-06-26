#include "game_server/GameServer.hpp"
#include <unordered_set>

GameServer::GameServer(GameServices &gameServices,
                        EventHandler &eventHandler,
                        EventQueue &eventQueueGameServer,
                        EventQueue &eventQueueChunkServer,
                        EventQueue &eventQueueGameServerPing,
                        Scheduler &scheduler)
    :
      eventQueueGameServer_(eventQueueGameServer),
      eventQueueChunkServer_(eventQueueChunkServer),
      eventQueueGameServerPing_(eventQueueGameServerPing),
      eventHandler_(eventHandler),
      scheduler_(scheduler),
      gameServices_(gameServices)
{
    
}

void GameServer::mainEventLoopGS()
{
    gameServices_.getLogger().log("Add Tasks To Game Server Scheduler...", YELLOW);
    constexpr int BATCH_SIZE = 10;

    // TODO - save different data to the database in different time intervals

    try
    {
        gameServices_.getLogger().log("Starting Game Server Event Loop...", YELLOW);
        while (running_)
        {
            std::vector<Event> eventsBatch;
            if (eventQueueGameServer_.popBatch(eventsBatch, BATCH_SIZE))
            {
                processBatch(eventsBatch);
            }
        }
    }
    catch (const std::exception &e)
    {
        gameServices_.getLogger().logError(e.what(), RED);
    }
}



void GameServer::mainEventLoopCH()
{
    constexpr int BATCH_SIZE = 10;
    gameServices_.getLogger().log("Add Tasks To Chunk Server Scheduler...", YELLOW);

    // TODO - Get different data from chunk servers in different time intervals

    try
    {
        gameServices_.getLogger().log("Starting Chunk Server Event Loop...", YELLOW);
        while (running_)
        {
            std::vector<Event> eventsBatch;
            if (eventQueueChunkServer_.popBatch(eventsBatch, BATCH_SIZE))
            {
                processBatch(eventsBatch);
            }
        }
    }
    catch (const std::exception &e)
    {
        gameServices_.getLogger().logError(e.what(), RED);
    }
}

void GameServer::mainEventLoopPing()
{
    constexpr int BATCH_SIZE = 1; // Ping обрабатывай сразу

    gameServices_.getLogger().log("Starting Ping Event Loop...", YELLOW);

    try
    {
        while (running_)
        {
            std::vector<Event> pingEvents;
            if (eventQueueGameServerPing_.popBatch(pingEvents, BATCH_SIZE))
            {
                processPingBatch(pingEvents);
            }
        }
    }
    catch (const std::exception &e)
    {
        gameServices_.getLogger().logError("Ping Event Loop Error: " + std::string(e.what()), RED);
    }
}

void GameServer::processPingBatch(const std::vector<Event>& pingEvents)
{
    for (const auto& event : pingEvents)
    {
        threadPool_.enqueueTask([this, event] {
            try
            {
                eventHandler_.dispatchEvent(event);
            }
            catch (const std::exception &e)
            {
                gameServices_.getLogger().logError("Error processing PING_EVENT: " + std::string(e.what()));
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
        // Create a deep copy of the event to ensure its data remains valid
        // when processed asynchronously in the thread pool
        Event eventCopy = event;
        threadPool_.enqueueTask([this, eventCopy] {
            try
            {
                eventHandler_.dispatchEvent(eventCopy);
            }
            catch (const std::exception &e)
            {
                gameServices_.getLogger().logError("Error processing priority dispatchEvent: " + std::string(e.what()));
            }
        });
    }

    // Process normal events
    for (const auto& event : normalEvents)
    {
        // Create a deep copy of the event to ensure its data remains valid
        // when processed asynchronously in the thread pool
        Event eventCopy = event;
        threadPool_.enqueueTask([this, eventCopy] {
            try
            {
                eventHandler_.dispatchEvent(eventCopy);
            }
            catch (const std::exception &e)
            {
                gameServices_.getLogger().logError("Error in normal dispatchEvent: " + std::string(e.what()));
            }
        });
    }

    eventCondition.notify_all();
}



void GameServer::startMainEventLoop()
{
    if (event_game_server_thread_.joinable() || event_chunk_server_thread_.joinable())
    {
        gameServices_.getLogger().log("Game server event loops are already running!", RED);
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
    gameServices_.getLogger().log("Shutting down Game Server...", YELLOW);
    
    stop();  

    if (event_game_server_thread_.joinable())
        event_game_server_thread_.join();

    if (event_chunk_server_thread_.joinable())
        event_chunk_server_thread_.join();

    if (event_ping_thread_.joinable())
        event_ping_thread_.join();
}
