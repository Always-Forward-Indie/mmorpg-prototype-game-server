#include "game_server/ChunkServerWorker.hpp"

ChunkServerWorker::ChunkServerWorker(std::tuple<DatabaseConfig, GameServerConfig, ChunkServerConfig>& configs, Logger& logger) 
: io_context_chunk_(), 
chunk_socket_(io_context_chunk_), 
retry_timer_(io_context_chunk_), 
work_(boost::asio::make_work_guard(io_context_chunk_)),
logger_(logger)
{
    short port = std::get<2>(configs).port;
    std::string host = std::get<2>(configs).host;

    boost::asio::ip::tcp::resolver resolver(io_context_chunk_);
    auto endpoints = resolver.resolve(host, std::to_string(port));

    logger_.log("Connecting to the Chunk Server...", BLUE);

    connect(endpoints); // Start the connection attempt
}

void ChunkServerWorker::startIOEventLoop()
{
    logger_.log("Starting Chunk Server IO Context", BLUE);
    // Start io_service in a separate thread
    io_thread_ = std::thread([this]()
                             { io_context_chunk_.run(); });
}

// In your destructor, join the io_thread to ensure it's properly cleaned up
ChunkServerWorker::~ChunkServerWorker()
{
    logger_.logError("ChunkServerWorker destructor called");
    // Clean up
    work_.reset(); // Allow io_context to exit
    if (io_thread_.joinable())
    {
        io_thread_.join();
    }
    if (chunk_socket_.is_open())
    {
        chunk_socket_.close();
    }
}

void ChunkServerWorker::connect(boost::asio::ip::tcp::resolver::results_type endpoints)
{
    boost::asio::async_connect(chunk_socket_, endpoints,
                               [this, endpoints](const boost::system::error_code &ec, boost::asio::ip::tcp::endpoint)
                               {
                                   if (!ec)
                                   {
                                       // Connection successful
                                       logger_.log("Connected to the Chunk Server!", GREEN);
                                   }
                                   else
                                   {
                                       // Handle connection error
                                       logger_.logError("Error connecting to the Chunk Server: " + ec.message(), RED);
                                       logger_.log("Retrying connection in 5 seconds...", BLUE);
                                       retry_timer_.expires_after(std::chrono::seconds(5));
                                       retry_timer_.async_wait([this, endpoints](const boost::system::error_code &ec)
                                                               {
                        if (!ec) {
                            connect(endpoints); // Retry the connection attempt
                        } });
                                   }
                               });
}

void ChunkServerWorker::sendDataToChunkServer(const std::string &data)
{
    nlohmann::json response;
    response["status"] = "success";
    response["body"] = data;
    std::string responseString = response.dump();

    try
    {
        boost::asio::async_write(chunk_socket_, boost::asio::buffer(responseString),
                                 [this](const boost::system::error_code &error, size_t bytes_transferred)
                                 {
                                     {
                                         if (!error)
                                         {
                                         }
                                         else
                                         {
                                             // Handle error
                                             logger_.logError("Error in sending data to Chunk Server: " + error.message());
                                         }

                                         logger_.log("Data sent successfully to Chunk Server. Bytes transferred: " + std::to_string(bytes_transferred), GREEN);
                                     }
                                 });
    }
    catch (const std::exception &e)
    {
        logger_.logError("Exception: " + std::string(e.what()));
    }
}

void ChunkServerWorker::receiveDataFromChunkServer(std::function<void(const boost::system::error_code &, std::size_t)> callback)
{
    // Receive data asynchronously using the existing socket
    boost::asio::streambuf receive_buffer;
    boost::asio::async_read_until(chunk_socket_, receive_buffer, '\n', callback);
}

// Close the connection when done
void ChunkServerWorker::closeConnection()
{
    logger_.log("Closing connection to chunk server");
    chunk_socket_.close();
}
