#pragma once

#include "data/DataStructs.hpp"
#include <nlohmann/json.hpp>

class JSONParser
{
  public:
    JSONParser();
    ~JSONParser();

    CharacterDataStruct parseCharacterData(const char *data, size_t length);
    PositionStruct parsePositionData(const char *data, size_t length);
    ClientDataStruct parseClientData(const char *data, size_t length);
    MessageStruct parseMessage(const char *data, size_t length);
    std::string parseEventType(const char *data, size_t length);
    ChunkInfoStruct parseChunkServerHandshakeData(const char *data, size_t length);
    nlohmann::json parseCharactersList(const char *data, size_t length);
    std::vector<CharacterDataStruct> parseSavePositionsData(const char *data, size_t length);
    std::vector<CharacterDataStruct> parseSaveCharacterProgressData(const char *data, size_t length);
    // Single-parse overloads: MessageHandler parses the wire bytes once and
    // threads the json through these (7 prefix parses/message -> 1).
    // Buffer overloads above parse once and delegate here.
    CharacterDataStruct parseCharacterData(const nlohmann::json &jsonData);
    PositionStruct parsePositionData(const nlohmann::json &jsonData);
    ClientDataStruct parseClientData(const nlohmann::json &jsonData);
    MessageStruct parseMessage(const nlohmann::json &jsonData);
    std::string parseEventType(const nlohmann::json &jsonData);
    ChunkInfoStruct parseChunkServerHandshakeData(const nlohmann::json &jsonData);
};
