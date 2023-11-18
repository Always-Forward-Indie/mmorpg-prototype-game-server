#include "game_server/ChunkServerWorker.hpp"

    std::mutex consoleMutex;

    ChunkServerWorker::ChunkServerWorker() : io_context_chunk_(), socket_(io_context_chunk_), retry_timer_(io_context_chunk_) {
        // Initialize and establish the connection here (called only once)
        Config config;
        auto configs = config.parseConfig("config.json");
        short port = std::get<2>(configs).port;
        std::string host = std::get<2>(configs).host;

        boost::asio::ip::tcp::resolver resolver(io_context_chunk_);
        auto endpoints = resolver.resolve(host, std::to_string(port));

        std::cout << BLUE << "Connecting to the Chunk Server..." << RESET << std::endl;

        connect(endpoints); // Start the connection attempt
    }

    void ChunkServerWorker::startIOThread() {
        std::cout << "Thread started" << std::endl;
        io_context_chunk_.run();
        std::cout << "Thread finished" << std::endl;
    }

    // In your destructor, join the io_thread to ensure it's properly cleaned up
    ChunkServerWorker::~ChunkServerWorker() {
        std::cout << "ChunkServerWorker destructor called" << std::endl;

        if (io_thread_.joinable()) {
            io_context_chunk_.stop();
            io_thread_.join();
        }
    }

    void ChunkServerWorker::connect(boost::asio::ip::tcp::resolver::results_type endpoints) {
        boost::asio::async_connect(socket_, endpoints,
            [this, endpoints](const boost::system::error_code& ec, boost::asio::ip::tcp::endpoint) {
                if (!ec) {
                    // Connection successful
                    std::cout << GREEN << "Connected to the Chunk Server!" << RESET << std::endl;
                } else {
                    // Handle connection error
                    std::cerr << RED << "Error connecting to the Chunkserver: " << ec.message() << RESET << std::endl;
                    std::cout << BLUE << "Retrying connection in 5 seconds..." << RESET << std::endl;
                    retry_timer_.expires_after(std::chrono::seconds(5));
                    retry_timer_.async_wait([this, endpoints](const boost::system::error_code& ec) {
                        if (!ec) {
                            connect(endpoints); // Retry the connection attempt
                        }
                    });
                }
            });
    }

    void ChunkServerWorker::sendDataToChunkServer(const std::string& data, Logger& logger) {
        
          // Lock the mutex to ensure exclusive access to the console
    //std::lock_guard<std::mutex> lock(consoleMutex);

    logger.log("Sending data to chunk server: " + data);
   // std::cout << YELLOW << "Sending data to chunk server: " << data << RESET << std::endl;

    nlohmann::json response;

    response["status"] = "success";
    response["body"] = data;

    std::string responseString = response.dump();


    //TODO - Research and solve why I/O operations does not print to console
    // async_write operation
    boost::asio::async_write(socket_, boost::asio::buffer(responseString),
        [this, responseString, &logger](const boost::system::error_code &error, size_t bytes_transferred) {
            {
                // Lock the mutex to ensure exclusive access to the console
                //std::lock_guard<std::mutex> lock(consoleMutex);

                logger.log("Data sent successfully To The Chunk Server. Bytes transferred: " + std::to_string(bytes_transferred));
                //std::cout << GREEN << "Data sent successfully. Bytes transferred: " << bytes_transferred << RESET << std::endl;
                //std::cout << YELLOW << "Response sent to chunk server: " << responseString << RESET << std::endl;
                //callback(error, bytes_transferred);
            }
        });

    std::cout << YELLOW << "After sending data to chunk server" << RESET << std::endl;
    std::cout.flush(); // Flush the output to ensure it's immediately visible
        
        
        //io_context_.run();

        
        
        // std::cout << YELLOW << "Sending data to chunk server: " << data << RESET << std::endl;

        // nlohmann::json response;

        // response["status"] = "success";
        // response["body"] = data;

        // std::string responseString = response.dump();

        // boost::asio::async_write(socket_, boost::asio::buffer(responseString),
        //     [this, callback, responseString](const boost::system::error_code &error, size_t bytes_transferred)
        //     {
        //         if (!error)
        //         {
        //             std::cout << GREEN << "Data sent successfully. Bytes transferred: " << bytes_transferred << RESET << std::endl;
        //             std::cout << YELLOW << "Response sent to chunk server: " << responseString << RESET << std::endl;
        //             callback(error, bytes_transferred);
        //         }
        //         else
        //         {
        //             std::cerr << RED << "Error during async_write: " << error.message() << RESET << std::endl;
        //             // Handle the error (e.g., close the socket)
        //         }
        //     });

        //io_context_.run(); // Run the io_context to execute the async_write function
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
