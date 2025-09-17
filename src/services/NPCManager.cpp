#include "services/NPCManager.hpp"
#include <algorithm>

NPCManager::NPCManager(Database &database, Logger &logger)
    : database_(database), logger_(logger)
{
    // NPCs will be loaded when explicitly requested
    // This follows lazy loading pattern for better performance
}

void
NPCManager::loadNPCs()
{
    std::lock_guard<std::mutex> lock(npcsMutex_);

    if (loaded_)
    {
        logger_.log("NPCs already loaded, skipping reload", YELLOW);
        return;
    }

    try
    {
        pqxx::work transaction(database_.getConnection());

        // Load basic NPC data
        pqxx::result selectNPCs = database_.executeQueryWithTransaction(
            transaction,
            "get_npcs",
            {});

        if (selectNPCs.empty())
        {
            logger_.logError("No NPCs found in the database");
            transaction.abort();
            return;
        }

        logger_.log("Loading " + std::to_string(selectNPCs.size()) + " NPCs from database", BLUE);

        for (const auto &row : selectNPCs)
        {
            NPCDataStruct npcData;

            // Basic NPC information
            npcData.id = row["id"].as<int>();
            npcData.name = row["name"].as<std::string>();
            npcData.slug = row["slug"].as<std::string>();
            npcData.raceName = row["race"].as<std::string>();
            npcData.level = row["level"].as<int>();
            npcData.currentHealth = row["current_health"].as<int>();
            npcData.currentMana = row["current_mana"].as<int>();

            // NPC-specific properties with null checks
            if (!row["npc_type"].is_null())
            {
                npcData.npcType = row["npc_type"].as<std::string>();
            }
            else
            {
                npcData.npcType = "general"; // default value
            }

            if (!row["is_interactable"].is_null())
            {
                npcData.isInteractable = row["is_interactable"].as<bool>();
            }
            else
            {
                npcData.isInteractable = true; // default value
            }

            // Optional fields with null checks
            if (!row["dialogue_id"].is_null())
            {
                npcData.dialogueId = row["dialogue_id"].as<int>();
            }
            if (!row["quest_id"].is_null())
            {
                npcData.questId = row["quest_id"].as<int>();
            }

            // Load related data
            npcData.attributes = loadNPCAttributes(transaction, npcData.id);
            npcData.skills = loadNPCSkills(transaction, npcData.id);
            npcData.position = loadNPCPosition(transaction, npcData.id);

            // Calculate derived values
            npcData.maxHealth = calculateMaxHealth(npcData.attributes);
            npcData.maxMana = calculateMaxMana(npcData.attributes);

            // Ensure current health/mana don't exceed max values
            if (npcData.currentHealth > npcData.maxHealth)
            {
                npcData.currentHealth = npcData.maxHealth;
            }
            if (npcData.currentMana > npcData.maxMana)
            {
                npcData.currentMana = npcData.maxMana;
            }

            npcs_[npcData.id] = std::move(npcData);
        }

        transaction.commit();
        loaded_ = true;

        logger_.log("Successfully loaded " + std::to_string(npcs_.size()) + " NPCs", GREEN);
    }
    catch (const std::exception &e)
    {
        logger_.logError("Error loading NPCs: " + std::string(e.what()));
        npcs_.clear();
        loaded_ = false;
    }
}

std::map<int, NPCDataStruct>
NPCManager::getNPCs() const
{
    std::lock_guard<std::mutex> lock(npcsMutex_);
    return npcs_;
}

std::vector<NPCDataStruct>
NPCManager::getNPCsAsVector() const
{
    std::lock_guard<std::mutex> lock(npcsMutex_);
    std::vector<NPCDataStruct> npcs;
    npcs.reserve(npcs_.size());

    for (const auto &npcPair : npcs_)
    {
        npcs.push_back(npcPair.second);
    }

    return npcs;
}

NPCDataStruct
NPCManager::getNPCById(int npcId) const
{
    std::lock_guard<std::mutex> lock(npcsMutex_);

    auto it = npcs_.find(npcId);
    if (it != npcs_.end())
    {
        return it->second;
    }

    // Return empty NPC if not found
    NPCDataStruct emptyNPC;
    emptyNPC.id = -1; // Indicate not found
    return emptyNPC;
}

std::vector<NPCDataStruct>
NPCManager::getNPCsByChunk(float chunkX, float chunkY, float chunkZ) const
{
    std::lock_guard<std::mutex> lock(npcsMutex_);

    // Возвращаем ВСЕХ NPC, игнорируя координаты чанка
    std::vector<NPCDataStruct> allNPCs;
    allNPCs.reserve(npcs_.size());

    for (const auto &npcPair : npcs_)
    {
        allNPCs.push_back(npcPair.second);
    }

    return allNPCs;
}

bool
NPCManager::isLoaded() const
{
    std::lock_guard<std::mutex> lock(npcsMutex_);
    return loaded_;
}

size_t
NPCManager::getNPCCount() const
{
    std::lock_guard<std::mutex> lock(npcsMutex_);
    return npcs_.size();
}

std::vector<NPCAttributeStruct>
NPCManager::loadNPCAttributes(pqxx::work &transaction, int npcId)
{
    std::vector<NPCAttributeStruct> attributes;

    try
    {
        pqxx::result selectAttributes = database_.executeQueryWithTransaction(
            transaction,
            "get_npc_attributes",
            {npcId});

        for (const auto &attributeRow : selectAttributes)
        {
            NPCAttributeStruct attribute;
            attribute.npc_id = npcId;
            attribute.id = attributeRow["id"].as<int>();
            attribute.name = attributeRow["name"].as<std::string>();
            attribute.slug = attributeRow["slug"].as<std::string>();
            attribute.value = attributeRow["value"].as<int>();

            attributes.push_back(std::move(attribute));
        }
    }
    catch (const std::exception &e)
    {
        logger_.logError("Error loading NPC attributes for NPC ID " + std::to_string(npcId) + ": " + std::string(e.what()));
    }

    return attributes;
}

std::vector<SkillStruct>
NPCManager::loadNPCSkills(pqxx::work &transaction, int npcId)
{
    std::vector<SkillStruct> skills;

    try
    {
        pqxx::result selectSkills = database_.executeQueryWithTransaction(
            transaction,
            "get_npc_skills",
            {npcId});

        for (const auto &skillRow : selectSkills)
        {
            SkillStruct skill;
            skill.skillName = skillRow["skill_name"].as<std::string>();
            skill.skillSlug = skillRow["skill_slug"].as<std::string>();
            skill.scaleStat = skillRow["scale_stat"].as<std::string>();
            skill.school = skillRow["school"].as<std::string>();
            skill.skillEffectType = skillRow["skill_effect_type"].as<std::string>();
            skill.skillLevel = skillRow["skill_level"].as<int>();
            skill.coeff = skillRow["coeff"].as<float>();
            skill.flatAdd = skillRow["flat_add"].as<float>();
            skill.cooldownMs = skillRow["cooldown_ms"].as<int>();
            skill.gcdMs = skillRow["gcd_ms"].as<int>();
            skill.castMs = skillRow["cast_ms"].as<int>();
            skill.costMp = skillRow["cost_mp"].as<int>();
            skill.maxRange = skillRow["max_range"].as<float>();

            skills.push_back(std::move(skill));
        }
    }
    catch (const std::exception &e)
    {
        logger_.logError("Error loading NPC skills for NPC ID " + std::to_string(npcId) + ": " + std::string(e.what()));
    }

    return skills;
}

PositionStruct
NPCManager::loadNPCPosition(pqxx::work &transaction, int npcId)
{
    PositionStruct position;

    try
    {
        pqxx::result selectPosition = database_.executeQueryWithTransaction(
            transaction,
            "get_npc_position",
            {npcId});

        if (!selectPosition.empty())
        {
            const auto &row = selectPosition[0];
            position.positionX = row["x"].as<float>();
            position.positionY = row["y"].as<float>();
            position.positionZ = row["z"].as<float>();
            // rotationZ defaults to 0 if not in database
        }
    }
    catch (const std::exception &e)
    {
        logger_.logError("Error loading NPC position for NPC ID " + std::to_string(npcId) + ": " + std::string(e.what()));
    }

    return position;
}

int
NPCManager::calculateMaxHealth(const std::vector<NPCAttributeStruct> &attributes) const
{
    auto it = std::find_if(attributes.begin(), attributes.end(), [](const NPCAttributeStruct &attr)
        { return attr.slug == "max_health"; });

    return (it != attributes.end()) ? it->value : 100; // Default health
}

int
NPCManager::calculateMaxMana(const std::vector<NPCAttributeStruct> &attributes) const
{
    auto it = std::find_if(attributes.begin(), attributes.end(), [](const NPCAttributeStruct &attr)
        { return attr.slug == "max_mana"; });

    return (it != attributes.end()) ? it->value : 50; // Default mana
}