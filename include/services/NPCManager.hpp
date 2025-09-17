#pragma once
#include "data/DataStructs.hpp"
#include "utils/Database.hpp"
#include "utils/Logger.hpp"
#include <map>
#include <mutex>
#include <vector>

/**
 * @brief NPC Manager class following SOLID principles
 *
 * Single Responsibility: Manages NPC data loading and retrieval
 * Open/Closed: Extensible for new NPC types without modification
 * Liskov Substitution: Can be extended with specialized NPC managers
 * Interface Segregation: Provides only necessary NPC operations
 * Dependency Inversion: Depends on abstractions (Database, Logger)
 *
 * Thread-safe operations for concurrent access
 */
class NPCManager
{
  public:
    /**
     * @brief Constructor
     * @param database Database instance for data access
     * @param logger Logger instance for error handling
     */
    NPCManager(Database &database, Logger &logger);

    /**
     * @brief Load NPCs from database with all related data
     * Thread-safe operation
     */
    void loadNPCs();

    /**
     * @brief Get all NPCs as map (thread-safe)
     * @return Map of NPC ID to NPCDataStruct
     */
    std::map<int, NPCDataStruct> getNPCs() const;

    /**
     * @brief Get all NPCs as vector (thread-safe)
     * @return Vector of NPCDataStruct
     */
    std::vector<NPCDataStruct> getNPCsAsVector() const;

    /**
     * @brief Get NPC by ID (thread-safe)
     * @param npcId NPC identifier
     * @return NPCDataStruct if found, empty struct otherwise
     */
    NPCDataStruct getNPCById(int npcId) const;

    /**
     * @brief Get all NPCs (parameters ignored for now)
     * @param chunkX Chunk X coordinate (ignored)
     * @param chunkY Chunk Y coordinate (ignored)
     * @param chunkZ Chunk Z coordinate (ignored)
     * @return Vector of all NPCs
     */
    std::vector<NPCDataStruct> getNPCsByChunk(float chunkX, float chunkY, float chunkZ) const;

    /**
     * @brief Check if NPCs are loaded
     * @return True if NPCs are loaded, false otherwise
     */
    bool isLoaded() const;

    /**
     * @brief Get total count of loaded NPCs
     * @return Number of NPCs in memory
     */
    size_t getNPCCount() const;

  private:
    Database &database_;
    Logger &logger_;

    // Thread-safe storage for NPCs
    mutable std::mutex npcsMutex_;
    std::map<int, NPCDataStruct> npcs_;
    bool loaded_ = false;

    /**
     * @brief Load NPC attributes from database
     * @param transaction Database transaction
     * @param npcId NPC identifier
     * @return Vector of NPC attributes
     */
    std::vector<NPCAttributeStruct> loadNPCAttributes(pqxx::work &transaction, int npcId);

    /**
     * @brief Load NPC skills from database
     * @param transaction Database transaction
     * @param npcId NPC identifier
     * @return Vector of NPC skills
     */
    std::vector<SkillStruct> loadNPCSkills(pqxx::work &transaction, int npcId);

    /**
     * @brief Load NPC position from database
     * @param transaction Database transaction
     * @param npcId NPC identifier
     * @return PositionStruct with NPC coordinates
     */
    PositionStruct loadNPCPosition(pqxx::work &transaction, int npcId);

    /**
     * @brief Calculate max health from attributes
     * @param attributes Vector of NPC attributes
     * @return Max health value
     */
    int calculateMaxHealth(const std::vector<NPCAttributeStruct> &attributes) const;

    /**
     * @brief Calculate max mana from attributes
     * @param attributes Vector of NPC attributes
     * @return Max mana value
     */
    int calculateMaxMana(const std::vector<NPCAttributeStruct> &attributes) const;
};