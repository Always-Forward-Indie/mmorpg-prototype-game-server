#pragma once

#include "utils/JSONParser.hpp"
#include "utils/TimestampUtils.hpp"

#include <memory>

class MessageHandler
{
  public:
    MessageHandler(JSONParser &jsonParser);

    std::tuple<std::string, ClientDataStruct, ChunkInfoStruct, CharacterDataStruct, PositionStruct, MessageStruct>
    parseMessage(const std::string &message);

    // The 8th element is the once-parsed wire json, shared read-only with
    // all EventDispatcher handlers via EventPayload::parsed (single-parse).
    std::tuple<std::string, ClientDataStruct, ChunkInfoStruct, CharacterDataStruct, PositionStruct, MessageStruct,
        TimestampStruct, std::shared_ptr<const nlohmann::json>>
    parseMessageWithTimestamps(const std::string &message);

  private:
    JSONParser &jsonParser_;
};