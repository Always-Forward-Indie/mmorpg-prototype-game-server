#include "events/handlers/BaseEventHandler.hpp"

BaseEventHandler::BaseEventHandler(
    NetworkManager &networkManager,
    GameServices &gameServices,
    const std::string &loggerSubsystem)
    : networkManager_(networkManager),
      gameServices_(gameServices)
{
    log_ = gameServices_.getLogger().getSystem(loggerSubsystem);
}
