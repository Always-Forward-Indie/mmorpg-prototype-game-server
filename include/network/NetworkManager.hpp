#pragma once
#include <array>
#include <string>
#include <boost/asio.hpp>
#include <nlohmann/json.hpp>
#include "data/DataStructs.hpp"
#include "utils/Logger.hpp"
#include "utils/Config.hpp"
#include "utils/JSONParser.hpp"
#include "events/EventQueue.hpp"
#include <thread>
#include <vector>
#include <memory>

class NetworkManager {
public:
    NetworkManager(EventQueue& eventQueue, EventQueue& eventQueuePing, std::tuple<DatabaseConfig, GameServerConfig, ChunkServerConfig>& configs, Logger &logger);
    ~NetworkManager();
    void startAccept();
    void startIOEventLoop();
    void sendResponse(std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket, const std::string &responseString);
    std::string generateResponseMessage(const std::string &status, const nlohmann::json &message);

private:
    static constexpr size_t max_length = 1024;
    boost::asio::io_context io_context_;
    boost::asio::ip::tcp::acceptor acceptor_;
    std::vector<std::thread> threadPool_;
    std::tuple<DatabaseConfig, GameServerConfig, ChunkServerConfig>& configs_;
    EventQueue& eventQueue_;
    EventQueue& eventQueuePing_;
    Logger& logger_;
    JSONParser jsonParser_;
};
