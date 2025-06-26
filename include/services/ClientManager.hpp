#pragma once

#include <unordered_map>
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

    std::vector<ClientDataStruct> getClientsList() const;

private:
    Logger& logger_;
    mutable std::shared_mutex mutex_;

    std::unordered_map<int, ClientDataStruct> clientsMap_;
    std::unordered_map<std::shared_ptr<boost::asio::ip::tcp::socket>, int> socketToClientId_;
};
