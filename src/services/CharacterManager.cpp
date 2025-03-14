#include "services/CharacterManager.hpp"
#include "utils/Database.hpp"
#include <pqxx/pqxx>
#include <iostream>
#include <vector>
#include <random>


CharacterManager::CharacterManager(Logger& logger) 
: logger_(logger)
{
    // Initialize properties or perform any setup here
}

std::vector<CharacterDataStruct> CharacterManager::getCharactersList(Database &database, ClientData &clientData, int accountId)
{
    // initialize a vector of strings for characters
    std::vector<CharacterDataStruct> charactersList;
    // Create a CharacterDataStruct to save the character data from DB
    CharacterDataStruct characterDataStruct;

    try
    {
        pqxx::work transaction(database.getConnection()); // Start a transaction
        pqxx::result selectCharacterData = database.executeQueryWithTransaction(
            transaction,
            "get_characters_list",
            {accountId});

        if (selectCharacterData.empty())
        {
            transaction.abort(); // Rollback the transaction
            return charactersList;
        }

        // Iterate through the result set and populate CharacterDataStruct objects
        for (const auto &row : selectCharacterData)
        {
            CharacterDataStruct characterDataStruct;
            characterDataStruct.characterId = row["character_id"].as<int>();
            characterDataStruct.characterLevel = row["character_lvl"].as<int>();
            characterDataStruct.characterName = row["character_name"].as<std::string>();
            characterDataStruct.characterClass = row["character_class"].as<std::string>();

            // Add the populated CharacterDataStruct to the vector
            charactersList.push_back(characterDataStruct);
        }

        transaction.commit(); // Commit the transaction
    }
    catch (const std::exception &e)
    {
        // Handle database connection or query errors
        database.handleDatabaseError(e);
        // You might want to send an error response back to the client or log the error
        return charactersList;
    }

    return charactersList;
}

CharacterDataStruct CharacterManager::getBasicCharacterData(Database &database, ClientData &clientData, int accountId, int characterId)
{
    // Create a CharacterDataStruct to save the character data from DB
    CharacterDataStruct characterDataStruct;

    try
    {
        pqxx::work transaction(database.getConnection()); // Start a transaction
        // Execute the prepared query and assign the result to a pqxx::result object
        pqxx::result selectCharacterData = database.executeQueryWithTransaction(
            transaction,
            "get_character",
            {accountId, characterId});

        if (selectCharacterData.empty())
        {
            transaction.abort(); // Rollback the transaction
            return characterDataStruct;
        }

        // Get the character data
        characterDataStruct.characterId = selectCharacterData[0][0].as<int>();
        characterDataStruct.characterLevel = selectCharacterData[0][1].as<int>();
        characterDataStruct.characterName = selectCharacterData[0][2].as<std::string>();
        characterDataStruct.characterClass = selectCharacterData[0][3].as<std::string>();
        characterDataStruct.characterRace = selectCharacterData[0][4].as<std::string>();
        characterDataStruct.characterExperiencePoints = selectCharacterData[0][5].as<int>();
        characterDataStruct.characterCurrentHealth = selectCharacterData[0][6].as<int>();
        characterDataStruct.characterCurrentMana = selectCharacterData[0][7].as<int>();

        //get character attributes
        pqxx::result selectCharacterAttributes = database.executeQueryWithTransaction(
            transaction,
            "get_character_attributes",
            {characterId});

        // Iterate through the result set and populate CharacterAttributeStruct objects
        for (const auto &row : selectCharacterAttributes)
        {
            CharacterAttributeStruct characterAttribute;
            characterAttribute.id = row["id"].as<int>();
            characterAttribute.name = row["name"].as<std::string>();
            characterAttribute.slug = row["slug"].as<std::string>();
            characterAttribute.value = row["value"].as<int>();

            // Add the populated CharacterAttributeStruct to the vector
            characterDataStruct.attributes.push_back(characterAttribute);
        }

        //get exp for next level
        pqxx::result selectExpForNextLevel = database.executeQueryWithTransaction(
            transaction,
            "get_character_exp_for_next_level",
            {characterDataStruct.characterLevel});

        // add exp for next level to character data
        characterDataStruct.expForNextLevel = selectExpForNextLevel[0][0].as<int>();
        

        transaction.commit(); // Commit the transaction
    }
    catch (const std::exception &e)
    {
        // Handle database connection or query errors
        database.handleDatabaseError(e);
        // You might want to send an error response back to the client or log the error
        return characterDataStruct;
    }

    return characterDataStruct;
}

PositionStruct CharacterManager::getCharacterPosition(Database &database, ClientData &clientData, int accountId, int characterId)
{
    // Create a characterPosition to save the character position data from DB
    PositionStruct characterPosition;

    try
    {
        pqxx::work transaction(database.getConnection()); // Start a transaction
        // Execute the prepared query and assign the result to a pqxx::result object
        pqxx::result selectCharacterPosition = database.executeQueryWithTransaction(
            transaction,
            "get_character_position",
            {characterId});

        if (selectCharacterPosition.empty())
        {
            transaction.abort(); // Rollback the transaction
            return characterPosition;
        }

        // Get the character position
        characterPosition.positionX = selectCharacterPosition[0][0].as<float>();
        characterPosition.positionY = selectCharacterPosition[0][1].as<float>();
        characterPosition.positionZ = selectCharacterPosition[0][2].as<float>();

        transaction.commit(); // Commit the transaction
    }
    catch (const std::exception &e)
    {
        // Handle database connection or query errors
        database.handleDatabaseError(e);
        // You might want to send an error response back to the client or log the error
        return characterPosition;
    }

    return characterPosition;
}

void CharacterManager::updateCharacterPosition(Database &database, ClientData &clientData, int accountId, int characterId, PositionStruct &position)
{
    try
    {
        pqxx::work transaction(database.getConnection()); // Start a transaction
        pqxx::result updateCharacterPosition = database.executeQueryWithTransaction(
            transaction,
            "set_character_position",
            {characterId, position.positionX, position.positionY, position.positionZ});

        if (updateCharacterPosition.affected_rows() == 0)
        {
            transaction.abort(); // Rollback the transaction
            return;
        }

        transaction.commit(); // Commit the transaction

        logger_.log("Character position updated successfully", GREEN);
    }
    catch (const std::exception &e)
    {
        // Handle database connection or query errors
        database.handleDatabaseError(e);
        // You might want to send an error response back to the client or log the error
        return;
    }
}

void CharacterManager::updateBasicCharacterData(Database &database, ClientData &clientData, int accountId, int characterId, CharacterDataStruct &characterData)
{
    if(characterData.needDBUpdate == false) {
        logger_.log("Character with ID: "+std::to_string(characterId)+" - doesn't need update data in DB...");
        return;
    }

    std::cout << "Character data update..." << std::endl;

    try
    {
        pqxx::work transaction(database.getConnection()); // Start a transaction
        pqxx::result updateCharacterData = database.executeQueryWithTransaction(
            transaction,
            "set_basic_character_data",
            {characterId, characterData.characterLevel, characterData.characterExperiencePoints, characterData.characterCurrentHealth, characterData.characterCurrentMana});

        if (updateCharacterData.affected_rows() == 0)
        {
            logger_.logError("Character data not found in DB...");
            transaction.abort(); // Rollback the transaction
            return;
        }

        transaction.commit(); // Commit the transaction
        // Change flag to false, because data was updated in DB
        clientData.markClientUpdate(accountId, false);

        logger_.log("Character data updated successfully", GREEN);
    }
    catch (const std::exception &e)
    {
        // Handle database connection or query errors
        database.handleDatabaseError(e);
        // You might want to send an error response back to the client or log the error
        return;
    }
}

void CharacterManager::updateBasicCharactersData(Database &database, ClientData &clientData)
{
    logger_.log("Try update characters data in DB...", BLUE);
    try
    {
        // Get all existing clients data as array
        std::unordered_map<int, ClientDataStruct> clientDataMap = clientData.getClientsDataMap();

        // Iterate through the result set and populate CharacterDataStruct objects
        for (const auto &clientDataItem : clientDataMap)
        {
            // Get the character data
            CharacterDataStruct characterData = clientDataItem.second.characterData;
            PositionStruct characterPosition = clientDataItem.second.characterData.characterPosition;

            // Update character data in the database
            updateBasicCharacterData(database, clientData, clientDataItem.first, characterData.characterId, characterData);
            // Update character position in the database
            // updateCharacterPosition(database, clientData, clientDataItem.first, characterData.characterId, characterPosition);
        }
    }
    catch (const std::exception &e)
    {
        logger_.logError("Error while updating characters data in DB...");
    }
}