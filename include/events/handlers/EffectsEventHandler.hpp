#pragma once

#include "events/handlers/BaseEventHandler.hpp"

/**
 * @brief Active-effects / cooldowns / analytics domain (eleventh extracted domain).
 */
class EffectsEventHandler : public BaseEventHandler
{
  public:
    EffectsEventHandler(
        NetworkManager &networkManager,
        GameServices &gameServices);

    void handleSaveActiveEffectEvent(const Event &event);
    void handleSaveSkillCooldownEvent(const Event &event);
    void handleGetPlayerSkillCooldownsEvent(const Event &event);
    void handleSaveAnalyticsEventEvent(const Event &event);
};
