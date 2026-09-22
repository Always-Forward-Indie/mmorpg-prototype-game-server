#pragma once

#include "events/handlers/BaseEventHandler.hpp"

/**
 * @brief Reputation / mastery / learned-skills domain (ninth extracted domain).
 *
 * Includes the learned-skill save with live-socket resolution (stale
 * chunk-link guard) and skill-bar persistence.
 */
class SkillsEventHandler : public BaseEventHandler
{
  public:
    SkillsEventHandler(
        NetworkManager &networkManager,
        GameServices &gameServices);

    void handleGetPlayerReputationsEvent(const Event &event);
    void handleSaveReputationEvent(const Event &event);
    void handleGetPlayerMasteriesEvent(const Event &event);
    void handleSaveMasteryEvent(const Event &event);
    void handleSaveLearnedSkillEvent(const Event &event);
    void handleSaveSkillBarSlotEvent(const Event &event);
};
