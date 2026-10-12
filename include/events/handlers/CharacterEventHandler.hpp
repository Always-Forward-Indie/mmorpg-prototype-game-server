#pragma once

#include "events/handlers/BaseEventHandler.hpp"

/**
 * @brief Character domain (fourth extracted domain).
 *
 * Character data reads, periodic bulk saves (positions, HP/mana, progress,
 * playtime), experience debt and online-status recovery after reconnect.
 */
class CharacterEventHandler : public BaseEventHandler
{
  public:
    CharacterEventHandler(
        NetworkManager &networkManager,
        GameServices &gameServices);

    void handleGetCharacterDataEvent(const Event &event);
    void handleMoveCharacterChunkEvent(const Event &event);
    void handleDisconnectChunkEvent(const Event &event);
    void handleSavePositionsEvent(const Event &event);
    void handleSaveHpManaEvent(const Event &event);
    void handleSaveCharacterProgressEvent(const Event &event);
    void handleSavePlayTimeEvent(const Event &event);
    void handleSaveExperienceDebtEvent(const Event &event);
    void handleMarkCharactersOnlineEvent(const Event &event);
};
