#pragma once

#include "events/handlers/BaseEventHandler.hpp"

/**
 * @brief Title / emote domain (tenth extracted domain).
 *
 * Title definitions + per-player titles (+equip), emote definitions +
 * per-player emotes. Definition reads rethrow DatabaseError to
 * dispatchCatalog (JOIN entries).
 */
class SocialEventHandler : public BaseEventHandler
{
  public:
    SocialEventHandler(
        NetworkManager &networkManager,
        GameServices &gameServices);

    void handleGetTitleDefinitionsEvent(const Event &event);
    void handleGetPlayerTitlesEvent(const Event &event);
    void handleSavePlayerTitleEvent(const Event &event);
    void handleGetEmoteDefinitionsEvent(const Event &event);
    void handleGetPlayerEmotesEvent(const Event &event);
};
