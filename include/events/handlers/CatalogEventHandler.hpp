#pragma once

#include "events/handlers/BaseEventHandler.hpp"

/**
 * @brief Static-catalog read domain (sixth extracted domain).
 *
 * All JOIN catalog entries served by direct DB reads. DatabaseError is
 * rethrown past the handler catch so dispatchCatalog counts the entry as
 * failed (fail-loud incomplete JOIN, never a partial success).
 */
class CatalogEventHandler : public BaseEventHandler
{
  public:
    CatalogEventHandler(
        NetworkManager &networkManager,
        GameServices &gameServices);

    void handleGetExpLevelTableEvent(const Event &event);
    void handleGetCharacterExpForLevelEvent(const Event &event);
    void handleGetGameConfigEvent(const Event &event);
    void handleGetRespawnZonesEvent(const Event &event);
    void handleGetClassSpawnZonesEvent(const Event &event);
    void handleGetStatusEffectTemplatesEvent(const Event &event);
    void handleGetGameZonesEvent(const Event &event);
    void handleGetMasteryDefinitionsEvent(const Event &event);
    void handleGetNPCAmbientSpeechEvent(const Event &event);
    void handleGetWorldObjectsEvent(const Event &event);
    void handleGetZoneEventTemplatesEvent(const Event &event);
};
