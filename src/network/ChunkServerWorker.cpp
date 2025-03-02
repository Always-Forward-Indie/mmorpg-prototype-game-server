#include "network/ChunkServerWorker.hpp"
#include <chrono>
#include <cstdlib>

ChunkServerWorker::ChunkServerWorker(EventQueue &eventQueue,
                                     std::tuple<DatabaseConfig, GameServerConfig, ChunkServerConfig>& configs, Logger &logger)
    : io_context_chunk_(),
      work_(boost::asio::make_work_guard(io_context_chunk_)),
      chunk_socket_(std::make_shared<boost::asio::ip::tcp::socket>(io_context_chunk_)),
      retry_timer_(io_context_chunk_),
      eventQueue_(eventQueue),
      logger_(logger),
      jsonParser_()
{
    short port = std::get<2>(configs).port;
    std::string host = std::get<2>(configs).host;

    boost::asio::ip::tcp::resolver resolver(io_context_chunk_);
    auto endpoints = resolver.resolve(host, std::to_string(port));

    logger_.log("Connecting to the Chunk Server on IP: " + host + " Port: " + std::to_string(port), YELLOW);
    connect(endpoints, 0); // Start connection to the Chunk Server
}

void ChunkServerWorker::startIOEventLoop() {
    logger_.log("Starting Chunk Server IO Context...", YELLOW);
    auto numThreads = std::max(1u, std::thread::hardware_concurrency());
    for (size_t i = 0; i < numThreads; ++i) {
        io_threads_.emplace_back([this]() { io_context_chunk_.run(); });
    }
}

ChunkServerWorker::~ChunkServerWorker() {
    logger_.logError("Chunk Server destructor is called...", RED);
    work_.reset();
    for (auto &thread : io_threads_) {
        if (thread.joinable())
            thread.join();
    }
    if (chunk_socket_->is_open())
        chunk_socket_->close();
}

void ChunkServerWorker::connect(boost::asio::ip::tcp::resolver::results_type endpoints, int currentRetryCount) {
    boost::asio::async_connect(*chunk_socket_, endpoints,
        [this, endpoints, currentRetryCount](const boost::system::error_code &ec, boost::asio::ip::tcp::endpoint) {
            if (!ec) {
                logger_.log("Connected to the Chunk Server!", GREEN);
                receiveDataFromChunkServer(); // Receive data from the Chunk Server
            } else {
                logger_.logError("Error connecting to the Chunk Server: " + ec.message());
                if (currentRetryCount < MAX_RETRY_COUNT) {
                    // Exponential backoff for retrying connection
                    int waitTime = RETRY_TIMEOUT * (1 << currentRetryCount);
                    retry_timer_.expires_after(std::chrono::seconds(waitTime));
                    retry_timer_.async_wait([this, endpoints, currentRetryCount](const boost::system::error_code &ecTimer) {
                        if (!ecTimer) {
                            logger_.log("Retrying connection to Chunk Server...", YELLOW);
                            connect(endpoints, currentRetryCount + 1);
                        }
                    });
                } else {
                    logger_.logError("Max retry count reached. Exiting...");
                    exit(1);
                }
            }
        });
}

void ChunkServerWorker::sendDataToChunkServer(const std::string &data) {
    try {
        boost::asio::async_write(*chunk_socket_, boost::asio::buffer(data),
            [this](const boost::system::error_code &error, size_t bytes_transferred) {
                if (!error) {
                    logger_.log("Bytes sent: " + std::to_string(bytes_transferred), BLUE);
                    logger_.log("Data sent successfully to the Chunk Server", BLUE);
                } else {
                    logger_.logError("Error in sending data to Chunk Server: " + error.message());
                }
            });
    } catch (const std::exception &e) {
        logger_.logError("Exception in sendDataToChunkServer: " + std::string(e.what()));
    }
}

void ChunkServerWorker::processChunkData(const std::array<char, 1024>& buffer, std::size_t bytes_transferred) {
    // Convert received data to string
    std::string receivedData(buffer.data(), bytes_transferred);
    logger_.log("Received data from Chunk Server: " + receivedData, YELLOW);

    // Parse JSON data
    std::string eventType = jsonParser_.parseEventType(buffer, bytes_transferred);
    ClientDataStruct clientData = jsonParser_.parseClientData(buffer, bytes_transferred);
    CharacterDataStruct characterData = jsonParser_.parseCharacterData(buffer, bytes_transferred);
    PositionStruct positionData = jsonParser_.parsePositionData(buffer, bytes_transferred);
    nlohmann::json charactersList = jsonParser_.parseCharactersList(buffer, bytes_transferred);

    std::vector<Event> eventsBatch;
    constexpr int BATCH_SIZE = 10;

    // Update character data
    characterData.characterPosition = positionData;
    clientData.characterData = characterData;

    if (eventType == "joinGame" && !clientData.hash.empty() && clientData.clientId != 0) {
        Event joinedClientEvent(Event::JOIN_CHARACTER_CLIENT, clientData.clientId, clientData, chunk_socket_);
        eventsBatch.push_back(joinedClientEvent);
    }
    if (eventType == "getConnectedCharacters" && clientData.clientId != 0) {
        Event getConnectedCharactersEvent(Event::GET_CONNECTED_CHARACTERS_CLIENT, clientData.clientId, charactersList, chunk_socket_);
        eventsBatch.push_back(getConnectedCharactersEvent);
    }
    if (eventType == "moveCharacter" && clientData.clientId != 0 && characterData.characterId != 0) {
        Event moveCharacterEvent(Event::MOVE_CHARACTER_CLIENT, clientData.clientId, clientData, chunk_socket_);
        eventsBatch.push_back(moveCharacterEvent);
    }
    if (eventType == "disconnectClient" && clientData.clientId != 0) {
        Event disconnectClientEvent(Event::DISCONNECT_CLIENT, clientData.clientId, clientData, chunk_socket_);
        eventsBatch.push_back(disconnectClientEvent);
    }
    if (!eventsBatch.empty()) {
        eventQueue_.pushBatch(eventsBatch);
    }
}

void ChunkServerWorker::receiveDataFromChunkServer() {
    auto dataBufferChunk = std::make_shared<std::array<char, 1024>>();
    chunk_socket_->async_read_some(boost::asio::buffer(*dataBufferChunk),
        [this, dataBufferChunk](const boost::system::error_code &ec, std::size_t bytes_transferred) {
            if (!ec) {
                boost::system::error_code ecEndpoint;
                boost::asio::ip::tcp::endpoint remoteEndpoint = chunk_socket_->remote_endpoint(ecEndpoint);
                if (!ecEndpoint) {
                    std::string ipAddress = remoteEndpoint.address().to_string();
                    std::string portNumber = std::to_string(remoteEndpoint.port());
                    logger_.log("Bytes received: " + std::to_string(bytes_transferred), YELLOW);
                    logger_.log("Data received from Chunk Server - IP: " + ipAddress + " Port: " + portNumber, YELLOW);
                }
                // Process received data
                processChunkData(*dataBufferChunk, bytes_transferred);
                // Continue reading data from the Chunk Server
                receiveDataFromChunkServer();
            } else {
                logger_.logError("Error in receiving data from Chunk Server: " + ec.message());
            }
        });
}

void ChunkServerWorker::closeConnection() {
    if (chunk_socket_->is_open()) {
        chunk_socket_->close();
    }
}
