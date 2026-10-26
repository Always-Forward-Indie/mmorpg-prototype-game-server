#pragma once

#include <array>
#include <boost/asio.hpp>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>

#include "events/EventQueue.hpp"
#include "utils/Logger.hpp"
#include "utils/ProtocolLimits.hpp"
#include "utils/WireFraming.hpp"

#include <functional>

class EventDispatcher; // ✅ Forward declare EventDispatcher
class MessageHandler;  // ✅ Forward declare MessageHandler

class ClientSession : public std::enable_shared_from_this<ClientSession>
{
  public:
    // Accept a shared_ptr to a tcp::socket instead of a raw socket.
    // (No GameServer link: the session only needs queues + dispatcher.)
    ClientSession(std::shared_ptr<boost::asio::ip::tcp::socket> socket,
        Logger &logger,
        EventQueue &eventQueue,
        EventQueue &eventQueuePing,
        EventDispatcher &eventDispatcher,
        MessageHandler &messageHandler);

    void start();
    void setDisconnectCallback(std::function<void(std::shared_ptr<ClientSession>)> callback);
    /// Strand-safe error path: NetworkManager installs this at session setup
    /// so framing failures (oversize/malformed) get an explicit wire ERR_*
    /// instead of a silent drop. Goes through the per-socket strand queue
    /// (direct async_write here would race with in-flight writes = Asio UB).
    /// Same lifetime pattern as disconnectCallback_ (NetworkManager outlives sessions).
    void setSendCallback(std::function<void(std::shared_ptr<boost::asio::ip::tcp::socket>, const std::string &)> callback);
    /// Owning socket (for write-state reclamation in NetworkManager).
    std::shared_ptr<boost::asio::ip::tcp::socket> socket() const { return socket_; }
    /// Transport mode reporter (S14): NetworkManager installs this at session
    /// setup (mirrors setSendCallback); doRead fires it once, right after
    /// the mode is sniffed from the first bytes.
    void setFramingCallback(std::function<void(std::shared_ptr<boost::asio::ip::tcp::socket>, bool)> callback);

  private:
    void doRead();
    void processMessage(const std::string &message);
    void handleClientDisconnect();
    // v2 extraction (S14): length-prefixed frames from accumulatedData_.
    void extractV2Messages();

    // Transport mode, sniffed once from the first bytes (S14). Decided
    // before any message is processed; never changes mid-session.
    wirev2::FramingMode framingMode_ = wirev2::FramingMode::NeedMore;
    std::function<void(std::shared_ptr<boost::asio::ip::tcp::socket>, bool)> framingCallback_;

    std::shared_ptr<boost::asio::ip::tcp::socket> socket_;
    std::array<char, protocol::kReadChunkBytes> dataBuffer_;
    std::string accumulatedData_;
    Logger &logger_;
    std::shared_ptr<spdlog::logger> log_;
    EventQueue &eventQueue_;
    EventQueue &eventQueuePing_;
    EventDispatcher &eventDispatcher_;
    MessageHandler &messageHandler_;
    std::function<void(std::shared_ptr<ClientSession>)> disconnectCallback_;
    std::function<void(std::shared_ptr<boost::asio::ip::tcp::socket>, const std::string &)> sendCallback_;
};
