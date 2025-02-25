#pragma once
#include <string>
#include <unordered_map>
#include <mutex>
#include "DataStructs.hpp"

class ClientData
{
public:
    ClientData();

    void storeClientData(const ClientDataStruct &clientData);
    void markClientUpdate(const int &id, const bool &dbNeedsUpdate);
    void markCharacterUpdate(const int &id, const bool &dbNeedsUpdate);
    void markPositionUpdate(const int &id, const bool &dbNeedsUpdate);
    void updateClientData(const int &id, const std::string &field, const std::string &value);
    void updateCharacterData(const int &id, const CharacterDataStruct &characterData);
    void updateCharacterPositionData(const int &id, const PositionStruct &positionData);
    const ClientDataStruct *getClientData(const int &id) const;
    std::unordered_map<int, ClientDataStruct> getClientsDataMap() const;
    void removeClientData(const int &id);
    void removeClientDataBySocket(const std::shared_ptr<boost::asio::ip::tcp::socket>& socket);

private:
    std::unordered_map<int, ClientDataStruct> clientDataMap_;
    mutable std::mutex clientDataMutex_; // mutex for each significant data segment if needed
};