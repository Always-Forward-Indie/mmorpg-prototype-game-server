#include "handlers/MessageHandler.hpp"

MessageHandler::MessageHandler(JSONParser &jsonParser) : jsonParser_(jsonParser) {}

std::tuple<std::string, ClientDataStruct, ChunkInfoStruct, CharacterDataStruct, PositionStruct, MessageStruct>
MessageHandler::parseMessage(const std::string &message)
{
    const char *data = message.data();
    size_t messageLength = message.size();

    std::string eventType = jsonParser_.parseEventType(data, messageLength);
    ClientDataStruct clientData = jsonParser_.parseClientData(data, messageLength);
    ChunkInfoStruct chunkData = jsonParser_.parseChunkServerHandshakeData(data, messageLength);

    CharacterDataStruct characterData = jsonParser_.parseCharacterData(data, messageLength);
    PositionStruct positionData = jsonParser_.parsePositionData(data, messageLength);
    MessageStruct messageStruct = jsonParser_.parseMessage(data, messageLength);

    return {eventType, clientData, chunkData, characterData, positionData, messageStruct};
}

std::tuple<std::string, ClientDataStruct, ChunkInfoStruct, CharacterDataStruct, PositionStruct, MessageStruct, TimestampStruct>
MessageHandler::parseMessageWithTimestamps(const std::string &message)
{
    const char *data = message.data();
    size_t messageLength = message.size();

    std::string eventType = jsonParser_.parseEventType(data, messageLength);
    ClientDataStruct clientData = jsonParser_.parseClientData(data, messageLength);
    ChunkInfoStruct chunkData = jsonParser_.parseChunkServerHandshakeData(data, messageLength);

    CharacterDataStruct characterData = jsonParser_.parseCharacterData(data, messageLength);
    PositionStruct positionData = jsonParser_.parsePositionData(data, messageLength);
    MessageStruct messageStruct = jsonParser_.parseMessage(data, messageLength);

    // Parse timestamps from message
    std::array<char, 1024> messageBuffer{};
    size_t copySize = std::min(messageLength, messageBuffer.size());
    std::memcpy(messageBuffer.data(), data, copySize);
    TimestampStruct timestamps = TimestampUtils::parseTimestampsFromBuffer(messageBuffer, copySize);

    return {eventType, clientData, chunkData, characterData, positionData, messageStruct, timestamps};
}