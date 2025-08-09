#pragma once
#include "data/DataStructs.hpp"
#include "utils/Database.hpp"
#include "utils/Logger.hpp"
#include <map>
#include <shared_mutex>

class ItemManager
{
  public:
    ItemManager(Database &database, Logger &logger);

    /**
     * @brief Load all items from database into memory
     */
    void loadItems();

    /**
     * @brief Load all mob loot information from database
     */
    void loadMobLoot();

    /**
     * @brief Get all items as map
     * @return Map of item ID to ItemDataStruct
     */
    std::map<int, ItemDataStruct> getItems() const;

    /**
     * @brief Get all items as vector
     * @return Vector of ItemDataStruct
     */
    std::vector<ItemDataStruct> getItemsAsVector() const;

    /**
     * @brief Get item by ID
     * @param itemId Item ID to retrieve
     * @return ItemDataStruct or empty struct if not found
     */
    ItemDataStruct getItemById(int itemId) const;

    /**
     * @brief Get mob loot information
     * @return Map of mob ID to vector of MobLootInfoStruct
     */
    std::map<int, std::vector<MobLootInfoStruct>> getMobLootInfo() const;

    /**
     * @brief Get loot for specific mob
     * @param mobId Mob ID
     * @return Vector of MobLootInfoStruct for the mob
     */
    std::vector<MobLootInfoStruct> getLootForMob(int mobId) const;

  private:
    Database &database_;
    Logger &logger_;

    // Store items in memory
    std::map<int, ItemDataStruct> items_;

    // Store mob loot information (mobId -> vector of loot entries)
    std::map<int, std::vector<MobLootInfoStruct>> mobLootInfo_;

    // Thread safety
    mutable std::shared_mutex itemsMutex_;
    mutable std::shared_mutex lootMutex_;
};
