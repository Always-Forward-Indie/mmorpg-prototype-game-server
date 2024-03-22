#pragma once
#include <array>
#include <string>
#include <iostream>
#include <chrono>
#include <thread>
#include "network/NetworkManager.hpp"
#include "events/Event.hpp"
#include "events/EventQueue.hpp"
#include "events/EventHandler.hpp"
#include "utils/Logger.hpp"
#include "utils/Scheduler.hpp"
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
    void mainEventLoop();

    void mainEventLoopGS();
    void mainEventLoopCH();

    
private:
    //Events
    std::thread event_game_server_thread_;
    std::thread event_chunk_server_thread_;

    std::thread event_thread_;
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
};
