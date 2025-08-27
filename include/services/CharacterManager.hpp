#pragma once

#include <data/DataStructs.hpp>
#include <iostream>
#include <shared_mutex>
#include <utils/Database.hpp>
#include <utils/Logger.hpp>
#include <vector>

class CharacterManager
{
  public:
    CharacterManager(Logger &logger);

    // Runtime access
    void addOrUpdateCharacter(const CharacterDataStruct &character);
    CharacterDataStruct getCharacterById(int characterId);
    void removeCharacter(int characterId);
    std::vector<CharacterDataStruct> getAllCharacters();
    bool hasCharacter(int characterId);

    // Load from DB and store into map
    CharacterDataStruct loadCharacterFromDatabase(Database &db, int accountId, int characterId);

    // Save updated data to DB
    void saveCharacterToDatabase(Database &db, const CharacterDataStruct &character);

    // Partial DB sync (optional direct DB access)
    PositionStruct getCharacterPositionFromDatabase(Database &db, int accountId, int characterId);
    std::vector<CharacterAttributeStruct> getCharacterAttributesFromDatabase(Database &db, int characterId);
    std::vector<SkillStruct> getCharacterSkillsFromDatabase(Database &db, int characterId);
    std::vector<SkillStruct> getMobSkillsFromDatabase(Database &db, int mobId);
    CharacterDataStruct getBasicCharacterDataFromDatabase(Database &db, int accountId, int characterId);

    void updateCharacterPosition(Database &db, int accountId, int characterId, const PositionStruct &position);
    void updateBasicCharacterData(Database &db, int accountId, int characterId, const CharacterDataStruct &characterData);

  private:
    Logger &logger_;
    std::unordered_map<int, CharacterDataStruct> charactersMap_;
    std::shared_mutex mutex_;
};