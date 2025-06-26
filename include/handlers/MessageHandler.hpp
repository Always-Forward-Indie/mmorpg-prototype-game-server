#pragma once

#include "utils/JSONParser.hpp"

class MessageHandler
{
  public:
    MessageHandler(JSONParser &jsonParser);

    std::tuple<std::string, ClientDataStruct, ChunkInfoStruct, CharacterDataStruct, PositionStruct, MessageStruct>
    parseMessage(const std::string &message);

  private:
    JSONParser &jsonParser_;
};