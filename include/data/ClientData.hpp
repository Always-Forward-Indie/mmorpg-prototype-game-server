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
    void updateClientData(const int &id, const std::string &field, const std::string &value);
    void updateCharacterData(const int &id, const CharacterDataStruct &characterData);
    void updateCharacterPositionData(const int &id, const PositionStruct &positionData);
    const ClientDataStruct *getClientData(const int &id) const;

private:
    std::unordered_map<int, ClientDataStruct> clientDataMap_;
    mutable std::mutex clientDataMutex_; // mutex for each significant data segment if needed
};