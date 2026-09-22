#include "services/ClientManager.hpp"

#include <spdlog/logger.h>

ClientManager::ClientManager(Logger& logger)
    : logger_(logger) {
    log_ = logger.getSystem("client");}

void ClientManager::setClientData(const ClientDataStruct& clientData) {
    std::unique_lock lock(mutex_);
    ClientDataStruct stored = clientData;
    stored.lastSeenMs = steadyNowMs();
    auto it = clientsMap_.find(stored.clientId);
    if (stored.socket && (it == clientsMap_.end() || it->second.socket != stored.socket)) {
        // (Re)bind: new link generation; a late disconnect for the old
        // socket must not evict this session (see removeClientDataBySocket).
        if (it != clientsMap_.end() && it->second.socket) {
            socketToClientId_.erase(it->second.socket);
            socketGen_.erase(it->second.socket);
        }
        const uint64_t gen = nextGen_++;
        genById_[stored.clientId] = gen;
        socketToClientId_[stored.socket] = stored.clientId;
        socketGen_[stored.socket] = {stored.clientId, gen};
    }
    clientsMap_[stored.clientId] = std::move(stored);
}

void ClientManager::setClientSocket(int clientID, std::shared_ptr<boost::asio::ip::tcp::socket> socket) {
    std::unique_lock lock(mutex_);
    auto it = clientsMap_.find(clientID);
    if (it != clientsMap_.end()) {
        if (it->second.socket) {
            socketToClientId_.erase(it->second.socket);
            socketGen_.erase(it->second.socket);
        }
        it->second.socket = socket;
        it->second.lastSeenMs = steadyNowMs();
        const uint64_t gen = nextGen_++;
        genById_[clientID] = gen;
        socketToClientId_[socket] = clientID;
        socketGen_[socket] = {clientID, gen};
    }
}

ClientDataStruct ClientManager::getClientData(int clientID) const {
    std::shared_lock lock(mutex_);
    auto it = clientsMap_.find(clientID);
    return (it != clientsMap_.end()) ? it->second : ClientDataStruct{};
}

std::shared_ptr<boost::asio::ip::tcp::socket> ClientManager::getClientSocket(int clientID) const {
    std::shared_lock lock(mutex_);
    auto it = clientsMap_.find(clientID);
    return (it != clientsMap_.end()) ? it->second.socket : nullptr;
}

void ClientManager::removeClientData(int clientID) {
    std::unique_lock lock(mutex_);
    auto it = clientsMap_.find(clientID);
    if (it != clientsMap_.end()) {
        if (it->second.socket) {
            socketToClientId_.erase(it->second.socket);
            socketGen_.erase(it->second.socket);
        }
        genById_.erase(clientID);
        clientsMap_.erase(it);
    }
}

void ClientManager::removeClientDataBySocket(const std::shared_ptr<boost::asio::ip::tcp::socket>& socket) {
    std::unique_lock lock(mutex_);
    auto genIt = socketGen_.find(socket);
    if (genIt == socketGen_.end()) {
        return;
    }
    const int id = genIt->second.first;
    const uint64_t gen = genIt->second.second;
    socketGen_.erase(genIt);
    socketToClientId_.erase(socket);
    // Generation check: a stale disconnect (previous generation) drops only
    // its own reverse entry, never the live re-bound session.
    const auto liveIt = genById_.find(id);
    if (liveIt != genById_.end() && liveIt->second == gen) {
        genById_.erase(liveIt);
        clientsMap_.erase(id);
    }
}

void ClientManager::touchClientData(int clientID) {
    std::unique_lock lock(mutex_);
    auto it = clientsMap_.find(clientID);
    if (it != clientsMap_.end()) {
        it->second.lastSeenMs = steadyNowMs();
    }
}

std::vector<int> ClientManager::sweepSilentClients(int64_t silentThresholdMs) {
    std::vector<int> removed;
    const int64_t now = steadyNowMs();
    std::unique_lock lock(mutex_);
    for (auto it = clientsMap_.begin(); it != clientsMap_.end();) {
        if (now - it->second.lastSeenMs >= silentThresholdMs) {
            const int id = it->first;
            if (it->second.socket) {
                socketToClientId_.erase(it->second.socket);
                socketGen_.erase(it->second.socket);
            }
            genById_.erase(id);
            it = clientsMap_.erase(it);
            removed.push_back(id);
            log_->warn("[ClientManager] sweep: silent client id={} removed", id);
        } else {
            ++it;
        }
    }
    return removed;
}

int64_t ClientManager::steadyNowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

std::vector<ClientDataStruct> ClientManager::getClientsList() const {
    std::shared_lock lock(mutex_);
    std::vector<ClientDataStruct> result;
    result.reserve(clientsMap_.size());
    for (const auto& [_, client] : clientsMap_) {
        result.push_back(client);
    }
    return result;
}
