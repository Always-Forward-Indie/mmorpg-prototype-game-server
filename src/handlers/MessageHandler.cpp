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