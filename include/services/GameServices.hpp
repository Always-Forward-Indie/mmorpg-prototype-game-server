#pragma once
#include "services/CharacterManager.hpp"
#include "services/ChunkManager.hpp"
#include "services/ClassSpawnZoneManager.hpp"
#include "services/ClientManager.hpp"
#include "services/DialogueQuestManager.hpp"
#include "services/GameConfigService.hpp"
#include "services/ItemManager.hpp"
#include "services/MobManager.hpp"
#include "services/NPCManager.hpp"
#include "services/SpawnZoneManager.hpp"
#include "utils/Database.hpp"
#include "utils/Logger.hpp"

class GameServices
{
  public:
    GameServices(Database &database, Logger &logger)
        : logger_(logger),
          database_(database),
          mobManager_(logger_),
          itemManager_(logger_),
          npcManager_(logger_),
          spawnZoneManager_(mobManager_, logger_),
          characterManager_(logger_),
          classSpawnZoneManager_(logger_),
          clientManager_(logger_),
          chunkManager_(logger_),
          dialogueQuestManager_(database_, logger_),
          gameConfigService_(logger_)
    {
        // No I/O here: construction never touches the DB (see loadStaticData).
    }

    /// Load all static catalog data in dependency order. Called once from
    /// main after construction; throws DatabaseError on transport failure
    /// (fail-fast with the query name, caught in main -> exit 1).
    void loadStaticData()
    {
        mobManager_.loadMobs(database_);
        itemManager_.loadItems(database_);
        itemManager_.loadMobLoot(database_);
        spawnZoneManager_.loadMobSpawnZones(database_);
        classSpawnZoneManager_.loadClassSpawnZones(database_);
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
    ClassSpawnZoneManager &getClassSpawnZoneManager()
    {
        return classSpawnZoneManager_;
    }
    ClientManager &getClientManager()
    {
        return clientManager_;
    }
    ChunkManager &getChunkManager()
    {
        return chunkManager_;
    }

    /// Ordered-shutdown hook (S16a): release every socket held by the
    /// managers BEFORE NetworkManager's io_context is destroyed. Called once
    /// from main after the event loops join; afterwards the maps are empty.
    void dropAllSockets()
    {
        clientManager_.dropAllSockets();
        chunkManager_.dropAllSockets();
    }
    DialogueQuestManager &getDialogueQuestManager()
    {
        return dialogueQuestManager_;
    }
    GameConfigService &getGameConfigService()
    {
        return gameConfigService_;
    }

  private:
    Logger &logger_;
    Database &database_;
    MobManager mobManager_;
    ItemManager itemManager_;
    NPCManager npcManager_;
    SpawnZoneManager spawnZoneManager_;
    CharacterManager characterManager_;
    ClassSpawnZoneManager classSpawnZoneManager_;
    ClientManager clientManager_;
    ChunkManager chunkManager_;
    DialogueQuestManager dialogueQuestManager_;
    GameConfigService gameConfigService_;
};
