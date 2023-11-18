#include "game_server/GameServer.hpp"
#include <iostream>
#include <nlohmann/json.hpp>

GameServer::GameServer(ChunkServerWorker &chunkServerWorker, std::tuple<DatabaseConfig, GameServerConfig, ChunkServerConfig> &configs, Logger &logger)
    : acceptor_(io_context_),
      clientData_(),
      authenticator_(),
      characterManager_(),
      database_(configs, logger),
      chunkServerWorker_(chunkServerWorker),
      logger_(logger),
      configs_(configs)
{
    boost::system::error_code ec;

    // Get the custom port and IP address from the configs
    short customPort = std::get<1>(configs).port;
    std::string customIP = std::get<1>(configs).host;
    short maxClients = std::get<1>(configs).max_clients;

    // Create an endpoint with the custom IP and port
    boost::asio::ip::tcp::endpoint endpoint(boost::asio::ip::address::from_string(customIP), customPort);

    // Open the acceptor and bind it to the endpoint
    acceptor_.open(endpoint.protocol(), ec);
    if (!ec)
    {
        acceptor_.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true), ec);
        acceptor_.bind(endpoint, ec);
        acceptor_.listen(maxClients, ec);
    }

    if (ec)
    {
        logger.logError("Error during server initialization: " + ec.message(), RED);
        return;
    }

    // Start accepting new connections
    startAccept();

    // Print IP address and port when the server starts
    logger_.log("Game Server started on IP: " + customIP + ", Port: " + std::to_string(customPort), GREEN);
}

void GameServer::startIOEventLoop()
{
    io_context_.run(); // Start the event loop
}

void GameServer::startAccept()
{
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = std::make_shared<boost::asio::ip::tcp::socket>(io_context_);
    acceptor_.async_accept(*clientSocket, [this, clientSocket](const boost::system::error_code &error)
                           {
        if (!error) {
            // Get the client's remote endpoint (IP address and port)
            boost::asio::ip::tcp::endpoint remoteEndpoint = clientSocket->remote_endpoint();
            std::string clientIP = remoteEndpoint.address().to_string();

            // Print the client's IP address
            logger_.log("New client with IP: " + clientIP + " connected!", GREEN);
            
            // Start reading data from the client
            startReadingFromClient(clientSocket);
        }

        // Continue accepting new connections even if there's an error
        startAccept(); });
}

void GameServer::handleClientData(std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket, const std::array<char, max_length> &dataBuffer, size_t bytes_transferred)
{
    try
    {
        nlohmann::json jsonData = nlohmann::json::parse(dataBuffer.data(), dataBuffer.data() + bytes_transferred);
        // Create a JSON object for the response
        nlohmann::json response;

        // Extract hash, login, type and password fields from the jsonData
        std::string type = jsonData["type"] != nullptr ? jsonData["type"] : "";
        std::string hash = jsonData["hash"] != nullptr ? jsonData["hash"] : "";

        // Check if the type of request is joinGame
        if (type == "joinGame")
        {
            int character_id = jsonData["characterId"] != nullptr ? jsonData["characterId"].get<int>() : 0;
            int client_id = jsonData["clientId"] != nullptr ? jsonData["clientId"].get<int>() : 0;

            joinGame(clientSocket, hash, character_id, client_id);
        }
    }
    catch (const nlohmann::json::parse_error &e)
    {
        logger_.logError("JSON parsing error: " + std::string(e.what()), RED);
    }
}

void GameServer::joinGame(std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket, const std::string &hash, const int &characterId, const int &clientId)
{
    // Authenticate the client
    int characterID = authenticator_.authenticate(database_, clientData_, hash, clientId);
    // Create a JSON object for the response
    nlohmann::json response;

    // Check if the authentication was successful
    if (characterID == 0)
    {
        // Add the message to the response
        response["message"] = "Authentication failed for user!";
        // Prepare a response message
        std::string responseData = generateResponseMessage("error", response, 0);
        // Send the response to the client
        sendResponse(clientSocket, responseData);
        return;
    }

    // Get the character data from the database
    CharacterDataStruct characterData = characterManager_.getCharacterData(database_, clientData_, clientId, characterId);
    // Set the character data in the clientData_ object
    characterManager_.setCharacterData(clientData_, clientId, characterData);
    // Get the character position from the database
    PositionStruct characterPosition = characterManager_.getCharacterPosition(database_, clientData_, clientId, characterId);
    // Set the character position in the clientData_ object
    characterManager_.setCharacterPosition(clientData_, clientId, characterPosition);

    // Get the current client data
    const ClientDataStruct *currentClientData = clientData_.getClientData(clientId);
    if (currentClientData == nullptr)
    {
        // Add the message to the response
        response["message"] = "Client data not found!";
        // Prepare a response message
        std::string responseData = generateResponseMessage("error", response, 0);
        // Send the response to the client
        sendResponse(clientSocket, responseData);
        return;
    }
    characterData = currentClientData->characterData;

    // Send data to the chunk server
    chunkServerWorker_.sendDataToChunkServer("Hello, Chunk Server!");

    // Add the message to the response
    response["message"] = "Authentication success for user!";
    response["hash"] = currentClientData->hash;
    response["clientId"] = clientId;
    response["characterId"] = characterData.characterId;
    response["characterPosX"] = characterData.characterPosition.positionX;
    response["characterPosY"] = characterData.characterPosition.positionY;
    response["characterPosZ"] = characterData.characterPosition.positionZ;
    // Prepare a response message
    std::string responseData = generateResponseMessage("success", response, clientId);
    // Send the response to the client
    sendResponse(clientSocket, responseData);
}

void GameServer::sendResponse(std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket, const std::string &responseString)
{
    boost::asio::async_write(*clientSocket, boost::asio::buffer(responseString),
                             [this, clientSocket](const boost::system::error_code &error, size_t bytes_transferred)
                             {
                                 logger_.log("Data sent successfully. Bytes transferred: " + std::to_string(bytes_transferred));

                                 if (!error)
                                 {
                                     // Response sent successfully, now start listening for the client's next message
                                     startReadingFromClient(clientSocket);
                                 }
                                 else
                                 {
                                     logger_.logError("Error during async_write: " + error.message(), RED);
                                 }
                             });
}

void GameServer::startReadingFromClient(std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket)
{
    auto dataBuffer = std::make_shared<std::array<char, max_length>>();
    clientSocket->async_read_some(boost::asio::buffer(*dataBuffer),
                                  [this, clientSocket, dataBuffer](const boost::system::error_code &error, size_t bytes_transferred)
                                  {
                                      if (!error)
                                      {
                                          // Data has been read successfully, handle it
                                          handleClientData(clientSocket, *dataBuffer, bytes_transferred);

                                          // Continue reading from the client
                                          startReadingFromClient(clientSocket);
                                      }
                                      else if (error == boost::asio::error::eof)
                                      {
                                        
                                          // The client has closed the connection
                                          logger_.logError("Client disconnected gracefully.", RED);

                                          // You can perform any cleanup or logging here if needed

                                          // Close the client socket
                                          clientSocket->close();
                                      }
                                      else if (error == boost::asio::error::operation_aborted)
                                      {
                                          // The read operation was canceled, likely due to the client disconnecting
                                          logger_.logError("Read operation canceled (client disconnected).", RED);
                                          
                                          // You can perform any cleanup or logging here if needed

                                          // Close the client socket
                                          clientSocket->close();
                                      }
                                      else
                                      {
                                          // Handle other errors
                                          logger_.logError("Error during async_read_some: " + error.message(), RED);
                                          
                                          // You can also close the socket in case of other errors if needed
                                          clientSocket->close();
                                      }
                                  });
}

std::string GameServer::generateResponseMessage(const std::string &status, const nlohmann::json &message, const int &id)
{
    nlohmann::json response;

    response["status"] = status;
    response["body"] = message;

    std::string responseString = response.dump();

    logger_.log("Response generated: " + responseString, YELLOW);

    return responseString;
}