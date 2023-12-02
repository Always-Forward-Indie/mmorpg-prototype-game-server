#include "game_server/ChunkServerWorker.hpp"

ChunkServerWorker::ChunkServerWorker(EventQueue& eventQueue, NetworkManager& networkManager, std::tuple<DatabaseConfig, GameServerConfig, ChunkServerConfig> &configs, Logger &logger)
    : io_context_chunk_(),
    chunk_socket_(std::make_shared<boost::asio::ip::tcp::socket>(io_context_chunk_)),
    //chunk_socket_(io_context_chunk_),
    retry_timer_(io_context_chunk_),
    work_(boost::asio::make_work_guard(io_context_chunk_)),
    logger_(logger),
    networkManager_(networkManager),
    eventQueue_(eventQueue),
    jsonParser_()
{
    short port = std::get<2>(configs).port;
    std::string host = std::get<2>(configs).host;

    boost::asio::ip::tcp::resolver resolver(io_context_chunk_);
    auto endpoints = resolver.resolve(host, std::to_string(port));

    logger_.log("Connecting to the Chunk Server...", BLUE);

    connect(endpoints, retryCount); // Start the connection attempt
}

void ChunkServerWorker::startIOEventLoop()
{
    logger_.log("Starting Chunk Server IO Context", YELLOW);
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
    if (chunk_socket_->is_open())
    {
        chunk_socket_->close();
    }
}

void ChunkServerWorker::connect(boost::asio::ip::tcp::resolver::results_type endpoints, int retryCount = 0)
{
    boost::asio::async_connect(*chunk_socket_, endpoints,
        [this, endpoints, retryCount](const boost::system::error_code &ec, boost::asio::ip::tcp::endpoint)
        {
            if (!ec)
            {
                logger_.log("Connected to the Chunk Server!", GREEN);
                // Start reading data from Chunk server
                receiveDataFromChunkServer(); // Start the reading process
            }
            else
            {
                logger_.logError("Error connecting to the Chunk Server: " + ec.message(), RED);

                if (retryCount < MAX_RETRY_COUNT) // Define MAX_RETRY_COUNT as needed
                {
                    // Set the timer for retrying the connection
                    retry_timer_.expires_after(std::chrono::seconds(RETRY_TIMEOUT)); // Define RETRY_INTERVAL_SECONDS as needed
                    retry_timer_.async_wait([this, endpoints, retryCount](const boost::system::error_code &ec)
                    {
                        if (!ec)
                        {
                            // Retry the connection attempt to the Chunk Server with an incremented retryCount
                            logger_.log("Retrying connection to Chunk Server...", BLUE);
                            connect(endpoints, retryCount + 1); // Retry the connection
                        }
                    });
                }
                else
                {
                    logger_.logError("Max retry count reached. Exiting...", RED);
                    exit(1);
                }
            }
        });
}

void ChunkServerWorker::sendDataToChunkServer(const std::string &data)
{
    try
    {
        boost::asio::async_write(*chunk_socket_, boost::asio::buffer(data),
                                 [this](const boost::system::error_code &error, size_t bytes_transferred)
                                 {
                                     if (!error)
                                     {
                                         logger_.log("Data sent successfully to Chunk Server. Bytes transferred: " + std::to_string(bytes_transferred), GREEN);

                                         // Start reading data from Chunk server
                                        // receiveDataFromChunkServer();
                                     }
                                     else
                                     {
                                         logger_.logError("Error in sending data to Chunk Server: " + error.message());
                                     }
                                 });
    }
    catch (const std::exception &e)
    {
        logger_.logError("Exception: " + std::string(e.what()));
    }
}

void ChunkServerWorker::receiveDataFromChunkServer()
{
    auto dataBufferChunk = std::make_shared<std::array<char, 1024>>();

    chunk_socket_->async_read_some(boost::asio::buffer(*dataBufferChunk),
                                   [this, dataBufferChunk](const boost::system::error_code &ec, 
                                                            std::size_t bytes_transferred) {
                                      if (!ec)
                                      {
                                            boost::system::error_code ec;
                                            boost::asio::ip::tcp::endpoint remoteEndpoint = chunk_socket_->remote_endpoint(ec);
                                            if (!ec) {
                                                // Successfully retrieved the remote endpoint
                                                std::string ipAddress = remoteEndpoint.address().to_string();
                                                std::string portNumber = std::to_string(remoteEndpoint.port());


                                                logger_.log("Data received from Chunk IP address: " + ipAddress + ", Port: " + portNumber, GREEN);
                                                logger_.log("Bytes transferred: " + std::to_string(bytes_transferred), BLUE);
                                                // Now you can use ipAddress and portNumber as needed
                                            } else {
                                                // Handle error
                                            }

                                            std::string receivedData(dataBufferChunk->data(), bytes_transferred);
                                            logger_.log("Received data from Chunk Server: " + receivedData, BLUE);

                                            // Parse the data received from the client using JSONParser
                                            std::string eventType = jsonParser_.parseEventType(*dataBufferChunk, bytes_transferred);
                                            ClientDataStruct clientData = jsonParser_.parseClientData(*dataBufferChunk, bytes_transferred);
                                            CharacterDataStruct characterData = jsonParser_.parseCharacterData(*dataBufferChunk, bytes_transferred);
                                            PositionStruct positionData = jsonParser_.parsePositionData(*dataBufferChunk, bytes_transferred);
                                            MessageStruct message = jsonParser_.parseMessage(*dataBufferChunk, bytes_transferred);

                                            // Set the client data
                                            characterData.characterPosition = positionData;
                                            clientData.characterData = characterData;

                                            //ClientDataStruct currentClientData = *clientData.getClientData(initData.clientId);

                                            //TODO - need to pass correct one socket according client id (not chunk_socket_)
                                            
                                            // Create a new event where send recieved from Chunk data back to the Client and push it to the queue
                                            Event joinedClientEvent(Event::JOINED_CLIENT, clientData.clientId, clientData, chunk_socket_);
                                            eventQueue_.push(joinedClientEvent);

                                            // Continue reading from the server
                                            receiveDataFromChunkServer();
                                      }
                                      else
                                      {
                                          logger_.logError("Error in receiving data from Chunk Server: " + ec.message());
                                      }
                                  });
}

// Close the connection when done
void ChunkServerWorker::closeConnection()
{
    logger_.log("Closing connection to chunk server");
    chunk_socket_->close();
}