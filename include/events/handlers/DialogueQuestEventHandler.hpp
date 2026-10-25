#pragma once

#include "events/handlers/BaseEventHandler.hpp"

/**
 * @brief Dialogue / quest domain (third extracted domain).
 *
 * Static dialogue/quest catalog reads plus per-character quest state
 * (quests, flags, active effects, attribute refresh) and quest/flag updates.
 */
class DialogueQuestEventHandler : public BaseEventHandler
{
  public:
    DialogueQuestEventHandler(
        NetworkManager &networkManager,
        GameServices &gameServices);

    void handleGetDialoguesEvent(const Event &event);
    void handleGetQuestsEvent(const Event &event);
    void handleGetPlayerQuestsEvent(const Event &event);
    void handleGetPlayerFlagsEvent(const Event &event);
    void handleGetPlayerActiveEffectsEvent(const Event &event);
    void handleGetCharacterAttributesRefreshEvent(const Event &event);
    void handleUpdatePlayerQuestProgressEvent(const Event &event);
    void handleDeletePlayerQuestEvent(const Event &event);
    void handleUpdatePlayerFlagEvent(const Event &event);
};
