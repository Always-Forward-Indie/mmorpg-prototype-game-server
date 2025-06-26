#pragma once

#include <array>
#include <boost/asio.hpp>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>

#include "events/EventQueue.hpp"
#include "utils/JSONParser.hpp"
#include "utils/Logger.hpp"

class GameServer;
class EventDispatcher;  // ✅ Forward declare EventDispatcher
class MessageHandler;   // ✅ Forward declare MessageHandler

class ClientSession : public std::enable_shared_from_this<ClientSession>
{
   public:
    // Accept a shared_ptr to a tcp::socket instead of a raw socket.
    ClientSession(std::shared_ptr<boost::asio::ip::tcp::socket> socket,
        GameServer *gameServer,
        Logger &logger,
        EventQueue &eventQueue,
        EventQueue &eventQueuePing,
        JSONParser &jsonParser,
        EventDispatcher &eventDispatcher,
        MessageHandler &messageHandler);

    void start();

   private:
    void doRead();
    void processMessage(const std::string &message);
    void handleClientDisconnect();

    std::shared_ptr<boost::asio::ip::tcp::socket> socket_;
    std::array<char, 1024> dataBuffer_;
    std::string accumulatedData_;
    Logger &logger_;
    EventQueue &eventQueue_;
    EventQueue &eventQueuePing_;
    JSONParser &jsonParser_;
    GameServer *gameServer_;
    EventDispatcher &eventDispatcher_;
    MessageHandler &messageHandler_;
};
