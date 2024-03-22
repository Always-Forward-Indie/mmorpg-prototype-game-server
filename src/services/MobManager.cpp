#include "services/MobManager.hpp"

MobManager::MobManager(Database& database, Logger& logger)
    : database_(database), logger_(logger)
{
    loadMobs();
}

// Function to load mobs from the database and store them in memory
void MobManager::loadMobs()
{
    try
    {
        pqxx::work transaction(database_.getConnection()); // Start a transaction
        pqxx::result selectMobs = database_.executeQueryWithTransaction(
            transaction,
            "get_mobs",
            {});

        if (selectMobs.empty())
        {
            // log that the data is empty
            logger_.logError("No mobs found in the database");
            // Rollback the transaction
            transaction.abort(); 
        }

        for (const auto& row : selectMobs)
        {
            MobDataStruct mobData;
            mobData.id = row["id"].as<int>();
            mobData.name = row["name"].as<std::string>();
            mobData.level = row["level"].as<int>();
            mobData.raceName = row["race"].as<std::string>();

            // get mob attributes
            pqxx::result selectMobAttributes = database_.executeQueryWithTransaction(
                transaction,
                "get_mob_attributes",
                {mobData.id});

            if (selectMobAttributes.empty())
            {
                transaction.abort(); // Rollback the transaction
            }

            // populate mob attributes
            for (const auto& attributeRow : selectMobAttributes)
            {
                MobAttributeStruct mobAttribute;
                mobAttribute.id = attributeRow["id"].as<int>();
                mobAttribute.name = attributeRow["name"].as<std::string>();
                mobAttribute.slug = attributeRow["slug"].as<std::string>();
                mobAttribute.value = attributeRow["value"].as<int>();

                mobData.attributes.push_back(mobAttribute);
            }

            //get health and mana from attributes
            for (const auto& attribute : mobData.attributes)
            {
                if (attribute.slug == "max_health")
                {
                    mobData.currentHealth = attribute.value;
                }
                else if (attribute.slug == "max_mana")
                {
                    mobData.currentMana = attribute.value;
                }
            }

            mobs_[mobData.id] = mobData;
        }
    }
    catch (const std::exception& e)
    {
        logger_.logError("Error loading mobs: " + std::string(e.what()));
    }
}

// Function to get all mobs from memory as map
std::map<int, MobDataStruct> MobManager::getMobs() const
{
    return mobs_;
}

// Function to get all mobs from memory as vector
std::vector<MobDataStruct> MobManager::getMobsAsVector() const
{
    std::vector<MobDataStruct> mobs;
    for (const auto& mob : mobs_)
    {
        mobs.push_back(mob.second);
    }
    return mobs;
}

// Function to get a mob by ID
MobDataStruct MobManager::getMobById(int mobId) const
{
    auto mob = mobs_.find(mobId);
    if (mob != mobs_.end())
    {
        return mob->second;
    }
    else
    {
        return MobDataStruct();
    }
}