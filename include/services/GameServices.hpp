#pragma once
#include "utils/Logger.hpp"
#include "services/MobManager.hpp"
#include "services/SpawnZoneManager.hpp"
#include "services/CharacterManager.hpp"
#include "services/ClientManager.hpp"
#include "services/ChunkManager.hpp"
#include "utils/Database.hpp"

class GameServices {
public:
    // Передаем необходимые зависимости (например, Logger), остальные менеджеры создаются внутри
    GameServices(Database &database, Logger &logger)
        : logger_(logger),
            database_(database),
            mobManager_(database_, logger_),
            spawnZoneManager_(mobManager_, database_, logger_),
            characterManager_(logger_),
            clientManager_(logger_),
            chunkManager_(logger_)
    {}

    Logger& getLogger() { return logger_; }
    Database& getDatabase() { return database_; }
    MobManager& getMobManager() { return mobManager_; }
    SpawnZoneManager& getSpawnZoneManager() { return spawnZoneManager_; }
    CharacterManager& getCharacterManager() { return characterManager_; }
    ClientManager& getClientManager() { return clientManager_; }
    ChunkManager& getChunkManager() { return chunkManager_; }

private:
    Logger &logger_;
    Database &database_;
    MobManager mobManager_;
    SpawnZoneManager spawnZoneManager_;
    CharacterManager characterManager_;
    ClientManager clientManager_;   
    ChunkManager chunkManager_;
};
