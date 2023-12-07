#pragma once

#include <iostream>
#include <vector>
#include <utils/Database.hpp>
#include <data/ClientData.hpp>
#include <utils/Logger.hpp>

class CharacterManager {
public:
    // Constructor
    CharacterManager(Logger& logger);

    // Method to get characters list
    std::vector<CharacterDataStruct> getCharactersList(Database& database, ClientData& clientData, int accountId);
    // Method to get a character
    CharacterDataStruct getBasicCharacterData(Database& database, ClientData& clientData, int accountId, int characterId);
    // Method to get a character position
    PositionStruct getCharacterPosition(Database& database, ClientData& clientData, int accountId, int characterId);

    // update character position in the database
    void updateCharacterPosition(Database& database, ClientData& clientData, int accountId, int characterId, PositionStruct &position);
    // update character data in the database
    void updateBasicCharacterData(Database& database, ClientData& clientData, int accountId, int characterId, CharacterDataStruct &characterData);
    // update characters data in the database
    void updateBasicCharactersData(Database& database, ClientData& clientData);

private:
    Logger& logger_;
};