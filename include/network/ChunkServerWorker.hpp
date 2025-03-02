#pragma once

#include <string>
#include <vector>
#include <thread>
#include <memory>
#include <array>
#include <boost/asio.hpp>
#include <nlohmann/json.hpp>
#include "utils/Config.hpp"
#include "utils/Logger.hpp"
#include "events/EventQueue.hpp"
#include "network/NetworkManager.hpp"
#include "utils/JSONParser.hpp"

class ChunkServerWorker {
private:
    boost::asio::io_context io_context_chunk_;
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> work_;
    std::shared_ptr<boost::asio::ip::tcp::socket> chunk_socket_;
    boost::asio::steady_timer retry_timer_;
    std::vector<std::thread> io_threads_;
    EventQueue &eventQueue_;
    Logger &logger_;
    int retryCount = 0;
    static constexpr int MAX_RETRY_COUNT = 5;
    static constexpr int RETRY_TIMEOUT = 5;
    JSONParser jsonParser_;

    // Process received data from the Chunk Server
    void processChunkData(const std::array<char, 1024>& buffer, std::size_t bytes_transferred);

public:
    ChunkServerWorker(EventQueue &eventQueue,
                        std::tuple<DatabaseConfig, GameServerConfig, ChunkServerConfig>& configs, Logger &logger);
    ~ChunkServerWorker();
    void startIOEventLoop();
    void sendDataToChunkServer(const std::string &data);
    void receiveDataFromChunkServer();
    void connect(boost::asio::ip::tcp::resolver::results_type endpoints, int currentRetryCount = 0);
    void closeConnection();
};
