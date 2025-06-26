#include "services/ClientManager.hpp"

ClientManager::ClientManager(Logger& logger)
    : logger_(logger) {}

void ClientManager::setClientData(const ClientDataStruct& clientData) {
    std::unique_lock lock(mutex_);
    clientsMap_[clientData.clientId] = clientData;
    if (clientData.socket) {
        socketToClientId_[clientData.socket] = clientData.clientId;
    }
}

void ClientManager::setClientSocket(int clientID, std::shared_ptr<boost::asio::ip::tcp::socket> socket) {
    std::unique_lock lock(mutex_);
    auto it = clientsMap_.find(clientID);
    if (it != clientsMap_.end()) {
        it->second.socket = socket;
        socketToClientId_[socket] = clientID;
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
        }
        clientsMap_.erase(it);
    }
}

void ClientManager::removeClientDataBySocket(const std::shared_ptr<boost::asio::ip::tcp::socket>& socket) {
    std::unique_lock lock(mutex_);
    auto it = socketToClientId_.find(socket);
    if (it != socketToClientId_.end()) {
        clientsMap_.erase(it->second);
        socketToClientId_.erase(it);
    }
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
