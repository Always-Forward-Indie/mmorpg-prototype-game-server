#include "events/handlers/CharacterEventHandler.hpp"

#include "data/DataStructs.hpp"
#include "events/Outbox.hpp"
#include "utils/ResponseBuilder.hpp"
#include "utils/TerminalColors.hpp"

CharacterEventHandler::CharacterEventHandler(
    NetworkManager &networkManager,
    GameServices &gameServices)
    : BaseEventHandler(networkManager, gameServices)
{
}

void
CharacterEventHandler::handleGetCharacterDataEvent(const Event &event)
{
    // Here we will init data from DB of the character when client joined and send it to the chunk server
    // Retrieve the data from the event
    const auto data = event.getData();
    int clientID = event.getClientID();

    // get socket from the event
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();

    // Extract init data
    try
    {
        // Try to extract the data
        if (std::holds_alternative<ClientDataStruct>(data))
        {
            ClientDataStruct passedClientData = std::get<ClientDataStruct>(data);
            // Save the clientData object with the new init data
            gameServices_.getClientManager().setClientData(passedClientData);

            // debug log
            gameServices_.getLogger().log("Passed - Client ID: " + std::to_string(passedClientData.clientId) +
                                          ", Character ID: " + std::to_string(passedClientData.characterId) +
                                          ", Hash: " + passedClientData.hash);

            // Get the character data from the database
            CharacterDataStruct characterData = gameServices_.getCharacterManager().loadCharacterFromDatabase(
                gameServices_.getDatabase(),
                passedClientData.clientId,
                passedClientData.characterId);

            if (characterData.characterId > 0)
            {
                gameServices_.getCharacterManager().setCharacterOnline(gameServices_.getDatabase(), characterData.characterId);
                gameServices_.getCharacterManager().touchCharacter(characterData.characterId);
            }

            // Get the clientData object with the new init data
            const ClientDataStruct currentClientData = gameServices_.getClientManager().getClientData(passedClientData.clientId);

            // Prepare the response message
            nlohmann::json response;
            ResponseBuilder builder;

            // Check if the authentication is not successful
            if (passedClientData.clientId == 0 || passedClientData.hash == "")
            {
                // Add response data
                response = builder
                               .setHeader("message", "Join Game Character failed for Client!")
                               .setHeader("hash", passedClientData.hash)
                               .setHeader("clientId", passedClientData.clientId)
                               .setHeader("eventType", "setCharacterData")
                               .setBody("", "")
                               .build();
                // Prepare a response message
                std::string responseData = networkManager_.generateResponseMessage("error", response);

                // Send the response to the chunk server
                networkManager_.sendResponse(
                    passedClientData.socket,
                    responseData);
                return;
            }

            // create attributes array
            nlohmann::json attributes = nlohmann::json::array();

            for (const auto &attribute : characterData.attributes)
            {
                nlohmann::json attributeData;
                attributeData["id"] = attribute.id;
                attributeData["slug"] = attribute.slug;
                attributeData["name"] = attribute.name;
                attributeData["value"] = attribute.value;

                // Set the character max health and mana
                if (attribute.slug == "max_health")
                {
                    characterData.characterMaxHealth = attribute.value;
                }
                if (attribute.slug == "max_mana")
                {
                    characterData.characterMaxMana = attribute.value;
                }

                attributes.push_back(attributeData);
            }

            // Cap current HP to base max as a sanity guard against stale test data.
            // HP currently has no permanent passive effects that raise it above base.
            if (characterData.characterMaxHealth > 0)
                characterData.characterCurrentHealth = std::min(characterData.characterCurrentHealth, characterData.characterMaxHealth);
            // NOTE: Do NOT clamp mana here with base characterMaxMana.
            // Passive skills (e.g. mana_shield) can legitimately raise effective max_mana
            // well above the base attribute value.  The chunk server already enforces the
            // correct effective cap (base + passive bonuses) during regen and healing.
            // Clamping to the base value here would silently discard legitimately saved mana.

            // New character detection: level 1, no XP, HP/MP at the init default of 1.
            // On first login, set HP/MP to the configured starting percentage of max.
            // Condition is strict enough (all four fields) to avoid affecting real characters.
            if (characterData.characterLevel == 1 &&
                characterData.characterExperiencePoints == 0 &&
                characterData.characterCurrentHealth == 1 &&
                characterData.characterCurrentMana == 1 &&
                characterData.characterMaxHealth > 1)
            {
                const auto &cfgMap = gameServices_.getGameConfigService().getAll();
                auto cfgGet = [&cfgMap](const std::string &key, float def) -> float
                {
                    auto it = cfgMap.find(key);
                    return (it != cfgMap.end()) ? std::stof(it->second) : def;
                };
                const float startHpPct = cfgGet("character.starting_hp_pct", 0.50f);
                const float startMpPct = cfgGet("character.starting_mp_pct", 0.50f);
                characterData.characterCurrentHealth = std::max(1, static_cast<int>(characterData.characterMaxHealth * startHpPct));
                characterData.characterCurrentMana = std::max(0, static_cast<int>(characterData.characterMaxMana * startMpPct));
                log_->info("[JOIN] New character {}: starting HP={}/{} MP={}/{} (pct={}/{})",
                    characterData.characterId,
                    characterData.characterCurrentHealth,
                    characterData.characterMaxHealth,
                    characterData.characterCurrentMana,
                    characterData.characterMaxMana,
                    startHpPct,
                    startMpPct);

                // Override position with class-specific spawn zone for new characters.
                // A random point within the zone bounds is selected so each new character
                // of the same class spawns at a different location.
                const auto *spawnZone = gameServices_.getClassSpawnZoneManager().getSpawnZoneForClass(characterData.classId);
                if (spawnZone)
                {
                    PositionStruct spawnPos = ClassSpawnZoneManager::getRandomPointInZone(*spawnZone);
                    characterData.characterPosition.positionX = spawnPos.positionX;
                    characterData.characterPosition.positionY = spawnPos.positionY;
                    characterData.characterPosition.positionZ = spawnPos.positionZ;
                    // Persist the new spawn position to DB so subsequent logins use it.
                    gameServices_.getCharacterManager().updateCharacterPosition(
                        gameServices_.getDatabase(), passedClientData.clientId,
                        characterData.characterId, characterData.characterPosition);
                    log_->info("[JOIN] New character {} assigned class spawn zone {} at ({},{},{})",
                        characterData.characterId,
                        spawnZone->className,
                        spawnPos.positionX,
                        spawnPos.positionY,
                        spawnPos.positionZ);
                }
                else
                {
                    log_->warn("[JOIN] New character {} class_id={} has no class spawn zone configured",
                        characterData.characterId, characterData.classId);
                }
            }

            // Create skills array
            nlohmann::json skills = nlohmann::json::array();

            for (const auto &skill : characterData.skills)
            {
                nlohmann::json skillData;
                skillData["skillName"] = skill.skillName;
                skillData["skillSlug"] = skill.skillSlug;
                skillData["scaleStat"] = skill.scaleStat;
                skillData["school"] = skill.school;
                skillData["skillEffectType"] = skill.skillEffectType;
                skillData["skillLevel"] = skill.skillLevel;
                skillData["coeff"] = skill.coeff;
                skillData["flatAdd"] = skill.flatAdd;
                skillData["cooldownMs"] = skill.cooldownMs;
                skillData["gcdMs"] = skill.gcdMs;
                skillData["castMs"] = skill.castMs;
                skillData["costMp"] = skill.costMp;
                skillData["maxRange"] = skill.maxRange;
                skillData["areaRadius"] = skill.areaRadius;
                skillData["swingMs"] = skill.swingMs;
                skillData["animationName"] = skill.animationName;
                skillData["isPassive"] = skill.isPassive;

                // Include effect definitions so the chunk server can apply buffs/heals on cast
                nlohmann::json effectsArr = nlohmann::json::array();
                for (const auto &eff : skill.effects)
                {
                    nlohmann::json effJson;
                    effJson["effectSlug"] = eff.effectSlug;
                    effJson["effectTypeSlug"] = eff.effectTypeSlug;
                    effJson["attributeSlug"] = eff.attributeSlug;
                    effJson["value"] = eff.value;
                    effJson["durationSeconds"] = eff.durationSeconds;
                    effJson["tickMs"] = eff.tickMs;
                    effectsArr.push_back(std::move(effJson));
                }
                skillData["effects"] = std::move(effectsArr);

                skills.push_back(skillData);
            }

            // Create skill bar slots array
            nlohmann::json skillBarData = nlohmann::json::array();
            for (const auto &slot : characterData.skillBarSlots)
            {
                nlohmann::json slotJson;
                slotJson["slotIndex"] = slot.slotIndex;
                slotJson["skillSlug"] = slot.skillSlug;
                skillBarData.push_back(slotJson);
            }

            // Add the message to the response
            response = builder
                           .setHeader("message", "Join Game Character success for Client!")
                           .setHeader("hash", currentClientData.hash)
                           .setHeader("clientId", currentClientData.clientId)
                           .setHeader("eventType", "setCharacterData")
                           .setBody("id", currentClientData.characterId)
                           .setBody("class", characterData.characterClass)
                           .setBody("classId", characterData.classId)
                           .setBody("level", characterData.characterLevel)
                           .setBody("expForNextLevel", characterData.expForNextLevel)
                           .setBody("name", characterData.characterName)
                           .setBody("race", characterData.characterRace)
                           .setBody("gender", characterData.characterGender)
                           .setBody("currentExp", characterData.characterExperiencePoints)
                           .setBody("experienceDebt", characterData.experienceDebt)
                           .setBody("freeSkillPoints", characterData.freeSkillPoints)
                           .setBody("currentHealth", characterData.characterCurrentHealth)
                           .setBody("currentMana", characterData.characterCurrentMana)
                           .setBody("maxHealth", characterData.characterMaxHealth)
                           .setBody("maxMana", characterData.characterMaxMana)
                           .setBody("posX", characterData.characterPosition.positionX)
                           .setBody("posY", characterData.characterPosition.positionY)
                           .setBody("posZ", characterData.characterPosition.positionZ)
                           .setBody("rotZ", characterData.characterPosition.rotationZ)
                           .setBody("attributesData", attributes)
                           .setBody("skillsData", skills)
                           .setBody("skillBarData", skillBarData)
                           .build();
            // Prepare a response message
            std::string responseData = networkManager_.generateResponseMessage("success", response);

            // Send the response to the chunk server
            networkManager_.sendResponse(
                clientSocket,
                responseData);
        }
        else
        {
            log_->info("Error with extracting data!");
        }
    }
    catch (const std::bad_variant_access &ex)
    {
        gameServices_.getLogger().log("Error here: " + std::string(ex.what()));
    }
}

void
CharacterEventHandler::handleSavePositionsEvent(const Event &event)
{
    const auto &data = event.getData();

    try    {
        if (!std::holds_alternative<std::vector<CharacterDataStruct>>(data))
        {
            log_->error("handleSavePositionsEvent: unexpected data type");
            return;
        }

        const auto &charactersList = std::get<std::vector<CharacterDataStruct>>(data);

        if (charactersList.empty())
        {
            log_->info("handleSavePositionsEvent: empty positions list, skipping");
            return;
        }

        int savedCount = 0;
        for (const auto &charData : charactersList)
        {
            if (charData.characterId <= 0)
                continue;

            gameServices_.getCharacterManager().updateCharacterPosition(
                gameServices_.getDatabase(),
                0, // accountId not needed here
                charData.characterId,
                charData.characterPosition);
            gameServices_.getCharacterManager().touchCharacter(charData.characterId);

            ++savedCount;
        }

        gameServices_.getLogger().log(
            "Periodic position save: persisted " + std::to_string(savedCount) + " character(s) to DB", GREEN);
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("Error in handleSavePositionsEvent: " + std::string(ex.what()));
    }
}

void
CharacterEventHandler::handleSaveHpManaEvent(const Event &event)
{
    const auto &data = event.getData();

    try
    {
        if (!std::holds_alternative<std::vector<CharacterDataStruct>>(data))
        {
            log_->error("handleSaveHpManaEvent: unexpected data type");
            return;
        }

        const auto &charactersList = std::get<std::vector<CharacterDataStruct>>(data);
        if (charactersList.empty())
            return;

        int savedCount = 0;
        for (const auto &charData : charactersList)
        {
            if (charData.characterId <= 0)
                continue;
            gameServices_.getCharacterManager().saveCharacterHpMana(
                gameServices_.getDatabase(),
                charData.characterId,
                charData.characterCurrentHealth,
                charData.characterCurrentMana);
            gameServices_.getCharacterManager().touchCharacter(charData.characterId);
            ++savedCount;
        }

        gameServices_.getLogger().log(
            "[SAVE_HP_MANA] Persisted HP/Mana for " + std::to_string(savedCount) + " character(s)", GREEN);
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("Error in handleSaveHpManaEvent: " + std::string(ex.what()));
    }
}

void
CharacterEventHandler::handleSaveCharacterProgressEvent(const Event &event)
{
    const auto &data = event.getData();

    try
    {
        if (!std::holds_alternative<std::vector<CharacterDataStruct>>(data))
        {
            log_->error("handleSaveCharacterProgressEvent: unexpected data type");
            return;
        }

        const auto &charactersList = std::get<std::vector<CharacterDataStruct>>(data);

        if (charactersList.empty())
        {
            log_->info("handleSaveCharacterProgressEvent: empty list, skipping");
            return;
        }

        int savedCount = 0;
        for (const auto &charData : charactersList)
        {
            if (charData.characterId <= 0 || charData.characterLevel <= 0)
                continue;

            gameServices_.getCharacterManager().updateCharacterExperienceAndLevel(
                gameServices_.getDatabase(),
                charData.characterId,
                charData.characterExperiencePoints,
                charData.characterLevel);
            ++savedCount;
        }

        gameServices_.getLogger().log(
            "Instant character progress save: persisted " + std::to_string(savedCount) + " character(s) exp/level to DB",
            GREEN);
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("Error in handleSaveCharacterProgressEvent: " + std::string(ex.what()));
    }
}

void
CharacterEventHandler::handleSaveExperienceDebtEvent(const Event &event)
{
    try
    {
        const auto &data = event.getData();
        if (!std::holds_alternative<nlohmann::json>(data))
            return;
        const auto &j = std::get<nlohmann::json>(data);
        int characterId = j.value("characterId", 0);
        int debt = j.value("experienceDebt", 0);
        if (characterId <= 0)
            return;

        auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
        pqxx::work txn(_dbConn.get());
        // Outbox idempotency: claim first (atomic with the writes below).
        const std::string debtFactKey = outboxFactKeyOf(j);
        if (!debtFactKey.empty())
        {
            const int debtClaimed = outboxClaimFactKey(
                gameServices_.getDatabase(), txn, debtFactKey);
            if (debtClaimed == 0)
            {
                outboxSendFactAck(networkManager_, event.getClientSocket(), debtFactKey, "duplicate");
                return;
            }
            if (debtClaimed < 0)
                return;
        }
        static_cast<void>(gameServices_.getDatabase().execChecked(txn, "set_character_experience_debt", {characterId, debt}));
        txn.commit();
        if (!debtFactKey.empty())
            outboxSendFactAck(networkManager_, event.getClientSocket(), debtFactKey, "applied");

        log_->info("[SAVE_EXP_DEBT] char=" + std::to_string(characterId) + " debt=" + std::to_string(debt));
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("handleSaveExperienceDebtEvent error: " + std::string(ex.what()));
    }
}

void
CharacterEventHandler::handleSavePlayTimeEvent(const Event &event)
{
    const auto &data = event.getData();
    if (!std::holds_alternative<PlayTimeDataStruct>(data))
    {
        log_->error("handleSavePlayTimeEvent: unexpected data type");
        return;
    }

    try
    {
        const auto &pt = std::get<PlayTimeDataStruct>(data);
        if (pt.characterId <= 0)
            return;

        gameServices_.getCharacterManager().updatePlayTime(
            gameServices_.getDatabase(),
            pt.characterId,
            pt.sessionPlayTimeSec,
            pt.lastSessionPlayTimeSec,
            pt.isDisconnect);
        if (pt.isDisconnect)
            gameServices_.getCharacterManager().forgetCharacter(pt.characterId);
        else
            gameServices_.getCharacterManager().touchCharacter(pt.characterId);
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("handleSavePlayTimeEvent error: " + std::string(ex.what()));
    }
}

void
CharacterEventHandler::handleMarkCharactersOnlineEvent(const Event &event)
{
    const auto &data = event.getData();

    try
    {
        if (!std::holds_alternative<nlohmann::json>(data))
        {
            log_->error("handleMarkCharactersOnlineEvent: unexpected data type");
            return;
        }

        const nlohmann::json &body = std::get<nlohmann::json>(data);
        if (!body.contains("characterIds") || !body["characterIds"].is_array())
        {
            log_->error("handleMarkCharactersOnlineEvent: missing characterIds array");
            return;
        }

        for (const auto &id : body["characterIds"])
        {
            int characterId = id.get<int>();
            if (characterId > 0)
            {
                gameServices_.getCharacterManager().setCharacterOnline(
                    gameServices_.getDatabase(), characterId);
                gameServices_.getCharacterManager().touchCharacter(characterId);
            }
        }

        log_->info("Marked " + std::to_string(body["characterIds"].size()) +
                    " characters as online after chunk-server reconnect");
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("handleMarkCharactersOnlineEvent error: " + std::string(ex.what()));
    }
}

void
CharacterEventHandler::handleMoveCharacterChunkEvent(const Event &event)
{
    // Live position mirror from chunk (moved here from EventHandler facade).
    // Retrieve the data from the event
    const auto &data = event.getData();
    int clientID = event.getClientID();

    // get socket from the event
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();

    try
    {
        // Try to extract the data
        if (std::holds_alternative<CharacterDataStruct>(data))
        {
            CharacterDataStruct passedCharacterData = std::get<CharacterDataStruct>(data);

            // Prepare the response message
            nlohmann::json response;
            ResponseBuilder builder;

            // Check if the authentication is not successful
            if (clientID == 0)
            {
                // Add response data
                response = builder
                               .setHeader("message", "Movement update failed for the Character!")
                               .setHeader("clientId", clientID)
                               .setHeader("eventType", "updateCharacterMovement")
                               .setBody("", "")
                               .build();
                // Prepare a response message
                std::string responseData = networkManager_.generateResponseMessage("error", response);

                // Send the response to the chunk server
                networkManager_.sendResponse(
                    clientSocket,
                    responseData);
                return;
            }

            // Position is persisted to DB only on disconnect (see handleDisconnectChunkEvent).
            // Writing on every move packet would produce 20+ SQL UPDATE/s per player.
            // Update the in-memory position so data is fresh for other GS queries.
            gameServices_.getCharacterManager().updateCharacterPositionInMemory(
                passedCharacterData.clientId,
                passedCharacterData.characterId,
                passedCharacterData.characterPosition);

            // Add the message to the response
            response = builder
                           .setHeader("message", "Movement success updated for the Character!")
                           .setHeader("clientId", passedCharacterData.clientId)
                           .setHeader("eventType", "updateCharacterMovement")
                           .setBody("characterId", passedCharacterData.characterId)
                           .setBody("posX", passedCharacterData.characterPosition.positionX)
                           .setBody("posY", passedCharacterData.characterPosition.positionY)
                           .setBody("posZ", passedCharacterData.characterPosition.positionZ)
                           .setBody("rotZ", passedCharacterData.characterPosition.rotationZ)
                           .build();
            // Prepare a response message
            std::string responseData = networkManager_.generateResponseMessage("success", response);

            log_->info("Sending data to Chunk Server: " + responseData);

            // Send the response to the chunk server
            networkManager_.sendResponse(
                clientSocket,
                responseData);
        }
        else
        {
            log_->info("Error with extracting data!");
        }
    }
    catch (const std::bad_variant_access &ex)
    {
        gameServices_.getLogger().log("Error here:" + std::string(ex.what()));
    }
}

void
CharacterEventHandler::handleDisconnectChunkEvent(const Event &event)
{
    // Disconnect persist + client evict (moved here from EventHandler facade).
    // Here we will disconnect the client
    const auto &data = event.getData();
    int clientID = event.getClientID();
    // get socket from the event
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();

    // Extract init data
    try
    {
        // Save last position to DB if we have a valid CharacterDataStruct
        if (std::holds_alternative<CharacterDataStruct>(data))
        {
            const CharacterDataStruct &charData = std::get<CharacterDataStruct>(data);
            if (charData.characterId > 0)
            {
                gameServices_.getCharacterManager().updateCharacterPosition(
                    gameServices_.getDatabase(),
                    clientID,
                    charData.characterId,
                    charData.characterPosition);
                log_->info(
                    "Saved position on disconnect for characterId: " + std::to_string(charData.characterId));
            }
        }

        if (clientID == 0)
        {
            log_->info("Client ID is 0, so we will just remove client from our list!");

            // Remove the client data by socket
            gameServices_.getClientManager().removeClientDataBySocket(clientSocket);

            return;
        }

        // Remove the client data
        gameServices_.getClientManager().removeClientData(clientID);

        // send the response to all clients
        nlohmann::json response;
        ResponseBuilder builder;
        response = builder
                       .setHeader("message", "Client disconnected!")
                       .setHeader("hash", "")
                       .setHeader("clientId", clientID)
                       .setHeader("eventType", "disconnectClient")
                       .setBody("", "")
                       .build();
        std::string responseData = networkManager_.generateResponseMessage("success", response);

        // Send the response to the client
        networkManager_.sendResponse(clientSocket, responseData);
    }
    catch (const std::bad_variant_access &ex)
    {
        gameServices_.getLogger().log("Error here:" + std::string(ex.what()));
    }
}
