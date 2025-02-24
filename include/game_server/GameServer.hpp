#pragma once

#include <array>
#include <string>
#include <iostream>
#include <chrono>
#include <thread>
#include <atomic>
#include "network/NetworkManager.hpp"
#include "events/Event.hpp"
#include "events/EventQueue.hpp"
#include "events/EventHandler.hpp"
#include "utils/Logger.hpp"
#include "utils/Scheduler.hpp"
#include "utils/ThreadPool.hpp"
#include "services/CharacterManager.hpp"
#include "services/MobManager.hpp"
#include "services/SpawnZoneManager.hpp"

class GameServer {
public:
    GameServer(ClientData &clientData,
               EventQueue& eventQueueGameServer, 
               EventQueue& eventQueueChunkServer, 
               Scheduler& scheduler,
               NetworkManager& networkManager, 
               ChunkServerWorker& chunkServerWorker, 
               Database& database,
               CharacterManager& characterManager,
               Logger& logger);
    
    ~GameServer();

    void processBatch(const std::vector<Event> &eventsBatch);
    void startMainEventLoop();
    void stop();

    void mainEventLoopGS();
    void mainEventLoopCH();

private:
    std::atomic<bool> running_{true};

    std::thread event_game_server_thread_;
    std::thread event_chunk_server_thread_;

    ClientData& clientData_;
    Logger& logger_;
    EventQueue& eventQueueGameServer_;
    EventQueue& eventQueueChunkServer_;
    EventHandler eventHandler_;
    NetworkManager& networkManager_;

    Scheduler& scheduler_;
    Database& database_;

    CharacterManager& characterManager_;
    MobManager mobManager_;
    SpawnZoneManager spawnZoneManager_;

    std::mutex eventMutex;
    std::condition_variable eventCondition;

    ThreadPool threadPool_{std::thread::hardware_concurrency()};
};
