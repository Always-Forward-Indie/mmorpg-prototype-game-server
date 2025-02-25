#include "network/NetworkManager.hpp"
#include "network/ClientSession.hpp"

NetworkManager::NetworkManager(
    EventQueue &eventQueue, 
    EventQueue &eventQueuePing,
    std::tuple<DatabaseConfig, GameServerConfig, ChunkServerConfig>& configs, 
    Logger &logger)
    : acceptor_(io_context_), 
    logger_(logger), 
    configs_(configs), 
    jsonParser_(), 
    eventQueue_(eventQueue),
    eventQueuePing_(eventQueuePing)
{
    boost::system::error_code ec;

    short customPort = std::get<1>(configs).port;
    std::string customIP = std::get<1>(configs).host;
    short maxClients = std::get<1>(configs).max_clients;

    boost::asio::ip::tcp::endpoint endpoint(boost::asio::ip::address::from_string(customIP), customPort);
    acceptor_.open(endpoint.protocol(), ec);
    if (!ec)
    {
        logger_.log("Starting Game Server...", YELLOW);
        acceptor_.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true), ec);
        acceptor_.bind(endpoint, ec);
        acceptor_.listen(maxClients, ec);
    }
    if (ec)
    {
        logger_.logError("Error during server initialization: " + ec.message(), RED);
        return;
    }
    logger_.log("Game Server started on IP: " + customIP + ", Port: " + std::to_string(customPort), GREEN);
}

void NetworkManager::startAccept() {
    auto clientSocket = std::make_shared<boost::asio::ip::tcp::socket>(io_context_);
    acceptor_.async_accept(*clientSocket, [this, clientSocket](const boost::system::error_code &error) {
        if (!error) {
            boost::asio::ip::tcp::endpoint remoteEndpoint = clientSocket->remote_endpoint();
            std::string clientIP = remoteEndpoint.address().to_string();
            std::string portNumber = std::to_string(remoteEndpoint.port());
            logger_.log("New Client with IP: " + clientIP + " Port: " + portNumber + " - connected!", GREEN);
            // Pass the shared pointer to the ClientSession
            auto session = std::make_shared<ClientSession>(clientSocket, logger_, eventQueue_, eventQueuePing_, jsonParser_);
            session->start();
        }
        startAccept();
    });
}


void NetworkManager::startIOEventLoop() {
    logger_.log("Starting Game Server IO Context...", YELLOW);
    auto numThreads = std::thread::hardware_concurrency();
    for (size_t i = 0; i < numThreads; ++i) {
        threadPool_.emplace_back([this]() { io_context_.run(); });
    }
}

NetworkManager::~NetworkManager() {
    logger_.log("Network Manager destructor is called...", RED);
    acceptor_.close();
    io_context_.stop();
    for (auto &thread : threadPool_) {
        if (thread.joinable())
            thread.join();
    }
}

void NetworkManager::sendResponse(std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket, const std::string &responseString) {
    if (!clientSocket || !clientSocket->is_open()) {
        logger_.logError("Attempted write on closed or invalid socket.", RED);
        
        return;
    }

    boost::asio::async_write(*clientSocket, boost::asio::buffer(responseString),
        [this, clientSocket](const boost::system::error_code &error, size_t bytes_transferred) {
            if (error) {
                logger_.logError("Error during async_write: " + error.message(), RED);
                if (clientSocket->is_open()) {
                    boost::system::error_code close_ec;
                    clientSocket->close(close_ec);
                    if (close_ec) {
                        logger_.logError("Error closing socket after write failure: " + close_ec.message(), RED);
                    }
                }
            } else {
                logger_.log("Bytes sent: " + std::to_string(bytes_transferred), BLUE);
                boost::system::error_code ec;
                auto remoteEndpoint = clientSocket->remote_endpoint(ec);
                if (!ec) {
                    logger_.log("Data sent successfully to Client: " + remoteEndpoint.address().to_string() + ":" + std::to_string(remoteEndpoint.port()), BLUE);
                }
            }
        });
}


std::string NetworkManager::generateResponseMessage(const std::string &status, const nlohmann::json &message) {
    nlohmann::json response;
    std::string currentTimestamp = logger_.getCurrentTimestamp();
    response["header"] = message["header"];
    response["header"]["status"] = status;
    response["header"]["timestamp"] = currentTimestamp;
    response["header"]["version"] = "1.0";
    response["body"] = message["body"];

    std::string responseString = response.dump();
    logger_.log("Response generated: " + responseString, YELLOW);
    return responseString + "\n";
}
