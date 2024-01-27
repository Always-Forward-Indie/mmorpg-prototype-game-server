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
                           .setBody("characterPosX", currentClientData->characterData.characterPosition.positionX)
                           .setBody("characterPosY", currentClientData->characterData.characterPosition.positionY)
                           .setBody("characterPosZ", currentClientData->characterData.characterPosition.positionZ)
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
                           .setBody("characterPosX", passedClientData.characterData.characterPosition.positionX)
                           .setBody("characterPosY", passedClientData.characterData.characterPosition.positionY)
                           .setBody("characterPosZ", passedClientData.characterData.characterPosition.positionZ)
                           .build();
            // Prepare a response message
            std::string responseData = networkManager_.generateResponseMessage("success", response);

            //TODO - Implement Chunk ID ?? (maybe in the future) and send data only to the clients in the same chunk
            // Get all existing clients data as array
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
                           .setBody("characterPosX", passedClientData.characterData.characterPosition.positionX)
                           .setBody("characterPosY", passedClientData.characterData.characterPosition.positionY)
                           .setBody("characterPosZ", passedClientData.characterData.characterPosition.positionZ)
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

            //TODO - Do we need hash key here?
            // Add the message to the response
            response = builder
                           .setHeader("message", "Movement success for character!")
                           .setHeader("hash", "")
                           .setHeader("clientId", currentClientData->clientId)
                           .setHeader("eventType", "moveCharacter")
                           .setBody("characterId", currentClientData->characterData.characterId)
                           .setBody("characterPosX", passedClientData.characterData.characterPosition.positionX)
                           .setBody("characterPosY", passedClientData.characterData.characterPosition.positionY)
                           .setBody("characterPosZ", passedClientData.characterData.characterPosition.positionZ)
                           .build();
            // Prepare a response message
            std::string responseData = networkManager_.generateResponseMessage("success", response);

            //TODO - Implement Chunk ID ?? (maybe in the future) and send data only to the clients in the same chunk
            // Get all existing clients data as array
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

            //TODO - Implement Chunk ID ?? (maybe in the future) and send data only to the clients in the same chunk
            //TODO - Do we need hash key here?
            // Add the message to the response
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

            //TODO - Do we need hash key here?
            // Add the message to the response
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

void EventHandler::handleInteractChunkEvent(const Event &event, ClientData &clientData)
{
    //  TODO - Implement this method
}

void EventHandler::handleInteractClientEvent(const Event &event, ClientData &clientData)
{
    //  TODO - Implement this method
}


void EventHandler::dispatchEvent(const Event &event, ClientData &clientData)
{
    switch (event.getType())
    {
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
        // Other cases...
    }
}