#pragma once

#include <boost/asio.hpp>
#include <array>
#include <string>
#include "Authenticator.hpp"
#include "ChunkServerWorker.hpp"


class GameServer {
public:
    GameServer(boost::asio::io_context& io_context, const std::string& customIP, short customPort, short maxClients);

private:
    static constexpr size_t max_length = 1024; // Define the appropriate value
    
    void startAccept();
    void handleClientData(std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket, const std::array<char, max_length>& dataBuffer, size_t bytes_transferred);
    void startReadingFromClient(std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket);
    void authenticateClient(std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket, const std::string& login, const std::string& password);
    void sendResponse(std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket, const std::string& responseString);
    std::string generateResponseMessage(const std::string& status, const std::string& message, const int& id);

    boost::asio::io_context& io_context_;
    boost::asio::ip::tcp::acceptor acceptor_;

    ClientData clientData_;
    Authenticator authenticator_;
    ChunkServerWorker chunkServerWorker_;
};
