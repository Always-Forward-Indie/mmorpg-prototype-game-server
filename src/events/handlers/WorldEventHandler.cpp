#include "events/handlers/WorldEventHandler.hpp"

#include "data/DataStructs.hpp"
#include "events/Outbox.hpp"
#include "utils/ResponseBuilder.hpp"
#include "utils/TerminalColors.hpp"

WorldEventHandler::WorldEventHandler(
    NetworkManager &networkManager,
    GameServices &gameServices)
    : BaseEventHandler(networkManager, gameServices)
{
}

void
WorldEventHandler::handleGetMobsAttributesEvent(const Event &event)
{
    // Retrieve the data from the event
    const auto &data = event.getData();
    int clientID = event.getClientID();
    // get socket from the event
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();

    try
    {
        // Get the mobs list from the database as map
        auto mobsList = gameServices_.getMobManager().getMobs();

        nlohmann::json mobsAttributesListJson = nlohmann::json::array();

        for (const auto &mobItem : mobsList)
        {
            const MobDataStruct &mobData = mobItem.second;

            for (const auto &attribute : mobData.attributes)
            {
                nlohmann::json attributeJson;
                attributeJson["id"] = attribute.id;
                attributeJson["mob_id"] = mobData.id;
                attributeJson["slug"] = attribute.slug;
                attributeJson["value"] = attribute.value;

                mobsAttributesListJson.push_back(attributeJson);
            }
        }

        // If the mobs attributes list is empty, log a message
        if (mobsAttributesListJson.empty())
        {
            log_->info("Mobs attributes list is empty!");
        }

        // Prepare the response message
        nlohmann::json response;
        ResponseBuilder builder;

        // Add the message to the response
        response = builder
                       .setHeader("message", "Getting mobs attributes list success!")
                       .setHeader("hash", "")
                       .setHeader("clientId", clientID)
                       .setHeader("eventType", "setMobsAttributes")
                       .setBody("mobsAttributesList", mobsAttributesListJson)
                       .build();
        // Prepare a response message
        std::string responseData = networkManager_.generateResponseMessage("success", response);

        // Send the response to the client
        networkManager_.sendResponse(clientSocket, responseData);
    }
    catch (const std::bad_variant_access &ex)
    {
        gameServices_.getLogger().log("Error here: " + std::string(ex.what()));
    }
}

void
WorldEventHandler::handleGetMobsListEvent(const Event &event)
{
    // Retrieve the data from the event
    const auto &data = event.getData();
    int clientID = event.getClientID();
    // get socket from the event
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();

    try
    {
        // load the mobs list from the database
        gameServices_.getMobManager().loadMobs(gameServices_.getDatabase());

        // Get the mobs list from the database as map
        auto mobsListMap = gameServices_.getMobManager().getMobs();

        nlohmann::json mobsListJson;
        for (const auto &mobItem : mobsListMap)
        {
            const MobDataStruct &mobData = mobItem.second;

            // Create a JSON object for the current mob
            nlohmann::json mobJson;
            mobJson["id"] = mobData.id;
            mobJson["UID"] = mobData.uid;
            mobJson["zoneId"] = mobData.zoneId;
            mobJson["name"] = mobData.name;
            mobJson["slug"] = mobData.slug;
            mobJson["race"] = mobData.raceName;
            mobJson["level"] = mobData.level;
            mobJson["baseExperience"] = mobData.baseExperience;
            mobJson["radius"] = mobData.radius;
            mobJson["currentHealth"] = mobData.currentHealth;
            mobJson["currentMana"] = mobData.currentMana;
            mobJson["maxMana"] = mobData.maxMana;
            mobJson["maxHealth"] = mobData.maxHealth;
            mobJson["isAggressive"] = mobData.isAggressive;
            mobJson["isDead"] = mobData.isDead;
            mobJson["posX"] = mobData.position.positionX;
            mobJson["posY"] = mobData.position.positionY;
            mobJson["posZ"] = mobData.position.positionZ;
            mobJson["rotZ"] = mobData.position.rotationZ;

            // Per-mob AI config (migration 011)
            mobJson["aggroRange"] = mobData.aggroRange;
            mobJson["attackRange"] = mobData.attackRange;
            mobJson["attackCooldown"] = mobData.attackCooldown;
            mobJson["chaseMultiplier"] = mobData.chaseMultiplier;
            mobJson["patrolSpeed"] = mobData.patrolSpeed;
            mobJson["patrolRadius"] = mobData.patrolRadius;

            // Social behaviour (migration 012)
            mobJson["isSocial"] = mobData.isSocial;
            mobJson["chaseDuration"] = mobData.chaseDuration;

            // Rank / difficulty tier (migrations 006/023)
            mobJson["rankId"] = mobData.rankId;
            mobJson["rankCode"] = mobData.rankCode;
            mobJson["rankMult"] = mobData.rankMult;

            // AI depth: flee behavior + archetype (migration 016)
            mobJson["fleeHpThreshold"] = mobData.fleeHpThreshold;
            mobJson["aiArchetype"] = mobData.aiArchetype;

            // Survival / Rare mob groundwork (Stage 3, migration 038)
            mobJson["canEvolve"] = mobData.canEvolve;
            mobJson["isRare"] = mobData.isRare;
            mobJson["rareSpawnChance"] = mobData.rareSpawnChance;
            mobJson["rareSpawnCondition"] = mobData.rareSpawnCondition;
            mobJson["rareOfMobId"] = mobData.rareOfMobId;

            // Social systems (Stage 4, migration 039)
            mobJson["factionSlug"] = mobData.factionSlug;
            mobJson["repDeltaPerKill"] = mobData.repDeltaPerKill;

            // Bestiary metadata (migration 040)
            mobJson["biomeSlug"] = mobData.biomeSlug;
            mobJson["mobTypeSlug"] = mobData.mobTypeSlug;
            mobJson["hpMin"] = mobData.hpMin;
            mobJson["hpMax"] = mobData.hpMax;

            // Add the current item to the mobs list json
            mobsListJson.push_back(mobJson);
        }

        // If the mobs list is empty, log a message
        if (mobsListJson.empty())
        {
            log_->info("Mobs list is empty!");
        }

        // Prepare the response message
        nlohmann::json response;
        ResponseBuilder builder;

        // Add the message to the response
        response = builder
                       .setHeader("message", "Getting mobs list success!")
                       .setHeader("hash", "")
                       .setHeader("clientId", clientID)
                       .setHeader("eventType", "setMobsList")
                       .setBody("mobsList", mobsListJson)
                       .build();
        // Prepare a response message
        std::string responseData = networkManager_.generateResponseMessage("success", response);

        // Send the response to the client
        networkManager_.sendResponse(clientSocket, responseData);

        // After sending mobs list, send mobs skills
        nlohmann::json mobsSkillsJson;
        for (const auto &mobItem : mobsListMap)
        {
            const MobDataStruct &mobData = mobItem.second;

            // Get skills for this mob from database
            auto mobSkills = gameServices_.getCharacterManager().getMobSkillsFromDatabase(
                gameServices_.getDatabase(), mobData.id);

            if (!mobSkills.empty())
            {
                nlohmann::json mobSkillsEntry;
                mobSkillsEntry["mobId"] = mobData.id;

                nlohmann::json skillsArray;
                for (const auto &skill : mobSkills)
                {
                    nlohmann::json skillJson;
                    skillJson["skillName"] = skill.skillName;
                    skillJson["skillSlug"] = skill.skillSlug;
                    skillJson["scaleStat"] = skill.scaleStat;
                    skillJson["school"] = skill.school;
                    skillJson["skillEffectType"] = skill.skillEffectType;
                    skillJson["skillLevel"] = skill.skillLevel;
                    skillJson["coeff"] = skill.coeff;
                    skillJson["flatAdd"] = skill.flatAdd;
                    skillJson["cooldownMs"] = skill.cooldownMs;
                    skillJson["gcdMs"] = skill.gcdMs;
                    skillJson["castMs"] = skill.castMs;
                    skillJson["costMp"] = skill.costMp;
                    skillJson["maxRange"] = skill.maxRange;
                    skillJson["areaRadius"] = skill.areaRadius;
                    skillJson["swingMs"] = skill.swingMs;
                    skillJson["animationName"] = skill.animationName;
                    skillsArray.push_back(skillJson);
                }

                mobSkillsEntry["skills"] = skillsArray;
                mobsSkillsJson.push_back(mobSkillsEntry);
            }
        }

        // Send mobs skills if any exist
        if (!mobsSkillsJson.empty())
        {
            nlohmann::json skillsResponse;
            ResponseBuilder skillsBuilder;

            skillsResponse = skillsBuilder
                                 .setHeader("message", "Getting mobs skills success!")
                                 .setHeader("hash", "")
                                 .setHeader("clientId", clientID)
                                 .setHeader("eventType", "setMobsSkills")
                                 .setBody("mobsSkills", mobsSkillsJson)
                                 .build();

            std::string skillsResponseData = networkManager_.generateResponseMessage("success", skillsResponse);
            networkManager_.sendResponse(clientSocket, skillsResponseData);

            gameServices_.getLogger().log("Sent skills for " + std::to_string(mobsSkillsJson.size()) + " mobs", GREEN);
        }
        else
        {
            log_->info("No mob skills found to send");
        }

        // Send mob weaknesses and resistances (migration 040)
        {
            auto _dbConn2 = gameServices_.getDatabase().getConnectionLocked();
            pqxx::work txnWR(_dbConn2.get());

            auto weakRows = gameServices_.getDatabase().execChecked(
                txnWR, "get_mob_weaknesses_all", {});
            auto resistRows = gameServices_.getDatabase().execChecked(
                txnWR, "get_mob_resistances_all", {});

            // Build per-mob weaknesses map
            std::unordered_map<int, std::vector<std::string>> weakMap;
            for (const auto &row : weakRows)
                weakMap[row["mob_id"].as<int>()].push_back(row["element_slug"].as<std::string>());

            std::unordered_map<int, std::vector<std::string>> resistMap;
            for (const auto &row : resistRows)
                resistMap[row["mob_id"].as<int>()].push_back(row["element_slug"].as<std::string>());

            nlohmann::json wrJson = nlohmann::json::array();
            for (const auto &mobItem : mobsListMap)
            {
                int mid = mobItem.first;
                nlohmann::json entry;
                entry["mobId"] = mid;
                entry["weaknesses"] = weakMap.count(mid) ? nlohmann::json(weakMap[mid]) : nlohmann::json::array();
                entry["resistances"] = resistMap.count(mid) ? nlohmann::json(resistMap[mid]) : nlohmann::json::array();
                wrJson.push_back(std::move(entry));
            }

            if (!wrJson.empty())
            {
                nlohmann::json wrResponse = ResponseBuilder()
                                                .setHeader("message", "Mob weaknesses/resistances")
                                                .setHeader("hash", "")
                                                .setHeader("clientId", clientID)
                                                .setHeader("eventType", "setMobWeaknessesResistances")
                                                .setBody("data", wrJson)
                                                .build();
                networkManager_.sendResponse(clientSocket,
                    networkManager_.generateResponseMessage("success", wrResponse));
                log_->info("[EH] Sent weaknesses/resistances for " +
                           std::to_string(wrJson.size()) + " mobs");
            }
        }
    }
    catch (const std::bad_variant_access &ex)
    {
        gameServices_.getLogger().log("Error here: " + std::string(ex.what()));
    }
}

void
WorldEventHandler::handleGetMobDataEvent(const Event &event)
{
    // Here we will get the mob data from the database and send it to the client
    // Retrieve the data from the event
    const auto &data = event.getData();
    int clientID = event.getClientID();

    // get socket from the event
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();

    try
    {
        // Try to extract the data
        if (std::holds_alternative<MobDataStruct>(data))
        {
            MobDataStruct passedMobData = std::get<MobDataStruct>(data);
            // Get the mob data from the database as map
            auto mobDataMap = gameServices_.getMobManager().getMobs();

            // Check if the mob data exists in the database
            if (mobDataMap.find(passedMobData.id) != mobDataMap.end())
            {
                // Get the mob data from the map
                MobDataStruct mobData = mobDataMap[passedMobData.id];

                nlohmann::json mobJson;
                mobJson["id"] = mobData.id;
                mobJson["UID"] = mobData.uid;
                mobJson["zoneId"] = mobData.zoneId;
                mobJson["name"] = mobData.name;
                mobJson["slug"] = mobData.slug;
                mobJson["race"] = mobData.raceName;
                mobJson["level"] = mobData.level;
                mobJson["baseExperience"] = mobData.baseExperience;
                mobJson["radius"] = mobData.radius;
                mobJson["currentHealth"] = mobData.currentHealth;
                mobJson["currentMana"] = mobData.currentMana;
                mobJson["maxHealth"] = mobData.maxHealth;
                mobJson["maxMana"] = mobData.maxMana;
                mobJson["isAggressive"] = mobData.isAggressive;
                mobJson["isDead"] = mobData.isDead;
                mobJson["posX"] = mobData.position.positionX;
                mobJson["posY"] = mobData.position.positionY;
                mobJson["posZ"] = mobData.position.positionZ;
                mobJson["rotZ"] = mobData.position.rotationZ;

                // Per-mob AI config (migration 011)
                mobJson["aggroRange"] = mobData.aggroRange;
                mobJson["attackRange"] = mobData.attackRange;
                mobJson["attackCooldown"] = mobData.attackCooldown;
                mobJson["chaseMultiplier"] = mobData.chaseMultiplier;
                mobJson["patrolSpeed"] = mobData.patrolSpeed;
                mobJson["patrolRadius"] = mobData.patrolRadius;

                // Social behaviour (migration 012)
                mobJson["isSocial"] = mobData.isSocial;
                mobJson["chaseDuration"] = mobData.chaseDuration;

                // Survival / Rare mob groundwork (Stage 3, migration 038)
                mobJson["canEvolve"] = mobData.canEvolve;
                mobJson["isRare"] = mobData.isRare;
                mobJson["rareSpawnChance"] = mobData.rareSpawnChance;
                mobJson["rareSpawnCondition"] = mobData.rareSpawnCondition;

                // Prepare the response message
                nlohmann::json response;
                ResponseBuilder builder;

                // Add the message to the response
                response = builder
                               .setHeader("message", "Get mob data success!")
                               .setHeader("hash", "")
                               .setHeader("clientId", clientID)
                               .setHeader("eventType", "getMobData")
                               .setBody("mobData", mobJson)
                               .build();
                // Prepare a response message
                std::string responseData = networkManager_.generateResponseMessage("success", response);

                // Send the response to the client
                networkManager_.sendResponse(clientSocket, responseData);
            }
            else
            {
                // Prepare the response message
                nlohmann::json response;
                ResponseBuilder builder;

                // Add response data
                response = builder
                               .setHeader("message", "Mob data not found!")
                               .setHeader("hash", "")
                               .setHeader("clientId", clientID)
                               .setHeader("eventType", "getMobData")
                               .setBody("", "")
                               .build();
                // Prepare a response message
                std::string responseData = networkManager_.generateResponseMessage("error", response);

                // Send the response to the client
                networkManager_.sendResponse(clientSocket, responseData);
            }
        }
        else
        {
            log_->info("Error with extracting data!");
            // Prepare the response message
            nlohmann::json response;
            ResponseBuilder builder;
            // Add response data
            response = builder
                           .setHeader("message", "Error with extracting data!")
                           .setHeader("hash", "")
                           .setHeader("clientId", clientID)
                           .setHeader("eventType", "getMobData")
                           .setBody("", "")
                           .build();
            // Prepare a response message
            std::string responseData = networkManager_.generateResponseMessage("error", response);
            // Send the response to the client
            networkManager_.sendResponse(clientSocket, responseData);
        }
    }
    catch (const std::bad_variant_access &ex)
    {
        gameServices_.getLogger().log("Error here:" + std::string(ex.what()));
    }
}

void
WorldEventHandler::handleGetSpawnZonesEvent(const Event &event)
{
    // get the data from the event
    const auto &data = event.getData();
    int clientID = event.getClientID();

    // get socket from the event
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();

    // Get the spawn zones from the database
    std::map<int, SpawnZoneStruct> spawnZones = gameServices_.getSpawnZoneManager().getMobSpawnZones();

    // Group spawn zone entries by zoneId so multiple mob types in the same zone
    // are sent as one JSON object with a mobEntries array (multi-mob zone support).
    // Map: zoneId → zone JSON (geometry set on first entry; mobEntries appended on each).
    nlohmann::json spawnZonesJson;
    std::map<int, nlohmann::json> zoneMap;

    auto shapeToStr = [](ZoneShape s) -> std::string
    {
        switch (s)
        {
        case ZoneShape::CIRCLE:
            return "CIRCLE";
        case ZoneShape::ANNULUS:
            return "ANNULUS";
        default:
            return "RECT";
        }
    };

    for (const auto &spawnZone : spawnZones)
    {
        int zid = spawnZone.second.zoneId;
        if (zoneMap.find(zid) == zoneMap.end())
        {
            nlohmann::json zj;
            zj["id"] = zid;
            zj["name"] = spawnZone.second.zoneName;
            zj["gameZoneId"] = spawnZone.second.gameZoneId;
            zj["shape"] = shapeToStr(spawnZone.second.shape);
            zj["minX"] = spawnZone.second.minX;
            zj["maxX"] = spawnZone.second.maxX;
            zj["minY"] = spawnZone.second.minY;
            zj["maxY"] = spawnZone.second.maxY;
            zj["minZ"] = spawnZone.second.minZ;
            zj["maxZ"] = spawnZone.second.maxZ;
            zj["centerX"] = spawnZone.second.centerX;
            zj["centerY"] = spawnZone.second.centerY;
            zj["innerRadius"] = spawnZone.second.innerRadius;
            zj["outerRadius"] = spawnZone.second.outerRadius;
            zj["exclusionGameZoneId"] = spawnZone.second.exclusionGameZoneId;
            zj["mobEntries"] = nlohmann::json::array();
            zoneMap[zid] = std::move(zj);
        }

        nlohmann::json mobEntry;
        mobEntry["szmId"] = spawnZone.second.id;
        mobEntry["mobId"] = spawnZone.second.spawnMobId;
        mobEntry["maxCount"] = spawnZone.second.spawnCount;
        mobEntry["respawnTimeSec"] = spawnZone.second.respawnTime.count();
        zoneMap[zid]["mobEntries"].push_back(std::move(mobEntry));
    }

    for (const auto &[zid, zj] : zoneMap)
        spawnZonesJson.push_back(zj);

    // Prepare the response message
    try
    {
        nlohmann::json response;
        ResponseBuilder builder;

        // Check if the authentication is not successful
        if (clientID == 0)
        {
            // Add response data
            response = builder
                           .setHeader("message", "Getting spawn zones failed!")
                           .setHeader("hash", "")
                           .setHeader("clientId", clientID)
                           .setHeader("eventType", "setSpawnZonesList")
                           .setBody("", "")
                           .build();
            // Prepare a response message
            std::string responseData = networkManager_.generateResponseMessage("error", response);
            // Send the response to the client
            networkManager_.sendResponse(clientSocket, responseData);
            return;
        }

        // Add the message to the response
        response = builder
                       .setHeader("message", "Getting spawn zones success!")
                       .setHeader("hash", "")
                       .setHeader("clientId", clientID)
                       .setHeader("eventType", "setSpawnZonesList")
                       .setBody("spawnZonesData", spawnZonesJson)
                       .build();
        // Prepare a response message
        std::string responseData = networkManager_.generateResponseMessage("success", response);

        // Send the response to the client
        networkManager_.sendResponse(clientSocket, responseData);
    }
    catch (const std::bad_variant_access &ex)
    {
        gameServices_.getLogger().log("Error here: " + std::string(ex.what()));
    }
}

void
WorldEventHandler::handleGetItemsListEvent(const Event &event)
{
    // Retrieve the data from the event
    const auto &data = event.getData();
    int clientID = event.getClientID();
    // get socket from the event
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();

    try
    {
        // Get the items list from the database as map
        auto itemsList = gameServices_.getItemManager().getItems();

        nlohmann::json itemsListJson = nlohmann::json::array();

        for (const auto &itemItem : itemsList)
        {
            const ItemDataStruct &itemData = itemItem.second;

            nlohmann::json itemJson;
            itemJson["id"] = itemData.id;
            itemJson["slug"] = itemData.slug;
            itemJson["isQuestItem"] = itemData.isQuestItem;
            itemJson["itemType"] = itemData.itemType;
            itemJson["itemTypeSlug"] = itemData.itemTypeSlug;
            itemJson["isContainer"] = itemData.isContainer;
            itemJson["isDurable"] = itemData.isDurable;
            itemJson["isTradable"] = itemData.isTradable;
            itemJson["isEquippable"] = itemData.isEquippable;
            itemJson["isHarvest"] = itemData.isHarvest;
            itemJson["isUsable"] = itemData.isUsable;
            itemJson["weight"] = itemData.weight;
            itemJson["rarityId"] = itemData.rarityId;
            itemJson["raritySlug"] = itemData.raritySlug;
            itemJson["stackMax"] = itemData.stackMax;
            itemJson["durabilityMax"] = itemData.durabilityMax;
            itemJson["vendorPriceBuy"] = itemData.vendorPriceBuy;
            itemJson["vendorPriceSell"] = itemData.vendorPriceSell;
            itemJson["equipSlot"] = itemData.equipSlot;
            itemJson["equipSlotSlug"] = itemData.equipSlotSlug;
            itemJson["levelRequirement"] = itemData.levelRequirement;
            itemJson["isTwoHanded"] = itemData.isTwoHanded;

            nlohmann::json allowedClassIdsArray = nlohmann::json::array();
            for (int classId : itemData.allowedClassIds)
                allowedClassIdsArray.push_back(classId);
            itemJson["allowedClassIds"] = allowedClassIdsArray;

            itemJson["setId"] = itemData.setId;
            itemJson["setSlug"] = itemData.setSlug;

            // Add attributes (slug+value only: names resolve client-side via slug)
            nlohmann::json attributesArray = nlohmann::json::array();
            for (const auto &attribute : itemData.attributes)
            {
                nlohmann::json attributeJson;
                attributeJson["slug"] = attribute.slug;
                attributeJson["value"] = attribute.value;
                attributesArray.push_back(attributeJson);
            }
            itemJson["attributes"] = attributesArray;

            // Add use effects
            nlohmann::json useEffectsArray = nlohmann::json::array();
            for (const auto &ue : itemData.useEffects)
            {
                nlohmann::json ueJson;
                ueJson["effectSlug"] = ue.effectSlug;
                ueJson["attributeSlug"] = ue.attributeSlug;
                ueJson["value"] = ue.value;
                ueJson["isInstant"] = ue.isInstant;
                ueJson["durationSeconds"] = ue.durationSeconds;
                ueJson["tickMs"] = ue.tickMs;
                ueJson["cooldownSeconds"] = ue.cooldownSeconds;
                useEffectsArray.push_back(ueJson);
            }
            itemJson["useEffects"] = useEffectsArray;

            // Social systems (Stage 4, migration 039)
            itemJson["masterySlug"] = itemData.masterySlug;

            // Add the current item to the items list json
            itemsListJson.push_back(itemJson);
        }

        // Build response
        nlohmann::json response = ResponseBuilder()
                                      .setHeader("message", "Items list success!")
                                      .setHeader("hash", "")
                                      .setHeader("clientId", clientID)
                                      .setHeader("eventType", "getItemsList")
                                      .setBody("itemsList", itemsListJson)
                                      .build();

        std::string responseData = networkManager_.generateResponseMessage("success", response);
        networkManager_.sendResponse(clientSocket, responseData);
    }
    catch (const DatabaseError &)
    {
        // DB transport failure: no partial/empty success here.
        // JOIN entries are counted by dispatchCatalog; runtime
        // reads fall through to the error response below.
        throw;
    }
    catch (const std::exception &e)
    {
        gameServices_.getLogger().logError("Error getting items list: " + std::string(e.what()));
    }
}

void
WorldEventHandler::handleGetMobLootInfoEvent(const Event &event)
{
    // Retrieve the data from the event
    const auto &data = event.getData();
    int clientID = event.getClientID();
    // get socket from the event
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();

    try
    {
        // Get the mob loot info from the database
        auto mobLootInfo = gameServices_.getItemManager().getMobLootInfo();

        nlohmann::json mobLootListJson = nlohmann::json::array();

        for (const auto &mobLootEntry : mobLootInfo)
        {
            for (const auto &lootInfo : mobLootEntry.second)
            {
                nlohmann::json lootJson;
                lootJson["id"] = lootInfo.id;
                lootJson["mobId"] = lootInfo.mobId;
                lootJson["itemId"] = lootInfo.itemId;
                lootJson["dropChance"] = lootInfo.dropChance;
                lootJson["isHarvestOnly"] = lootInfo.isHarvestOnly;
                lootJson["minQuantity"] = lootInfo.minQuantity;
                lootJson["maxQuantity"] = lootInfo.maxQuantity;
                lootJson["lootTier"] = lootInfo.lootTier;

                mobLootListJson.push_back(lootJson);
            }
        }

        // Build response
        nlohmann::json response = ResponseBuilder()
                                      .setHeader("message", "Mob loot info success!")
                                      .setHeader("hash", "")
                                      .setHeader("clientId", clientID)
                                      .setHeader("eventType", "getMobLootInfo")
                                      .setBody("mobLootInfo", mobLootListJson)
                                      .build();

        std::string responseData = networkManager_.generateResponseMessage("success", response);
        networkManager_.sendResponse(clientSocket, responseData);
    }
    catch (const DatabaseError &)
    {
        // DB transport failure: no partial/empty success here.
        // JOIN entries are counted by dispatchCatalog; runtime
        // reads fall through to the error response below.
        throw;
    }
    catch (const std::exception &e)
    {
        gameServices_.getLogger().logError("Error getting mob loot info: " + std::string(e.what()));
    }
}

void
WorldEventHandler::handleGetNPCsListEvent(const Event &event)
{
    // Retrieve the data from the event
    const auto &data = event.getData();
    int clientID = event.getClientID();
    // get socket from the event
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();

    try
    {
        // load the NPCs list from the database
        gameServices_.getNPCManager().loadNPCs(gameServices_.getDatabase());

        // Get the NPCs list from the database as map
        auto npcsListMap = gameServices_.getNPCManager().getNPCs();

        nlohmann::json npcsListJson;
        for (const auto &npcItem : npcsListMap)
        {
            const NPCDataStruct &npcData = npcItem.second;

            // Create a JSON object for the current NPC
            nlohmann::json npcJson;
            npcJson["id"] = npcData.id;
            npcJson["name"] = npcData.name;
            npcJson["slug"] = npcData.slug;
            npcJson["race"] = npcData.raceName;
            npcJson["level"] = npcData.level;
            npcJson["currentHealth"] = npcData.currentHealth;
            npcJson["currentMana"] = npcData.currentMana;
            npcJson["maxMana"] = npcData.maxMana;
            npcJson["maxHealth"] = npcData.maxHealth;
            npcJson["npcType"] = npcData.npcType;
            npcJson["isInteractable"] = npcData.isInteractable;
            npcJson["dialogueId"] = npcData.dialogueId;
            nlohmann::json questSlugsJson = nlohmann::json::array();
            for (const auto &slug : npcData.questSlugs)
                questSlugsJson.push_back(slug);
            npcJson["questSlugs"] = questSlugsJson;
            npcJson["factionSlug"] = npcData.factionSlug;
            npcJson["posX"] = npcData.position.positionX;
            npcJson["posY"] = npcData.position.positionY;
            npcJson["posZ"] = npcData.position.positionZ;
            npcJson["rotZ"] = npcData.position.rotationZ;

            // Add the current item to the NPCs list json
            npcsListJson.push_back(npcJson);
        }

        // If the NPCs list is empty, log a message
        if (npcsListJson.empty())
        {
            log_->info("NPCs list is empty!");
        }

        // Prepare the response message
        nlohmann::json response;
        ResponseBuilder builder;

        // Add the message to the response
        response = builder
                       .setHeader("message", "Getting NPCs list success!")
                       .setHeader("hash", "")
                       .setHeader("clientId", clientID)
                       .setHeader("eventType", "setNPCsList")
                       .setBody("npcsList", npcsListJson)
                       .build();
        // Prepare a response message
        std::string responseData = networkManager_.generateResponseMessage("success", response);

        // Send the response to the client
        networkManager_.sendResponse(clientSocket, responseData);

        // After sending NPCs list, send NPCs skills
        nlohmann::json npcsSkillsJson;
        for (const auto &npcItem : npcsListMap)
        {
            const NPCDataStruct &npcData = npcItem.second;

            for (const auto &skill : npcData.skills)
            {
                nlohmann::json skillJson;
                skillJson["npc_id"] = npcData.id;
                skillJson["skillName"] = skill.skillName;
                skillJson["skillSlug"] = skill.skillSlug;
                skillJson["scaleStat"] = skill.scaleStat;
                skillJson["school"] = skill.school;
                skillJson["skillEffectType"] = skill.skillEffectType;
                skillJson["skillLevel"] = skill.skillLevel;
                skillJson["coeff"] = skill.coeff;
                skillJson["flatAdd"] = skill.flatAdd;
                skillJson["cooldownMs"] = skill.cooldownMs;
                skillJson["gcdMs"] = skill.gcdMs;
                skillJson["castMs"] = skill.castMs;
                skillJson["costMp"] = skill.costMp;
                skillJson["maxRange"] = skill.maxRange;
                skillJson["areaRadius"] = skill.areaRadius;
                skillJson["swingMs"] = skill.swingMs;
                skillJson["animationName"] = skill.animationName;

                npcsSkillsJson.push_back(skillJson);
            }
        }

        // If the NPCs skills list is empty, log a message
        if (npcsSkillsJson.empty())
        {
            log_->info("NPCs skills list is empty!");
        }

        // Add the message to the response
        response = builder
                       .setHeader("message", "Getting NPCs skills list success!")
                       .setHeader("hash", "")
                       .setHeader("clientId", clientID)
                       .setHeader("eventType", "setNPCsSkills")
                       .setBody("npcsSkillsList", npcsSkillsJson)
                       .build();
        responseData = networkManager_.generateResponseMessage("success", response);

        // Send the response to the client
        networkManager_.sendResponse(clientSocket, responseData);
    }
    catch (const std::bad_variant_access &ex)
    {
        gameServices_.getLogger().log("Error here: " + std::string(ex.what()));
    }
    catch (const DatabaseError &)
    {
        // DB transport failure: no partial/empty success here.
        // JOIN entries are counted by dispatchCatalog; runtime
        // reads fall through to the error response below.
        throw;
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("Error in handleGetNPCsListEvent: " + std::string(ex.what()));
    }
}

void
WorldEventHandler::handleGetNPCsAttributesEvent(const Event &event)
{
    // Retrieve the data from the event
    const auto &data = event.getData();
    int clientID = event.getClientID();
    // get socket from the event
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();

    try
    {
        // Get the NPCs list from the database as map
        auto npcsList = gameServices_.getNPCManager().getNPCs();

        nlohmann::json npcsAttributesListJson = nlohmann::json::array();

        for (const auto &npcItem : npcsList)
        {
            const NPCDataStruct &npcData = npcItem.second;

            for (const auto &attribute : npcData.attributes)
            {
                nlohmann::json attributeJson;
                attributeJson["id"] = attribute.id;
                attributeJson["npc_id"] = npcData.id;
                attributeJson["slug"] = attribute.slug;
                attributeJson["value"] = attribute.value;

                npcsAttributesListJson.push_back(attributeJson);
            }
        }

        // If the NPCs attributes list is empty, log a message
        if (npcsAttributesListJson.empty())
        {
            log_->info("NPCs attributes list is empty!");
        }

        // Prepare the response message
        nlohmann::json response;
        ResponseBuilder builder;

        // Add the message to the response
        response = builder
                       .setHeader("message", "Getting NPCs attributes list success!")
                       .setHeader("hash", "")
                       .setHeader("clientId", clientID)
                       .setHeader("eventType", "setNPCsAttributes")
                       .setBody("npcsAttributesList", npcsAttributesListJson)
                       .build();
        // Prepare a response message
        std::string responseData = networkManager_.generateResponseMessage("success", response);

        // Send the response to the client
        networkManager_.sendResponse(clientSocket, responseData);
    }
    catch (const std::bad_variant_access &ex)
    {
        gameServices_.getLogger().log("Error here: " + std::string(ex.what()));
    }
    catch (const DatabaseError &)
    {
        // DB transport failure: no partial/empty success here.
        // JOIN entries are counted by dispatchCatalog; runtime
        // reads fall through to the error response below.
        throw;
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("Error in handleGetNPCsAttributesEvent: " + std::string(ex.what()));
    }
}
