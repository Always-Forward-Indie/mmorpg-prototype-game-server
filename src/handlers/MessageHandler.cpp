#include "handlers/MessageHandler.hpp"

MessageHandler::MessageHandler(JSONParser &jsonParser) : jsonParser_(jsonParser) {}

std::tuple<std::string, ClientDataStruct, ChunkInfoStruct, CharacterDataStruct, PositionStruct, MessageStruct>
MessageHandler::parseMessage(const std::string &message)
{
    const nlohmann::json jsonData = nlohmann::json::parse(message);

    std::string eventType = jsonParser_.parseEventType(jsonData);
    ClientDataStruct clientData = jsonParser_.parseClientData(jsonData);
    ChunkInfoStruct chunkData = jsonParser_.parseChunkServerHandshakeData(jsonData);

    CharacterDataStruct characterData = jsonParser_.parseCharacterData(jsonData);
    PositionStruct positionData = jsonParser_.parsePositionData(jsonData);
    MessageStruct messageStruct = jsonParser_.parseMessage(jsonData);

    return {eventType, clientData, chunkData, characterData, positionData, messageStruct};
}

std::tuple<std::string, ClientDataStruct, ChunkInfoStruct, CharacterDataStruct, PositionStruct, MessageStruct, TimestampStruct>
MessageHandler::parseMessageWithTimestamps(const std::string &message)
{
    // Single parse of the wire bytes (was 7 prefix parses); extractors read
    // the shared json. Timestamps now read from the full message too (the old
    // 1024B truncation could drop header.timestamps on large packets).
    const nlohmann::json jsonData = nlohmann::json::parse(message);

    std::string eventType = jsonParser_.parseEventType(jsonData);
    ClientDataStruct clientData = jsonParser_.parseClientData(jsonData);
    ChunkInfoStruct chunkData = jsonParser_.parseChunkServerHandshakeData(jsonData);

    CharacterDataStruct characterData = jsonParser_.parseCharacterData(jsonData);
    PositionStruct positionData = jsonParser_.parsePositionData(jsonData);
    MessageStruct messageStruct = jsonParser_.parseMessage(jsonData);

    TimestampStruct timestamps = TimestampUtils::parseTimestampsFromHeader(jsonData);

    return {eventType, clientData, chunkData, characterData, positionData, messageStruct, timestamps};
}