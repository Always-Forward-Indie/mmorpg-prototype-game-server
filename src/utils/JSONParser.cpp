#include "utils/JSONParser.hpp"
#include <iostream>

JSONParser::JSONParser() {}

JSONParser::~JSONParser() {}

CharacterDataStruct
JSONParser::parseCharacterData(const char *data, size_t length)
{
    nlohmann::json jsonData = nlohmann::json::parse(data, data + length);
    CharacterDataStruct characterData;

    if (jsonData.contains("body") && jsonData["body"].is_object() &&
        jsonData["body"].contains("characterId") && jsonData["body"]["characterId"].is_number_integer())
    {
        characterData.characterId = jsonData["body"]["characterId"].get<int>();
    }

    if (jsonData.contains("body") && jsonData["body"].is_object() &&
        jsonData["body"].contains("characterLevel") && jsonData["body"]["characterLevel"].is_number_integer())
    {
        characterData.characterLevel = jsonData["body"]["characterLevel"].get<int>();
    }

    // get character experience points for next level
    if (jsonData.contains("body") && jsonData["body"].is_object() &&
        jsonData["body"].contains("characterExpForNextLevel") && jsonData["body"]["characterExpForNextLevel"].is_number_integer())
    {
        characterData.expForNextLevel = jsonData["body"]["characterExpForNextLevel"].get<int>();
    }

    // get character experience points
    if (jsonData.contains("body") && jsonData["body"].is_object() &&
        jsonData["body"].contains("characterExp") && jsonData["body"]["characterExp"].is_number_integer())
    {
        characterData.characterExperiencePoints = jsonData["body"]["characterExp"].get<int>();
    }

    if (jsonData.contains("body") && jsonData["body"].is_object() &&
        jsonData["body"].contains("characterCurrentHealth") && jsonData["body"]["characterCurrentHealth"].is_number_integer())
    {
        characterData.characterCurrentHealth = jsonData["body"]["characterCurrentHealth"].get<int>();
    }

    if (jsonData.contains("body") && jsonData["body"].is_object() &&
        jsonData["body"].contains("characterCurrentMana") && jsonData["body"]["characterCurrentMana"].is_number_integer())
    {
        characterData.characterCurrentMana = jsonData["body"]["characterCurrentMana"].get<int>();
    }

    if (jsonData.contains("body") && jsonData["body"].is_object() &&
        jsonData["body"].contains("characterName") && jsonData["body"]["characterName"].is_string())
    {
        characterData.characterName = jsonData["body"]["characterName"].get<std::string>();
    }

    if (jsonData.contains("body") && jsonData["body"].is_object() &&
        jsonData["body"].contains("characterClass") && jsonData["body"]["characterClass"].is_string())
    {
        characterData.characterClass = jsonData["body"]["characterClass"].get<std::string>();
    }

    if (jsonData.contains("body") && jsonData["body"].is_object() &&
        jsonData["body"].contains("characterRace") && jsonData["body"]["characterRace"].is_string())
    {
        characterData.characterRace = jsonData["body"]["characterRace"].get<std::string>();
    }

    // get character attributes
    if (jsonData.contains("body") && jsonData["body"].is_object() &&
        jsonData["body"].contains("attributesData") && jsonData["body"]["attributesData"].is_array())
    {
        for (const auto &attribute : jsonData["body"]["attributesData"])
        {
            CharacterAttributeStruct attributeData;
            if (attribute.contains("id") && attribute["id"].is_number_integer())
            {
                attributeData.id = attribute["id"].get<int>();
            }
            if (attribute.contains("name") && attribute["name"].is_string())
            {
                attributeData.name = attribute["name"].get<std::string>();
            }
            if (attribute.contains("slug") && attribute["slug"].is_string())
            {
                attributeData.slug = attribute["slug"].get<std::string>();
            }
            if (attribute.contains("value") && attribute["value"].is_number_integer())
            {
                attributeData.value = attribute["value"].get<int>();
            }
            characterData.attributes.push_back(attributeData);
        }
    }

    return characterData;
}

PositionStruct
JSONParser::parsePositionData(const char *data, size_t length)
{
    nlohmann::json jsonData = nlohmann::json::parse(data, data + length);
    PositionStruct positionData;

    if (jsonData.contains("body") && jsonData["body"].is_object() &&
        jsonData["body"].contains("posX") && (jsonData["body"]["posX"].is_number_float() || jsonData["body"]["posX"].is_number_integer()))
    {
        positionData.positionX = jsonData["body"]["posX"].get<float>();
    }
    if (jsonData.contains("body") && jsonData["body"].is_object() &&
        jsonData["body"].contains("posY") && (jsonData["body"]["posY"].is_number_float() || jsonData["body"]["posY"].is_number_integer()))
    {
        positionData.positionY = jsonData["body"]["posY"].get<float>();
    }
    if (jsonData.contains("body") && jsonData["body"].is_object() &&
        jsonData["body"].contains("posZ") && (jsonData["body"]["posZ"].is_number_float() || jsonData["body"]["posZ"].is_number_integer()))
    {
        positionData.positionZ = jsonData["body"]["posZ"].get<float>();
    }
    if (jsonData.contains("body") && jsonData["body"].is_object() &&
        jsonData["body"].contains("rotZ") && (jsonData["body"]["rotZ"].is_number_float() || jsonData["body"]["rotZ"].is_number_integer()))
    {
        positionData.rotationZ = jsonData["body"]["rotZ"].get<float>();
    }

    return positionData;
}

ClientDataStruct
JSONParser::parseClientData(const char *data, size_t length)
{
    nlohmann::json jsonData = nlohmann::json::parse(data, data + length);
    ClientDataStruct clientData;

    if (jsonData.contains("header") && jsonData["header"].is_object() &&
        jsonData["header"].contains("clientId") && jsonData["header"]["clientId"].is_number_integer())
    {
        clientData.clientId = jsonData["header"]["clientId"].get<int>();
    }
    if (jsonData.contains("header") && jsonData["header"].is_object() &&
        jsonData["header"].contains("hash") && jsonData["header"]["hash"].is_string())
    {
        clientData.hash = jsonData["header"]["hash"].get<std::string>();
    }

    return clientData;
}

nlohmann::json
JSONParser::parseCharactersList(const char *data, size_t length)
{
    nlohmann::json jsonData = nlohmann::json::parse(data, data + length);
    nlohmann::json charactersList;

    if (jsonData.contains("body") && jsonData["body"].is_object() &&
        jsonData["body"].contains("charactersList") && jsonData["body"]["charactersList"].is_array())
    {
        charactersList = jsonData["body"]["charactersList"];
    }

    return charactersList;
}

MessageStruct
JSONParser::parseMessage(const char *data, size_t length)
{
    nlohmann::json jsonData = nlohmann::json::parse(data, data + length);
    MessageStruct message;

    if (jsonData.contains("header") && jsonData["header"].is_object() &&
        jsonData["header"].contains("status") && jsonData["header"]["status"].is_string())
    {
        message.status = jsonData["header"]["status"].get<std::string>();
    }
    if (jsonData.contains("header") && jsonData["header"].is_object() &&
        jsonData["header"].contains("message") && jsonData["header"]["message"].is_string())
    {
        message.message = jsonData["header"]["message"].get<std::string>();
    }

    return message;
}

std::string
JSONParser::parseEventType(const char *data, size_t length)
{
    nlohmann::json jsonData = nlohmann::json::parse(data, data + length);
    std::string eventType;

    if (jsonData.contains("header") && jsonData["header"].is_object() &&
        jsonData["header"].contains("eventType") && jsonData["header"]["eventType"].is_string())
    {
        eventType = jsonData["header"]["eventType"].get<std::string>();
    }

    return eventType;
}

// parse chunk server handshake data
ChunkInfoStruct
JSONParser::parseChunkServerHandshakeData(const char *data, size_t length)
{
    nlohmann::json jsonData = nlohmann::json::parse(data, data + length);
    ChunkInfoStruct chunkData;

    if (jsonData.contains("header") && jsonData["header"].is_object() &&
        jsonData["header"].contains("id") && jsonData["header"]["id"].is_number_integer())
    {
        chunkData.id = jsonData["header"]["id"].get<int>();
    }
    if (jsonData.contains("header") && jsonData["header"].is_object() &&
        jsonData["header"].contains("ip") && jsonData["header"]["ip"].is_string())
    {
        chunkData.ip = jsonData["header"]["ip"].get<std::string>();
    }
    if (jsonData.contains("header") && jsonData["header"].is_object() &&
        jsonData["header"].contains("port") && jsonData["header"]["port"].is_number_integer())
    {
        chunkData.port = jsonData["header"]["port"].get<int>();
    }

    return chunkData;
}

std::vector<CharacterDataStruct>
JSONParser::parseSavePositionsData(const char *data, size_t length)
{
    nlohmann::json jsonData = nlohmann::json::parse(data, data + length);
    std::vector<CharacterDataStruct> characters;

    if (!jsonData.contains("body") || !jsonData["body"].is_object())
        return characters;

    if (!jsonData["body"].contains("characters") || !jsonData["body"]["characters"].is_array())
        return characters;

    for (const auto &entry : jsonData["body"]["characters"])
    {
        CharacterDataStruct charData;

        if (entry.contains("characterId") && entry["characterId"].is_number_integer())
            charData.characterId = entry["characterId"].get<int>();

        if (entry.contains("posX") && (entry["posX"].is_number_float() || entry["posX"].is_number_integer()))
            charData.characterPosition.positionX = entry["posX"].get<float>();

        if (entry.contains("posY") && (entry["posY"].is_number_float() || entry["posY"].is_number_integer()))
            charData.characterPosition.positionY = entry["posY"].get<float>();

        if (entry.contains("posZ") && (entry["posZ"].is_number_float() || entry["posZ"].is_number_integer()))
            charData.characterPosition.positionZ = entry["posZ"].get<float>();

        if (entry.contains("rotZ") && (entry["rotZ"].is_number_float() || entry["rotZ"].is_number_integer()))
            charData.characterPosition.rotationZ = entry["rotZ"].get<float>();

        if (charData.characterId > 0)
            characters.push_back(charData);
    }

    return characters;
}

// Parse a "saveCharacterProgress" packet sent by the chunk server.
// Expected JSON shape:
//   { "body": { "characters": [ { "characterId": N, "exp": N, "level": N }, ... ] } }
std::vector<CharacterDataStruct>
JSONParser::parseSaveCharacterProgressData(const char *data, size_t length)
{
    nlohmann::json jsonData = nlohmann::json::parse(data, data + length);
    std::vector<CharacterDataStruct> characters;

    if (!jsonData.contains("body") || !jsonData["body"].is_object())
        return characters;

    if (!jsonData["body"].contains("characters") || !jsonData["body"]["characters"].is_array())
        return characters;

    for (const auto &entry : jsonData["body"]["characters"])
    {
        CharacterDataStruct charData;

        if (entry.contains("characterId") && entry["characterId"].is_number_integer())
            charData.characterId = entry["characterId"].get<int>();

        if (entry.contains("exp") && entry["exp"].is_number_integer())
            charData.characterExperiencePoints = entry["exp"].get<int>();

        if (entry.contains("level") && entry["level"].is_number_integer())
            charData.characterLevel = entry["level"].get<int>();

        if (charData.characterId > 0)
            characters.push_back(charData);
    }

    return characters;
}