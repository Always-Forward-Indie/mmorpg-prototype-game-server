#include "game_server/ChunkServerWorker.hpp"

    ChunkServerWorker::ChunkServerWorker() : io_context_(), socket_(io_context_) {
        // Initialize and establish the connection here (called only once)
        Config config;
        auto configs = config.parseConfig("config.json");
        short port = std::get<2>(configs).port;
        std::string host = std::get<2>(configs).host;

        boost::asio::ip::tcp::resolver resolver(io_context_);
        auto endpoints = resolver.resolve(host, std::to_string(port));

        boost::asio::async_connect(socket_, endpoints,
            [this](const boost::system::error_code& ec, boost::asio::ip::tcp::endpoint) {
                if (!ec) {
                    // Connection successful
                    std::cout << "Connected to Chunk Server!" << std::endl;
                } else {
                    // Handle connection error
                    std::cerr << "Error connecting to the server: " << ec.message() << std::endl;
                }
            });
    }

    void ChunkServerWorker::sendDataToChunkServer(const std::string& data, std::function<void(const boost::system::error_code&, std::size_t)> callback) {
        // Send data asynchronously using the existing socket
        boost::asio::async_write(socket_, boost::asio::buffer(data), callback);
    }

    void ChunkServerWorker::receiveDataFromChunkServer(std::function<void(const boost::system::error_code&, std::size_t)> callback) {
        // Receive data asynchronously using the existing socket
        boost::asio::streambuf receive_buffer;
        boost::asio::async_read_until(socket_, receive_buffer, '\n', callback);
    }

    // Close the connection when done
    void ChunkServerWorker::closeConnection() {
        socket_.close();
    }
