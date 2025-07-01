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
                               .setHeader("eventType", "joinGame")
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
                           .setHeader("eventType", "joinGame")
                           .setBody("chunkServerData", chunkServerDataJson)
                           .build();
            // Prepare a response message
            std::string responseData = networkManager_.generateResponseMessage("success", response);

            // Send the response to the Client
            networkManager_.sendResponse(
                passedClientData.socket,
                responseData);

            gameServices_.getLogger().log("Sending data to the Client: " + responseData, YELLOW);

            // Dispatch the event to get character data and send it to the chunk server
            Event getCharacterDataEvent(Event::GET_CHARACTER_DATA, clientID, passedClientData, chunkServerData.socket);
            dispatchEvent(getCharacterDataEvent);
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

        // Try to extract the data
        if (std::holds_alternative<ChunkInfoStruct>(data))
        {
            ChunkInfoStruct chunkData = std::get<ChunkInfoStruct>(data);

            chunkData.ip = "192.168.50.50";  // Set the chunk server IP address for testing purposes
            chunkData.socket = clientSocket; // Set the socket for the chunk server

            // Save the chunk data to memory
            gameServices_.getChunkManager().addChunkInfo(chunkData);

            // load spawn zones
            Event spawnZonesEvent(Event::GET_SPAWN_ZONES, clientID, SpawnZoneStruct(), clientSocket);
            dispatchEvent(spawnZonesEvent);

            // load mobs
            Event mobDataEvent(Event::GET_MOBS_LIST, clientID, MobDataStruct(), clientSocket);
            dispatchEvent(mobDataEvent);

            // load mobs attributes
            Event mobAttributesEvent(Event::GET_MOBS_ATTRIBUTES, clientID, MobAttributeStruct(), clientSocket);
            dispatchEvent(mobAttributesEvent);
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
        // load the mobs list from the database
        gameServices_.getMobManager().loadMobs();

        // Get the mobs list from the database as map
        auto mobsAttributesListMap = gameServices_.getMobManager().getMobsAttributes();

        nlohmann::json mobsAttributesListJson;

        for (const auto &mobAttributeItem : mobsAttributesListMap)
        {
            const MobAttributeStruct &mobAttributeData = mobAttributeItem.second;
            nlohmann::json mobAttributeJson;
            mobAttributeJson["mob_id"] = mobAttributeData.mob_id;
            mobAttributeJson["id"] = mobAttributeData.id;
            mobAttributeJson["name"] = mobAttributeData.name;
            mobAttributeJson["slug"] = mobAttributeData.slug;
            mobAttributeJson["value"] = mobAttributeData.value;
            // Add the current item to the mobs attributes list json
            mobsAttributesListJson.push_back(mobAttributeJson);
        }

        // If the mobs attributes list is empty, log a message
        if (mobsAttributesListJson.empty())
        {
            gameServices_.getLogger().log("Mobs attributes list is empty!", YELLOW);
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
            mobJson["race"] = mobData.raceName;
            mobJson["level"] = mobData.level;
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

            // Add the current item to the mobs list json
            mobsListJson.push_back(mobJson);
        }

        // If the mobs list is empty, log a message
        if (mobsListJson.empty())
        {
            gameServices_.getLogger().log("Mobs list is empty!", YELLOW);
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
                mobJson["race"] = mobData.raceName;
                mobJson["level"] = mobData.level;
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

    // chunk server events
    case Event::JOIN_CHUNK_SERVER:
        handleJoinChunkServerEvent(event);
        break;
    case Event::DISCONNECT_CHUNK_SERVER:
        handleDisconnectChunkServerEvent(event);
        break;
    }
}