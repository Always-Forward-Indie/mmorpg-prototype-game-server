#pragma once

#include <nlohmann/json.hpp>
#include "data/DataStructs.hpp"

class JSONParser {
public:
    JSONParser();
    ~JSONParser();

    CharacterDataStruct parseCharacterData(const char* data, size_t length);
    PositionStruct parsePositionData(const char* data, size_t length);
    ClientDataStruct parseClientData(const char* data, size_t length);
    MessageStruct parseMessage(const char* data, size_t length);
    std::string parseEventType(const char* data, size_t length);
    ChunkInfoStruct parseChunkServerHandshakeData(const char *data, size_t length);
    nlohmann::json parseCharactersList(const char *data, size_t length);
};
