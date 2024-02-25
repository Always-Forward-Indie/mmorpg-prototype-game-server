#include "network/NetworkManager.hpp"

NetworkManager::NetworkManager(EventQueue &eventQueue, std::tuple<DatabaseConfig, GameServerConfig, ChunkServerConfig> &configs, Logger &logger)
    : acceptor_(io_context_),
      logger_(logger),
      configs_(configs),
      jsonParser_(),
      eventQueue_(eventQueue)
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
        logger_.log("Starting Game Server...", YELLOW);
        acceptor_.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true), ec);
        acceptor_.bind(endpoint, ec);
        acceptor_.listen(maxClients, ec);
    }

    if (ec)
    {
        logger.logError("Error during server initialization: " + ec.message());
        return;
    }

    // Print IP address and port when the server starts
    logger_.log("Game Server started on IP: " + customIP + ", Port: " + std::to_string(customPort), GREEN);
}

void NetworkManager::startAccept()
{
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = std::make_shared<boost::asio::ip::tcp::socket>(io_context_);

    acceptor_.async_accept(*clientSocket, [this, clientSocket](const boost::system::error_code &error)
                           {
                if (!error) {
                    // Get the Client remote endpoint (IP address and port)
                    boost::asio::ip::tcp::endpoint remoteEndpoint = clientSocket->remote_endpoint();
                    std::string clientIP = remoteEndpoint.address().to_string();
                    std::string portNumber = std::to_string(remoteEndpoint.port());

                    // Print the Client IP address
                    logger_.log("New Client with IP: " + clientIP + " Port: " + portNumber + " - connected!", GREEN);
                    
                    // Start reading data from the client
                    startReadingFromClient(clientSocket);
                }

                // Continue accepting new connections even if there's an error
                startAccept(); });
}

void NetworkManager::startIOEventLoop()
{
    logger_.log("Starting Game Server IO Context...", YELLOW);

    // Start io_service in a separate thread
    networkManagerThread_ = std::thread([this]()
                                        { io_context_.run(); });
}

NetworkManager::~NetworkManager()
{
    logger_.log("Network Manager destructor is called...", RED);
    // Close the acceptor and all client sockets
    acceptor_.close();
    io_context_.stop();
    networkManagerThread_.join();
}

void NetworkManager::handleClientData(std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket,
                                      const std::array<char, max_length> &dataBuffer,
                                      size_t bytes_transferred)
{
    static std::string accumulatedData; // Buffer to accumulate data

    // Append new data to the accumulated buffer
    accumulatedData.append(dataBuffer.data(), bytes_transferred);

    // Check for the delimiter in the accumulated data
    std::string delimiter = "\r\n\r\n"; // Your chosen delimiter
    size_t delimiterPos;
    while ((delimiterPos = accumulatedData.find(delimiter)) != std::string::npos)
    { // Assuming '\r\n\r\n' is the delimiter
        // Extract one message up to the delimiter
        std::string message = accumulatedData.substr(0, delimiterPos);

        // Log the received message
        logger_.log("Received data from Client: " + message, YELLOW);

        // Process the message
        processMessage(clientSocket, message);

        // Erase processed message and delimiter from the buffer
        accumulatedData.erase(0, delimiterPos + 1); // +1 to remove the delimiter as well
    }
}

void NetworkManager::processMessage(std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket, const std::string &message)
{
    // Convert the message string to a buffer array for parsing
    std::array<char, max_length> messageBuffer;
    std::copy(message.begin(), message.end(), messageBuffer.begin());
    size_t messageLength = message.length();

    // Now we can use JSON parsing logic on the `messageBuffer` with `messageLength` bytes of data
    try
    {
        // Parse the data received from the client using JSONParser
        std::string eventType = jsonParser_.parseEventType(messageBuffer, messageLength);
        ClientDataStruct clientData = jsonParser_.parseClientData(messageBuffer, messageLength);
        CharacterDataStruct characterData = jsonParser_.parseCharacterData(messageBuffer, messageLength);
        PositionStruct positionData = jsonParser_.parsePositionData(messageBuffer, messageLength);
        MessageStruct message = jsonParser_.parseMessage(messageBuffer, messageLength);
        // Create a new events batch
        std::vector<Event> eventsBatch;


        // Define a constant for the batch size
        constexpr int BATCH_SIZE = 10;

        logger_.log("Event type: " + eventType, YELLOW);
        logger_.log("Client ID: " + std::to_string(clientData.clientId), YELLOW);
        logger_.log("Client hash: " + clientData.hash, YELLOW);
        logger_.log("Character ID: " + std::to_string(characterData.characterId), YELLOW);

        // Check if the type of request is joinGame
        if (eventType == "joinGame" && clientData.hash != "" && clientData.clientId != 0)
        {
            // Set the client data
            characterData.characterPosition = positionData;
            clientData.characterData = characterData;
            clientData.socket = clientSocket;

            // Create a new event where join to Chunk Server and push it to the queue
            Event joinToChunkEvent(Event::JOIN_CHARACTER_CHUNK, clientData.clientId, clientData, clientSocket);
            // eventQueue_.push(joinToChunkEvent);

            // Add the event to the current batch
            eventsBatch.push_back(joinToChunkEvent);

            // If the batch size has been reached, push the batch to the event queue
            if (eventsBatch.size() >= BATCH_SIZE)
            {
                eventQueue_.pushBatch(eventsBatch);
                // Clear the batch for the next set of events
                eventsBatch.clear();
            }
        }

        // Check if the type of request is getConnectedCharacters
        if (eventType == "getConnectedCharacters" && clientData.hash != "" && clientData.clientId != 0)
        {
            // Set the client data
            characterData.characterPosition = positionData;
            clientData.characterData = characterData;
            clientData.socket = clientSocket;

            // Create a new event where join to Chunk Server and push it to the queue
            Event getConnectedCharactersEvent(Event::GET_CONNECTED_CHARACTERS_CHUNK, clientData.clientId, clientData, clientSocket);
            // eventQueue_.push(getConnectedCharactersEvent);

            // Add the event to the current batch
            eventsBatch.push_back(getConnectedCharactersEvent);

            // If the batch size has been reached, push the batch to the event queue
            if (eventsBatch.size() >= BATCH_SIZE)
            {
                eventQueue_.pushBatch(eventsBatch);
                // Clear the batch for the next set of events
                eventsBatch.clear();
            }
        }

        // Check if the type of request is moveCharacters
        if (eventType == "moveCharacter" && clientData.hash != "" && clientData.clientId != 0 && characterData.characterId != 0)
        {
            // Set the client data
            characterData.characterPosition = positionData;
            clientData.characterData = characterData;
            clientData.socket = clientSocket;

            Event moveCharacterEvent(Event::MOVE_CHARACTER_CHUNK, clientData.clientId, clientData, clientSocket);
            // eventQueue_.push(moveCharacterEvent);

            // Add the event to the current batch
            eventsBatch.push_back(moveCharacterEvent);

            // If the batch size has been reached, push the batch to the event queue
            if (eventsBatch.size() >= BATCH_SIZE)
            {
                eventQueue_.pushBatch(eventsBatch);
                // Clear the batch for the next set of events
                eventsBatch.clear();
            }
        }

        // Check if the type of request is disconnectClient
        if (eventType == "disconnectClient" && clientData.hash != "" && clientData.clientId != 0)
        {
            // Set the client data
            characterData.characterPosition = positionData;
            clientData.characterData = characterData;
            clientData.socket = clientSocket;

            Event disconnectEvent(Event::DISCONNECT_CLIENT_CHUNK, clientData.clientId, clientData, clientSocket);
            // eventQueue_.push(disconnectEvent);

            // Add the event to the current batch
            eventsBatch.push_back(disconnectEvent);

            // If the batch size has been reached, push the batch to the event queue
            if (eventsBatch.size() >= BATCH_SIZE)
            {
                eventQueue_.pushBatch(eventsBatch);
                // Clear the batch for the next set of events
                eventsBatch.clear();
            }
        }

        // ping client
        if (eventType == "pingClient")
        {
            // Set the client data
            // characterData.characterPosition = positionData;
            // clientData.characterData = characterData;
            clientData.socket = clientSocket;

            Event pingEvent(Event::PING_CLIENT, clientData.clientId, clientData, clientSocket);
            // eventQueue_.push(pingEvent);

            // Add the event to the current batch
            eventsBatch.push_back(pingEvent);

            // If the batch size has been reached, push the batch to the event queue
            if (eventsBatch.size() >= BATCH_SIZE)
            {
                eventQueue_.pushBatch(eventsBatch);
                // Clear the batch for the next set of events
                eventsBatch.clear();
            }
        }

        // Push any remaining events in the batch to the event queue
        if (!eventsBatch.empty())
        {
            eventQueue_.pushBatch(eventsBatch);
        }
    }
    catch (const nlohmann::json::parse_error &e)
    {
        logger_.logError("JSON parsing error: " + std::string(e.what()), RED);
    }
}

void NetworkManager::sendResponse(std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket, const std::string &responseString)
{
    boost::asio::async_write(*clientSocket, boost::asio::buffer(responseString),
                             [this, clientSocket](const boost::system::error_code &error, size_t bytes_transferred)
                             {
                                 boost::system::error_code ec;
                                 boost::asio::ip::tcp::endpoint remoteEndpoint = clientSocket->remote_endpoint(ec);
                                 if (!ec)
                                 {
                                     // Successfully retrieved the remote endpoint
                                     std::string ipAddress = remoteEndpoint.address().to_string();
                                     std::string portNumber = std::to_string(remoteEndpoint.port());

                                     logger_.log("Bytes send: " + std::to_string(bytes_transferred), BLUE);
                                     logger_.log("Data send successfully to the Client: " + ipAddress + ", Port: " + portNumber, BLUE);

                                     // Now you can use ipAddress and portNumber as needed
                                 }
                                 else
                                 {
                                     // Handle error
                                 }

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

void NetworkManager::startReadingFromClient(std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket)
{
    auto dataBufferClient = std::make_shared<std::array<char, max_length>>();
    std::fill(dataBufferClient->begin(), dataBufferClient->end(), 0); // Clear the buffer

    clientSocket->async_read_some(boost::asio::buffer(*dataBufferClient),
                                  [this, clientSocket, dataBufferClient](const boost::system::error_code &error, size_t bytes_transferred)
                                  {
                                      boost::system::error_code ec;
                                      boost::asio::ip::tcp::endpoint remoteEndpoint = clientSocket->remote_endpoint(ec);
                                      if (!ec)
                                      {
                                          // Successfully retrieved the remote endpoint
                                          std::string ipAddress = remoteEndpoint.address().to_string();
                                          std::string portNumber = std::to_string(remoteEndpoint.port());

                                          logger_.log("Bytes received: " + std::to_string(bytes_transferred), YELLOW);
                                          logger_.log("Data received from Client IP address: " + ipAddress + ", Port: " + portNumber, YELLOW);
                                      }
                                      else
                                      {
                                          // Handle error
                                      }

                                      // start reading from the client
                                      if (!error)
                                      {
                                          // Data has been read successfully, handle it
                                          handleClientData(clientSocket, *dataBufferClient, bytes_transferred);

                                          // Continue reading from the client
                                          startReadingFromClient(clientSocket);
                                      }
                                      else if (error == boost::asio::error::eof)
                                      {

                                          // The client has closed the connection
                                          logger_.logError("Client disconnected gracefully.");

                                          // You can perform any cleanup or logging here if needed

                                          // Close the client socket
                                          clientSocket->close();
                                      }
                                      else if (error == boost::asio::error::operation_aborted)
                                      {
                                          // The read operation was canceled, likely due to the client disconnecting
                                          logger_.logError("Read operation canceled (client disconnected).");

                                          // You can perform any cleanup or logging here if needed

                                          // Close the client socket
                                          clientSocket->close();
                                      }
                                      else
                                      {
                                          // Handle other errors
                                          logger_.logError("Error during async_read_some: " + error.message());

                                          // You can also close the socket in case of other errors if needed
                                          clientSocket->close();
                                      }
                                  });
}

std::string NetworkManager::generateResponseMessage(const std::string &status, const nlohmann::json &message)
{
    nlohmann::json response;
    std::string currentTimestamp = logger_.getCurrentTimestamp();
    response["header"] = message["header"];
    response["header"]["status"] = status;
    response["header"]["timestamp"] = currentTimestamp;
    response["header"]["version"] = "1.0";
    response["body"] = message["body"];

    std::string responseString = response.dump();

    logger_.log("Response generated: " + responseString, YELLOW);

    return responseString + "\n";
}
