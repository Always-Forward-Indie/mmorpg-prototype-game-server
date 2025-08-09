#include "services/ItemManager.hpp"

ItemManager::ItemManager(Database &database, Logger &logger)
    : database_(database), logger_(logger)
{
    loadItems();
    loadMobLoot();
}

void
ItemManager::loadItems()
{
    try
    {
        pqxx::work transaction(database_.getConnection());
        pqxx::result selectItems = database_.executeQueryWithTransaction(
            transaction,
            "get_items",
            {});

        if (selectItems.empty())
        {
            logger_.logError("No items found in the database");
            transaction.abort();
            return;
        }

        std::unique_lock<std::shared_mutex> lock(itemsMutex_);

        for (const auto &row : selectItems)
        {
            ItemDataStruct itemData;
            itemData.id = row["id"].as<int>();
            itemData.name = row["name"].as<std::string>();
            itemData.slug = row["slug"].as<std::string>();
            itemData.description = row["description"].as<std::string>();
            itemData.isQuestItem = row["is_quest_item"].as<bool>();
            itemData.itemType = row["item_type"].as<int>();
            itemData.itemTypeName = row["item_type_name"].as<std::string>();
            itemData.itemTypeSlug = row["item_type_slug"].as<std::string>();

            // Load item attributes
            pqxx::result selectItemAttributes = database_.executeQueryWithTransaction(
                transaction,
                "get_item_attributes",
                {itemData.id});

            for (const auto &attributeRow : selectItemAttributes)
            {
                ItemAttributeStruct itemAttribute;
                itemAttribute.item_id = itemData.id;
                itemAttribute.id = attributeRow["id"].as<int>();
                itemAttribute.name = attributeRow["name"].as<std::string>();
                itemAttribute.slug = attributeRow["slug"].as<std::string>();
                itemAttribute.value = attributeRow["value"].as<int>();

                itemData.attributes.push_back(itemAttribute);
            }

            items_[itemData.id] = itemData;
        }

        transaction.commit();
        logger_.log("Loaded " + std::to_string(items_.size()) + " items from database");
    }
    catch (const std::exception &e)
    {
        logger_.logError("Error loading items: " + std::string(e.what()));
    }
}

void
ItemManager::loadMobLoot()
{
    try
    {
        pqxx::work transaction(database_.getConnection());
        pqxx::result selectMobLoot = database_.executeQueryWithTransaction(
            transaction,
            "get_mobs_loot",
            {});

        if (selectMobLoot.empty())
        {
            logger_.logError("No mob loot information found in the database");
            transaction.abort();
            return;
        }

        std::unique_lock<std::shared_mutex> lock(lootMutex_);

        for (const auto &row : selectMobLoot)
        {
            MobLootInfoStruct lootInfo;
            lootInfo.id = row["id"].as<int>();
            lootInfo.mobId = row["mob_id"].as<int>();
            lootInfo.itemId = row["item_id"].as<int>();
            lootInfo.dropChance = row["drop_chance"].as<float>();

            mobLootInfo_[lootInfo.mobId].push_back(lootInfo);
        }

        transaction.commit();

        int totalLootEntries = 0;
        for (const auto &mobLoot : mobLootInfo_)
        {
            totalLootEntries += mobLoot.second.size();
        }

        logger_.log("Loaded loot information for " + std::to_string(mobLootInfo_.size()) +
                    " mobs with " + std::to_string(totalLootEntries) + " total loot entries");
    }
    catch (const std::exception &e)
    {
        logger_.logError("Error loading mob loot: " + std::string(e.what()));
    }
}

std::map<int, ItemDataStruct>
ItemManager::getItems() const
{
    std::shared_lock<std::shared_mutex> lock(itemsMutex_);
    return items_;
}

std::vector<ItemDataStruct>
ItemManager::getItemsAsVector() const
{
    std::shared_lock<std::shared_mutex> lock(itemsMutex_);
    std::vector<ItemDataStruct> itemsVector;

    for (const auto &item : items_)
    {
        itemsVector.push_back(item.second);
    }

    return itemsVector;
}

ItemDataStruct
ItemManager::getItemById(int itemId) const
{
    std::shared_lock<std::shared_mutex> lock(itemsMutex_);
    auto it = items_.find(itemId);
    if (it != items_.end())
    {
        return it->second;
    }
    return ItemDataStruct();
}

std::map<int, std::vector<MobLootInfoStruct>>
ItemManager::getMobLootInfo() const
{
    std::shared_lock<std::shared_mutex> lock(lootMutex_);
    return mobLootInfo_;
}

std::vector<MobLootInfoStruct>
ItemManager::getLootForMob(int mobId) const
{
    std::shared_lock<std::shared_mutex> lock(lootMutex_);
    auto it = mobLootInfo_.find(mobId);
    if (it != mobLootInfo_.end())
    {
        return it->second;
    }
    return std::vector<MobLootInfoStruct>();
}
