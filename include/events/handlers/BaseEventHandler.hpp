#pragma once

#include <boost/asio.hpp>
#include <memory>
#include <string>

#include "events/Event.hpp"
#include "network/NetworkManager.hpp"
#include "services/GameServices.hpp"

/**
 * @brief Base class for game-server domain event handlers (mirrors chunk's
 * events/handlers/BaseEventHandler, minus GameServerWorker which is
 * chunk-only).
 *
 * Holds the three dependencies every handler needs with the same member
 * names EventHandler uses, so extracted handler bodies move verbatim.
 */
class BaseEventHandler
{
  public:
    BaseEventHandler(
        NetworkManager &networkManager,
        GameServices &gameServices,
        const std::string &loggerSubsystem = "events");

    virtual ~BaseEventHandler() = default;

  protected:
    NetworkManager &networkManager_;
    GameServices &gameServices_;
    std::shared_ptr<spdlog::logger> log_;
};
