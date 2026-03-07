#pragma once
#include <array>
#include <boost/asio.hpp>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "data/DataStructs.hpp"
#include "events/EventQueue.hpp"
#include "network/ClientSession.hpp"
#include "utils/Config.hpp"
#include "utils/JSONParser.hpp"
#include "utils/Logger.hpp"

class GameServer;
class EventDispatcher; // ✅ Forward declare EventDispatcher
class MessageHandler;  // ✅ Forward declare MessageHandler

class NetworkManager
{
  public:
    NetworkManager(EventQueue &eventQueue, EventQueue &eventQueuePing, std::tuple<DatabaseConfig, GameServerConfig> &configs, Logger &logger);
    ~NetworkManager();
    void startAccept();
    void startIOEventLoop();
    void sendResponse(std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket, const std::string &responseString);
    std::string generateResponseMessage(const std::string &status, const nlohmann::json &message);
    std::string generateResponseMessage(const std::string &status, const nlohmann::json &message, const TimestampStruct &timestamps);
    void setGameServer(GameServer *GameServer);

  private:
    // Per-socket write state: ensures async_write calls are serialised per socket
    // so that concurrent EventHandler threads never race on the same TCP connection.
    struct SocketWriteState
    {
        boost::asio::strand<boost::asio::io_context::executor_type> strand;
        std::queue<std::shared_ptr<const std::string>> writeQueue;
        bool writePending{false};
        explicit SocketWriteState(boost::asio::io_context &ctx)
            : strand(boost::asio::make_strand(ctx))
        {
        }
    };

    std::mutex socketStatesMutex_;
    std::unordered_map<boost::asio::ip::tcp::socket *, std::shared_ptr<SocketWriteState>> socketStates_;

    std::shared_ptr<SocketWriteState> getOrCreateSocketState(boost::asio::ip::tcp::socket *sock);
    void removeSocketState(boost::asio::ip::tcp::socket *sock);
    void doNextWrite(std::shared_ptr<boost::asio::ip::tcp::socket> socket,
        std::shared_ptr<SocketWriteState> state);

    static constexpr size_t max_length = 1024;
    boost::asio::io_context io_context_;
    boost::asio::ip::tcp::acceptor acceptor_;
    std::vector<std::thread> threadPool_;
    std::tuple<DatabaseConfig, GameServerConfig> &configs_;
    GameServer *gameServer_;
    EventQueue &eventQueue_;
    EventQueue &eventQueuePing_;
    Logger &logger_;
    std::shared_ptr<spdlog::logger> log_;
    JSONParser jsonParser_;

    // These are declared but NOT initialized here!
    std::unique_ptr<EventDispatcher> eventDispatcher_;
    std::unique_ptr<MessageHandler> messageHandler_;
};
