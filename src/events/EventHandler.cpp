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

void EventHandler::handleJoinToChunkEvent(const Event &event, ClientData &clientData)
{
    // Here we will update the init data of the character when it's joined in the object and send it back to the game server
    // Retrieve the data from the event
    const auto data = event.getData();
    int clientID = event.getClientID();
    // std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();

    // Extract init data
    try
    {
        // Try to extract the data as a ClientDataStruct
        if (std::holds_alternative<ClientDataStruct>(data))
        {
            ClientDataStruct initData = std::get<ClientDataStruct>(data);
            // Save the clientData object with the new init data
            clientData.storeClientData(initData);

            // TODO - Review this code to save the data in the database
            // TODO - Maybe move this code to a separate method and run it in some time interval to save the data in the database
            // Get the character data from the database
            CharacterDataStruct characterData = characterManager_.getCharacterData(database_, clientData, clientID, initData.characterData.characterId);
            clientData.updateCharacterData(clientID, characterData);
            // Get the character position from the database
            PositionStruct characterPosition = characterManager_.getCharacterPosition(database_, clientData, clientID, initData.characterData.characterId);
            clientData.updateCharacterPositionData(clientID, characterPosition);

            // Prepare the response message
            nlohmann::json response;
            ResponseBuilder builder;

            // Check if the authentication is not successful
            if (initData.clientId == 0 || initData.hash == "")
            {
                // Add response data
                response = builder
                               .setHeader("message", "Authentication failed for user!")
                               .setHeader("hash", initData.hash)
                               .setHeader("clientId", initData.clientId)
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
                           .setHeader("hash", initData.hash)
                           .setHeader("clientId", initData.clientId)
                           .setHeader("eventType", "joinGame")
                           .setBody("characterId", initData.characterData.characterId)
                           .setBody("characterPosX", initData.characterData.characterPosition.positionX)
                           .setBody("characterPosY", initData.characterData.characterPosition.positionY)
                           .setBody("characterPosZ", initData.characterData.characterPosition.positionZ)
                           .build();
            // Prepare a response message
            std::string responseData = networkManager_.generateResponseMessage("success", response);

            // Send data to the the chunk server
            chunkServerWorker_.sendDataToChunkServer(responseData);
        }
        else
        {
            logger_.log("Error with extracting data!");
            // Handle the case where the data is not of type ClientDataStruct
            // This might be logging the error, throwing another exception, etc.
        }
    }
    catch (const std::bad_variant_access &ex)
    {
        logger_.log("Error here: " + std::string(ex.what()));
        // Handle the case where the data is not of type ClientDataStruct
        // This might be logging the error, throwing another exception, etc.
    }
}

void EventHandler::handleJoinedClientEvent(const Event &event, ClientData &clientData)
{
    // Here we will update the init data of the character when it's joined in the object and send it back to the clients in the chunk
    // Retrieve the data from the event
    const auto data = event.getData();
    int clientID = event.getClientID();

    // Extract init data
    try
    {
        // Try to extract the data as a ClientDataStruct
        if (std::holds_alternative<ClientDataStruct>(data))
        {
            ClientDataStruct initData = std::get<ClientDataStruct>(data);
            // Get the clientData object with the new init data
            ClientDataStruct currentClientData = *clientData.getClientData(initData.clientId);
            std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = currentClientData.socket;

            // Prepare the response message
            nlohmann::json response;
            ResponseBuilder builder;

            // Check if the authentication is not successful
            if (initData.clientId == 0 || initData.hash == "")
            {
                // Add response data
                response = builder
                               .setHeader("message", "Authentication failed for user!")
                               .setHeader("hash", initData.hash)
                               .setHeader("clientId", initData.clientId)
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
                           .setHeader("hash", initData.hash)
                           .setHeader("clientId", initData.clientId)
                           .setHeader("eventType", "joinGame")
                           .setBody("characterId", initData.characterData.characterId)
                           .setBody("characterPosX", initData.characterData.characterPosition.positionX)
                           .setBody("characterPosY", initData.characterData.characterPosition.positionY)
                           .setBody("characterPosZ", initData.characterData.characterPosition.positionZ)
                           .build();
            // Prepare a response message
            std::string responseData = networkManager_.generateResponseMessage("success", response);

            // Send the response to the Client
            networkManager_.sendResponse(clientSocket, responseData);

            // TODO - send response to all other clients in the chunk
        }
        else
        {
            logger_.log("Error with extracting data!");
            // Handle the case where the data is not of type ClientDataStruct
            // This might be logging the error, throwing another exception, etc.
        }
    }
    catch (const std::bad_variant_access &ex)
    {
        logger_.log("Error here: " + std::string(ex.what()));
        // Handle the case where the data is not of type ClientDataStruct
        // This might be logging the error, throwing another exception, etc.
    }
}

void EventHandler::handleMoveEvent(const Event &event, ClientData &clientData)
{
    // Here we will update the position of the character in the object and send it back to the game server

    // Retrieve the data from the event
    const auto &data = event.getData();
    int clientID = event.getClientID();
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();
    PositionStruct characterPosition;
    // Extract movement data
    try
    {
        // Try to extract the data as a PositionStruct
        characterPosition = std::get<PositionStruct>(data);
    }
    catch (const std::bad_variant_access &ex)
    {
        logger_.log("Error here: " + std::string(ex.what()));
        // Handle the case where the data is not of type PositionStruct
        // This might be logging the error, throwing another exception, etc.
    }

    // Update the clientData object with the new position
    clientData.updateCharacterPositionData(clientID, characterPosition);

    // TODO - Send the updated object back to the game server
}

void EventHandler::handleInteractEvent(const Event &event, ClientData &clientData)
{
    // Here we will update the interaction of the character in the object and send it back to the game server
    //  TODO - Implement this method
}

void EventHandler::dispatchEvent(const Event &event, ClientData &clientData)
{
    switch (event.getType())
    {
    case Event::JOINED_CLIENT:
        handleJoinedClientEvent(event, clientData);
        break;
    case Event::JOIN_TO_CHUNK:
        handleJoinToChunkEvent(event, clientData);
        break;
    case Event::MOVE:
        handleMoveEvent(event, clientData);
        break;
    case Event::INTERACT:
        handleInteractEvent(event, clientData);
        break;
        // Other cases...
    }
}