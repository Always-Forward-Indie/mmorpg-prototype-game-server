#pragma once

#include <boost/asio.hpp>
#include <array>
#include <string>
#include "Authenticator.hpp"
#include "ChunkServerWorker.hpp"
#include "CharacterManager.hpp"
#include "helpers/Database.hpp"
#include "helpers/Logger.hpp"


class GameServer {
public:
    GameServer(ChunkServerWorker& chunkServerWorker, std::tuple<DatabaseConfig, GameServerConfig, ChunkServerConfig>& configs, Logger& logger);
    void startIOEventLoop();

private:
    static constexpr size_t max_length = 1024; // Define the appropriate value
    
    void startAccept();
    void handleClientData(std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket, const std::array<char, max_length>& dataBuffer, size_t bytes_transferred);
    void startReadingFromClient(std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket);
    void joinGame(std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket, const std::string &hash, const int &characterId, const int &clientId);
    void sendResponse(std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket, const std::string& responseString);
    std::string generateResponseMessage(const std::string &status, const nlohmann::json &message);
    
    boost::asio::io_context io_context_;
    boost::asio::ip::tcp::acceptor acceptor_;

    ClientData clientData_;
    Authenticator authenticator_;
    CharacterManager characterManager_;
    Database database_;
    ChunkServerWorker& chunkServerWorker_; // Reference to the ChunkServerWorker instance
    Logger& logger_; // Reference to the Logger instance
    std::tuple<DatabaseConfig, GameServerConfig, ChunkServerConfig>& configs_;
};
