#include "events/EventHandler.hpp"
#include "data/DataStructs.hpp"
#include "utils/TerminalColors.hpp"
#include "utils/TimestampUtils.hpp"

#include "events/Event.hpp"
#include <spdlog/logger.h>

EventHandler::EventHandler(
    NetworkManager &networkManager,
    GameServices &gameServices)
    : networkManager_(networkManager),
      gameServices_(gameServices)
{
    log_ = gameServices_.getLogger().getSystem("events");
}

void
EventHandler::handleJoinPlayerClientEvent(const Event &event)
{
    // Retrieve the data from the event
    const auto &data = event.getData();
    int clientID = event.getClientID();

    // get socket from the event
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();

    try
    {
        // Try to extract the data
        if (std::holds_alternative<ClientDataStruct>(data))
        {
            ClientDataStruct passedClientData = std::get<ClientDataStruct>(data);
            // Save the clientData object with the new init data
            gameServices_.getClientManager().setClientData(passedClientData);

            // get chunk server connection data
            ChunkInfoStruct chunkServerData = gameServices_.getChunkManager().getChunkById(1);

            // Prepare a response message
            nlohmann::json response;
            ResponseBuilder builder;

            // Check if the authentication is not successful
            if (passedClientData.clientId == 0 || passedClientData.hash == "")
            {
                // Add response data
                response = builder
                               .setHeader("message", "Authentication failed for user!")
                               .setHeader("hash", passedClientData.hash)
                               .setHeader("clientId", passedClientData.clientId)
                               .setHeader("eventType", "joinGameClient")
                               .setBody("", "")
                               .build();
                // Prepare a response message
                std::string responseData = networkManager_.generateResponseMessage("error", response);

                // Send the response to the Client
                networkManager_.sendResponse(
                    passedClientData.socket,
                    responseData);
                return;
            }

            // generate chunk server connection data
            nlohmann::json chunkServerDataJson;
            chunkServerDataJson["chunkId"] = chunkServerData.id;
            chunkServerDataJson["chunkIp"] = chunkServerData.ip;
            chunkServerDataJson["chunkPort"] = chunkServerData.port;
            chunkServerDataJson["chunkPosX"] = chunkServerData.posX;
            chunkServerDataJson["chunkPosY"] = chunkServerData.posY;
            chunkServerDataJson["chunkPosZ"] = chunkServerData.posZ;
            chunkServerDataJson["chunkSizeX"] = chunkServerData.sizeX;
            chunkServerDataJson["chunkSizeY"] = chunkServerData.sizeY;
            chunkServerDataJson["chunkSizeZ"] = chunkServerData.sizeZ;

            // Add the message to the response
            response = builder
                           .setHeader("message", "Authentication success for user!")
                           .setHeader("hash", passedClientData.hash)
                           .setHeader("clientId", passedClientData.clientId)
                           .setHeader("eventType", "joinGameClient")
                           .setBody("chunkServerData", chunkServerDataJson)
                           .build();
            // Prepare a response message
            std::string responseData = networkManager_.generateResponseMessage("success", response);

            // Send the response to the Client
            networkManager_.sendResponse(
                passedClientData.socket,
                responseData);

            log_->info("Sending data to the Client: " + responseData);

            // Dispatch the event to get character data and send it to the chunk server
            Event getCharacterDataEvent(Event::GET_CHARACTER_DATA, clientID, passedClientData, chunkServerData.socket);
            dispatchEvent(getCharacterDataEvent);
        }
        else
        {
            log_->info("Error with extracting data!");
        }
    }
    catch (const std::bad_variant_access &ex)
    {
        gameServices_.getLogger().log("Error here: " + std::string(ex.what()));
    }
}

void
EventHandler::handleGetCharacterDataEvent(const Event &event)
{
    // Here we will init data from DB of the character when client joined and send it to the chunk server
    // Retrieve the data from the event
    const auto data = event.getData();
    int clientID = event.getClientID();

    // get socket from the event
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();

    // Extract init data
    try
    {
        // Try to extract the data
        if (std::holds_alternative<ClientDataStruct>(data))
        {
            ClientDataStruct passedClientData = std::get<ClientDataStruct>(data);
            // Save the clientData object with the new init data
            gameServices_.getClientManager().setClientData(passedClientData);

            // debug log
            gameServices_.getLogger().log("Passed - Client ID: " + std::to_string(passedClientData.clientId) +
                                          ", Character ID: " + std::to_string(passedClientData.characterId) +
                                          ", Hash: " + passedClientData.hash);

            // Get the character data from the database
            CharacterDataStruct characterData = gameServices_.getCharacterManager().loadCharacterFromDatabase(
                gameServices_.getDatabase(),
                passedClientData.clientId,
                passedClientData.characterId);

            // Get the clientData object with the new init data
            const ClientDataStruct currentClientData = gameServices_.getClientManager().getClientData(passedClientData.clientId);

            // Prepare the response message
            nlohmann::json response;
            ResponseBuilder builder;

            // Check if the authentication is not successful
            if (passedClientData.clientId == 0 || passedClientData.hash == "")
            {
                // Add response data
                response = builder
                               .setHeader("message", "Join Game Character failed for Client!")
                               .setHeader("hash", passedClientData.hash)
                               .setHeader("clientId", passedClientData.clientId)
                               .setHeader("eventType", "setCharacterData")
                               .setBody("", "")
                               .build();
                // Prepare a response message
                std::string responseData = networkManager_.generateResponseMessage("error", response);

                // Send the response to the chunk server
                networkManager_.sendResponse(
                    passedClientData.socket,
                    responseData);
                return;
            }

            // create attributes array
            nlohmann::json attributes = nlohmann::json::array();

            for (const auto &attribute : characterData.attributes)
            {
                nlohmann::json attributeData;
                attributeData["id"] = attribute.id;
                attributeData["slug"] = attribute.slug;
                attributeData["name"] = attribute.name;
                attributeData["value"] = attribute.value;

                // Set the character max health and mana
                if (attribute.slug == "max_health")
                {
                    characterData.characterMaxHealth = attribute.value;
                }
                if (attribute.slug == "max_mana")
                {
                    characterData.characterMaxMana = attribute.value;
                }

                attributes.push_back(attributeData);
            }

            // Cap current HP/MP to their respective max values.
            // Stale DB data (e.g. test value 999) can exceed the computed max,
            // which would produce nonsense like "999/197" on the client.
            if (characterData.characterMaxHealth > 0)
                characterData.characterCurrentHealth = std::min(characterData.characterCurrentHealth, characterData.characterMaxHealth);
            if (characterData.characterMaxMana > 0)
                characterData.characterCurrentMana = std::min(characterData.characterCurrentMana, characterData.characterMaxMana);

            // Create skills array
            nlohmann::json skills = nlohmann::json::array();

            for (const auto &skill : characterData.skills)
            {
                nlohmann::json skillData;
                skillData["skillName"] = skill.skillName;
                skillData["skillSlug"] = skill.skillSlug;
                skillData["scaleStat"] = skill.scaleStat;
                skillData["school"] = skill.school;
                skillData["skillEffectType"] = skill.skillEffectType;
                skillData["skillLevel"] = skill.skillLevel;
                skillData["coeff"] = skill.coeff;
                skillData["flatAdd"] = skill.flatAdd;
                skillData["cooldownMs"] = skill.cooldownMs;
                skillData["gcdMs"] = skill.gcdMs;
                skillData["castMs"] = skill.castMs;
                skillData["costMp"] = skill.costMp;
                skillData["maxRange"] = skill.maxRange;
                skillData["areaRadius"] = skill.areaRadius;
                skillData["swingMs"] = skill.swingMs;
                skillData["animationName"] = skill.animationName;
                skillData["isPassive"] = skill.isPassive;

                skills.push_back(skillData);
            }

            // Add the message to the response
            response = builder
                           .setHeader("message", "Join Game Character success for Client!")
                           .setHeader("hash", currentClientData.hash)
                           .setHeader("clientId", currentClientData.clientId)
                           .setHeader("eventType", "setCharacterData")
                           .setBody("id", currentClientData.characterId)
                           .setBody("class", characterData.characterClass)
                           .setBody("classId", characterData.classId)
                           .setBody("level", characterData.characterLevel)
                           .setBody("expForNextLevel", characterData.expForNextLevel)
                           .setBody("name", characterData.characterName)
                           .setBody("race", characterData.characterRace)
                           .setBody("currentExp", characterData.characterExperiencePoints)
                           .setBody("experienceDebt", characterData.experienceDebt)
                           .setBody("currentHealth", characterData.characterCurrentHealth)
                           .setBody("currentMana", characterData.characterCurrentMana)
                           .setBody("maxHealth", characterData.characterMaxHealth)
                           .setBody("maxMana", characterData.characterMaxMana)
                           .setBody("posX", characterData.characterPosition.positionX)
                           .setBody("posY", characterData.characterPosition.positionY)
                           .setBody("posZ", characterData.characterPosition.positionZ)
                           .setBody("rotZ", characterData.characterPosition.rotationZ)
                           .setBody("attributesData", attributes)
                           .setBody("skillsData", skills)
                           .build();
            // Prepare a response message
            std::string responseData = networkManager_.generateResponseMessage("success", response);

            // Send the response to the chunk server
            networkManager_.sendResponse(
                clientSocket,
                responseData);
        }
        else
        {
            log_->info("Error with extracting data!");
        }
    }
    catch (const std::bad_variant_access &ex)
    {
        gameServices_.getLogger().log("Error here: " + std::string(ex.what()));
    }
}

void
EventHandler::handleMoveCharacterChunkEvent(const Event &event)
{
    // Retrieve the data from the event
    const auto &data = event.getData();
    int clientID = event.getClientID();

    // get socket from the event
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();

    try
    {
        // Try to extract the data
        if (std::holds_alternative<CharacterDataStruct>(data))
        {
            CharacterDataStruct passedCharacterData = std::get<CharacterDataStruct>(data);

            // Prepare the response message
            nlohmann::json response;
            ResponseBuilder builder;

            // Check if the authentication is not successful
            if (clientID == 0)
            {
                // Add response data
                response = builder
                               .setHeader("message", "Movement update failed for the Character!")
                               .setHeader("clientId", clientID)
                               .setHeader("eventType", "updateCharacterMovement")
                               .setBody("", "")
                               .build();
                // Prepare a response message
                std::string responseData = networkManager_.generateResponseMessage("error", response);

                // Send the response to the chunk server
                networkManager_.sendResponse(
                    clientSocket,
                    responseData);
                return;
            }

            // Position is persisted to DB only on disconnect (see handleDisconnectChunkEvent).
            // Writing on every move packet would produce 20+ SQL UPDATE/s per player.
            // Update the in-memory position so data is fresh for other GS queries.
            gameServices_.getCharacterManager().updateCharacterPositionInMemory(
                passedCharacterData.clientId,
                passedCharacterData.characterId,
                passedCharacterData.characterPosition);

            // Add the message to the response
            response = builder
                           .setHeader("message", "Movement success updated for the Character!")
                           .setHeader("clientId", passedCharacterData.clientId)
                           .setHeader("eventType", "updateCharacterMovement")
                           .setBody("characterId", passedCharacterData.characterId)
                           .setBody("posX", passedCharacterData.characterPosition.positionX)
                           .setBody("posY", passedCharacterData.characterPosition.positionY)
                           .setBody("posZ", passedCharacterData.characterPosition.positionZ)
                           .setBody("rotZ", passedCharacterData.characterPosition.rotationZ)
                           .build();
            // Prepare a response message
            std::string responseData = networkManager_.generateResponseMessage("success", response);

            log_->info("Sending data to Chunk Server: " + responseData);

            // Send the response to the chunk server
            networkManager_.sendResponse(
                clientSocket,
                responseData);
        }
        else
        {
            log_->info("Error with extracting data!");
        }
    }
    catch (const std::bad_variant_access &ex)
    {
        gameServices_.getLogger().log("Error here: " + std::string(ex.what()));
    }
}

// disconnect the client
void
EventHandler::handleDisconnectChunkEvent(const Event &event)
{
    // Here we will disconnect the client
    const auto &data = event.getData();
    int clientID = event.getClientID();
    // get socket from the event
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();

    // Extract init data
    try
    {
        // Save last position to DB if we have a valid CharacterDataStruct
        if (std::holds_alternative<CharacterDataStruct>(data))
        {
            const CharacterDataStruct &charData = std::get<CharacterDataStruct>(data);
            if (charData.characterId > 0)
            {
                gameServices_.getCharacterManager().updateCharacterPosition(
                    gameServices_.getDatabase(),
                    clientID,
                    charData.characterId,
                    charData.characterPosition);
                log_->info(
                    "Saved position on disconnect for characterId: " + std::to_string(charData.characterId));
            }
        }

        if (clientID == 0)
        {
            log_->info("Client ID is 0, so we will just remove client from our list!");

            // Remove the client data by socket
            gameServices_.getClientManager().removeClientDataBySocket(clientSocket);

            return;
        }

        // Remove the client data
        gameServices_.getClientManager().removeClientData(clientID);

        // send the response to all clients
        nlohmann::json response;
        ResponseBuilder builder;
        response = builder
                       .setHeader("message", "Client disconnected!")
                       .setHeader("hash", "")
                       .setHeader("clientId", clientID)
                       .setHeader("eventType", "disconnectClient")
                       .setBody("", "")
                       .build();
        std::string responseData = networkManager_.generateResponseMessage("success", response);

        // Send the response to the client
        networkManager_.sendResponse(clientSocket, responseData);
    }
    catch (const std::bad_variant_access &ex)
    {
        gameServices_.getLogger().log("Error here:" + std::string(ex.what()));
    }
}

// join the chunk server
void
EventHandler::handleJoinChunkServerEvent(const Event &event)
{
    // get the data from the event
    const auto &data = event.getData();
    int clientID = event.getClientID();
    // get socket from the event
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();

    // prepare the chunk server data
    nlohmann::json chunkServerDataJson;

    try
    {
        // Prepare the response message
        nlohmann::json response;
        ResponseBuilder builder;

        // Check if the authentication is not successful
        if (clientID == 0)
        {
            // Add response data
            response = builder
                           .setHeader("message", "Joining chunk server failed!")
                           .setHeader("hash", "")
                           .setHeader("clientId", clientID)
                           .setHeader("eventType", "setChunkData")
                           .setBody("chunkServerData", chunkServerDataJson)
                           .build();
            // Prepare a response message
            std::string responseData = networkManager_.generateResponseMessage("error", response);
            // Send the response to the client
            networkManager_.sendResponse(clientSocket, responseData);
            return;
        }

        // Try to extract the data
        if (std::holds_alternative<ChunkInfoStruct>(data))
        {
            ChunkInfoStruct chunkData = std::get<ChunkInfoStruct>(data);

            chunkData.ip = "192.168.50.50";  // Set the chunk server IP address for testing purposes
            chunkData.socket = clientSocket; // Set the socket for the chunk server

            // Save the chunk data to memory
            gameServices_.getChunkManager().addChunkInfo(chunkData);

            chunkServerDataJson["id"] = chunkData.id;
            chunkServerDataJson["ip"] = chunkData.ip;
            chunkServerDataJson["port"] = chunkData.port;
            chunkServerDataJson["posX"] = chunkData.posX;
            chunkServerDataJson["posY"] = chunkData.posY;
            chunkServerDataJson["posZ"] = chunkData.posZ;
            chunkServerDataJson["sizeX"] = chunkData.sizeX;
            chunkServerDataJson["sizeY"] = chunkData.sizeY;
            chunkServerDataJson["sizeZ"] = chunkData.sizeZ;

            // load spawn zones
            Event spawnZonesEvent(Event::GET_SPAWN_ZONES, clientID, SpawnZoneStruct(), clientSocket);
            dispatchEvent(spawnZonesEvent);

            // load mobs
            Event mobDataEvent(Event::GET_MOBS_LIST, clientID, MobDataStruct(), clientSocket);
            dispatchEvent(mobDataEvent);

            // load mobs attributes
            Event mobAttributesEvent(Event::GET_MOBS_ATTRIBUTES, clientID, MobAttributeStruct(), clientSocket);
            dispatchEvent(mobAttributesEvent);

            // load NPCs
            Event npcDataEvent(Event::GET_NPCS_LIST, clientID, NPCDataStruct(), clientSocket);
            dispatchEvent(npcDataEvent);

            // load NPCs attributes
            Event npcAttributesEvent(Event::GET_NPCS_ATTRIBUTES, clientID, NPCAttributeStruct(), clientSocket);
            dispatchEvent(npcAttributesEvent);

            // load items
            Event itemsEvent(Event::GET_ITEMS_LIST, clientID, ItemDataStruct(), clientSocket);
            dispatchEvent(itemsEvent);

            // load mob loot info
            Event mobLootEvent(Event::GET_MOB_LOOT_INFO, clientID, MobLootInfoStruct(), clientSocket);
            dispatchEvent(mobLootEvent);

            // load experience level table
            Event expLevelTableEvent(Event::GET_EXP_LEVEL_TABLE, clientID, ClientDataStruct(), clientSocket);
            dispatchEvent(expLevelTableEvent);

            // load dialogues and NPC dialogue mappings
            Event dialoguesEvent(Event::GET_DIALOGUES, clientID, ClientDataStruct(), clientSocket);
            dispatchEvent(dialoguesEvent);

            // load quests
            Event questsEvent(Event::GET_QUESTS, clientID, ClientDataStruct(), clientSocket);
            dispatchEvent(questsEvent);

            // load game config
            gameServices_.getGameConfigService().loadConfig();
            Event gameConfigEvent(Event::GET_GAME_CONFIG, clientID, ClientDataStruct(), clientSocket);
            dispatchEvent(gameConfigEvent);

            // load vendor NPC inventory
            Event vendorDataEvent(Event::GET_VENDOR_DATA, clientID, ClientDataStruct(), clientSocket);
            dispatchEvent(vendorDataEvent);

            // load respawn zones
            Event respawnZonesEvent(Event::GET_RESPAWN_ZONES, clientID, ClientDataStruct(), clientSocket);
            dispatchEvent(respawnZonesEvent);

            // load game zones (AABB bounds + exploration XP)
            Event gameZonesEvent(Event::GET_GAME_ZONES, clientID, ClientDataStruct(), clientSocket);
            dispatchEvent(gameZonesEvent);

            // load status effect templates (data-driven buff/debuff config)
            Event statusEffectTemplatesEvent(Event::GET_STATUS_EFFECT_TEMPLATES, clientID, ClientDataStruct(), clientSocket);
            dispatchEvent(statusEffectTemplatesEvent);

            // load timed champion templates (Stage 3 — mob ecosystem)
            Event timedChampionTemplatesEvent(Event::GET_TIMED_CHAMPION_TEMPLATES, clientID, ClientDataStruct(), clientSocket);
            dispatchEvent(timedChampionTemplatesEvent);

            // load zone event templates (Stage 4 — world events)
            Event zoneEventTemplatesEvent(Event::GET_ZONE_EVENT_TEMPLATES, clientID, ClientDataStruct(), clientSocket);
            dispatchEvent(zoneEventTemplatesEvent);
        }

        // Add the message to the response
        response = builder
                       .setHeader("message", "Joining chunk server success!")
                       .setHeader("hash", "")
                       .setHeader("clientId", clientID)
                       .setHeader("eventType", "setChunkData")
                       .setBody("id", chunkServerDataJson["id"])
                       .setBody("ip", chunkServerDataJson["ip"])
                       .setBody("port", chunkServerDataJson["port"])
                       .setBody("posX", chunkServerDataJson["posX"])
                       .setBody("posY", chunkServerDataJson["posY"])
                       .setBody("posZ", chunkServerDataJson["posZ"])
                       .setBody("sizeX", chunkServerDataJson["sizeX"])
                       .setBody("sizeY", chunkServerDataJson["sizeY"])
                       .setBody("sizeZ", chunkServerDataJson["sizeZ"])
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
EventHandler::handleDisconnectChunkServerEvent(const Event &event)
{
    // Here we will disconnect the chunk server
    const auto data = event.getData();
    int clientID = event.getClientID();
    // get socket from the event
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();

    // Extract init data
    try
    {
        if (clientID == 0)
        {
            log_->info("Chunk Server ID is 0, so we will just remove chunk server from our list!");

            // Remove the chunk server data by socket
            gameServices_.getChunkManager().removeChunkServerDataBySocket(clientSocket);

            return;
        }

        // Remove the chunk server data
        gameServices_.getChunkManager().removeChunkServerDataById(clientID);

        // send the response to all clients
        nlohmann::json response;
        ResponseBuilder builder;
        response = builder
                       .setHeader("message", "Chunk Server disconnected!")
                       .setHeader("hash", "")
                       .setHeader("clientId", clientID)
                       .setHeader("eventType", "disconnectChunkServer")
                       .setBody("", "")
                       .build();
        std::string responseData = networkManager_.generateResponseMessage("success", response);

        // Send the response to the client
        networkManager_.sendResponse(clientSocket, responseData);
    }
    catch (const std::bad_variant_access &ex)
    {
        gameServices_.getLogger().log("Error here:" + std::string(ex.what()));
    }
}

// handle get mobs attributes event
void
EventHandler::handleGetMobsAttributesEvent(const Event &event)
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
                attributeJson["name"] = attribute.name;
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

// handle get mobs list event
void
EventHandler::handleGetMobsListEvent(const Event &event)
{
    // Retrieve the data from the event
    const auto &data = event.getData();
    int clientID = event.getClientID();
    // get socket from the event
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();

    try
    {
        // load the mobs list from the database
        gameServices_.getMobManager().loadMobs();

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

            auto weakRows = gameServices_.getDatabase().executeQueryWithTransaction(
                txnWR, "get_mob_weaknesses_all", {});
            auto resistRows = gameServices_.getDatabase().executeQueryWithTransaction(
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

// handle get mob data event
void
EventHandler::handleGetMobDataEvent(const Event &event)
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
                mobJson["maxMana"] = mobData.maxMana;
                mobJson["currentHealth"] = mobData.maxHealth;
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

// handle get spawn zones event
void
EventHandler::handleGetSpawnZonesEvent(const Event &event)
{
    // get the data from the event
    const auto &data = event.getData();
    int clientID = event.getClientID();

    // get socket from the event
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();

    // Get the spawn zones from the database
    std::map<int, SpawnZoneStruct> spawnZones = gameServices_.getSpawnZoneManager().getMobSpawnZones();

    // go thru spawnZones map and format the data to json
    nlohmann::json spawnZonesJson;
    for (const auto &spawnZone : spawnZones)
    {
        nlohmann::json spawnZoneJson;
        spawnZoneJson["id"] = spawnZone.second.zoneId;
        spawnZoneJson["name"] = spawnZone.second.zoneName;
        spawnZoneJson["posX"] = spawnZone.second.posX;
        spawnZoneJson["sizeX"] = spawnZone.second.sizeX;
        spawnZoneJson["posY"] = spawnZone.second.posY;
        spawnZoneJson["sizeY"] = spawnZone.second.sizeY;
        spawnZoneJson["posZ"] = spawnZone.second.posZ;
        spawnZoneJson["sizeZ"] = spawnZone.second.sizeZ;
        spawnZoneJson["spawnMobId"] = spawnZone.second.spawnMobId;
        spawnZoneJson["maxMobSpawnCount"] = spawnZone.second.spawnCount;
        spawnZoneJson["respawnTime"] = spawnZone.second.respawnTime.count();
        spawnZoneJson["spawnEnabled"] = true;

        spawnZonesJson.push_back(spawnZoneJson);
    }

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

// events dispatcher
void
EventHandler::dispatchEvent(const Event &event)
{
    switch (event.getType())
    {
    // character/client events
    case Event::PING_CLIENT:
        handlePingClientEvent(event);
        break;
    case Event::JOIN_PLAYER_CLIENT:
        handleJoinPlayerClientEvent(event);
        break;

    case Event::GET_CHARACTER_DATA:
        handleGetCharacterDataEvent(event);
        break;
    case Event::MOVE_CHARACTER:
        handleMoveCharacterChunkEvent(event);
        break;
    case Event::DISCONNECT_CLIENT:
        handleDisconnectChunkEvent(event);
        break;

    // spawn zones events
    case Event::GET_SPAWN_ZONES:
        handleGetSpawnZonesEvent(event);
        break;

    // mobs events
    case Event::GET_MOBS_LIST:
        handleGetMobsListEvent(event);
        break;
    case Event::GET_MOBS_ATTRIBUTES:
        handleGetMobsAttributesEvent(event);
        break;
    case Event::GET_MOB_DATA:
        handleGetMobDataEvent(event);
        break;
    case Event::GET_CHARACTER_EXP_FOR_LEVEL:
        handleGetCharacterExpForLevelEvent(event);
        break;
    case Event::GET_EXP_LEVEL_TABLE:
        handleGetExpLevelTableEvent(event);
        break;

    // items events
    case Event::GET_ITEMS_LIST:
        handleGetItemsListEvent(event);
        break;
    case Event::GET_MOB_LOOT_INFO:
        handleGetMobLootInfoEvent(event);
        break;

    // NPC events
    case Event::GET_NPCS_LIST:
        handleGetNPCsListEvent(event);
        break;
    case Event::GET_NPCS_ATTRIBUTES:
        handleGetNPCsAttributesEvent(event);
        break;

    case Event::SAVE_POSITIONS:
        handleSavePositionsEvent(event);
        break;

    case Event::SAVE_HP_MANA:
        handleSaveHpManaEvent(event);
        break;

    case Event::SAVE_CHARACTER_PROGRESS:
        handleSaveCharacterProgressEvent(event);
        break;

    case Event::SAVE_INVENTORY_CHANGE:
        handleSaveInventoryChangeEvent(event);
        break;

    case Event::GET_PLAYER_INVENTORY:
        handleGetPlayerInventoryEvent(event);
        break;

    // Dialogue & Quest events
    case Event::GET_DIALOGUES:
        handleGetDialoguesEvent(event);
        break;
    case Event::GET_QUESTS:
        handleGetQuestsEvent(event);
        break;
    case Event::GET_PLAYER_QUESTS:
        handleGetPlayerQuestsEvent(event);
        break;
    case Event::GET_PLAYER_FLAGS:
        handleGetPlayerFlagsEvent(event);
        break;
    case Event::GET_PLAYER_ACTIVE_EFFECTS:
        handleGetPlayerActiveEffectsEvent(event);
        break;
    case Event::GET_CHARACTER_ATTRIBUTES_REFRESH:
        handleGetCharacterAttributesRefreshEvent(event);
        break;
    case Event::UPDATE_PLAYER_QUEST_PROGRESS:
        handleUpdatePlayerQuestProgressEvent(event);
        break;
    case Event::UPDATE_PLAYER_FLAG:
        handleUpdatePlayerFlagEvent(event);
        break;
    case Event::GET_GAME_CONFIG:
        handleGetGameConfigEvent(event);
        break;
    case Event::GET_VENDOR_DATA:
        handleGetVendorDataEvent(event);
        break;
    case Event::SAVE_DURABILITY_CHANGE:
        handleSaveDurabilityChangeEvent(event);
        break;
    case Event::SAVE_CURRENCY_TRANSACTION:
        handleSaveCurrencyTransactionEvent(event);
        break;
    case Event::SAVE_EQUIPMENT_CHANGE:
        handleSaveEquipmentChangeEvent(event);
        break;
    case Event::GET_RESPAWN_ZONES:
        handleGetRespawnZonesEvent(event);
        break;
    case Event::GET_STATUS_EFFECT_TEMPLATES:
        handleGetStatusEffectTemplatesEvent(event);
        break;
    case Event::GET_GAME_ZONES:
        handleGetGameZonesEvent(event);
        break;
    case Event::SAVE_EXPERIENCE_DEBT:
        handleSaveExperienceDebtEvent(event);
        break;
    case Event::SAVE_ACTIVE_EFFECT:
        handleSaveActiveEffectEvent(event);
        break;
    case Event::SAVE_ITEM_KILL_COUNT:
        handleSaveItemKillCountEvent(event);
        break;
    case Event::TRANSFER_INVENTORY_ITEM:
        handleTransferInventoryItemEvent(event);
        break;
    case Event::NULLIFY_ITEM_OWNER:
        handleNullifyItemOwnerEvent(event);
        break;
    case Event::DELETE_INVENTORY_ITEM:
        handleDeleteInventoryItemEvent(event);
        break;
    case Event::GET_PLAYER_PITY:
        handleGetPlayerPityEvent(event);
        break;
    case Event::GET_PLAYER_BESTIARY:
        handleGetPlayerBestiaryEvent(event);
        break;
    case Event::SAVE_PITY_COUNTER:
        handleSavePityCounterEvent(event);
        break;
    case Event::SAVE_BESTIARY_KILL:
        handleSaveBestiaryKillEvent(event);
        break;
    case Event::GET_TIMED_CHAMPION_TEMPLATES:
        handleGetTimedChampionTemplatesEvent(event);
        break;
    case Event::TIMED_CHAMPION_KILLED:
        handleTimedChampionKilledEvent(event);
        break;

    // Stage 4: Reputation & Mastery
    case Event::GET_PLAYER_REPUTATIONS:
        handleGetPlayerReputationsEvent(event);
        break;
    case Event::SAVE_REPUTATION:
        handleSaveReputationEvent(event);
        break;
    case Event::GET_PLAYER_MASTERIES:
        handleGetPlayerMasteriesEvent(event);
        break;
    case Event::SAVE_MASTERY:
        handleSaveMasteryEvent(event);
        break;
    case Event::GET_ZONE_EVENT_TEMPLATES:
        handleGetZoneEventTemplatesEvent(event);
        break;

    // chunk server events
    case Event::JOIN_CHUNK_SERVER:
        handleJoinChunkServerEvent(event);
        break;
    case Event::DISCONNECT_CHUNK_SERVER:
        handleDisconnectChunkServerEvent(event);
        break;
    }
}

// handle get items list event
void
EventHandler::handleGetItemsListEvent(const Event &event)
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
            itemJson["itemTypeName"] = itemData.itemTypeName;
            itemJson["itemTypeSlug"] = itemData.itemTypeSlug;
            itemJson["isContainer"] = itemData.isContainer;
            itemJson["isDurable"] = itemData.isDurable;
            itemJson["isTradable"] = itemData.isTradable;
            itemJson["isEquippable"] = itemData.isEquippable;
            itemJson["isHarvest"] = itemData.isHarvest;
            itemJson["isUsable"] = itemData.isUsable;
            itemJson["weight"] = itemData.weight;
            itemJson["rarityId"] = itemData.rarityId;
            itemJson["rarityName"] = itemData.rarityName;
            itemJson["raritySlug"] = itemData.raritySlug;
            itemJson["stackMax"] = itemData.stackMax;
            itemJson["durabilityMax"] = itemData.durabilityMax;
            itemJson["vendorPriceBuy"] = itemData.vendorPriceBuy;
            itemJson["vendorPriceSell"] = itemData.vendorPriceSell;
            itemJson["equipSlot"] = itemData.equipSlot;
            itemJson["equipSlotName"] = itemData.equipSlotName;
            itemJson["equipSlotSlug"] = itemData.equipSlotSlug;
            itemJson["levelRequirement"] = itemData.levelRequirement;
            itemJson["isTwoHanded"] = itemData.isTwoHanded;

            nlohmann::json allowedClassIdsArray = nlohmann::json::array();
            for (int classId : itemData.allowedClassIds)
                allowedClassIdsArray.push_back(classId);
            itemJson["allowedClassIds"] = allowedClassIdsArray;

            itemJson["setId"] = itemData.setId;
            itemJson["setSlug"] = itemData.setSlug;

            // Add attributes
            nlohmann::json attributesArray = nlohmann::json::array();
            for (const auto &attribute : itemData.attributes)
            {
                nlohmann::json attributeJson;
                attributeJson["id"] = attribute.id;
                attributeJson["item_id"] = attribute.item_id;
                attributeJson["name"] = attribute.name;
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
    catch (const std::exception &e)
    {
        gameServices_.getLogger().logError("Error getting items list: " + std::string(e.what()));
    }
}

// handle get mob loot info event
void
EventHandler::handleGetMobLootInfoEvent(const Event &event)
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
    catch (const std::exception &e)
    {
        gameServices_.getLogger().logError("Error getting mob loot info: " + std::string(e.what()));
    }
}

// handle ping client event
void
EventHandler::handlePingClientEvent(const Event &event)
{
    // Retrieve the data from the event
    const auto &data = event.getData();
    int clientID = event.getClientID();

    // get socket from the event
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();

    log_->info("Handling PING event for client ID: " + std::to_string(clientID));

    if (!clientSocket || !clientSocket->is_open())
    {
        log_->info("Skipping ping - socket is closed for client ID: " + std::to_string(clientID));
        return;
    }

    try
    {
        // Try to extract the data
        if (std::holds_alternative<ClientDataStruct>(data))
        {
            ClientDataStruct passedClientData = std::get<ClientDataStruct>(data);

            // Check if we have timestamps (for request-response lag compensation)
            TimestampStruct timestamps;
            bool hasTimestamps = false;

            try
            {
                timestamps = event.getTimestamps();
                hasTimestamps = true;
            }
            catch (const std::exception &)
            {
                // Event doesn't have timestamps, create new ones
                timestamps = TimestampUtils::createTimestamp();
                hasTimestamps = true;
            }

            // Prepare a response message
            nlohmann::json response;
            ResponseBuilder builder;

            response = builder
                           .setHeader("message", "Pong!")
                           .setHeader("hash", passedClientData.hash)
                           .setHeader("clientId", passedClientData.clientId)
                           .setHeader("eventType", "pingClient")
                           .setTimestamps(timestamps)
                           .setBody("", "")
                           .build();

            // Prepare a response message with timestamps
            std::string responseData = networkManager_.generateResponseMessage("success", response, timestamps);

            // Send the response to the client
            networkManager_.sendResponse(clientSocket, responseData);

            log_->info("Sending PING response with timestamps to Client ID: " + std::to_string(clientID));
        }
        else
        {
            log_->error("Error extracting data from ping event for client ID: " + std::to_string(clientID));
        }
    }
    catch (const std::bad_variant_access &ex)
    {
        gameServices_.getLogger().logError("Variant access error in ping event for client ID " + std::to_string(clientID) + ": " + std::string(ex.what()));
    }
}

void
EventHandler::handleGetCharacterExpForLevelEvent(const Event &event)
{
    const auto &data = event.getData();
    int clientID = event.getClientID();
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();

    try
    {
        log_->debug("Processing getCharacterExpForLevel request from chunk server");

        // Извлекаем данные клиента из события
        if (std::holds_alternative<ClientDataStruct>(data))
        {
            ClientDataStruct clientData = std::get<ClientDataStruct>(data);

            // Парсим JSON данные из запроса, чтобы получить уровень
            std::string requestData = clientData.hash; // Используем hash поле для передачи JSON данных
            nlohmann::json requestJson = nlohmann::json::parse(requestData);

            int level = requestJson["body"]["level"].get<int>();

            log_->debug("Requesting experience points for level: " + std::to_string(level));

            // Получаем опыт для указанного уровня из базы данных
            int experiencePoints = 0;
            try
            {
                auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
                pqxx::work txn(_dbConn.get());
                auto result = gameServices_.getDatabase().executeQueryWithTransaction(txn, "get_character_exp_for_next_level", {level});

                if (!result.empty())
                {
                    experiencePoints = result[0][0].as<int>();
                    gameServices_.getLogger().log("Found experience points for level " + std::to_string(level) + ": " +
                                                      std::to_string(experiencePoints),
                        GREEN);
                }
                else
                {
                    log_->error("No experience data found for level " + std::to_string(level));
                    experiencePoints = 0;
                }

                txn.commit();
            }
            catch (const std::exception &e)
            {
                gameServices_.getLogger().logError("Database error getting experience for level " +
                                                   std::to_string(level) + ": " + std::string(e.what()));
                experiencePoints = 0;
            }

            // Подготавливаем ответ
            ResponseBuilder builder;
            nlohmann::json response = builder
                                          .setHeader("message", "Experience for level retrieved successfully!")
                                          .setHeader("hash", clientData.hash)
                                          .setHeader("clientId", clientData.clientId)
                                          .setHeader("eventType", "getCharacterExpForLevel")
                                          .setBody("level", level)
                                          .setBody("experiencePoints", experiencePoints)
                                          .build();

            // Отправляем ответ чанк-серверу
            std::string responseData = networkManager_.generateResponseMessage("success", response);
            networkManager_.sendResponse(clientSocket, responseData);

            log_->info("Sent experience data for level " + std::to_string(level) +
                       " to chunk server");
        }
        else
        {
            log_->error("Error extracting client data from getCharacterExpForLevel event");

            // Отправляем ошибку
            ResponseBuilder builder;
            nlohmann::json errorResponse = builder
                                               .setHeader("message", "Error processing getCharacterExpForLevel request!")
                                               .setHeader("eventType", "getCharacterExpForLevel")
                                               .build();

            std::string responseData = networkManager_.generateResponseMessage("error", errorResponse);
            networkManager_.sendResponse(clientSocket, responseData);
        }
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("Exception in handleGetCharacterExpForLevelEvent: " + std::string(ex.what()));

        // Отправляем ошибку
        ResponseBuilder builder;
        nlohmann::json errorResponse = builder
                                           .setHeader("message", "Server error processing getCharacterExpForLevel request!")
                                           .setHeader("eventType", "getCharacterExpForLevel")
                                           .build();

        std::string responseData = networkManager_.generateResponseMessage("error", errorResponse);
        networkManager_.sendResponse(clientSocket, responseData);
    }
}

void
EventHandler::handleGetExpLevelTableEvent(const Event &event)
{
    const auto &data = event.getData();
    int clientID = event.getClientID();
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();

    try
    {
        log_->debug("Processing getExpLevelTable request from chunk server");

        // Извлекаем данные клиента из события
        if (std::holds_alternative<ClientDataStruct>(data))
        {
            ClientDataStruct clientData = std::get<ClientDataStruct>(data);

            log_->debug("Requesting experience level table from database");

            // Получаем всю таблицу опыта из базы данных
            nlohmann::json expLevelTable = nlohmann::json::array();
            try
            {
                auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
                pqxx::work txn(_dbConn.get());
                auto result = gameServices_.getDatabase().executeQueryWithTransaction(txn, "get_exp_level_table", {});

                for (const auto &row : result)
                {
                    nlohmann::json levelEntry;
                    levelEntry["level"] = row["level"].as<int>();
                    levelEntry["experiencePoints"] = row["experience_points"].as<int>();
                    expLevelTable.push_back(levelEntry);
                }

                txn.commit();

                gameServices_.getLogger().log("Retrieved " + std::to_string(expLevelTable.size()) +
                                                  " experience level entries from database",
                    GREEN);
            }
            catch (const std::exception &e)
            {
                gameServices_.getLogger().logError("Database error getting experience level table: " + std::string(e.what()));
                expLevelTable = nlohmann::json::array(); // Пустой массив в случае ошибки
            }

            // Подготавливаем ответ
            ResponseBuilder builder;
            nlohmann::json response = builder
                                          .setHeader("message", "Experience level table retrieved successfully!")
                                          .setHeader("hash", clientData.hash)
                                          .setHeader("clientId", clientData.clientId)
                                          .setHeader("eventType", "getExpLevelTable")
                                          .setBody("expLevelTable", expLevelTable)
                                          .build();

            // Отправляем ответ чанк-серверу
            std::string responseData = networkManager_.generateResponseMessage("success", response);
            networkManager_.sendResponse(clientSocket, responseData);

            gameServices_.getLogger().log("Sent experience level table (" + std::to_string(expLevelTable.size()) +
                                              " entries) to chunk server",
                GREEN);
        }
        else
        {
            log_->error("Error extracting client data from getExpLevelTable event");

            // Отправляем ошибку
            ResponseBuilder builder;
            nlohmann::json errorResponse = builder
                                               .setHeader("message", "Error processing getExpLevelTable request!")
                                               .setHeader("eventType", "getExpLevelTable")
                                               .build();

            std::string responseData = networkManager_.generateResponseMessage("error", errorResponse);
            networkManager_.sendResponse(clientSocket, responseData);
        }
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("Exception in handleGetExpLevelTableEvent: " + std::string(ex.what()));

        // Отправляем ошибку
        ResponseBuilder builder;
        nlohmann::json errorResponse = builder
                                           .setHeader("message", "Server error processing getExpLevelTable request!")
                                           .setHeader("eventType", "getExpLevelTable")
                                           .build();

        std::string responseData = networkManager_.generateResponseMessage("error", errorResponse);
        networkManager_.sendResponse(clientSocket, responseData);
    }
}

// handle get NPCs list event
void
EventHandler::handleGetNPCsListEvent(const Event &event)
{
    // Retrieve the data from the event
    const auto &data = event.getData();
    int clientID = event.getClientID();
    // get socket from the event
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();

    try
    {
        // load the NPCs list from the database
        gameServices_.getNPCManager().loadNPCs();

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
            npcJson["questId"] = npcData.questId;
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
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("Error in handleGetNPCsListEvent: " + std::string(ex.what()));
    }
}

// handle get NPCs attributes event
void
EventHandler::handleGetNPCsAttributesEvent(const Event &event)
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
                attributeJson["name"] = attribute.name;
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
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("Error in handleGetNPCsAttributesEvent: " + std::string(ex.what()));
    }
}

void
EventHandler::handleSavePositionsEvent(const Event &event)
{
    const auto &data = event.getData();

    try
    {
        if (!std::holds_alternative<std::vector<CharacterDataStruct>>(data))
        {
            log_->error("handleSavePositionsEvent: unexpected data type");
            return;
        }

        const auto &charactersList = std::get<std::vector<CharacterDataStruct>>(data);

        if (charactersList.empty())
        {
            log_->info("handleSavePositionsEvent: empty positions list, skipping");
            return;
        }

        int savedCount = 0;
        for (const auto &charData : charactersList)
        {
            if (charData.characterId <= 0)
                continue;

            gameServices_.getCharacterManager().updateCharacterPosition(
                gameServices_.getDatabase(),
                0, // accountId not needed here
                charData.characterId,
                charData.characterPosition);

            ++savedCount;
        }

        gameServices_.getLogger().log(
            "Periodic position save: persisted " + std::to_string(savedCount) + " character(s) to DB", GREEN);
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("Error in handleSavePositionsEvent: " + std::string(ex.what()));
    }
}

// ARCH-4: Handles the periodic HP/Mana snapshot from chunk-server.
void
EventHandler::handleSaveHpManaEvent(const Event &event)
{
    const auto &data = event.getData();

    try
    {
        if (!std::holds_alternative<std::vector<CharacterDataStruct>>(data))
        {
            log_->error("handleSaveHpManaEvent: unexpected data type");
            return;
        }

        const auto &charactersList = std::get<std::vector<CharacterDataStruct>>(data);
        if (charactersList.empty())
            return;

        int savedCount = 0;
        for (const auto &charData : charactersList)
        {
            if (charData.characterId <= 0)
                continue;
            gameServices_.getCharacterManager().saveCharacterHpMana(
                gameServices_.getDatabase(),
                charData.characterId,
                charData.characterCurrentHealth,
                charData.characterCurrentMana);
            ++savedCount;
        }

        gameServices_.getLogger().log(
            "[SAVE_HP_MANA] Persisted HP/Mana for " + std::to_string(savedCount) + " character(s)", GREEN);
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("Error in handleSaveHpManaEvent: " + std::string(ex.what()));
    }
}

void
EventHandler::handleSaveInventoryChangeEvent(const Event &event)
{
    const auto &data = event.getData();

    try
    {
        if (!std::holds_alternative<nlohmann::json>(data))
        {
            log_->error("handleSaveInventoryChangeEvent: unexpected data type");
            return;
        }
        const auto &j = std::get<nlohmann::json>(data);
        int characterId = j.value("characterId", 0);
        int itemId = j.value("itemId", 0);
        int quantity = j.value("quantity", 0);

        if (characterId <= 0 || itemId <= 0)
            return;

        std::shared_ptr<boost::asio::ip::tcp::socket> chunkSocket = event.getClientSocket();

        auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
        pqxx::work txn(_dbConn.get());
        if (quantity > 0)
        {
            auto result = gameServices_.getDatabase().executeQueryWithTransaction(
                txn, "upsert_player_inventory_item", {characterId, itemId, quantity});
            txn.commit();

            // Send back the assigned player_inventory.id so chunk server can fix in-memory id=0
            if (!result.empty() && chunkSocket && chunkSocket->is_open())
            {
                int64_t assignedId = result[0]["id"].as<int64_t>();
                nlohmann::json syncPkt;
                syncPkt["header"]["eventType"] = "inventoryItemIdSync";
                syncPkt["header"]["clientId"] = 0;
                syncPkt["body"]["characterId"] = characterId;
                syncPkt["body"]["itemId"] = itemId;
                syncPkt["body"]["inventoryItemId"] = assignedId;
                networkManager_.sendResponse(chunkSocket,
                    networkManager_.generateResponseMessage("success", syncPkt));
            }
        }
        else
        {
            gameServices_.getDatabase().executeQueryWithTransaction(
                txn, "delete_player_inventory_item", {characterId, itemId});
            txn.commit();
        }
        log_->info("[SAVE_INVENTORY] character=" + std::to_string(characterId) +
                   " item=" + std::to_string(itemId) + " qty=" + std::to_string(quantity));
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("Error in handleSaveInventoryChangeEvent: " + std::string(ex.what()));
    }
}

void
EventHandler::handleGetPlayerInventoryEvent(const Event &event)
{
    const auto &data = event.getData();
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();

    try
    {
        if (!std::holds_alternative<int>(data))
        {
            log_->error("handleGetPlayerInventoryEvent: unexpected data type");
            return;
        }
        int characterId = std::get<int>(data);

        auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
        pqxx::work txn(_dbConn.get());
        auto result = gameServices_.getDatabase().executeQueryWithTransaction(
            txn, "get_player_inventory", {characterId});

        nlohmann::json itemsJson = nlohmann::json::array();
        for (const auto &row : result)
        {
            nlohmann::json item;
            item["id"] = row["id"].as<int64_t>();
            item["itemId"] = row["item_id"].as<int>();
            item["quantity"] = row["quantity"].as<int>();
            item["slotIndex"] = row["slot_index"].as<int>();
            item["durabilityCurrent"] = row["durability_current"].as<int>();
            item["isEquipped"] = row["is_equipped"].as<bool>(false);
            item["killCount"] = row["kill_count"].as<int>(0);
            itemsJson.push_back(std::move(item));
        }

        ResponseBuilder builder;
        nlohmann::json response = builder
                                      .setHeader("message", "Player inventory")
                                      .setHeader("hash", "")
                                      .setHeader("clientId", characterId)
                                      .setHeader("eventType", "setPlayerInventoryData")
                                      .setBody("characterId", characterId)
                                      .setBody("items", itemsJson)
                                      .build();
        networkManager_.sendResponse(clientSocket,
            networkManager_.generateResponseMessage("success", response));

        gameServices_.getLogger().log("[EH] Sent " + std::to_string(itemsJson.size()) +
                                          " inventory items for characterId=" + std::to_string(characterId),
            GREEN);
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("Error in handleGetPlayerInventoryEvent: " + std::string(ex.what()));
    }
}

void
EventHandler::handleSaveCharacterProgressEvent(const Event &event)
{
    const auto &data = event.getData();

    try
    {
        if (!std::holds_alternative<std::vector<CharacterDataStruct>>(data))
        {
            log_->error("handleSaveCharacterProgressEvent: unexpected data type");
            return;
        }

        const auto &charactersList = std::get<std::vector<CharacterDataStruct>>(data);

        if (charactersList.empty())
        {
            log_->info("handleSaveCharacterProgressEvent: empty list, skipping");
            return;
        }

        int savedCount = 0;
        for (const auto &charData : charactersList)
        {
            if (charData.characterId <= 0 || charData.characterLevel <= 0)
                continue;

            gameServices_.getCharacterManager().updateCharacterExperienceAndLevel(
                gameServices_.getDatabase(),
                charData.characterId,
                charData.characterExperiencePoints,
                charData.characterLevel);
            ++savedCount;
        }

        gameServices_.getLogger().log(
            "Instant character progress save: persisted " + std::to_string(savedCount) + " character(s) exp/level to DB",
            GREEN);
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("Error in handleSaveCharacterProgressEvent: " + std::string(ex.what()));
    }
}

// ---------------------------------------------------------------------------
// Dialogue & Quest handlers
// ---------------------------------------------------------------------------

void
EventHandler::handleGetDialoguesEvent(const Event &event)
{
    int clientID = event.getClientID();
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();

    try
    {
        auto dialoguesJson = gameServices_.getDialogueQuestManager().getAllDialoguesJson();
        auto mappingsJson = gameServices_.getDialogueQuestManager().getAllNPCDialogueMappingsJson();

        ResponseBuilder builder;
        nlohmann::json resp1 = builder
                                   .setHeader("message", "Dialogues data")
                                   .setHeader("hash", "")
                                   .setHeader("clientId", clientID)
                                   .setHeader("eventType", "setDialoguesData")
                                   .setBody("dialogues", dialoguesJson)
                                   .build();
        networkManager_.sendResponse(clientSocket, networkManager_.generateResponseMessage("success", resp1));

        nlohmann::json resp2 = builder
                                   .setHeader("message", "NPC dialogue mappings")
                                   .setHeader("hash", "")
                                   .setHeader("clientId", clientID)
                                   .setHeader("eventType", "setNPCDialogueMappings")
                                   .setBody("mappings", mappingsJson)
                                   .build();
        networkManager_.sendResponse(clientSocket, networkManager_.generateResponseMessage("success", resp2));

        gameServices_.getLogger().log("[EH] Sent " + std::to_string(dialoguesJson.size()) +
                                          " dialogues + " + std::to_string(mappingsJson.size()) + " mappings to chunk",
            GREEN);
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("Error in handleGetDialoguesEvent: " + std::string(ex.what()));
    }
}

void
EventHandler::handleGetQuestsEvent(const Event &event)
{
    int clientID = event.getClientID();
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();

    try
    {
        auto questsJson = gameServices_.getDialogueQuestManager().getAllQuestsJson();

        ResponseBuilder builder;
        nlohmann::json response = builder
                                      .setHeader("message", "Quests data")
                                      .setHeader("hash", "")
                                      .setHeader("clientId", clientID)
                                      .setHeader("eventType", "setQuestsData")
                                      .setBody("quests", questsJson)
                                      .build();
        networkManager_.sendResponse(clientSocket, networkManager_.generateResponseMessage("success", response));

        gameServices_.getLogger().log("[EH] Sent " + std::to_string(questsJson.size()) +
                                          " quests to chunk",
            GREEN);
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("Error in handleGetQuestsEvent: " + std::string(ex.what()));
    }
}

void
EventHandler::handleGetPlayerQuestsEvent(const Event &event)
{
    const auto &data = event.getData();
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();

    try
    {
        if (!std::holds_alternative<int>(data))
        {
            log_->error("handleGetPlayerQuestsEvent: unexpected data type");
            return;
        }
        int characterId = std::get<int>(data);

        auto questsJson = gameServices_.getDialogueQuestManager().getPlayerQuestsJson(characterId);

        ResponseBuilder builder;
        nlohmann::json response = builder
                                      .setHeader("message", "Player quests")
                                      .setHeader("hash", "")
                                      .setHeader("clientId", characterId)
                                      .setHeader("eventType", "setPlayerQuestsData")
                                      .setBody("characterId", characterId)
                                      .setBody("quests", questsJson)
                                      .build();
        networkManager_.sendResponse(clientSocket, networkManager_.generateResponseMessage("success", response));

        gameServices_.getLogger().log("[EH] Sent " + std::to_string(questsJson.size()) +
                                          " quests for characterId=" + std::to_string(characterId),
            GREEN);
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("Error in handleGetPlayerQuestsEvent: " + std::string(ex.what()));
    }
}

void
EventHandler::handleGetPlayerFlagsEvent(const Event &event)
{
    const auto &data = event.getData();
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();

    try
    {
        if (!std::holds_alternative<int>(data))
        {
            log_->error("handleGetPlayerFlagsEvent: unexpected data type");
            return;
        }
        int characterId = std::get<int>(data);

        auto flagsJson = gameServices_.getDialogueQuestManager().getPlayerFlagsJson(characterId);

        ResponseBuilder builder;
        nlohmann::json response = builder
                                      .setHeader("message", "Player flags")
                                      .setHeader("hash", "")
                                      .setHeader("clientId", characterId)
                                      .setHeader("eventType", "setPlayerFlagsData")
                                      .setBody("characterId", characterId)
                                      .setBody("flags", flagsJson)
                                      .build();
        networkManager_.sendResponse(clientSocket, networkManager_.generateResponseMessage("success", response));

        gameServices_.getLogger().log("[EH] Sent " + std::to_string(flagsJson.size()) +
                                          " flags for characterId=" + std::to_string(characterId),
            GREEN);
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("Error in handleGetPlayerFlagsEvent: " + std::string(ex.what()));
    }
}

void
EventHandler::handleUpdatePlayerQuestProgressEvent(const Event &event)
{
    const auto &data = event.getData();

    try
    {
        if (!std::holds_alternative<nlohmann::json>(data))
        {
            log_->error("handleUpdatePlayerQuestProgressEvent: unexpected data type");
            return;
        }
        const auto &j = std::get<nlohmann::json>(data);
        auto &body = j.at("body");

        int characterId = body.value("characterId", 0);
        int questId = body.value("questId", 0);
        std::string state = body.value("state", "active");
        int currentStep = body.value("currentStep", 0);
        // progress may be a JSON object or a pre-serialized string
        std::string progressJson = "{}";
        if (body.contains("progress"))
        {
            const auto &prog = body.at("progress");
            progressJson = prog.is_string() ? prog.get<std::string>() : prog.dump();
        }

        if (characterId <= 0 || questId <= 0)
        {
            log_->error("handleUpdatePlayerQuestProgressEvent: invalid characterId/questId");
            return;
        }

        gameServices_.getDialogueQuestManager().savePlayerQuestProgress(
            characterId, questId, state, currentStep, progressJson);
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("Error in handleUpdatePlayerQuestProgressEvent: " + std::string(ex.what()));
    }
}

void
EventHandler::handleUpdatePlayerFlagEvent(const Event &event)
{
    const auto &data = event.getData();

    try
    {
        if (!std::holds_alternative<nlohmann::json>(data))
        {
            log_->error("handleUpdatePlayerFlagEvent: unexpected data type");
            return;
        }
        const auto &j = std::get<nlohmann::json>(data);
        auto &body = j.at("body");

        int characterId = body.value("characterId", 0);
        std::string flagKey = body.value("flagKey", "");
        int intValue = body.value("intValue", 0);
        bool boolValue = body.value("boolValue", false);

        if (characterId <= 0 || flagKey.empty())
        {
            log_->error("handleUpdatePlayerFlagEvent: invalid characterId or flagKey");
            return;
        }

        gameServices_.getDialogueQuestManager().savePlayerFlag(characterId, flagKey, intValue, boolValue);
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("Error in handleUpdatePlayerFlagEvent: " + std::string(ex.what()));
    }
}

void
EventHandler::handleGetGameConfigEvent(const Event &event)
{
    int clientID = event.getClientID();
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();

    try
    {
        // Получаем snapshot конфига (уже загружен loadConfig() выше)
        auto configMap = gameServices_.getGameConfigService().getAll();

        nlohmann::json configListJson = nlohmann::json::array();
        for (const auto &[key, value] : configMap)
        {
            nlohmann::json entry;
            entry["key"] = key;
            entry["value"] = value;
            configListJson.push_back(entry);
        }

        nlohmann::json response = ResponseBuilder()
                                      .setHeader("message", "Game config loaded")
                                      .setHeader("hash", "")
                                      .setHeader("clientId", clientID)
                                      .setHeader("eventType", "setGameConfig")
                                      .setBody("configList", configListJson)
                                      .build();

        std::string responseData = networkManager_.generateResponseMessage("success", response);
        networkManager_.sendResponse(clientSocket, responseData);

        gameServices_.getLogger().log("Sent game config (" +
                                      std::to_string(configMap.size()) + " entries) to chunk-server.");
    }
    catch (const std::exception &e)
    {
        gameServices_.getLogger().logError("handleGetGameConfigEvent error: " + std::string(e.what()));
    }
}

void
EventHandler::handleGetPlayerActiveEffectsEvent(const Event &event)
{
    const auto &data = event.getData();
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();

    try
    {
        if (!std::holds_alternative<int>(data))
        {
            log_->error("handleGetPlayerActiveEffectsEvent: unexpected data type");
            return;
        }
        int characterId = std::get<int>(data);

        auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
        pqxx::work txn(_dbConn.get());
        auto result = gameServices_.getDatabase().executeQueryWithTransaction(
            txn, "get_player_active_effects", {characterId});

        nlohmann::json effectsJson = nlohmann::json::array();
        for (const auto &row : result)
        {
            nlohmann::json eff;
            eff["id"] = row["id"].as<int64_t>();
            eff["effectId"] = row["effect_id"].as<int>();
            eff["effectSlug"] = row["effect_slug"].as<std::string>();
            eff["effectTypeSlug"] = row["effect_type_slug"].as<std::string>();
            eff["attributeId"] = row["attribute_id"].as<int>();
            eff["attributeSlug"] = row["attribute_slug"].as<std::string>();
            eff["value"] = row["value"].as<float>();
            eff["sourceType"] = row["source_type"].as<std::string>();
            eff["tickMs"] = row["tick_ms"].as<int>();
            eff["expiresAt"] = row["expires_at_unix"].as<int64_t>();
            effectsJson.push_back(std::move(eff));
        }

        // Also append permanent modifiers from passive skills the character has learned.
        // These are computed from passive_skill_modifiers × character_skills and arrive as
        // permanent (expiresAt=0, tickMs=0) flat/percent stat modifiers.
        try
        {
            auto passiveResult = gameServices_.getDatabase().executeQueryWithTransaction(
                txn, "get_player_passive_skill_effects", {characterId});

            for (const auto &row : passiveResult)
            {
                nlohmann::json eff;
                eff["id"] = row["id"].as<int64_t>();
                eff["effectId"] = 0;
                eff["effectSlug"] = row["effect_slug"].as<std::string>();
                eff["effectTypeSlug"] = std::string("passive");
                eff["attributeId"] = 0;
                eff["attributeSlug"] = row["attribute_slug"].as<std::string>();
                eff["value"] = row["value"].as<float>();
                eff["sourceType"] = std::string("skill_passive");
                eff["tickMs"] = 0;
                eff["expiresAt"] = int64_t(0); // permanent
                effectsJson.push_back(std::move(eff));
            }
        }
        catch (const std::exception &passiveEx)
        {
            // passive_skill_modifiers table may not exist in older DBs — degrade gracefully
            log_->warn("[EH] Could not load passive skill effects for character " +
                       std::to_string(characterId) + ": " + passiveEx.what());
        }

        txn.commit();

        ResponseBuilder builder;
        nlohmann::json response = builder
                                      .setHeader("message", "Player active effects")
                                      .setHeader("hash", "")
                                      .setHeader("clientId", characterId)
                                      .setHeader("eventType", "setPlayerActiveEffects")
                                      .setBody("characterId", characterId)
                                      .setBody("effects", effectsJson)
                                      .build();
        networkManager_.sendResponse(clientSocket,
            networkManager_.generateResponseMessage("success", response));

        gameServices_.getLogger().log("[EH] Sent " + std::to_string(effectsJson.size()) +
                                          " active effects for characterId=" + std::to_string(characterId),
            GREEN);
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("Error in handleGetPlayerActiveEffectsEvent: " + std::string(ex.what()));
    }
}

void
EventHandler::handleGetCharacterAttributesRefreshEvent(const Event &event)
{
    const auto &data = event.getData();
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();

    try
    {
        if (!std::holds_alternative<int>(data))
        {
            log_->error("handleGetCharacterAttributesRefreshEvent: unexpected data type");
            return;
        }
        int characterId = std::get<int>(data);

        auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
        pqxx::work txn(_dbConn.get());
        auto result = gameServices_.getDatabase().executeQueryWithTransaction(
            txn, "get_character_attributes", {characterId});

        nlohmann::json attrsJson = nlohmann::json::array();
        for (const auto &row : result)
        {
            nlohmann::json attr;
            attr["id"] = row["id"].as<int>();
            attr["name"] = row["name"].as<std::string>();
            attr["slug"] = row["slug"].as<std::string>();
            attr["value"] = row["value"].as<int>();
            attrsJson.push_back(std::move(attr));
        }

        ResponseBuilder builder;
        nlohmann::json response = builder
                                      .setHeader("message", "Character attributes refresh")
                                      .setHeader("hash", "")
                                      .setHeader("clientId", characterId)
                                      .setHeader("eventType", "setCharacterAttributesRefresh")
                                      .setBody("characterId", characterId)
                                      .setBody("attributesData", attrsJson)
                                      .build();
        networkManager_.sendResponse(clientSocket,
            networkManager_.generateResponseMessage("success", response));

        gameServices_.getLogger().log("[EH] Sent " + std::to_string(attrsJson.size()) +
                                          " attribute entries (refresh) for characterId=" + std::to_string(characterId),
            GREEN);
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("Error in handleGetCharacterAttributesRefreshEvent: " + std::string(ex.what()));
    }
}

void
EventHandler::handleGetVendorDataEvent(const Event &event)
{
    int clientID = event.getClientID();
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();

    try
    {
        auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
        pqxx::work txn(_dbConn.get());

        // Get all vendor NPC IDs
        auto npcRows = gameServices_.getDatabase().executeQueryWithTransaction(txn, "get_vendor_npcs", {});

        nlohmann::json vendorList = nlohmann::json::array();
        for (const auto &npcRow : npcRows)
        {
            int npcId = npcRow["npc_id"].as<int>();

            auto itemRows = gameServices_.getDatabase().executeQueryWithTransaction(
                txn, "get_vendor_inventory", {npcId});

            nlohmann::json items = nlohmann::json::array();
            for (const auto &row : itemRows)
            {
                nlohmann::json item;
                item["itemId"] = row["item_id"].as<int>();
                item["stockCurrent"] = row["stock_current"].as<int>();
                item["stockMax"] = row["stock_max"].as<int>();
                item["restockAmount"] = row["restock_amount"].as<int>();
                item["restockIntervalSec"] = row["restock_interval_sec"].as<int>();
                item["priceOverrideBuy"] = row["price_override_buy"].as<int>();
                item["priceOverrideSell"] = row["price_override_sell"].as<int>();
                items.push_back(std::move(item));
            }

            nlohmann::json vendor;
            vendor["npcId"] = npcId;
            vendor["items"] = std::move(items);
            vendorList.push_back(std::move(vendor));
        }
        txn.commit();

        ResponseBuilder builder;
        nlohmann::json response = builder
                                      .setHeader("message", "Vendor data loaded")
                                      .setHeader("hash", "")
                                      .setHeader("clientId", clientID)
                                      .setHeader("eventType", "setVendorData")
                                      .setBody("vendors", vendorList)
                                      .build();

        networkManager_.sendResponse(clientSocket,
            networkManager_.generateResponseMessage("success", response));

        gameServices_.getLogger().log("[EH] Sent vendor data: " +
                                      std::to_string(vendorList.size()) + " vendors.");
    }
    catch (const std::exception &e)
    {
        gameServices_.getLogger().logError("handleGetVendorDataEvent error: " + std::string(e.what()));
    }
}

void
EventHandler::handleSaveDurabilityChangeEvent(const Event &event)
{
    const auto &data = event.getData();

    try
    {
        if (!std::holds_alternative<nlohmann::json>(data))
        {
            log_->error("handleSaveDurabilityChangeEvent: unexpected data type");
            return;
        }
        const auto &j = std::get<nlohmann::json>(data);
        int characterId = j.value("characterId", 0);
        int inventoryItemId = j.value("inventoryItemId", 0);
        int durabilityCurrent = j.value("durabilityCurrent", 0);

        if (characterId <= 0 || inventoryItemId <= 0)
            return;

        auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
        pqxx::work txn(_dbConn.get());
        gameServices_.getDatabase().executeQueryWithTransaction(
            txn, "update_durability_current", {durabilityCurrent, inventoryItemId, characterId});
        txn.commit();

        log_->info("[SAVE_DUR] char=" + std::to_string(characterId) +
                   " item=" + std::to_string(inventoryItemId) +
                   " dur=" + std::to_string(durabilityCurrent));
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("Error in handleSaveDurabilityChangeEvent: " + std::string(ex.what()));
    }
}

void
EventHandler::handleSaveCurrencyTransactionEvent(const Event &event)
{
    const auto &data = event.getData();

    try
    {
        if (!std::holds_alternative<nlohmann::json>(data))
        {
            log_->error("handleSaveCurrencyTransactionEvent: unexpected data type");
            return;
        }
        const auto &j = std::get<nlohmann::json>(data);
        int characterId = j.value("characterId", 0);
        int npcId = j.value("npcId", 0);
        int itemId = j.value("itemId", 0);
        int quantity = j.value("quantity", 0);
        int totalPrice = j.value("totalPrice", 0);
        std::string txType = j.value("transactionType", "");

        if (characterId <= 0)
            return;

        auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
        pqxx::work txn(_dbConn.get());
        gameServices_.getDatabase().executeQueryWithTransaction(
            txn, "insert_currency_transaction", {characterId, npcId, totalPrice, txType});
        txn.commit();

        log_->info("[CURRENCY_TX] char=" + std::to_string(characterId) +
                   " type=" + txType + " item=" + std::to_string(itemId) +
                   " qty=" + std::to_string(quantity) + " price=" + std::to_string(totalPrice));
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("Error in handleSaveCurrencyTransactionEvent: " + std::string(ex.what()));
    }
}

void
EventHandler::handleSaveEquipmentChangeEvent(const Event &event)
{
    const auto &data = event.getData();

    try
    {
        if (!std::holds_alternative<nlohmann::json>(data))
        {
            log_->error("handleSaveEquipmentChangeEvent: unexpected data type");
            return;
        }
        const auto &j = std::get<nlohmann::json>(data);
        int characterId = j.value("characterId", 0);
        int inventoryItemId = j.value("inventoryItemId", 0);
        std::string action = j.value("action", "");
        std::string equipSlotSlug = j.value("equipSlotSlug", "");

        if (characterId <= 0 || inventoryItemId <= 0 || action.empty())
            return;

        auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
        pqxx::work txn(_dbConn.get());

        if (action == "equip")
        {
            if (equipSlotSlug.empty())
            {
                log_->error("[SAVE_EQUIP] equipSlotSlug is empty for char=" + std::to_string(characterId));
                return;
            }
            gameServices_.getDatabase().executeQueryWithTransaction(
                txn, "insert_character_equipment", {characterId, equipSlotSlug, inventoryItemId});
        }
        else if (action == "unequip")
        {
            gameServices_.getDatabase().executeQueryWithTransaction(
                txn, "delete_character_equipment", {characterId, inventoryItemId});
        }
        else
        {
            log_->error("[SAVE_EQUIP] Unknown action: " + action);
            return;
        }
        txn.commit();

        log_->info("[SAVE_EQUIP] char=" + std::to_string(characterId) +
                   " action=" + action +
                   " slot=" + equipSlotSlug +
                   " invItemId=" + std::to_string(inventoryItemId));
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("Error in handleSaveEquipmentChangeEvent: " + std::string(ex.what()));
    }
}

void
EventHandler::handleGetRespawnZonesEvent(const Event &event)
{
    int clientID = event.getClientID();
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();

    try
    {
        auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
        pqxx::work txn(_dbConn.get());
        auto rows = gameServices_.getDatabase().executeQueryWithTransaction(txn, "get_respawn_zones", {});
        txn.commit();

        nlohmann::json zonesJson = nlohmann::json::array();
        for (const auto &row : rows)
        {
            nlohmann::json z;
            z["id"] = row["id"].as<int>();
            z["name"] = row["name"].as<std::string>();
            z["x"] = row["x"].as<float>();
            z["y"] = row["y"].as<float>();
            z["z"] = row["z"].as<float>();
            z["zoneId"] = row["zone_id"].as<int>();
            z["isDefault"] = row["is_default"].as<bool>();
            zonesJson.push_back(z);
        }

        ResponseBuilder builder;
        nlohmann::json response = builder
                                      .setHeader("message", "Respawn zones loaded")
                                      .setHeader("hash", "")
                                      .setHeader("clientId", clientID)
                                      .setHeader("eventType", "setRespawnZonesList")
                                      .setBody("respawnZonesData", zonesJson)
                                      .build();
        std::string responseData = networkManager_.generateResponseMessage("success", response);
        networkManager_.sendResponse(clientSocket, responseData);

        log_->info("[RESPAWN_ZONES] Sent " + std::to_string(zonesJson.size()) + " respawn zones to chunk server");
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("handleGetRespawnZonesEvent error: " + std::string(ex.what()));
    }
}

void
EventHandler::handleSaveExperienceDebtEvent(const Event &event)
{
    try
    {
        const auto &data = event.getData();
        if (!std::holds_alternative<nlohmann::json>(data))
            return;
        const auto &j = std::get<nlohmann::json>(data);
        int characterId = j.value("characterId", 0);
        int debt = j.value("experienceDebt", 0);
        if (characterId <= 0)
            return;

        auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
        pqxx::work txn(_dbConn.get());
        gameServices_.getDatabase().executeQueryWithTransaction(txn, "set_character_experience_debt", {characterId, debt});
        txn.commit();

        log_->info("[SAVE_EXP_DEBT] char=" + std::to_string(characterId) + " debt=" + std::to_string(debt));
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("handleSaveExperienceDebtEvent error: " + std::string(ex.what()));
    }
}

void
EventHandler::handleSaveActiveEffectEvent(const Event &event)
{
    try
    {
        const auto &data = event.getData();
        if (!std::holds_alternative<nlohmann::json>(data))
            return;
        const auto &j = std::get<nlohmann::json>(data);

        int characterId = j.value("characterId", 0);
        std::string effectSlug = j.value("effectSlug", std::string(""));
        std::string attributeSlug = j.value("attributeSlug", std::string(""));
        std::string sourceType = j.value("sourceType", std::string("death"));
        double value = j.value("value", 0.0);
        int64_t expiresAt = j.value("expiresAt", int64_t(0));
        int tickMs = j.value("tickMs", 0);

        if (characterId <= 0 || effectSlug.empty())
            return;

        // $1=player_id $2=effect_slug $3=attribute_slug $4=source_type $5=value $6=expires_at $7=tick_ms
        // An empty attributeSlug results in NULL attribute_id (no match in entity_attributes).
        using DbParam = std::variant<int, float, double, std::string>;
        std::vector<DbParam> params{
            characterId,
            effectSlug,
            attributeSlug, // '' → NULL attribute_id via SQL subquery
            sourceType,
            value,                          // double
            static_cast<double>(expiresAt), // int64 → double (unix sec, safe precision)
            tickMs};

        auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
        pqxx::work txn(_dbConn.get());
        gameServices_.getDatabase().executeQueryWithTransaction(
            txn, "insert_player_active_effect", params);
        txn.commit();

        log_->info("[SAVE_ACTIVE_EFFECT] char=" + std::to_string(characterId) + " effect=" + effectSlug);
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("handleSaveActiveEffectEvent error: " + std::string(ex.what()));
    }
}

void
EventHandler::handleSaveItemKillCountEvent(const Event &event)
{
    const auto &data = event.getData();

    try
    {
        if (!std::holds_alternative<nlohmann::json>(data))
        {
            log_->error("handleSaveItemKillCountEvent: unexpected data type");
            return;
        }
        const auto &j = std::get<nlohmann::json>(data);
        int characterId = j.value("characterId", 0);
        int inventoryItemId = j.value("inventoryItemId", 0);
        int killCount = j.value("killCount", 0);

        if (characterId <= 0 || inventoryItemId <= 0)
            return;

        auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
        pqxx::work txn(_dbConn.get());
        gameServices_.getDatabase().executeQueryWithTransaction(
            txn, "update_item_kill_count", {killCount, inventoryItemId, characterId});
        txn.commit();

        log_->info("[SAVE_KILL_COUNT] char=" + std::to_string(characterId) +
                   " item=" + std::to_string(inventoryItemId) +
                   " kills=" + std::to_string(killCount));
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("Error in handleSaveItemKillCountEvent: " + std::string(ex.what()));
    }
}

void
EventHandler::handleTransferInventoryItemEvent(const Event &event)
{
    const auto &data = event.getData();

    try
    {
        if (!std::holds_alternative<nlohmann::json>(data))
        {
            log_->error("handleTransferInventoryItemEvent: unexpected data type");
            return;
        }
        const auto &j = std::get<nlohmann::json>(data);
        int toCharId = j.value("toCharId", 0);
        int inventoryItemId = j.value("inventoryItemId", 0);
        int fromCharId = j.value("fromCharId", 0);

        if (toCharId <= 0 || inventoryItemId <= 0)
            return;

        auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
        pqxx::work txn(_dbConn.get());
        if (fromCharId > 0)
        {
            // P2P trade: item still owned by the sender in DB
            gameServices_.getDatabase().executeQueryWithTransaction(
                txn, "transfer_item_between_chars", {toCharId, inventoryItemId, fromCharId});
        }
        else
        {
            // Ground pickup: character_id IS NULL in DB
            gameServices_.getDatabase().executeQueryWithTransaction(
                txn, "transfer_item_ownership", {toCharId, inventoryItemId});
        }
        txn.commit();

        log_->info("[TRANSFER_ITEM] to=" + std::to_string(toCharId) +
                   " invItem=" + std::to_string(inventoryItemId) +
                   (fromCharId > 0 ? " from=" + std::to_string(fromCharId) : " (ground)"));
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("Error in handleTransferInventoryItemEvent: " + std::string(ex.what()));
    }
}

void
EventHandler::handleNullifyItemOwnerEvent(const Event &event)
{
    const auto &data = event.getData();
    try
    {
        if (!std::holds_alternative<nlohmann::json>(data))
            return;
        const auto &j = std::get<nlohmann::json>(data);
        int inventoryItemId = j.value("inventoryItemId", 0);
        int fromCharId = j.value("fromCharId", 0);
        if (inventoryItemId <= 0 || fromCharId <= 0)
            return;

        auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
        pqxx::work txn(_dbConn.get());
        gameServices_.getDatabase().executeQueryWithTransaction(
            txn, "nullify_item_owner", {inventoryItemId, fromCharId});
        txn.commit();

        log_->info("[NULLIFY_OWNER] char=" + std::to_string(fromCharId) +
                   " invItem=" + std::to_string(inventoryItemId));
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("Error in handleNullifyItemOwnerEvent: " + std::string(ex.what()));
    }
}

void
EventHandler::handleDeleteInventoryItemEvent(const Event &event)
{
    const auto &data = event.getData();
    try
    {
        if (!std::holds_alternative<nlohmann::json>(data))
            return;
        const auto &j = std::get<nlohmann::json>(data);
        int inventoryItemId = j.value("inventoryItemId", 0);
        if (inventoryItemId <= 0)
            return;

        auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
        pqxx::work txn(_dbConn.get());
        gameServices_.getDatabase().executeQueryWithTransaction(
            txn, "delete_inventory_item_by_id", {inventoryItemId});
        txn.commit();

        log_->info("[DELETE_ITEM] invItem=" + std::to_string(inventoryItemId));
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("Error in handleDeleteInventoryItemEvent: " + std::string(ex.what()));
    }
}

void
EventHandler::handleGetStatusEffectTemplatesEvent(const Event &event)
{
    int clientID = event.getClientID();
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();

    try
    {
        auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
        pqxx::work txn(_dbConn.get());
        auto rows = gameServices_.getDatabase().executeQueryWithTransaction(txn, "get_status_effect_templates", {});
        txn.commit();

        // Group rows by effect_slug into a nested JSON structure:
        // { "effectSlug": "resurrection_sickness", "category": "debuff",
        //   "durationSec": 120,
        //   "modifiers": [{"modifierType":"percent_all","attributeSlug":"","value":-20}] }
        nlohmann::json effectsJson = nlohmann::json::array();
        std::string currentSlug;
        nlohmann::json currentEffect;

        auto pushCurrentEffect = [&]()
        {
            if (!currentSlug.empty())
                effectsJson.push_back(std::move(currentEffect));
        };

        for (const auto &row : rows)
        {
            std::string slug = row["effect_slug"].as<std::string>();

            if (slug != currentSlug)
            {
                pushCurrentEffect();
                currentEffect = nlohmann::json::object();
                currentEffect["effectSlug"] = slug;
                currentEffect["category"] = row["category"].as<std::string>();
                currentEffect["durationSec"] = row["duration_sec"].as<int>();
                currentEffect["modifiers"] = nlohmann::json::array();
                currentSlug = slug;
            }

            // A LEFT JOIN row with no modifier columns means the effect has no modifiers yet.
            if (!row["modifier_type"].is_null())
            {
                nlohmann::json mod;
                mod["modifierType"] = row["modifier_type"].as<std::string>();
                mod["attributeSlug"] = row["attribute_slug"].as<std::string>(); // '' for percent_all
                mod["value"] = row["value"].as<double>();
                currentEffect["modifiers"].push_back(std::move(mod));
            }
        }
        pushCurrentEffect(); // push the last effect

        ResponseBuilder builder;
        nlohmann::json response = builder
                                      .setHeader("message", "Status effect templates loaded")
                                      .setHeader("hash", "")
                                      .setHeader("clientId", clientID)
                                      .setHeader("eventType", "setStatusEffectTemplates")
                                      .setBody("templates", effectsJson)
                                      .build();
        std::string responseData = networkManager_.generateResponseMessage("success", response);
        networkManager_.sendResponse(clientSocket, responseData);

        log_->info("[STATUS_EFFECT_TEMPLATES] Sent " + std::to_string(effectsJson.size()) + " templates to chunk server");
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("handleGetStatusEffectTemplatesEvent error: " + std::string(ex.what()));
    }
}

void
EventHandler::handleGetGameZonesEvent(const Event &event)
{
    int clientID = event.getClientID();
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();

    try
    {
        auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
        pqxx::work txn(_dbConn.get());
        auto rows = gameServices_.getDatabase().executeQueryWithTransaction(txn, "get_game_zones", {});
        txn.commit();

        nlohmann::json zonesJson = nlohmann::json::array();
        for (const auto &row : rows)
        {
            nlohmann::json z;
            z["id"] = row["id"].as<int>();
            z["slug"] = row["slug"].as<std::string>();
            z["name"] = row["name"].as<std::string>();
            z["minLevel"] = row["min_level"].as<int>();
            z["maxLevel"] = row["max_level"].as<int>();
            z["isPvp"] = row["is_pvp"].as<bool>();
            z["isSafeZone"] = row["is_safe_zone"].as<bool>();
            z["minX"] = row["min_x"].as<float>();
            z["maxX"] = row["max_x"].as<float>();
            z["minY"] = row["min_y"].as<float>();
            z["maxY"] = row["max_y"].as<float>();
            z["explorationXpReward"] = row["exploration_xp_reward"].as<int>();
            z["championThresholdKills"] = row["champion_threshold_kills"].as<int>();
            zonesJson.push_back(z);
        }

        ResponseBuilder builder;
        nlohmann::json response = builder
                                      .setHeader("message", "Game zones loaded")
                                      .setHeader("hash", "")
                                      .setHeader("clientId", clientID)
                                      .setHeader("eventType", "setGameZonesList")
                                      .setBody("gameZonesData", zonesJson)
                                      .build();
        std::string responseData = networkManager_.generateResponseMessage("success", response);
        networkManager_.sendResponse(clientSocket, responseData);

        log_->info("[GAME_ZONES] Sent " + std::to_string(zonesJson.size()) + " game zones to chunk server");
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("handleGetGameZonesEvent error: " + std::string(ex.what()));
    }
}

// ── GET_PLAYER_PITY ────────────────────────────────────────────────────────
void
EventHandler::handleGetPlayerPityEvent(const Event &event)
{
    const auto &data = event.getData();
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();

    try
    {
        if (!std::holds_alternative<int>(data))
        {
            log_->error("handleGetPlayerPityEvent: unexpected data type");
            return;
        }
        int characterId = std::get<int>(data);

        auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
        pqxx::work txn(_dbConn.get());
        auto result = gameServices_.getDatabase().executeQueryWithTransaction(
            txn, "get_player_pity", {characterId});

        nlohmann::json entriesJson = nlohmann::json::array();
        for (const auto &row : result)
        {
            nlohmann::json entry;
            entry["itemId"] = row["item_id"].as<int>();
            entry["killCount"] = row["kill_count"].as<int>();
            entriesJson.push_back(std::move(entry));
        }

        ResponseBuilder builder;
        nlohmann::json response = builder
                                      .setHeader("message", "Player pity data")
                                      .setHeader("hash", "")
                                      .setHeader("clientId", characterId)
                                      .setHeader("eventType", "setPlayerPityData")
                                      .setBody("characterId", characterId)
                                      .setBody("entries", entriesJson)
                                      .build();
        networkManager_.sendResponse(clientSocket,
            networkManager_.generateResponseMessage("success", response));

        log_->info("[EH] Sent " + std::to_string(entriesJson.size()) +
                   " pity counters for characterId=" + std::to_string(characterId));
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("handleGetPlayerPityEvent error: " + std::string(ex.what()));
    }
}

// ── GET_PLAYER_BESTIARY ────────────────────────────────────────────────────
void
EventHandler::handleGetPlayerBestiaryEvent(const Event &event)
{
    const auto &data = event.getData();
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();

    try
    {
        if (!std::holds_alternative<int>(data))
        {
            log_->error("handleGetPlayerBestiaryEvent: unexpected data type");
            return;
        }
        int characterId = std::get<int>(data);

        auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
        pqxx::work txn(_dbConn.get());
        auto result = gameServices_.getDatabase().executeQueryWithTransaction(
            txn, "get_player_bestiary", {characterId});

        nlohmann::json entriesJson = nlohmann::json::array();
        for (const auto &row : result)
        {
            nlohmann::json entry;
            entry["mobTemplateId"] = row["mob_template_id"].as<int>();
            entry["killCount"] = row["kill_count"].as<int>();
            entriesJson.push_back(std::move(entry));
        }

        ResponseBuilder builder;
        nlohmann::json response = builder
                                      .setHeader("message", "Player bestiary data")
                                      .setHeader("hash", "")
                                      .setHeader("clientId", characterId)
                                      .setHeader("eventType", "setPlayerBestiaryData")
                                      .setBody("characterId", characterId)
                                      .setBody("entries", entriesJson)
                                      .build();
        networkManager_.sendResponse(clientSocket,
            networkManager_.generateResponseMessage("success", response));

        log_->info("[EH] Sent " + std::to_string(entriesJson.size()) +
                   " bestiary entries for characterId=" + std::to_string(characterId));
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("handleGetPlayerBestiaryEvent error: " + std::string(ex.what()));
    }
}

// ── SAVE_PITY_COUNTER ──────────────────────────────────────────────────────
void
EventHandler::handleSavePityCounterEvent(const Event &event)
{
    const auto &data = event.getData();

    try
    {
        if (!std::holds_alternative<nlohmann::json>(data))
        {
            log_->error("handleSavePityCounterEvent: unexpected data type");
            return;
        }
        const auto &j = std::get<nlohmann::json>(data);
        int characterId = j.value("characterId", 0);
        int itemId = j.value("itemId", 0);
        int killCount = j.value("killCount", 0);

        if (characterId <= 0 || itemId <= 0)
            return;

        auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
        pqxx::work txn(_dbConn.get());
        gameServices_.getDatabase().executeQueryWithTransaction(
            txn, "upsert_pity_counter", {characterId, itemId, killCount});
        txn.commit();

        log_->info("[SAVE_PITY] char=" + std::to_string(characterId) +
                   " item=" + std::to_string(itemId) +
                   " kills=" + std::to_string(killCount));
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("handleSavePityCounterEvent error: " + std::string(ex.what()));
    }
}

// ── SAVE_BESTIARY_KILL ─────────────────────────────────────────────────────
void
EventHandler::handleSaveBestiaryKillEvent(const Event &event)
{
    const auto &data = event.getData();

    try
    {
        if (!std::holds_alternative<nlohmann::json>(data))
        {
            log_->error("handleSaveBestiaryKillEvent: unexpected data type");
            return;
        }
        const auto &j = std::get<nlohmann::json>(data);
        int characterId = j.value("characterId", 0);
        int mobTemplateId = j.value("mobTemplateId", 0);
        int killCount = j.value("killCount", 0);

        if (characterId <= 0 || mobTemplateId <= 0)
            return;

        auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
        pqxx::work txn(_dbConn.get());
        gameServices_.getDatabase().executeQueryWithTransaction(
            txn, "upsert_bestiary_kill", {characterId, mobTemplateId, killCount});
        txn.commit();

        log_->info("[SAVE_BESTIARY] char=" + std::to_string(characterId) +
                   " mob=" + std::to_string(mobTemplateId) +
                   " kills=" + std::to_string(killCount));
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("handleSaveBestiaryKillEvent error: " + std::string(ex.what()));
    }
}

// ── GET_TIMED_CHAMPION_TEMPLATES ──────────────────────────────────────────────

void
EventHandler::handleGetTimedChampionTemplatesEvent(const Event &event)
{
    int clientID = event.getClientID();
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();

    try
    {
        auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
        pqxx::work txn(_dbConn.get());
        auto rows = gameServices_.getDatabase().executeQueryWithTransaction(txn, "get_timed_champion_templates", {});
        txn.commit();

        nlohmann::json arr = nlohmann::json::array();
        for (const auto &row : rows)
        {
            nlohmann::json t;
            t["id"] = row["id"].as<int>();
            t["slug"] = row["slug"].as<std::string>();
            t["gameZoneId"] = row["game_zone_id"].as<int>();
            t["mobTemplateId"] = row["mob_template_id"].as<int>();
            t["intervalHours"] = row["interval_hours"].as<int>();
            t["windowMinutes"] = row["window_minutes"].as<int>();
            t["nextSpawnAt"] = row["next_spawn_at"].as<int64_t>();
            t["announceKey"] = row["announce_key"].as<std::string>();
            arr.push_back(std::move(t));
        }

        ResponseBuilder builder;
        nlohmann::json response = builder
                                      .setHeader("message", "Timed champion templates loaded")
                                      .setHeader("hash", "")
                                      .setHeader("clientId", clientID)
                                      .setHeader("eventType", "setTimedChampionTemplatesList")
                                      .setBody("timedChampionTemplates", arr)
                                      .build();

        std::string responseData = networkManager_.generateResponseMessage("success", response);
        networkManager_.sendResponse(clientSocket, responseData);

        log_->info("[TIMED_CHAMP] Sent {} timed champion templates to chunk server", arr.size());
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("handleGetTimedChampionTemplatesEvent error: " + std::string(ex.what()));
    }
}

// ── TIMED_CHAMPION_KILLED ─────────────────────────────────────────────────────

void
EventHandler::handleTimedChampionKilledEvent(const Event &event)
{
    try
    {
        const auto &data = event.getData();
        if (!std::holds_alternative<nlohmann::json>(data))
        {
            log_->error("handleTimedChampionKilledEvent: unexpected data type");
            return;
        }
        const auto &j = std::get<nlohmann::json>(data);
        const std::string slug = j.value("slug", "");
        const int64_t killedAt = j.value("killedAt", int64_t{0});

        if (slug.empty())
            return;

        // Compute next_spawn_at = killedAt + interval_hours * 3600
        // We need the interval_hours for this slug. Query and then update.
        auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
        pqxx::work txn(_dbConn.get());

        // Get interval_hours for this slug
        auto rows = txn.exec_params(
            "SELECT interval_hours FROM timed_champion_templates WHERE slug = $1", slug);
        if (!rows.empty())
        {
            int intervalHours = rows[0]["interval_hours"].as<int>();
            int64_t nextSpawnAt = killedAt + static_cast<int64_t>(intervalHours) * 3600;

            gameServices_.getDatabase().executeQueryWithTransaction(
                txn, "update_timed_champion_next_spawn", {slug, static_cast<double>(nextSpawnAt)});
        }
        txn.commit();

        log_->info("[TIMED_CHAMP] Updated next_spawn for slug='{}' after kill", slug);
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("handleTimedChampionKilledEvent error: " + std::string(ex.what()));
    }
}

// ── GET_PLAYER_REPUTATIONS ─────────────────────────────────────────────────
void
EventHandler::handleGetPlayerReputationsEvent(const Event &event)
{
    const auto &data = event.getData();
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();

    try
    {
        if (!std::holds_alternative<int>(data))
        {
            log_->error("handleGetPlayerReputationsEvent: unexpected data type");
            return;
        }
        int characterId = std::get<int>(data);

        auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
        pqxx::work txn(_dbConn.get());
        auto result = gameServices_.getDatabase().executeQueryWithTransaction(
            txn, "get_player_reputations", {characterId});

        nlohmann::json entriesJson = nlohmann::json::array();
        for (const auto &row : result)
        {
            nlohmann::json entry;
            entry["factionSlug"] = row["faction_slug"].as<std::string>();
            entry["value"] = row["value"].as<int>();
            entriesJson.push_back(std::move(entry));
        }

        ResponseBuilder builder;
        nlohmann::json response = builder
                                      .setHeader("message", "Player reputations")
                                      .setHeader("hash", "")
                                      .setHeader("clientId", characterId)
                                      .setHeader("eventType", "setPlayerReputationsData")
                                      .setBody("characterId", characterId)
                                      .setBody("entries", entriesJson)
                                      .build();
        networkManager_.sendResponse(clientSocket,
            networkManager_.generateResponseMessage("success", response));

        log_->info("[REPUTATION] Sent {} entries for characterId={}", entriesJson.size(), characterId);
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("handleGetPlayerReputationsEvent error: " + std::string(ex.what()));
    }
}

// ── SAVE_REPUTATION ────────────────────────────────────────────────────────
void
EventHandler::handleSaveReputationEvent(const Event &event)
{
    const auto &data = event.getData();

    try
    {
        if (!std::holds_alternative<nlohmann::json>(data))
        {
            log_->error("handleSaveReputationEvent: unexpected data type");
            return;
        }
        const auto &j = std::get<nlohmann::json>(data);
        int characterId = j.value("characterId", 0);
        std::string faction = j.value("factionSlug", "");
        int value = j.value("value", 0);

        if (characterId <= 0 || faction.empty())
            return;

        auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
        pqxx::work txn(_dbConn.get());
        gameServices_.getDatabase().executeQueryWithTransaction(
            txn, "upsert_reputation", {characterId, faction, value});
        txn.commit();

        log_->info("[REPUTATION] Saved char={} faction={} value={}", characterId, faction, value);
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("handleSaveReputationEvent error: " + std::string(ex.what()));
    }
}

// ── GET_PLAYER_MASTERIES ───────────────────────────────────────────────────
void
EventHandler::handleGetPlayerMasteriesEvent(const Event &event)
{
    const auto &data = event.getData();
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();

    try
    {
        if (!std::holds_alternative<int>(data))
        {
            log_->error("handleGetPlayerMasteriesEvent: unexpected data type");
            return;
        }
        int characterId = std::get<int>(data);

        auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
        pqxx::work txn(_dbConn.get());
        auto result = gameServices_.getDatabase().executeQueryWithTransaction(
            txn, "get_player_masteries", {characterId});

        nlohmann::json entriesJson = nlohmann::json::array();
        for (const auto &row : result)
        {
            nlohmann::json entry;
            entry["masterySlug"] = row["mastery_slug"].as<std::string>();
            entry["value"] = row["value"].as<float>();
            entriesJson.push_back(std::move(entry));
        }

        ResponseBuilder builder;
        nlohmann::json response = builder
                                      .setHeader("message", "Player masteries")
                                      .setHeader("hash", "")
                                      .setHeader("clientId", characterId)
                                      .setHeader("eventType", "setPlayerMasteriesData")
                                      .setBody("characterId", characterId)
                                      .setBody("entries", entriesJson)
                                      .build();
        networkManager_.sendResponse(clientSocket,
            networkManager_.generateResponseMessage("success", response));

        log_->info("[MASTERY] Sent {} entries for characterId={}", entriesJson.size(), characterId);
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("handleGetPlayerMasteriesEvent error: " + std::string(ex.what()));
    }
}

// ── SAVE_MASTERY ───────────────────────────────────────────────────────────
void
EventHandler::handleSaveMasteryEvent(const Event &event)
{
    const auto &data = event.getData();

    try
    {
        if (!std::holds_alternative<nlohmann::json>(data))
        {
            log_->error("handleSaveMasteryEvent: unexpected data type");
            return;
        }
        const auto &j = std::get<nlohmann::json>(data);
        int characterId = j.value("characterId", 0);
        std::string masterySlug = j.value("masterySlug", "");
        float value = j.value("value", 0.0f);

        if (characterId <= 0 || masterySlug.empty())
            return;

        auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
        pqxx::work txn(_dbConn.get());
        gameServices_.getDatabase().executeQueryWithTransaction(
            txn, "upsert_mastery", {characterId, masterySlug, value});
        txn.commit();

        log_->info("[MASTERY] Saved char={} slug={} value={}", characterId, masterySlug, value);
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("handleSaveMasteryEvent error: " + std::string(ex.what()));
    }
}

// ── GET_ZONE_EVENT_TEMPLATES ───────────────────────────────────────────────
void
EventHandler::handleGetZoneEventTemplatesEvent(const Event &event)
{
    int clientID = event.getClientID();
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();

    try
    {
        auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
        pqxx::work txn(_dbConn.get());
        auto rows = gameServices_.getDatabase().executeQueryWithTransaction(
            txn, "get_zone_event_templates", {});

        nlohmann::json arr = nlohmann::json::array();
        for (const auto &row : rows)
        {
            nlohmann::json t;
            t["id"] = row["id"].as<int>();
            t["slug"] = row["slug"].as<std::string>();
            t["gameZoneId"] = row["game_zone_id"].as<int>();
            t["triggerType"] = row["trigger_type"].as<std::string>();
            t["durationSec"] = row["duration_sec"].as<int>();
            t["lootMultiplier"] = row["loot_multiplier"].as<float>();
            t["spawnRateMultiplier"] = row["spawn_rate_multiplier"].as<float>();
            t["mobSpeedMultiplier"] = row["mob_speed_multiplier"].as<float>();
            t["announceKey"] = row["announce_key"].as<std::string>();
            t["intervalHours"] = row["interval_hours"].as<int>();
            t["randomChancePerHour"] = row["random_chance_per_hour"].as<float>();
            t["hasInvasionWave"] = row["has_invasion_wave"].as<bool>();
            t["invasionMobTemplateId"] = row["invasion_mob_template_id"].is_null()
                                             ? 0
                                             : row["invasion_mob_template_id"].as<int>();
            t["invasionWaveCount"] = row["invasion_wave_count"].as<int>();
            arr.push_back(std::move(t));
        }

        ResponseBuilder builder;
        nlohmann::json response = builder
                                      .setHeader("message", "Zone event templates loaded")
                                      .setHeader("hash", "")
                                      .setHeader("clientId", clientID)
                                      .setHeader("eventType", "setZoneEventTemplatesList")
                                      .setBody("templates", arr)
                                      .build();
        networkManager_.sendResponse(clientSocket,
            networkManager_.generateResponseMessage("success", response));

        log_->info("[ZONE_EVENT] Sent {} zone event templates to chunk server", arr.size());
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("handleGetZoneEventTemplatesEvent error: " + std::string(ex.what()));
    }
}
