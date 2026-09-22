#pragma once

#include "events/handlers/BaseEventHandler.hpp"

/**
 * @brief Pity / bestiary / timed-champion domain (eighth extracted domain).
 */
class ProgressionEventHandler : public BaseEventHandler
{
  public:
    ProgressionEventHandler(
        NetworkManager &networkManager,
        GameServices &gameServices);

    void handleGetPlayerPityEvent(const Event &event);
    void handleGetPlayerBestiaryEvent(const Event &event);
    void handleSavePityCounterEvent(const Event &event);
    void handleSaveBestiaryKillEvent(const Event &event);
    void handleGetTimedChampionTemplatesEvent(const Event &event);
    void handleTimedChampionKilledEvent(const Event &event);
};
