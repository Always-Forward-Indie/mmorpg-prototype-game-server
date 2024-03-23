#include "events/EventHandler.hpp"
#include "events/Event.hpp"

EventHandler::EventHandler(NetworkManager &networkManager,
                           ChunkServerWorker &chunkServerWorker,
                           Database &database,
                           CharacterManager &characterManager,
                           Logger &logger)
    : networkManager_(networkManager),
      chunkServerWorker_(chunkServerWorker),
      database_(database),
      logger_(logger),
      characterManager_(characterManager)
{
}

void EventHandler::handleJoinChunkEvent(const Event &event, ClientData &clientData)
{
    // Here we will update the init data of the character when client joined in the object and send it to the chunk server
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
            clientData.storeClientData(passedClientData);

            // Get the character data from the database
            CharacterDataStruct characterData = characterManager_.getBasicCharacterData(database_, clientData, clientID, passedClientData.characterData.characterId);
            // Update the clientData object with the new character data
            clientData.updateCharacterData(clientID, characterData);
            // Get the character position from the database
            PositionStruct characterPosition = characterManager_.getCharacterPosition(database_, clientData, clientID, passedClientData.characterData.characterId);
            // Update the clientData object with the new position
            clientData.updateCharacterPositionData(clientID, characterPosition);

            // Get the clientData object with the new init data
            const ClientDataStruct *currentClientData = clientData.getClientData(passedClientData.clientId);

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
                // Send the response to the client
                chunkServerWorker_.sendDataToChunkServer(responseData);
                return;
            }

            // Add the message to the response
            response = builder
                           .setHeader("message", "Authentication success for user!")
                           .setHeader("hash", currentClientData->hash)
                           .setHeader("clientId", currentClientData->clientId)
                           .setHeader("eventType", "joinGame")
                           .setBody("characterId", currentClientData->characterData.characterId)
                           .setBody("characterClass", currentClientData->characterData.characterClass)
                           .setBody("characterLevel", currentClientData->characterData.characterLevel)
                           .setBody("characterName", currentClientData->characterData.characterName)
                           .setBody("characterRace", currentClientData->characterData.characterRace)
                           .setBody("characterExp", currentClientData->characterData.characterExperiencePoints)
                           .setBody("characterCurrentHealth", currentClientData->characterData.characterCurrentHealth)
                           .setBody("characterCurrentMana", currentClientData->characterData.characterCurrentMana)
                           .setBody("posX", currentClientData->characterData.characterPosition.positionX)
                           .setBody("posY", currentClientData->characterData.characterPosition.positionY)
                           .setBody("posZ", currentClientData->characterData.characterPosition.positionZ)
                           .setBody("rotZ", currentClientData->characterData.characterPosition.rotationZ)
                           .build();
            // Prepare a response message
            std::string responseData = networkManager_.generateResponseMessage("success", response);

            // Send data to the the chunk server
            chunkServerWorker_.sendDataToChunkServer(responseData);
        }
        else
        {
            logger_.log("Error with extracting data!");
        }
    }
    catch (const std::bad_variant_access &ex)
    {
        logger_.log("Error here: " + std::string(ex.what()));
    }
}

void EventHandler::handleJoinClientEvent(const Event &event, ClientData &clientData)
{
    // Here we will send recieved data from chunk server back to all clients in this chunk
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
            // Get the clientData object with the new init data
            ClientDataStruct currentClientData = *clientData.getClientData(passedClientData.clientId);
            std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = currentClientData.socket;

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
                // Send the response to the client
                networkManager_.sendResponse(clientSocket, responseData);
                return;
            }

            // Add the message to the response
            response = builder
                           .setHeader("message", "Authentication success for user!")
                           .setHeader("hash", passedClientData.hash)
                           .setHeader("clientId", passedClientData.clientId)
                           .setHeader("eventType", "joinGame")
                           .setBody("characterId", passedClientData.characterData.characterId)
                           .setBody("characterClass", passedClientData.characterData.characterClass)
                           .setBody("characterLevel", passedClientData.characterData.characterLevel)
                           .setBody("characterName", passedClientData.characterData.characterName)
                           .setBody("characterRace", passedClientData.characterData.characterRace)
                           .setBody("characterExp", passedClientData.characterData.characterExperiencePoints)
                           .setBody("characterCurrentHealth", passedClientData.characterData.characterCurrentHealth)
                           .setBody("characterCurrentMana", passedClientData.characterData.characterCurrentMana)
                           .setBody("posX", passedClientData.characterData.characterPosition.positionX)
                           .setBody("posY", passedClientData.characterData.characterPosition.positionY)
                           .setBody("posZ", passedClientData.characterData.characterPosition.positionZ)
                           .setBody("rotZ", passedClientData.characterData.characterPosition.rotationZ)
                           .build();
            // Prepare a response message
            std::string responseData = networkManager_.generateResponseMessage("success", response);

            // TODO - Implement Chunk ID ?? (maybe in the future) and send data only to the clients in the same chunk
            //  Get all existing clients data as array
            std::unordered_map<int, ClientDataStruct> clientDataMap = clientData.getClientsDataMap();

            // Iterate through all exist clients to send data to them
            for (const auto &clientDataItem : clientDataMap)
            {
                // Send the response to the current item Client
                networkManager_.sendResponse(clientDataItem.second.socket, responseData);
            }
        }
        else
        {
            logger_.log("Error with extracting data!");
        }
    }
    catch (const std::bad_variant_access &ex)
    {
        logger_.log("Error here: " + std::string(ex.what()));
    }
}

void EventHandler::handleMoveCharacterChunkEvent(const Event &event, ClientData &clientData)
{
    // Here we will update the position of the character in the object and send it back to the game server

    // Retrieve the data from the event
    const auto &data = event.getData();
    int clientID = event.getClientID();

    try
    {
        // Try to extract the dat
        if (std::holds_alternative<ClientDataStruct>(data))
        {
            ClientDataStruct passedClientData = std::get<ClientDataStruct>(data);
            // Save new Position data object
            clientData.updateCharacterPositionData(clientID, passedClientData.characterData.characterPosition);

            // Prepare the response message
            nlohmann::json response;
            ResponseBuilder builder;

            // Check if the authentication is not successful
            if (clientID == 0 || passedClientData.hash == "")
            {
                // Add response data
                response = builder
                               .setHeader("message", "Movement failed for character!")
                               .setHeader("hash", passedClientData.hash)
                               .setHeader("clientId", clientID)
                               .setHeader("eventType", "moveCharacter")
                               .setBody("", "")
                               .build();
                // Prepare a response message
                std::string responseData = networkManager_.generateResponseMessage("error", response);
                // Send the response to the client
                chunkServerWorker_.sendDataToChunkServer(responseData);
                return;
            }

            // Add the message to the response
            response = builder
                           .setHeader("message", "Movement success for character!")
                           .setHeader("hash", passedClientData.hash)
                           .setHeader("clientId", passedClientData.clientId)
                           .setHeader("eventType", "moveCharacter")
                           .setBody("characterId", passedClientData.characterData.characterId)
                           .setBody("posX", passedClientData.characterData.characterPosition.positionX)
                           .setBody("posY", passedClientData.characterData.characterPosition.positionY)
                           .setBody("posZ", passedClientData.characterData.characterPosition.positionZ)
                           .setBody("rotZ", passedClientData.characterData.characterPosition.rotationZ)
                           .build();
            // Prepare a response message
            std::string responseData = networkManager_.generateResponseMessage("success", response);

            logger_.log("Sending data to Chunk Server: " + responseData, YELLOW);

            // Send data to the the chunk server
            chunkServerWorker_.sendDataToChunkServer(responseData);
        }
        else
        {
            logger_.log("Error with extracting data!");
        }
    }
    catch (const std::bad_variant_access &ex)
    {
        logger_.log("Error here: " + std::string(ex.what()));
    }
}

void EventHandler::handleMoveCharacterClientEvent(const Event &event, ClientData &clientData)
{
    // Here we will send recieved data from chunk server back to all clients in this chunk
    // Retrieve the data from the event
    const auto &data = event.getData();
    int clientID = event.getClientID();

    try
    {
        // Try to extract the data
        if (std::holds_alternative<ClientDataStruct>(data))
        {
            ClientDataStruct passedClientData = std::get<ClientDataStruct>(data);
            // Get the clientData object with the new init data
            const ClientDataStruct *currentClientData = clientData.getClientData(clientID);
            std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = currentClientData->socket;

            // Prepare the response message
            nlohmann::json response;
            ResponseBuilder builder;

            // Check if the authentication is not successful
            if (clientID == 0)
            {
                // Add response data
                response = builder
                               .setHeader("message", "Movement failed for character!")
                               .setHeader("hash", "")
                               .setHeader("clientId", clientID)
                               .setHeader("eventType", "moveCharacter")
                               .setBody("", "")
                               .build();
                // Prepare a response message
                std::string responseData = networkManager_.generateResponseMessage("error", response);
                // Send the response to the client
                networkManager_.sendResponse(clientSocket, responseData);
                return;
            }

            // TODO - Do we need hash key here?
            //  Add the message to the response
            response = builder
                           .setHeader("message", "Movement success for character!")
                           .setHeader("hash", "")
                           .setHeader("clientId", currentClientData->clientId)
                           .setHeader("eventType", "moveCharacter")
                           .setBody("characterId", currentClientData->characterData.characterId)
                           .setBody("posX", passedClientData.characterData.characterPosition.positionX)
                           .setBody("posY", passedClientData.characterData.characterPosition.positionY)
                           .setBody("posZ", passedClientData.characterData.characterPosition.positionZ)
                           .setBody("rotZ", passedClientData.characterData.characterPosition.rotationZ)
                           .build();
            // Prepare a response message
            std::string responseData = networkManager_.generateResponseMessage("success", response);

            // TODO - Implement Chunk ID ?? (maybe in the future) and send data only to the clients in the same chunk
            //  Get all existing clients data as array
            std::unordered_map<int, ClientDataStruct> clientDataMap = clientData.getClientsDataMap();

            // Iterate through all exist clients to send data to them
            for (const auto &clientDataItem : clientDataMap)
            {
                // Send the response to the current item Client
                networkManager_.sendResponse(clientDataItem.second.socket, responseData);
            }
        }
        else
        {
            logger_.log("Error with extracting data!");
        }
    }
    catch (const std::bad_variant_access &ex)
    {
        logger_.log("Error here: " + std::string(ex.what()));
    }
}

// get connected characters list
void EventHandler::handleGetConnectedCharactersChunkEvent(const Event &event, ClientData &clientData)
{
    // Here we will send recieved data from chunk server back to all clients in this chunk
    // Retrieve the data from the event
    const auto &data = event.getData();
    int clientID = event.getClientID();

    try
    {
        // Try to extract the data
        if (std::holds_alternative<ClientDataStruct>(data))
        {
            ClientDataStruct passedClientData = std::get<ClientDataStruct>(data);
            // Get the clientData object with the new init data
            const ClientDataStruct *currentClientData = clientData.getClientData(clientID);

            // Prepare the response message
            nlohmann::json response;
            ResponseBuilder builder;

            // Check if the authentication is not successful
            if (clientID == 0)
            {
                // Add response data
                response = builder
                               .setHeader("message", "Getting connected characters failed!")
                               .setHeader("hash", "")
                               .setHeader("clientId", clientID)
                               .setHeader("eventType", "getConnectedCharacters")
                               .setBody("", "")
                               .build();
                // Prepare a response message
                std::string responseData = networkManager_.generateResponseMessage("error", response);
                // Send the response to the client
                chunkServerWorker_.sendDataToChunkServer(responseData);
                return;
            }

            // TODO - Implement Chunk ID ?? (maybe in the future) and send data only to the clients in the same chunk
            // TODO - Do we need hash key here?
            //  Add the message to the response
            response = builder
                           .setHeader("message", "Getting connected characters success!")
                           .setHeader("hash", "")
                           .setHeader("clientId", currentClientData->clientId)
                           .setHeader("eventType", "getConnectedCharacters")
                           .setBody("", "")
                           .build();
            // Prepare a response message
            std::string responseData = networkManager_.generateResponseMessage("success", response);

            // Send the response to the client
            chunkServerWorker_.sendDataToChunkServer(responseData);
        }
        else
        {
            logger_.log("Error with extracting data!");
        }
    }
    catch (const std::bad_variant_access &ex)
    {
        logger_.log("Error here: " + std::string(ex.what()));
    }
}

// get connected characters list
void EventHandler::handleGetConnectedCharactersClientEvent(const Event &event, ClientData &clientData)
{
    // Here we will send recieved data from chunk server back to all clients in this chunk
    // Retrieve the data from the event
    const auto &data = event.getData();
    int clientID = event.getClientID();

    try
    {
        // Try to extract the data
        if (std::holds_alternative<nlohmann::json>(data))
        {
            nlohmann::json passedClientData = std::get<nlohmann::json>(data);
            // Get the clientData object with the new init data
            const ClientDataStruct *currentClientData = clientData.getClientData(clientID);
            std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = currentClientData->socket;

            // Prepare the response message
            nlohmann::json response;
            ResponseBuilder builder;

            // Check if the authentication is not successful
            if (clientID == 0)
            {
                // Add response data
                response = builder
                               .setHeader("message", "Getting connected characters failed!")
                               .setHeader("hash", "")
                               .setHeader("clientId", clientID)
                               .setHeader("eventType", "getConnectedCharacters")
                               .setBody("", "")
                               .build();
                // Prepare a response message
                std::string responseData = networkManager_.generateResponseMessage("error", response);
                // Send the response to the client
                networkManager_.sendResponse(clientSocket, responseData);
                return;
            }

            // TODO - Do we need hash key here?
            //  Add the message to the response
            response = builder
                           .setHeader("message", "Getting connected characters success!")
                           .setHeader("hash", "")
                           .setHeader("clientId", currentClientData->clientId)
                           .setHeader("eventType", "getConnectedCharacters")
                           .setBody("charactersList", passedClientData)
                           .build();
            // Prepare a response message
            std::string responseData = networkManager_.generateResponseMessage("success", response);

            // Send the response to the client
            networkManager_.sendResponse(clientSocket, responseData);
        }
        else
        {
            logger_.log("Error with extracting data!");
        }
    }
    catch (const std::bad_variant_access &ex)
    {
        logger_.log("Error here: " + std::string(ex.what()));
    }
}

// disconnect the client
void EventHandler::handleDisconnectClientEvent(const Event &event, ClientData &clientData)
{
    // Here we will disconnect the client
    const auto data = event.getData();

    // Extract init data
    try
    {
        // Try to extract the data
        if (std::holds_alternative<ClientDataStruct>(data))
        {
            ClientDataStruct passedClientData = std::get<ClientDataStruct>(data);

            // Remove the client data
            clientData.removeClientData(passedClientData.clientId);

            // send the response to all clients
            nlohmann::json response;
            ResponseBuilder builder;
            response = builder
                           .setHeader("message", "Client disconnected!")
                           .setHeader("hash", "")
                           .setHeader("clientId", passedClientData.clientId)
                           .setHeader("eventType", "disconnectClient")
                           .setBody("", "")
                           .build();
            std::string responseData = networkManager_.generateResponseMessage("success", response);

            // Send the response to the all existing clients in the clientDataMap
            for (auto const &client : clientData.getClientsDataMap())
            {
                networkManager_.sendResponse(client.second.socket, responseData);
            }
        }
        else
        {
            logger_.log("Error with extracting data!");
        }
    }
    catch (const std::bad_variant_access &ex)
    {
        logger_.log("Error here:" + std::string(ex.what()));
    }
}

// disconnect the client
void EventHandler::handleDisconnectChunkEvent(const Event &event, ClientData &clientData)
{
    // Here we will disconnect the client
    const auto data = event.getData();

    // Extract init data
    try
    {
        // Try to extract the data
        if (std::holds_alternative<ClientDataStruct>(data))
        {
            ClientDataStruct passedClientData = std::get<ClientDataStruct>(data);

            // send the response to all clients
            nlohmann::json response;
            ResponseBuilder builder;
            response = builder
                           .setHeader("message", "Client disconnected!")
                           .setHeader("hash", "")
                           .setHeader("clientId", passedClientData.clientId)
                           .setHeader("eventType", "disconnectClient")
                           .setBody("", "")
                           .build();
            std::string responseData = networkManager_.generateResponseMessage("success", response);

            // Send the response to the chunk server
            chunkServerWorker_.sendDataToChunkServer(responseData);
        }
        else
        {
            logger_.log("Error with extracting data!");
        }
    }
    catch (const std::bad_variant_access &ex)
    {
        logger_.log("Error here:" + std::string(ex.what()));
    }
}

// TODO - check this method why it's called only first time
//  ping the client
void EventHandler::handlePingClientEvent(const Event &event, ClientData &clientData)
{
    // Here we will ping the client
    const auto data = event.getData();

    logger_.log("Handling PING event!", YELLOW);

    // Extract init data
    try
    {
        // Try to extract the data
        if (std::holds_alternative<ClientDataStruct>(data))
        {
            ClientDataStruct passedClientData = std::get<ClientDataStruct>(data);

            // send the response to all clients
            nlohmann::json response;
            ResponseBuilder builder;
            response = builder
                           .setHeader("message", "Pong!")
                           .setHeader("eventType", "pingClient")
                           .setBody("", "")
                           .build();
            std::string responseData = networkManager_.generateResponseMessage("success", response);

            logger_.log("Sending PING data to Client: " + responseData, YELLOW);

            // Send the response to the client
            networkManager_.sendResponse(passedClientData.socket, responseData);
        }
        else
        {
            logger_.log("Error with extracting data!");
        }
    }
    catch (const std::bad_variant_access &ex)
    {
        logger_.log("Error here:" + std::string(ex.what()));
    }
}

void EventHandler::handleInteractChunkEvent(const Event &event, ClientData &clientData)
{
    //  TODO - Implement this method
}

void EventHandler::handleInteractClientEvent(const Event &event, ClientData &clientData)
{
    //  TODO - Implement this method
}

void EventHandler::handleSpawnMobsInZoneEvent(const Event &event, ClientData &clientData)
{
    // get the data from the event
    const auto &data = event.getData();
    int clientID = event.getClientID();

    try
    {
        // Try to extract the data
        if (std::holds_alternative<SpawnZoneStruct>(data))
        {
            SpawnZoneStruct spawnZoneData = std::get<SpawnZoneStruct>(data);
            
            // format the zone data to json
            nlohmann::json spawnZoneDataJson;
            //format the mob data to json using for loop
            nlohmann::json mobsListJson;

            spawnZoneDataJson["zoneId"] = spawnZoneData.zoneId;
            spawnZoneDataJson["zoneName"] = spawnZoneData.zoneName;
            spawnZoneDataJson["minX"] = spawnZoneData.minX;
            spawnZoneDataJson["maxX"] = spawnZoneData.maxX;
            spawnZoneDataJson["minY"] = spawnZoneData.minY;
            spawnZoneDataJson["maxY"] = spawnZoneData.maxY;
            spawnZoneDataJson["minZ"] = spawnZoneData.minZ;
            spawnZoneDataJson["maxZ"] = spawnZoneData.maxZ;
            spawnZoneDataJson["spawnMobId"] = spawnZoneData.spawnMobId;
            spawnZoneDataJson["maxSpawnCount"] = spawnZoneData.spawnCount;
            spawnZoneDataJson["spawnedMobsCount"] = spawnZoneData.spawnedMobsList.size();
            spawnZoneDataJson["respawnTime"] = spawnZoneData.respawnTime.count();
            spawnZoneDataJson["spawnEnabled"] = true;

            
            for (auto &mob : spawnZoneData.spawnedMobsList)
            {
                nlohmann::json mobJson;
                mobJson["mobId"] = mob.id;
                mobJson["mobUID"] = mob.uid;
                mobJson["mobZoneId"] = mob.zoneId;
                mobJson["mobName"] = mob.name;
                mobJson["mobRaceName"] = mob.raceName;
                mobJson["mobLevel"] = mob.level;
                mobJson["mobCurrentHealth"] = mob.currentHealth;
                mobJson["mobCurrentMana"] = mob.currentMana;
                mobJson["posX"] = mob.position.positionX;
                mobJson["posY"] = mob.position.positionY;
                mobJson["posZ"] = mob.position.positionZ;
                mobJson["rotZ"] = mob.position.rotationZ;

                for(auto &mobAttributeItem : mob.attributes)
                {
                    nlohmann::json mobItemJson;
                    mobItemJson["attributeId"] = mobAttributeItem.id;
                    mobItemJson["attributeName"] = mobAttributeItem.name;
                    mobItemJson["attributeSlug"] = mobAttributeItem.slug;
                    mobItemJson["attributeValue"] = mobAttributeItem.value;
                    mobJson["attributesData"].push_back(mobItemJson);
                }

                mobsListJson.push_back(mobJson);
            }


            // Get the clientData object with the new init data
            const ClientDataStruct *currentClientData = clientData.getClientData(clientID);
            std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = currentClientData->socket;

            // Prepare the response message
            nlohmann::json response;
            ResponseBuilder builder;

            // Check if the authentication is not successful
            if (clientID == 0)
            {
                // Add response data
                response = builder
                               .setHeader("message", "Spawning mobs failed!")
                               .setHeader("hash", "")
                               .setHeader("clientId", clientID)
                               .setHeader("eventType", "spawnMobsInZone")
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
                           .setHeader("message", "Spawning mobs success!")
                           .setHeader("hash", "")
                           .setHeader("clientId", currentClientData->clientId)
                           .setHeader("eventType", "spawnMobsInZone")
                           .setBody("spawnZoneData", spawnZoneDataJson)
                           .setBody("mobsData", mobsListJson)
                           .build();
            // Prepare a response message
            std::string responseData = networkManager_.generateResponseMessage("success", response);

            // Send the response to the client
            networkManager_.sendResponse(clientSocket, responseData);
        }
        else
        {
            logger_.log("Error with extracting data!");
        }
    }
    catch (const std::bad_variant_access &ex)
    {
        logger_.log("Error here: " + std::string(ex.what()));
    }
}

void EventHandler::handleGetSpawnZonesEvent(const Event &event, ClientData &clientData)
{
    //  TODO - Implement this method
}

void EventHandler::handleGetMobDataEvent(const Event &event, ClientData &clientData)
{
    //  TODO - Implement this method
}

void EventHandler::handleZoneMoveMobsEvent(const Event &event, ClientData &clientData)
{
    // get the data from the event
    const auto &data = event.getData();
    int clientID = event.getClientID();

    try
    {
        // Try to extract the data
        if (std::holds_alternative<std::vector<MobDataStruct>>(data))
        {
            std::vector<MobDataStruct> mobsList = std::get<std::vector<MobDataStruct>>(data);

            // format the mob data to json using for loop
            nlohmann::json mobsListJson;

            for (auto &mob : mobsList)
            {
                nlohmann::json mobJson;
                mobJson["mobId"] = mob.id;
                mobJson["mobUID"] = mob.uid;
                mobJson["mobZoneId"] = mob.zoneId;
                mobJson["mobName"] = mob.name;
                mobJson["mobRaceName"] = mob.raceName;
                mobJson["mobLevel"] = mob.level;
                mobJson["mobCurrentHealth"] = mob.currentHealth;
                mobJson["mobCurrentMana"] = mob.currentMana;
                mobJson["posX"] = mob.position.positionX;
                mobJson["posY"] = mob.position.positionY;
                mobJson["posZ"] = mob.position.positionZ;
                mobJson["rotZ"] = mob.position.rotationZ;

                for(auto &mobAttributeItem : mob.attributes)
                {
                    nlohmann::json mobItemJson;
                    mobItemJson["attributeId"] = mobAttributeItem.id;
                    mobItemJson["attributeName"] = mobAttributeItem.name;
                    mobItemJson["attributeSlug"] = mobAttributeItem.slug;
                    mobItemJson["attributeValue"] = mobAttributeItem.value;
                    mobJson["attributesData"].push_back(mobItemJson);
                }

                mobsListJson.push_back(mobJson);
            }

            // Get the clientData object with the new init data
            const ClientDataStruct *currentClientData = clientData.getClientData(clientID);
            std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = currentClientData->socket;

            // Prepare the response message
            nlohmann::json response;
            ResponseBuilder builder;

            // Check if the authentication is not successful
            if (clientID == 0)
            {
                // Add response data
                response = builder
                    .setHeader("message", "Moving mobs failed!")
                    .setHeader("hash", "")
                    .setHeader("clientId", clientID)
                    .setHeader("eventType", "zoneMoveMobs")
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
                .setHeader("message", "Moving mobs success!")
                .setHeader("hash", "")
                .setHeader("clientId", currentClientData->clientId)
                .setHeader("eventType", "zoneMoveMobs")
                .setBody("mobsData", mobsListJson)
                .build();

            // Prepare a response message
            std::string responseData = networkManager_.generateResponseMessage("success", response);

            // Send the response to the client
            networkManager_.sendResponse(clientSocket, responseData);
        }
        else
        {
            logger_.log("Error with extracting data!");
        }
    }
    catch (const std::bad_variant_access &ex)
    {
        logger_.log("Error here: " + std::string(ex.what()));
    }

}

void EventHandler::dispatchEvent(const Event& event, ClientData& clientData)
{
    switch (event.getType())
    {
    case Event::PING_CLIENT:
        handlePingClientEvent(event, clientData);
        break;
    case Event::JOIN_CHARACTER_CLIENT:
        handleJoinClientEvent(event, clientData);
        break;
    case Event::JOIN_CHARACTER_CHUNK:
        handleJoinChunkEvent(event, clientData);
        break;
    case Event::GET_CONNECTED_CHARACTERS_CHUNK:
        handleGetConnectedCharactersChunkEvent(event, clientData);
        break;
    case Event::GET_CONNECTED_CHARACTERS_CLIENT:
        handleGetConnectedCharactersClientEvent(event, clientData);
        break;
    case Event::MOVE_CHARACTER_CHUNK:
        handleMoveCharacterChunkEvent(event, clientData);
        break;
    case Event::MOVE_CHARACTER_CLIENT:
        handleMoveCharacterClientEvent(event, clientData);
        break;
    case Event::INTERACT:
        handleInteractChunkEvent(event, clientData);
        break;
    case Event::DISCONNECT_CLIENT:
        handleDisconnectClientEvent(event, clientData);
        break;
    case Event::DISCONNECT_CLIENT_CHUNK:
        handleDisconnectChunkEvent(event, clientData);
        break;
    case Event::SPAWN_MOBS_IN_ZONE:
        handleSpawnMobsInZoneEvent(event, clientData);
        break;
    case Event::GET_SPAWN_ZONES:
        handleGetSpawnZonesEvent(event, clientData);
        break;
    case Event::GET_MOB_DATA:
        handleGetMobDataEvent(event, clientData);
        break;
    case Event::ZONE_MOVE_MOBS:
        handleZoneMoveMobsEvent(event, clientData);
        break;
    
    }
}