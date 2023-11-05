// ClientData.hpp
#pragma once

#include <string>
#include <unordered_map>

struct PositionStruct
{
    std::string positionX;
    std::string positionY;
    std::string positionZ;
};

struct CharacterDataStruct
{
    int characterId;
    int characterLevel;
    std::string characterName;
    std::string characterClass;
    std::string characterRace;
    PositionStruct characterPosition;
};

struct ClientDataStruct
{
    int clientId;
    std::string hash;
    CharacterDataStruct characterData;
};

class ClientData {
public:
    void storeClientData(const ClientDataStruct& clientData);
    void updateClientData(const int& id, const std::string& field, const std::string& value);
    void updateCharacterData(const int& id, const CharacterDataStruct& characterData);
    void updateCharacterPositionData(const int& id, const PositionStruct& positionData);
    const ClientDataStruct* getClientData(const int& id) const;

private:
    std::unordered_map<int, ClientDataStruct> clientDataMap_;
};