#include "handlers/MessageHandler.hpp"

MessageHandler::MessageHandler(JSONParser &jsonParser) : jsonParser_(jsonParser) {}

std::tuple<std::string, ClientDataStruct, CharacterDataStruct, PositionStruct, MessageStruct>
MessageHandler::parseMessage(const std::string &message) {
    std::array<char, 1024> messageBuffer{};
    std::copy(message.begin(), message.end(), messageBuffer.begin());
    size_t messageLength = message.size();

    std::string eventType = jsonParser_.parseEventType(messageBuffer, messageLength);
    ClientDataStruct clientData = jsonParser_.parseClientData(messageBuffer, messageLength);
    CharacterDataStruct characterData = jsonParser_.parseCharacterData(messageBuffer, messageLength);
    PositionStruct positionData = jsonParser_.parsePositionData(messageBuffer, messageLength);
    MessageStruct messageStruct = jsonParser_.parseMessage(messageBuffer, messageLength);

    return {eventType, clientData, characterData, positionData, messageStruct};
}