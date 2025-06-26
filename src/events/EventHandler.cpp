#include "events/EventHandler.hpp"

#include "events/Event.hpp"

EventHandler::EventHandler(
    NetworkManager &networkManager,
    GameServices &gameServices)
    : networkManager_(networkManager),
      gameServices_(gameServices)
{
}

void
EventHandler::handleJoinChunkEvent(const Event &event)
{
    // Here we will init data from DB of the character when client joined and send it to the chunk server
    // Retrieve the data from the event
    const auto data = event.getData();
    int clientID = event.getClientID();

    // Extract init data
    try
    {
        // Try to extract the data
        if (std::holds_alternative<ClientDataStruct>(data))
        {
            ClientDataStruct passedClientData = std::get<ClientDataStruct>(data);
            // Save the clientData object with the new init data
            gameServices_.getClientManager().setClientData(passedClientData);

            // Get the character data from the database
            CharacterDataStruct characterData = gameServices_.getCharacterManager().getBasicCharacterDataFromDatabase(gameServices_.getDatabase(), clientID, passedClientData.characterId);
            // Update the clientData object with the new character data
            gameServices_.getCharacterManager().addOrUpdateCharacter(characterData);

            // Get the character position from the database
            PositionStruct characterPosition = gameServices_.getCharacterManager().getCharacterPositionFromDatabase(gameServices_.getDatabase(), clientID, passedClientData.characterId);
            // Update the clientData object with the new position
            gameServices_.getCharacterManager().updateCharacterPosition(gameServices_.getDatabase(), clientID, passedClientData.characterId, characterPosition);

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
                               .setHeader("message", "Authentication failed for user!")
                               .setHeader("hash", passedClientData.hash)
                               .setHeader("clientId", passedClientData.clientId)
                               .setHeader("eventType", "joinGame")
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
                attributeData["attributeId"] = attribute.id;
                attributeData["attributeSlug"] = attribute.slug;
                attributeData["attributeName"] = attribute.name;
                attributeData["attributeValue"] = attribute.value;
                attributes.push_back(attributeData);
            }

            // Add the message to the response
            response = builder
                           .setHeader("message", "Authentication success for user!")
                           .setHeader("hash", currentClientData.hash)
                           .setHeader("clientId", currentClientData.clientId)
                           .setHeader("eventType", "joinGame")
                           .setBody("characterId", currentClientData.characterId)
                           .setBody("characterClass", characterData.characterClass)
                           .setBody("characterLevel", characterData.characterLevel)
                           .setBody("characterExpForNextLevel", characterData.expForNextLevel)
                           .setBody("characterName", characterData.characterName)
                           .setBody("characterRace", characterData.characterRace)
                           .setBody("characterExp", characterData.characterExperiencePoints)
                           .setBody("characterCurrentHealth", characterData.characterCurrentHealth)
                           .setBody("characterCurrentMana", characterData.characterCurrentMana)
                           .setBody("posX", characterData.characterPosition.positionX)
                           .setBody("posY", characterData.characterPosition.positionY)
                           .setBody("posZ", characterData.characterPosition.positionZ)
                           .setBody("rotZ", characterData.characterPosition.rotationZ)
                           .setBody("attributesData", attributes)
                           .build();
            // Prepare a response message
            std::string responseData = networkManager_.generateResponseMessage("success", response);

            // Send the response to the chunk server
            networkManager_.sendResponse(
                passedClientData.socket,
                responseData);
        }
        else
        {
            gameServices_.getLogger().log("Error with extracting data!");
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
        // Try to extract the dat
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
                               .setHeader("message", "Movement failed for character!")
                               .setHeader("clientId", clientID)
                               .setHeader("eventType", "moveCharacter")
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
                           .setHeader("message", "Movement saved for character!")
                           .setHeader("clientId", passedCharacterData.clientId)
                           .setHeader("eventType", "moveCharacter")
                           .setBody("characterId", passedCharacterData.characterId)
                           .setBody("posX", passedCharacterData.characterPosition.positionX)
                           .setBody("posY", passedCharacterData.characterPosition.positionY)
                           .setBody("posZ", passedCharacterData.characterPosition.positionZ)
                           .setBody("rotZ", passedCharacterData.characterPosition.rotationZ)
                           .build();
            // Prepare a response message
            std::string responseData = networkManager_.generateResponseMessage("success", response);

            gameServices_.getLogger().log("Sending data to Chunk Server: " + responseData, YELLOW);

            // Send the response to the chunk server
            networkManager_.sendResponse(
                clientSocket,
                responseData);
        }
        else
        {
            gameServices_.getLogger().log("Error with extracting data!");
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
    const auto data = event.getData();
    int clientID = event.getClientID();
    // get socket from the event
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();

    // Extract init data
    try
    {
        if (clientID == 0)
        {
            gameServices_.getLogger().log("Client ID is 0, so we will just remove client from our list!");

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
                           .setHeader("eventType", "joinChunkServer")
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
                       .setHeader("message", "Joining chunk server success!")
                       .setHeader("hash", "")
                       .setHeader("clientId", clientID)
                       .setHeader("eventType", "joinChunkServer")
                       .setBody("", "")
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
            gameServices_.getLogger().log("Chunk Server ID is 0, so we will just remove chunk server from our list!");

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

// handle get mob data event
void
EventHandler::handleGetMobDataEvent(const Event &event)
{
    // Here we will get the mob data from the database and send it to the client
    // Retrieve the data from the event
    const auto &data = event.getData();
    int clientID = event.getClientID();

    // get socket from the event
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event
                                                                     .getClientSocket();

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
                mobJson["mobId"] = mobData.id;
                mobJson["mobUID"] = mobData.uid;
                mobJson["mobZoneId"] = mobData.zoneId;
                mobJson["mobName"] = mobData.name;
                mobJson["mobRaceName"] = mobData.raceName;
                mobJson["mobLevel"] = mobData.level;
                mobJson["mobCurrentHealth"] = mobData.currentHealth;
                mobJson["mobCurrentMana"] = mobData.currentMana;
                mobJson["isAggressive"] = mobData.isAggressive;
                mobJson["isDead"] = mobData.isDead;
                mobJson["posX"] = mobData.position.positionX;
                mobJson["posY"] = mobData.position.positionY;
                mobJson["posZ"] = mobData.position.positionZ;
                mobJson["rotZ"] = mobData.position.rotationZ;

                for (auto &mobAttributeItem : mobData.attributes)
                {
                    nlohmann::json mobItemJson;
                    mobItemJson["attributeId"] = mobAttributeItem.id;
                    mobItemJson["attributeName"] = mobAttributeItem.name;
                    mobItemJson["attributeSlug"] = mobAttributeItem.slug;
                    mobItemJson["attributeValue"] = mobAttributeItem.value;
                    mobJson["attributesData"].push_back(mobItemJson);
                }

                // Prepare the response message
                nlohmann::json response;
                ResponseBuilder builder;

                // Add the message to the response
                response = builder
                               .setHeader("message", "Spawning mobs success!")
                               .setHeader("hash", "")
                               .setHeader("clientId", clientID)
                               .setHeader("eventType", "spawnMobsInZone")
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
            gameServices_.getLogger().log("Error with extracting data!");
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
        spawnZoneJson["zoneId"] = spawnZone.second.zoneId;
        spawnZoneJson["zoneName"] = spawnZone.second.zoneName;
        spawnZoneJson["minX"] = spawnZone.second.posX;
        spawnZoneJson["maxX"] = spawnZone.second.sizeX;
        spawnZoneJson["minY"] = spawnZone.second.posY;
        spawnZoneJson["maxY"] = spawnZone.second.sizeY;
        spawnZoneJson["minZ"] = spawnZone.second.posZ;
        spawnZoneJson["maxZ"] = spawnZone.second.sizeZ;
        spawnZoneJson["spawnMobId"] = spawnZone.second.spawnMobId;
        spawnZoneJson["maxSpawnCount"] = spawnZone.second.spawnCount;
        spawnZoneJson["spawnedMobsCount"] = 0;
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
                           .setHeader("eventType", "getSpawnZones")
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
                       .setHeader("eventType", "getSpawnZones")
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
        case Event::JOIN_CHARACTER:
            handleJoinChunkEvent(event);
            break;
        case Event::MOVE_CHARACTER:
            handleMoveCharacterChunkEvent(event);
            break;
        case Event::DISCONNECT_CLIENT:
            handleDisconnectChunkEvent(event);
            break;
        case Event::DISCONNECT_CHUNK_SERVER:
            handleDisconnectChunkServerEvent(event);
            break;
        case Event::GET_SPAWN_ZONES:
            handleGetSpawnZonesEvent(event);
            break;
        case Event::GET_MOB_DATA:
            handleGetMobDataEvent(event);
            break;
        case Event::JOIN_CHUNK_SERVER:
            handleJoinChunkServerEvent(event);
            break;
    }
}