    #include "network/NetworkManager.hpp"

    NetworkManager::NetworkManager(EventQueue& eventQueue, std::tuple<DatabaseConfig, GameServerConfig, ChunkServerConfig> &configs, Logger &logger)
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
        io_context_.run(); // Start the event loop
    }

    void NetworkManager::handleClientData(std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket,
                                      const std::array<char, max_length> &dataBuffer,
                                      size_t bytes_transferred) {

            std::string receivedData(dataBuffer.data(), bytes_transferred);
            logger_.log("Received data from Client: " + receivedData, YELLOW);

        try
        {
            // Parse the data received from the client using JSONParser
            std::string eventType = jsonParser_.parseEventType(dataBuffer, bytes_transferred);
            ClientDataStruct clientData = jsonParser_.parseClientData(dataBuffer, bytes_transferred);
            CharacterDataStruct characterData = jsonParser_.parseCharacterData(dataBuffer, bytes_transferred);
            PositionStruct positionData = jsonParser_.parsePositionData(dataBuffer, bytes_transferred);
            MessageStruct message = jsonParser_.parseMessage(dataBuffer, bytes_transferred);

            // Check if the type of request is joinGame
            if (eventType == "joinGame" && clientData.hash != "" && clientData.clientId != 0)
            {
                // Set the client data
                characterData.characterPosition = positionData;
                clientData.characterData = characterData;
                clientData.socket = clientSocket;

                // Create a new event where join to Chunk Server and push it to the queue
                Event joinToChunkEvent(Event::JOIN_CHARACTER_CHUNK, clientData.clientId, clientData, clientSocket);
                eventQueue_.push(joinToChunkEvent);
            }

            // Check if the type of request is moveCharacters
            if(eventType == "moveCharacter" && clientData.hash != "" && clientData.clientId != 0 && clientData.characterData.characterId != 0) {
                Event moveCharEvent(Event::MOVE_CHARACTER_CHUNK, clientData.clientId, clientData, clientSocket);
                eventQueue_.push(moveCharEvent);
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
                                            if (!ec) {
                                                // Successfully retrieved the remote endpoint
                                                std::string ipAddress = remoteEndpoint.address().to_string();
                                                std::string portNumber = std::to_string(remoteEndpoint.port());

                                                logger_.log("Bytes send: " + std::to_string(bytes_transferred), BLUE);
                                                logger_.log("Data send successfully to the Client: " + ipAddress + ", Port: " + portNumber, BLUE);
                                               
                                                // Now you can use ipAddress and portNumber as needed
                                            } else {
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
                                            if (!ec) {
                                                // Successfully retrieved the remote endpoint
                                                std::string ipAddress = remoteEndpoint.address().to_string();
                                                std::string portNumber = std::to_string(remoteEndpoint.port());

                                                logger_.log("Bytes received: " + std::to_string(bytes_transferred), YELLOW);
                                                logger_.log("Data received from Client IP address: " + ipAddress + ", Port: " + portNumber, YELLOW);
                                            } else {
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

        //logger_.log("Response generated: " + responseString, YELLOW);

        return responseString+ "\n";
    }