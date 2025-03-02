#include "utils/JSONParser.hpp"
#include <iostream>

JSONParser::JSONParser() { }

JSONParser::~JSONParser() { }

CharacterDataStruct JSONParser::parseCharacterData(const char* data, size_t length)
{
    nlohmann::json jsonData = nlohmann::json::parse(data, data + length);
    CharacterDataStruct characterData;

    if (jsonData.contains("body") && jsonData["body"].is_object() &&
        jsonData["body"].contains("characterId") && jsonData["body"]["characterId"].is_number_integer()) {
        characterData.characterId = jsonData["body"]["characterId"].get<int>();
    }
    if (jsonData.contains("body") && jsonData["body"].is_object() &&
        jsonData["body"].contains("characterLevel") && jsonData["body"]["characterLevel"].is_number_integer()) {
        characterData.characterLevel = jsonData["body"]["characterLevel"].get<int>();
    }
    if (jsonData.contains("body") && jsonData["body"].is_object() &&
        jsonData["body"].contains("characterExp") && jsonData["body"]["characterExp"].is_number_integer()) {
        characterData.characterExperiencePoints = jsonData["body"]["characterExp"].get<int>();
    }
    if (jsonData.contains("body") && jsonData["body"].is_object() &&
        jsonData["body"].contains("characterCurrentHealth") && jsonData["body"]["characterCurrentHealth"].is_number_integer()) {
        characterData.characterCurrentHealth = jsonData["body"]["characterCurrentHealth"].get<int>();
    }
    if (jsonData.contains("body") && jsonData["body"].is_object() &&
        jsonData["body"].contains("characterCurrentMana") && jsonData["body"]["characterCurrentMana"].is_number_integer()) {
        characterData.characterCurrentMana = jsonData["body"]["characterCurrentMana"].get<int>();
    }
    if (jsonData.contains("body") && jsonData["body"].is_object() &&
        jsonData["body"].contains("characterName") && jsonData["body"]["characterName"].is_string()) {
        characterData.characterName = jsonData["body"]["characterName"].get<std::string>();
    }
    if (jsonData.contains("body") && jsonData["body"].is_object() &&
        jsonData["body"].contains("characterClass") && jsonData["body"]["characterClass"].is_string()) {
        characterData.characterClass = jsonData["body"]["characterClass"].get<std::string>();
    }
    if (jsonData.contains("body") && jsonData["body"].is_object() &&
        jsonData["body"].contains("characterRace") && jsonData["body"]["characterRace"].is_string()) {
        characterData.characterRace = jsonData["body"]["characterRace"].get<std::string>();
    }

    return characterData;
}

PositionStruct JSONParser::parsePositionData(const char* data, size_t length)
{
    nlohmann::json jsonData = nlohmann::json::parse(data, data + length);
    PositionStruct positionData;

    if (jsonData.contains("body") && jsonData["body"].is_object() &&
        jsonData["body"].contains("posX") && (jsonData["body"]["posX"].is_number_float() || jsonData["body"]["posX"].is_number_integer())) {
        positionData.positionX = jsonData["body"]["posX"].get<float>();
    }
    if (jsonData.contains("body") && jsonData["body"].is_object() &&
        jsonData["body"].contains("posY") && (jsonData["body"]["posY"].is_number_float() || jsonData["body"]["posY"].is_number_integer())) {
        positionData.positionY = jsonData["body"]["posY"].get<float>();
    }
    if (jsonData.contains("body") && jsonData["body"].is_object() &&
        jsonData["body"].contains("posZ") && (jsonData["body"]["posZ"].is_number_float() || jsonData["body"]["posZ"].is_number_integer())) {
        positionData.positionZ = jsonData["body"]["posZ"].get<float>();
    }
    if (jsonData.contains("body") && jsonData["body"].is_object() &&
        jsonData["body"].contains("rotZ") && (jsonData["body"]["rotZ"].is_number_float() || jsonData["body"]["rotZ"].is_number_integer())) {
        positionData.rotationZ = jsonData["body"]["rotZ"].get<float>();
    }

    return positionData;
}

ClientDataStruct JSONParser::parseClientData(const char* data, size_t length)
{
    nlohmann::json jsonData = nlohmann::json::parse(data, data + length);
    ClientDataStruct clientData;

    if (jsonData.contains("header") && jsonData["header"].is_object() &&
        jsonData["header"].contains("clientId") && jsonData["header"]["clientId"].is_number_integer()) {
        clientData.clientId = jsonData["header"]["clientId"].get<int>();
    }
    if (jsonData.contains("header") && jsonData["header"].is_object() &&
        jsonData["header"].contains("hash") && jsonData["header"]["hash"].is_string()) {
        clientData.hash = jsonData["header"]["hash"].get<std::string>();
    }

    return clientData;
}

nlohmann::json JSONParser::parseCharactersList(const char* data, size_t length)
{
    nlohmann::json jsonData = nlohmann::json::parse(data, data + length);
    nlohmann::json charactersList;

    if (jsonData.contains("body") && jsonData["body"].is_object() &&
        jsonData["body"].contains("charactersList") && jsonData["body"]["charactersList"].is_array()) {
        charactersList = jsonData["body"]["charactersList"];
    }

    return charactersList;
}

MessageStruct JSONParser::parseMessage(const char* data, size_t length)
{
    nlohmann::json jsonData = nlohmann::json::parse(data, data + length);
    MessageStruct message;

    if (jsonData.contains("header") && jsonData["header"].is_object() &&
        jsonData["header"].contains("status") && jsonData["header"]["status"].is_string()) {
        message.status = jsonData["header"]["status"].get<std::string>();
    }
    if (jsonData.contains("header") && jsonData["header"].is_object() &&
        jsonData["header"].contains("message") && jsonData["header"]["message"].is_string()) {
        message.message = jsonData["header"]["message"].get<std::string>();
    }

    return message;
}

std::string JSONParser::parseEventType(const char* data, size_t length)
{
    nlohmann::json jsonData = nlohmann::json::parse(data, data + length);
    std::string eventType;

    if (jsonData.contains("header") && jsonData["header"].is_object() &&
        jsonData["header"].contains("eventType") && jsonData["header"]["eventType"].is_string()) {
        eventType = jsonData["header"]["eventType"].get<std::string>();
    }

    return eventType;
}
