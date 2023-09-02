#include "game_server/ClientData.hpp"
#include <iostream>

void ClientData::storeClientData(const ClientDataStruct& clientData) {
    // Assuming that clientDataMap_ is an unordered_map with the key as the hash and the value as ClientDataStruct.
    clientDataMap_[clientData.clientId] = clientData;
   // std::cout << "ClientDataStruct stored in ClientData class with hash = " << clientData.hash << std::endl;
}

const ClientDataStruct* ClientData::getClientData(const int& id) const {
    if(id == 0) {
        return nullptr;
    }
    // Assuming that clientDataMap_ is an unordered_map with the key as the hash and the value as ClientDataStruct.
    auto it = clientDataMap_.find(id);
    if (it != clientDataMap_.end()) {
        return &it->second;
    }
    return nullptr;
}