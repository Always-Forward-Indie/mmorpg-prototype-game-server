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
#include <unordered_set>
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
    void addActiveSession(std::shared_ptr<ClientSession> session);
    void removeActiveSession(std::shared_ptr<ClientSession> session);
    /// Reclaim idle per-socket write queues (owner-expired only). Called on
    /// session teardown (see removeActiveSession).
    void gcWriteQueues();
    size_t writeQueueCount() const;

  private:
    // Per-socket write state: ensures async_write calls are serialised per socket
    // so that concurrent EventHandler threads never race on the same TCP connection.
    struct SocketWriteState
    {
        boost::asio::strand<boost::asio::io_context::executor_type> strand;
        std::queue<std::shared_ptr<const std::string>> writeQueue;
        bool writePending{false};
        // Owner identity: a raw socket* key alone is unsafe (free+realloc may
        // reuse the address); a stale queue must never swallow a new socket.
        // Ported from login-server (CRITICAL-11) and chunk-server (v0.2.32).
        std::weak_ptr<boost::asio::ip::tcp::socket> owner;
        explicit SocketWriteState(boost::asio::io_context &ctx)
            : strand(boost::asio::make_strand(ctx))
        {
        }
    };

    // Mutable: writeQueueCount() is a const observer used by health logging.
    mutable std::mutex socketStatesMutex_;
    std::unordered_map<boost::asio::ip::tcp::socket *, std::shared_ptr<SocketWriteState>> socketStates_;

    std::shared_ptr<SocketWriteState> getOrCreateSocketState(
        const std::shared_ptr<boost::asio::ip::tcp::socket> &socket);
    void removeSocketState(boost::asio::ip::tcp::socket *sock,
        const std::shared_ptr<boost::asio::ip::tcp::socket> &expectedOwner);
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

    std::unordered_set<std::shared_ptr<ClientSession>> activeSessions_;
    std::mutex sessionsMutex_;
};
