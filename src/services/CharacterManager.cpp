#include "services/CharacterManager.hpp"
#include "utils/Database.hpp"
#include <pqxx/pqxx>
#include <iostream>
#include <vector>
#include <random>


CharacterManager::CharacterManager(Logger& logger)
    : logger_(logger) {}

void CharacterManager::addOrUpdateCharacter(const CharacterDataStruct& character) {
    std::unique_lock lock(mutex_);
    charactersMap_[character.characterId] = character;
}

CharacterDataStruct CharacterManager::getCharacterById(int characterId) {
    std::shared_lock lock(mutex_);
    auto it = charactersMap_.find(characterId);
    return it != charactersMap_.end() ? it->second : CharacterDataStruct();
}

void CharacterManager::removeCharacter(int characterId) {
    std::unique_lock lock(mutex_);
    charactersMap_.erase(characterId);
}

std::vector<CharacterDataStruct> CharacterManager::getAllCharacters() {
    std::shared_lock lock(mutex_);
    std::vector<CharacterDataStruct> result;
    for (const auto& [id, character] : charactersMap_) {
        result.push_back(character);
    }
    return result;
}

bool CharacterManager::hasCharacter(int characterId) {
    std::shared_lock lock(mutex_);
    return charactersMap_.find(characterId) != charactersMap_.end();
}

CharacterDataStruct CharacterManager::loadCharacterFromDatabase(Database& db, int accountId, int characterId) {
    CharacterDataStruct character = getBasicCharacterDataFromDatabase(db, accountId, characterId);
    character.characterPosition = getCharacterPositionFromDatabase(db, accountId, characterId);
    character.attributes = getCharacterAttributesFromDatabase(db, characterId);
    addOrUpdateCharacter(character);
    return character;
}

void CharacterManager::saveCharacterToDatabase(Database& db, const CharacterDataStruct& character) {
    updateBasicCharacterData(db, 0, character.characterId, character);
    updateCharacterPosition(db, 0, character.characterId, character.characterPosition);
}

CharacterDataStruct CharacterManager::getBasicCharacterDataFromDatabase(Database& db, int accountId, int characterId) {
    CharacterDataStruct characterData;
    try {
        pqxx::work txn(db.getConnection());
        auto result = db.executeQueryWithTransaction(txn, "get_character", {accountId, characterId});
        if (!result.empty()) {
            const auto& row = result[0];
            characterData.characterId = row[0].as<int>();
            characterData.characterLevel = row[1].as<int>();
            characterData.characterName = row[2].as<std::string>();
            characterData.characterClass = row[3].as<std::string>();
            characterData.characterRace = row[4].as<std::string>();
            characterData.characterExperiencePoints = row[5].as<int>();
            characterData.characterCurrentHealth = row[6].as<int>();
            characterData.characterCurrentMana = row[7].as<int>();

            auto expResult = db.executeQueryWithTransaction(txn, "get_character_exp_for_next_level", {characterData.characterLevel});
            characterData.expForNextLevel = expResult[0][0].as<int>();
        }
        txn.commit();
    } catch (const std::exception& e) {
        db.handleDatabaseError(e);
    }
    return characterData;
}

PositionStruct CharacterManager::getCharacterPositionFromDatabase(Database& db, int accountId, int characterId) {
    PositionStruct pos;
    try {
        pqxx::work txn(db.getConnection());
        auto result = db.executeQueryWithTransaction(txn, "get_character_position", {characterId});
        if (!result.empty()) {
            pos.positionX = result[0][0].as<float>();
            pos.positionY = result[0][1].as<float>();
            pos.positionZ = result[0][2].as<float>();
        }
        txn.commit();
    } catch (const std::exception& e) {
        db.handleDatabaseError(e);
    }
    return pos;
}

std::vector<CharacterAttributeStruct> CharacterManager::getCharacterAttributesFromDatabase(Database& db, int characterId) {
    std::vector<CharacterAttributeStruct> attributes;
    try {
        pqxx::work txn(db.getConnection());
        auto result = db.executeQueryWithTransaction(txn, "get_character_attributes", {characterId});
        for (const auto& row : result) {
            CharacterAttributeStruct attr;
            attr.id = row["id"].as<int>();
            attr.name = row["name"].as<std::string>();
            attr.slug = row["slug"].as<std::string>();
            attr.value = row["value"].as<int>();
            attributes.push_back(attr);
        }
        txn.commit();
    } catch (const std::exception& e) {
        db.handleDatabaseError(e);
    }
    return attributes;
}

void CharacterManager::updateCharacterPosition(Database& db, int accountId, int characterId, const PositionStruct& position) {
    try {
        pqxx::work txn(db.getConnection());
        db.executeQueryWithTransaction(txn, "set_character_position", {characterId, position.positionX, position.positionY, position.positionZ});
        txn.commit();
        logger_.log("Character position updated successfully", GREEN);
    } catch (const std::exception& e) {
        db.handleDatabaseError(e);
    }
}

void CharacterManager::updateBasicCharacterData(Database& db, int accountId, int characterId, const CharacterDataStruct& characterData) {
    try {
        pqxx::work txn(db.getConnection());
        db.executeQueryWithTransaction(txn, "set_basic_character_data",
            {characterId, characterData.characterLevel, characterData.characterExperiencePoints,
             characterData.characterCurrentHealth, characterData.characterCurrentMana});
        txn.commit();
        logger_.log("Character data updated successfully", GREEN);
    } catch (const std::exception& e) {
        db.handleDatabaseError(e);
    }
}
