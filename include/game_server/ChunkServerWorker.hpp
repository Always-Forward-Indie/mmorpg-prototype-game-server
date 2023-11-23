#pragma once

#include <string>
#include <boost/asio.hpp>
#include <nlohmann/json.hpp>
#include "helpers/Config.hpp"
#include "helpers/Logger.hpp"

class ChunkServerWorker {
private:
    boost::asio::io_context io_context_chunk_;
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> work_;
    boost::asio::ip::tcp::socket chunk_socket_;
    boost::asio::steady_timer retry_timer_;
    std::thread io_thread_;
    Logger& logger_;

public:
    ChunkServerWorker(std::tuple<DatabaseConfig, GameServerConfig, ChunkServerConfig>& configs, Logger& logger);
    ~ChunkServerWorker();
    void startIOEventLoop();
    void sendDataToChunkServer(const std::string &data);
    void receiveDataFromChunkServer();
    void connect(boost::asio::ip::tcp::resolver::results_type endpoints);

    // Close the connection when done
    void closeConnection();
};
