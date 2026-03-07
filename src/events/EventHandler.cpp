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
                           .setBody("level", characterData.characterLevel)
                           .setBody("expForNextLevel", characterData.expForNextLevel)
                           .setBody("name", characterData.characterName)
                           .setBody("race", characterData.characterRace)
                           .setBody("currentExp", characterData.characterExperiencePoints)
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

            // Update the character position in the database
            gameServices_.getCharacterManager().updateCharacterPosition(
                gameServices_.getDatabase(),
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
            itemJson["name"] = itemData.name;
            itemJson["slug"] = itemData.slug;
            itemJson["description"] = itemData.description;
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