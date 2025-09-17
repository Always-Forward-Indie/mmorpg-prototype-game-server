#pragma once
#include "services/CharacterManager.hpp"
#include "services/ChunkManager.hpp"
#include "services/ClientManager.hpp"
#include "services/ItemManager.hpp"
#include "services/MobManager.hpp"
#include "services/NPCManager.hpp"
#include "services/SpawnZoneManager.hpp"
#include "utils/Database.hpp"
#include "utils/Logger.hpp"

class GameServices
{
  public:
    // Передаем необходимые зависимости (например, Logger), остальные менеджеры создаются внутри
    GameServices(Database &database, Logger &logger)
        : logger_(logger),
          database_(database),
          mobManager_(database_, logger_),
          itemManager_(database_, logger_),
          npcManager_(database_, logger_),
          spawnZoneManager_(mobManager_, database_, logger_),
          characterManager_(logger_),
          clientManager_(logger_),
          chunkManager_(logger_)
    {
    }

    Logger &getLogger()
    {
        return logger_;
    }
    Database &getDatabase()
    {
        return database_;
    }
    MobManager &getMobManager()
    {
        return mobManager_;
    }
    ItemManager &getItemManager()
    {
        return itemManager_;
    }
    NPCManager &getNPCManager()
    {
        return npcManager_;
    }
    SpawnZoneManager &getSpawnZoneManager()
    {
        return spawnZoneManager_;
    }
    CharacterManager &getCharacterManager()
    {
        return characterManager_;
    }
    ClientManager &getClientManager()
    {
        return clientManager_;
    }
    ChunkManager &getChunkManager()
    {
        return chunkManager_;
    }

  private:
    Logger &logger_;
    Database &database_;
    MobManager mobManager_;
    ItemManager itemManager_;
    NPCManager npcManager_;
    SpawnZoneManager spawnZoneManager_;
    CharacterManager characterManager_;
    ClientManager clientManager_;
    ChunkManager chunkManager_;
};
