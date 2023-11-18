#pragma once

#include <string>
#include <boost/asio.hpp>
#include <nlohmann/json.hpp>
#include "helpers/Config.hpp"
#include "helpers/TerminalColors.hpp"
#include "helpers/Logger.hpp"

class ChunkServerWorker {
private:
    boost::asio::io_context io_context_chunk_;
    boost::asio::ip::tcp::socket socket_;
    boost::asio::steady_timer retry_timer_;


public:
    std::thread io_thread_;
    ChunkServerWorker();
    ~ChunkServerWorker();
    void startIOThread();
    void sendDataToChunkServer(const std::string& data, Logger& logger);
    void receiveDataFromChunkServer(std::function<void(const boost::system::error_code&, std::size_t)> callback);
    void connect(boost::asio::ip::tcp::resolver::results_type endpoints);

    // Close the connection when done
    void closeConnection();
};
