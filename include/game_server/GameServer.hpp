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
    void startMainEventLoop();
    
private:
    //Events
    void mainEventLoop();

    std::thread event_thread_;
    ClientData& clientData_;
    Logger& logger_;
    EventQueue& eventQueueGameServer_;
    EventQueue& eventQueueChunkServer_;
    EventHandler eventHandler_;
    NetworkManager& networkManager_;
    CharacterManager& characterManager_;
    Scheduler& scheduler_;
    Database& database_;
};
