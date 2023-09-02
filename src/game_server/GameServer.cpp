#include "game_server/GameServer.hpp"
#include <iostream>
#include <nlohmann/json.hpp>

GameServer::GameServer(boost::asio::io_context& io_context, const std::string& customIP, short customPort, short maxClients)
    : io_context_(io_context),
      acceptor_(io_context),
      clientData_(),
      authenticator_(),
      chunkServerWorker_() {
    boost::system::error_code ec;

    //@TODO Integrate communication ChunkServerWorker into GameServer
    

    // Create an endpoint with the custom IP and port
    boost::asio::ip::tcp::endpoint endpoint(boost::asio::ip::address::from_string(customIP), customPort);

    acceptor_.open(endpoint.protocol(), ec);
    if (!ec) {
        acceptor_.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true), ec);
        acceptor_.bind(endpoint, ec);
        acceptor_.listen(maxClients, ec);
    }

    if (ec) {
        std::cerr << "Error during server initialization: " << ec.message() << std::endl;
        return;
    }

    startAccept();

    // Print IP address and port when the server starts
    std::cout << "Game Server started on IP: " << customIP << ", Port: " << customPort << std::endl;
}

void GameServer::startAccept() {
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = std::make_shared<boost::asio::ip::tcp::socket>(io_context_);
    acceptor_.async_accept(*clientSocket, [this, clientSocket](const boost::system::error_code& error) {
        if (!error) {
            // Get the client's remote endpoint (IP address and port)
            boost::asio::ip::tcp::endpoint remoteEndpoint = clientSocket->remote_endpoint();
            std::string clientIP = remoteEndpoint.address().to_string();

            // Print the client's IP address
            std::cout << "New client with IP: " << clientIP << " connected!" << std::endl;

            // Start reading data from the client
            startReadingFromClient(clientSocket);
        }

        // Continue accepting new connections even if there's an error
        startAccept();
    });
}

void GameServer::handleClientData(std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket, const std::array<char, max_length>& dataBuffer, size_t bytes_transferred) {
    try {
        nlohmann::json jsonData = nlohmann::json::parse(dataBuffer.data(), dataBuffer.data() + bytes_transferred);

        // Extract hash, login, type and password fields from the jsonData
        std::string type = jsonData["type"];
        std::string hash = jsonData["hash"];
        std::string login = jsonData["login"];
        int character_id = jsonData["character_id"];
        int id = jsonData["id"];

        // Authenticate the client
        int clientID = authenticator_.authenticate(login, id, character_id, hash, clientData_);

        if (id != 0) {
            // Define a callback function to handle the completion of the send operation
            auto sendToChunkCallback = [](const boost::system::error_code& error, std::size_t bytes_transferred) {
                if (!error) {
                    std::cout << "Data sent successfully to chunk server." << std::endl;
                } else {
                    std::cerr << "Error sending data to chunk server: " << error.message() << std::endl;
                }
            };

            //authenticator_.ChunkServerWorker(id, clientData_);
            chunkServerWorker_.sendDataToChunkServer("Hello, Chunk Server!\n", sendToChunkCallback);

            // Authentication successful, send a success response back to the client
            std::cerr << "Authentication success for user: " << login << std::endl;
            // Create a response message
            std::string responseData = generateResponseMessage("success", "Authentication successful", id);
            // Send the response to the client
            sendResponse(clientSocket, responseData);
        } else {
            // Authentication failed for the client
            std::cerr << "Authentication failed for user: " << login << std::endl;
            // Create a response message
            std::string responseData = generateResponseMessage("error", "Authentication failed", 0);
            // Send the response to the client
            sendResponse(clientSocket, responseData);
        }
    } catch (const nlohmann::json::parse_error& e) {
        std::cerr << "JSON parsing error: " << e.what() << std::endl;
        // Handle the error (e.g., close the socket)
    }
}

void GameServer::sendResponse(std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket, const std::string& responseString) {
    boost::asio::async_write(*clientSocket, boost::asio::buffer(responseString),
                             [this, clientSocket](const boost::system::error_code& error, size_t bytes_transferred) {
                                 if (!error) {
                                     // Response sent successfully, now start listening for the client's next message
                                     startReadingFromClient(clientSocket);
                                 } else {
                                     std::cerr << "Error during async_write: " << error.message() << std::endl;
                                     // Handle the error (e.g., close the socket)
                                 }
                             });
}

void GameServer::startReadingFromClient(std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket) {
    auto dataBuffer = std::make_shared<std::array<char, max_length>>();
    clientSocket->async_read_some(boost::asio::buffer(*dataBuffer),
        [this, clientSocket, dataBuffer](const boost::system::error_code& error, size_t bytes_transferred) {
            if (!error) {
                // Data has been read successfully, handle it
                handleClientData(clientSocket, *dataBuffer, bytes_transferred);

                // Continue reading from the client
                startReadingFromClient(clientSocket);
            } else if (error == boost::asio::error::eof) {
                // The client has closed the connection
                std::cerr << "Client disconnected gracefully." << std::endl;

                // You can perform any cleanup or logging here if needed

                // Close the client socket
                clientSocket->close();
            } else if (error == boost::asio::error::operation_aborted) {
                // The read operation was canceled, likely due to the client disconnecting
                std::cerr << "Read operation canceled (client disconnected)." << std::endl;

                // You can perform any cleanup or logging here if needed

                // Close the client socket
                clientSocket->close();
            } else {
                // Handle other errors
                std::cerr << "Error during async_read_some: " << error.message() << std::endl;

                // You can also close the socket in case of other errors if needed
                clientSocket->close();
            }
        });
}

std::string GameServer::generateResponseMessage(const std::string& status, const std::string& message, const int& id) {
    nlohmann::json response;

    response["status"] = status;
    response["message"] = message;

    const ClientDataStruct* currentClientData = clientData_.getClientData(id);
    if (currentClientData) {
        // Access members only if the pointer is not null
            std::cout << "Client ID: " << currentClientData->clientId << std::endl;
            std::cout << "Client hash: " << currentClientData->hash << std::endl;
            std::cout << "Client character ID: " << currentClientData->characterId << std::endl;
            std::cout << "Client character level: " << currentClientData->characterLevel << std::endl;
            std::cout << "Client character name: " << currentClientData->characterName << std::endl;

            response["clientId"] = currentClientData->clientId;
            response["hash"] = currentClientData->hash;
            response["characterId"] = currentClientData->characterId;
            response["characterLevel"] = currentClientData->characterLevel;
            response["characterName"] = currentClientData->characterName;
    } else {
        // Handle the case where the pointer is null (e.g., log an error)
        std::cerr << "Client data not found for id: " << id << std::endl;
    }

    std::string responseString = response.dump();

    return responseString;
}