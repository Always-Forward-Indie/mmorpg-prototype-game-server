#include "game_server/ClientData.hpp"
#include <iostream>

void ClientData::storeClientData(const ClientDataStruct& clientData) {
    // Assuming that clientDataMap_ is an unordered_map with the key as the hash and the value as ClientDataStruct.
    clientDataMap_[clientData.clientId] = clientData;
   // std::cout << "ClientDataStruct stored in ClientData class with hash = " << clientData.hash << std::endl;
}

//Update client data
void ClientData::updateClientData(const int& id, const std::string& field, const std::string& value) {
    // Assuming that clientDataMap_ is an unordered_map with the key as the hash and the value as ClientDataStruct.
    auto it = clientDataMap_.find(id);
    if (it != clientDataMap_.end()) {
        if(field == "characterId") {
            it->second.characterData.characterId = std::stoi(value);
        } else if(field == "characterLevel") {
            it->second.characterData.characterLevel = std::stoi(value);
        } else if(field == "characterName") {
            it->second.characterData.characterName = value;
        } else if(field == "characterClass") {
            it->second.characterData.characterClass = value;
        }
    }
}

//Update client character data with argument CharacterDataStruct
void ClientData::updateCharacterData(const int& id, const CharacterDataStruct& characterData) {
    // Assuming that clientDataMap_ is an unordered_map with the key as the hash and the value as ClientDataStruct.
    auto it = clientDataMap_.find(id);
    if (it != clientDataMap_.end()) {
        it->second.characterData = characterData;
    }
}

//Update client character position data with argument PositionStruct
void ClientData::updateCharacterPositionData(const int& id, const PositionStruct& positionData) {
    // Assuming that clientDataMap_ is an unordered_map with the key as the hash and the value as ClientDataStruct.
    auto it = clientDataMap_.find(id);
    if (it != clientDataMap_.end()) {
        it->second.characterData.characterPosition = positionData;
    }
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