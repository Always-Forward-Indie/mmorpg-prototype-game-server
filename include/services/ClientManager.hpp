#pragma once

#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>
#include <shared_mutex>
#include <memory>
#include <boost/asio.hpp>
#include "data/DataStructs.hpp"
#include "utils/Logger.hpp"

class ClientManager {
public:
    explicit ClientManager(Logger& logger);

    void setClientData(const ClientDataStruct& clientData);
    void setClientSocket(int clientID, std::shared_ptr<boost::asio::ip::tcp::socket> socket);

    ClientDataStruct getClientData(int clientID) const;
    std::shared_ptr<boost::asio::ip::tcp::socket> getClientSocket(int clientID) const;

    void removeClientData(int clientID);
    void removeClientDataBySocket(const std::shared_ptr<boost::asio::ip::tcp::socket>& socket);

    /// Refresh lastSeenMs (liveness). No-op for unknown ids. Called on ping.
    void touchClientData(int clientID);
    /// Evict entries silent longer than threshold (missed-FIN hygiene).
    /// Returns evicted ids. Conservative: call with a long threshold.
    std::vector<int> sweepSilentClients(int64_t silentThresholdMs);

    std::vector<ClientDataStruct> getClientsList() const;

private:
    static int64_t steadyNowMs();

    Logger& logger_;
    std::shared_ptr<spdlog::logger> log_;
    mutable std::shared_mutex mutex_;

    std::unordered_map<int, ClientDataStruct> clientsMap_;
    std::unordered_map<std::shared_ptr<boost::asio::ip::tcp::socket>, int> socketToClientId_;

    // Generation stamps (stale-disconnect protection, mirrors ChunkManager):
    // a late disconnect for an old socket must never evict a re-bound session.
    // socketGen_ maps socket -> (clientId, gen); genById_ holds the live gen.
    // Removal by socket proceeds only on generation match.
    std::unordered_map<std::shared_ptr<boost::asio::ip::tcp::socket>, std::pair<int, uint64_t>> socketGen_;
    std::unordered_map<int, uint64_t> genById_;
    uint64_t nextGen_ = 1;
};
