#include "services/MobManager.hpp"
#include <cmath>
#include <spdlog/logger.h>

MobManager::MobManager(Database &database, Logger &logger)
    : database_(database), logger_(logger)
{
    log_ = logger.getSystem("mob");
    loadMobs();
}

// Function to load mobs from the database and store them in memory
void
MobManager::loadMobs()
{
    try
    {
        auto _dbConn = database_.getConnectionLocked();
        pqxx::work transaction(_dbConn.get()); // Start a transaction
        pqxx::result selectMobs = database_.executeQueryWithTransaction(
            transaction,
            "get_mobs",
            {});

        if (selectMobs.empty())
        {
            // log that the data is empty
            log_->error("No mobs found in the database");
            // Rollback the transaction
            transaction.abort();
        }

        for (const auto &row : selectMobs)
        {
            MobDataStruct mobData;
            mobData.id = row["id"].as<int>();
            mobData.name = row["name"].as<std::string>();
            mobData.slug = row["slug"].as<std::string>();
            mobData.level = row["level"].as<int>();
            mobData.baseExperience = row["base_xp"].as<int>();
            mobData.radius = row["radius"].as<int>();
            mobData.raceName = row["race"].as<std::string>();
            mobData.currentHealth = row["spawn_health"].as<int>();
            mobData.currentMana = row["spawn_mana"].as<int>();
            mobData.isAggressive = row["is_aggressive"].as<bool>();
            mobData.isDead = row["is_dead"].as<bool>();
            mobData.rankId = row["rank_id"].as<int>();
            mobData.rankCode = row["rank_code"].as<std::string>();
            mobData.rankMult = row["rank_mult"].as<float>();

            // Per-mob AI configuration (migration 011)
            mobData.aggroRange = row["aggro_range"].as<float>();
            mobData.attackRange = row["attack_range"].as<float>();
            mobData.attackCooldown = row["attack_cooldown"].as<float>();
            mobData.chaseMultiplier = row["chase_multiplier"].as<float>();
            mobData.patrolSpeed = row["patrol_speed"].as<float>();

            // Social behaviour (migration 012)
            mobData.isSocial = row["is_social"].as<bool>();
            mobData.chaseDuration = row["chase_duration"].as<float>();

            // AI depth (migration 016)
            mobData.fleeHpThreshold = row["flee_hp_threshold"].as<float>();
            mobData.aiArchetype = row["ai_archetype"].as<std::string>();

            // Survival / Rare mob groundwork (Stage 3, migration 038)
            mobData.canEvolve = row["can_evolve"].as<bool>();
            mobData.isRare = row["is_rare"].as<bool>();
            mobData.rareSpawnChance = row["rare_spawn_chance"].as<float>();
            mobData.rareSpawnCondition = row["rare_spawn_condition"].as<std::string>();

            // Social systems (Stage 4, migration 039)
            mobData.factionSlug = row["faction_slug"].as<std::string>();
            mobData.repDeltaPerKill = row["rep_delta_per_kill"].as<int>();

            // Bestiary metadata (migration 040)
            mobData.biomeSlug = row["biome_slug"].as<std::string>();
            mobData.mobTypeSlug = row["mob_type_slug"].as<std::string>();
            mobData.hpMin = row["hp_min"].as<int>();
            mobData.hpMax = row["hp_max"].as<int>();

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
            // Apply rankMult to all attributes so elite/boss mobs have proportionally
            // stronger stats than normal mobs of the same template.
            for (const auto &attributeRow : selectMobAttributes)
            {
                MobAttributeStruct mobAttribute;
                mobAttribute.mob_id = mobData.id;
                mobAttribute.id = attributeRow["id"].as<int>();
                mobAttribute.name = attributeRow["name"].as<std::string>();
                mobAttribute.slug = attributeRow["slug"].as<std::string>();

                // Scale by rank multiplier (normal=1.0, elite=2.0, boss=3.0, …)
                const int rawValue = attributeRow["value"].as<int>();
                mobAttribute.value = static_cast<int>(std::round(rawValue * mobData.rankMult));

                if (mobAttribute.slug == "max_health")
                {
                    mobData.maxHealth = mobAttribute.value;
                }
                else if (mobAttribute.slug == "max_mana")
                {
                    mobData.maxMana = mobAttribute.value;
                }

                mobData.attributes.push_back(mobAttribute);
            }

            mobs_[mobData.id] = mobData;
        }
    }
    catch (const std::exception &e)
    {
        logger_.logError("Error loading mobs: " + std::string(e.what()));
    }
}

// Function to get all mobs from memory as map
std::map<int, MobDataStruct>
MobManager::getMobs() const
{
    return mobs_;
}

// Function to get all mobs from memory as vector
std::vector<MobDataStruct>
MobManager::getMobsAsVector() const
{
    std::vector<MobDataStruct> mobs;
    for (const auto &mob : mobs_)
    {
        mobs.push_back(mob.second);
    }
    return mobs;
}

// Function to get a mob by ID
MobDataStruct
MobManager::getMobById(int mobId) const
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

// get mobs attributes
std::map<int, MobAttributeStruct>
MobManager::getMobsAttributes() const
{
    std::map<int, MobAttributeStruct> mobAttributes;
    for (const auto &mob : mobs_)
    {
        for (const auto &attribute : mob.second.attributes)
        {
            mobAttributes[attribute.id] = attribute;
        }
    }
    return mobAttributes;
}