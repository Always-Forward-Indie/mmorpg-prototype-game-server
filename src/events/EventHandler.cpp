#include "events/EventHandler.hpp"
#include "data/DataStructs.hpp"
#include "events/Outbox.hpp"
#include "utils/TerminalColors.hpp"
#include "utils/TimestampUtils.hpp"

#include "events/Event.hpp"
#include <chrono>
#include <cstdlib>
#include <spdlog/logger.h>

EventHandler::EventHandler(
    NetworkManager &networkManager,
    GameServices &gameServices)
    : networkManager_(networkManager),
      gameServices_(gameServices),
      vendorHandler_(networkManager, gameServices),
      itemHandler_(networkManager, gameServices),
      worldHandler_(networkManager, gameServices),
      catalogHandler_(networkManager, gameServices),
      inventoryHandler_(networkManager, gameServices),
      characterHandler_(networkManager, gameServices),
      dialogueQuestHandler_(networkManager, gameServices)
{
    log_ = gameServices_.getLogger().getSystem("events");
}

std::shared_ptr<std::mutex>
EventHandler::joinLockFor(int chunkId)
{
    std::lock_guard<std::mutex> lock(joinMapMutex_);
    auto it = joinLocks_.find(chunkId);
    if (it != joinLocks_.end())
        return it->second;
    auto m = std::make_shared<std::mutex>();
    joinLocks_.emplace(chunkId, m);
    return m;
}

void
EventHandler::handleJoinPlayerClientEvent(const Event &event)
{
    // Retrieve the data from the event
    const auto &data = event.getData();
    int clientID = event.getClientID();

    // get socket from the event
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();

    try
    {
        // Try to extract the data
        if (std::holds_alternative<ClientDataStruct>(data))
        {
            ClientDataStruct passedClientData = std::get<ClientDataStruct>(data);
            // Save the clientData object with the new init data
            gameServices_.getClientManager().setClientData(passedClientData);

            // get chunk server connection data
            ChunkInfoStruct chunkServerData = gameServices_.getChunkManager().getChunkById(1);

            // Prepare a response message
            nlohmann::json response;
            ResponseBuilder builder;

            // Check if the authentication is not successful
            if (passedClientData.clientId == 0 || passedClientData.hash == "")
            {
                // Add response data
                response = builder
                               .setHeader("message", "Authentication failed for user!")
                               .setHeader("hash", passedClientData.hash)
                               .setHeader("clientId", passedClientData.clientId)
                               .setHeader("eventType", "joinGameClient")
                               .setBody("", "")
                               .build();
                // Prepare a response message
                std::string responseData = networkManager_.generateResponseMessage("error", response);

                // Send the response to the Client
                networkManager_.sendResponse(
                    passedClientData.socket,
                    responseData);
                return;
            }

            // generate chunk server connection data
            nlohmann::json chunkServerDataJson;
            chunkServerDataJson["chunkId"] = chunkServerData.id;
            // Clients may live outside the Docker net: advertise the public
            // host when configured, keep the internal one for server-to-server.
            {
                const char* publicHost = std::getenv("CHUNK_PUBLIC_HOST");
                chunkServerDataJson["chunkIp"] = (publicHost && publicHost[0] != '\0')
                    ? publicHost
                    : chunkServerData.ip;
            }
            chunkServerDataJson["chunkPort"] = chunkServerData.port;
            chunkServerDataJson["chunkPosX"] = chunkServerData.posX;
            chunkServerDataJson["chunkPosY"] = chunkServerData.posY;
            chunkServerDataJson["chunkPosZ"] = chunkServerData.posZ;
            chunkServerDataJson["chunkSizeX"] = chunkServerData.sizeX;
            chunkServerDataJson["chunkSizeY"] = chunkServerData.sizeY;
            chunkServerDataJson["chunkSizeZ"] = chunkServerData.sizeZ;

            // Add the message to the response
            response = builder
                           .setHeader("message", "Authentication success for user!")
                           .setHeader("hash", passedClientData.hash)
                           .setHeader("clientId", passedClientData.clientId)
                           .setHeader("eventType", "joinGameClient")
                           .setBody("chunkServerData", chunkServerDataJson)
                           .build();
            // Prepare a response message
            std::string responseData = networkManager_.generateResponseMessage("success", response);

            // Send the response to the Client
            networkManager_.sendResponse(
                passedClientData.socket,
                responseData);

            log_->info("Sending data to the Client: " + responseData);

            // Dispatch the event to get character data and send it to the chunk server
            Event getCharacterDataEvent(Event::GET_CHARACTER_DATA, clientID, passedClientData, chunkServerData.socket);
            dispatchEvent(getCharacterDataEvent);
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
EventHandler::handleMoveCharacterChunkEvent(const Event &event)
{
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
        gameServices_.getLogger().log("Error here: " + std::string(ex.what()));
    }
}

// disconnect the client
void
EventHandler::handleDisconnectChunkEvent(const Event &event)
{
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

// join the chunk server
void
EventHandler::handleJoinChunkServerEvent(const Event &event)
{
    // get the data from the event
    const auto &data = event.getData();
    int clientID = event.getClientID();
    // get socket from the event
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();

    // prepare the chunk server data
    nlohmann::json chunkServerDataJson;

    try
    {
        // Prepare the response message
        nlohmann::json response;
        ResponseBuilder builder;

        // Check if the authentication is not successful
        if (clientID == 0)
        {
            // Add response data
            response = builder
                           .setHeader("message", "Joining chunk server failed!")
                           .setHeader("hash", "")
                           .setHeader("clientId", clientID)
                           .setHeader("eventType", "setChunkData")
                           .setBody("chunkServerData", chunkServerDataJson)
                           .build();
            // Prepare a response message
            std::string responseData = networkManager_.generateResponseMessage("error", response);
            // Send the response to the client
            networkManager_.sendResponse(clientSocket, responseData);
            return;
        }

        // Try to extract the data
        if (std::holds_alternative<ChunkInfoStruct>(data))
        {
            ChunkInfoStruct chunkData = std::get<ChunkInfoStruct>(data);

            // Wave 1.7: id <= 0 is the "missing header.id" default, never a
            // real chunk (the real chunk hardcodes header.id = 1). Registering
            // it poisons joinGameClient with CHUNKID_0 and needs a chunk
            // reboot to clear — reject loudly instead (alert grep:
            // "[ChunkManager]").
            if (chunkData.id <= 0)
            {
                gameServices_.getLogger().log(
                    "[ChunkManager] rejecting chunk handshake with id=" +
                    std::to_string(chunkData.id) + " (malformed chunkServerConnection, no registration)");
                response = builder
                               .setHeader("message", "Joining chunk server failed: invalid chunk id!")
                               .setHeader("hash", "")
                               .setHeader("clientId", clientID)
                               .setHeader("eventType", "setChunkData")
                               .setBody("chunkServerData", chunkServerDataJson)
                               .build();
                std::string responseData = networkManager_.generateResponseMessage("error", response);
                networkManager_.sendResponse(clientSocket, responseData);
                return;
            }

            const char* envChunkHost = std::getenv("CHUNK_SERVER_HOST");
            if (envChunkHost && envChunkHost[0] != '\0') {
                chunkData.ip = envChunkHost;
            }
            chunkData.socket = clientSocket; // Set the socket for the chunk server

            // Heartbeat re-assert vs first boot: chunk re-sends
            // chunkServerConnection every 60s on the SAME socket. Re-pushing
            // the full catalog on every heartbeat wipes chunk-side runtime
            // state (timed-champion spawned flags -> infinite respawn every
            // minute) and wastes bandwidth. Same socket => refresh
            // registration, ack, skip the 24 boot dispatches below. A new
            // socket (chunk restarted) or unknown id (game restarted) =>
            // full push as before (fast recovery, no FIN dependence).
            const ChunkInfoStruct prevReg =
                gameServices_.getChunkManager().getChunkById(chunkData.id);
            const bool sameSocket =
                prevReg.socket && clientSocket && prevReg.socket == clientSocket;

            // Save the chunk data to memory
            gameServices_.getChunkManager().addChunkInfo(chunkData);

            chunkServerDataJson["id"] = chunkData.id;
            chunkServerDataJson["ip"] = chunkData.ip;
            chunkServerDataJson["port"] = chunkData.port;
            chunkServerDataJson["posX"] = chunkData.posX;
            chunkServerDataJson["posY"] = chunkData.posY;
            chunkServerDataJson["posZ"] = chunkData.posZ;
            chunkServerDataJson["sizeX"] = chunkData.sizeX;
            chunkServerDataJson["sizeY"] = chunkData.sizeY;
            chunkServerDataJson["sizeZ"] = chunkData.sizeZ;

            // Full catalog push only for a new link (see sameSocket above).
            // Heartbeat re-asserts skip straight to the ack below.
            if (!sameSocket)
            {
            // Serialize concurrent JOINs for one chunk id (flap + heartbeat
            // race): the push is ~24 sequential DB round-trips and two
            // interleaved pushes duplicate the whole catalog chunk-side.
            // Heartbeats skip this block, so steady-state is unaffected.
            std::lock_guard<std::mutex> joinGuard(*joinLockFor(chunkData.id));
            const auto joinStart = std::chrono::steady_clock::now();
            int catalogFailed = 0;
            // One bad catalog entry must not kill the whole join: each
            // dispatch runs isolated (pool timeout, bad row, etc.), failures
            // are counted and reported. Previously any std::exception here
            // skipped the remaining entries AND the ack below, leaving the
            // chunk booted partial with no ack (hang until next heartbeat).
            auto dispatchCatalog = [this, &catalogFailed](const Event &e, const char *name)
            {
                try
                {
                    dispatchEvent(e);
                }
                catch (const std::exception &ex)
                {
                    ++catalogFailed;
                    log_->error(std::string("[join] catalog '") + name + "' failed: " + ex.what());
                }
            };
            // load spawn zones
            Event spawnZonesEvent(Event::GET_SPAWN_ZONES, clientID, SpawnZoneStruct(), clientSocket);
            dispatchCatalog(spawnZonesEvent, "spawn_zones");

            // load mobs
            Event mobDataEvent(Event::GET_MOBS_LIST, clientID, MobDataStruct(), clientSocket);
            dispatchCatalog(mobDataEvent, "mobs");

            // load mobs attributes
            Event mobAttributesEvent(Event::GET_MOBS_ATTRIBUTES, clientID, MobAttributeStruct(), clientSocket);
            dispatchCatalog(mobAttributesEvent, "mobs_attributes");

            // load NPCs
            Event npcDataEvent(Event::GET_NPCS_LIST, clientID, NPCDataStruct(), clientSocket);
            dispatchCatalog(npcDataEvent, "npcs");

            // load NPCs attributes
            Event npcAttributesEvent(Event::GET_NPCS_ATTRIBUTES, clientID, NPCAttributeStruct(), clientSocket);
            dispatchCatalog(npcAttributesEvent, "npcs_attributes");

            // load items
            Event itemsEvent(Event::GET_ITEMS_LIST, clientID, ItemDataStruct(), clientSocket);
            dispatchCatalog(itemsEvent, "items");

            // load mob loot info
            Event mobLootEvent(Event::GET_MOB_LOOT_INFO, clientID, MobLootInfoStruct(), clientSocket);
            dispatchCatalog(mobLootEvent, "mob_loot");

            // load experience level table
            Event expLevelTableEvent(Event::GET_EXP_LEVEL_TABLE, clientID, ClientDataStruct(), clientSocket);
            dispatchCatalog(expLevelTableEvent, "exp_table");

            // load dialogues and NPC dialogue mappings
            Event dialoguesEvent(Event::GET_DIALOGUES, clientID, ClientDataStruct(), clientSocket);
            dispatchCatalog(dialoguesEvent, "dialogues");

            // load quests
            Event questsEvent(Event::GET_QUESTS, clientID, ClientDataStruct(), clientSocket);
            dispatchCatalog(questsEvent, "quests");

            // load game config
            gameServices_.getGameConfigService().loadConfig(gameServices_.getDatabase());
            Event gameConfigEvent(Event::GET_GAME_CONFIG, clientID, ClientDataStruct(), clientSocket);
            dispatchCatalog(gameConfigEvent, "game_config");

            // load vendor NPC inventory
            Event vendorDataEvent(Event::GET_VENDOR_DATA, clientID, ClientDataStruct(), clientSocket);
            dispatchCatalog(vendorDataEvent, "vendor");

            // load trainer NPC skill lists
            Event trainerDataEvent(Event::GET_TRAINER_DATA, clientID, ClientDataStruct(), clientSocket);
            dispatchCatalog(trainerDataEvent, "trainer");

            // load respawn zones
            Event respawnZonesEvent(Event::GET_RESPAWN_ZONES, clientID, ClientDataStruct(), clientSocket);
            dispatchCatalog(respawnZonesEvent, "respawn_zones");

            // load class spawn zones
            Event classSpawnZonesEvent(Event::GET_CLASS_SPAWN_ZONES, clientID, ClientDataStruct(), clientSocket);
            dispatchCatalog(classSpawnZonesEvent, "class_spawn_zones");

            // load game zones (AABB bounds + exploration XP)
            Event gameZonesEvent(Event::GET_GAME_ZONES, clientID, ClientDataStruct(), clientSocket);
            dispatchCatalog(gameZonesEvent, "game_zones");

            // load status effect templates (data-driven buff/debuff config)
            Event statusEffectTemplatesEvent(Event::GET_STATUS_EFFECT_TEMPLATES, clientID, ClientDataStruct(), clientSocket);
            dispatchCatalog(statusEffectTemplatesEvent, "status_effects");

            // load timed champion templates (Stage 3 — mob ecosystem)
            Event timedChampionTemplatesEvent(Event::GET_TIMED_CHAMPION_TEMPLATES, clientID, ClientDataStruct(), clientSocket);
            dispatchCatalog(timedChampionTemplatesEvent, "timed_champions");

            // load zone event templates (Stage 4 — world events)
            Event zoneEventTemplatesEvent(Event::GET_ZONE_EVENT_TEMPLATES, clientID, ClientDataStruct(), clientSocket);
            dispatchCatalog(zoneEventTemplatesEvent, "zone_events");

            // load mastery definitions (global catalog — defines which attribute each mastery type buffs)
            Event masteryDefsEvent(Event::GET_MASTERY_DEFINITIONS, clientID, 0, clientSocket);
            dispatchCatalog(masteryDefsEvent, "mastery_defs");

            // load title definitions (global catalog — same lifetime as zone templates)
            Event titleDefsEvent(Event::GET_TITLE_DEFINITIONS, clientID, 0, clientSocket);
            dispatchCatalog(titleDefsEvent, "title_defs");

            // load emote definitions (global catalog)
            Event emoteDefsEvent(Event::GET_EMOTE_DEFINITIONS, clientID, 0, clientSocket);
            dispatchCatalog(emoteDefsEvent, "emote_defs");

            // load NPC ambient speech configs + lines
            Event ambientSpeechEvent(Event::GET_NPC_AMBIENT_SPEECH, clientID, 0, clientSocket);
            dispatchCatalog(ambientSpeechEvent, "ambient_speech");

            // load world interactive objects (migration 043)
            Event worldObjectsEvent(Event::GET_WORLD_OBJECTS, clientID, 0, clientSocket);
            dispatchCatalog(worldObjectsEvent, "world_objects");

            const auto joinMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - joinStart).count();
            // NOTE: root logger, not the events system logger: LOG_LEVEL_EVENTS=warn
            // in dev env filters info, and join summaries must stay visible (the
            // per-entry failure above is error-level so it always shows).
            gameServices_.getLogger().info("[join] full catalog push chunk=" + std::to_string(chunkData.id) +
                       " entries=24 failed=" + std::to_string(catalogFailed) +
                       " tookMs=" + std::to_string(joinMs));
            if (catalogFailed > 0)
            {
                // Fail loud + self-heal: drop the registration so the next
                // 60s heartbeat is treated as a new link (unknown id) and
                // retries the full push. Acking success on a partial catalog
                // would boot a broken world with no retry (sameSocket path
                // skips the push).
                gameServices_.getChunkManager().removeChunkServerDataById(chunkData.id);
                response = builder
                               .setHeader("message", "ERR_JOIN_CATALOG_INCOMPLETE")
                               .setHeader("hash", "")
                               .setHeader("clientId", clientID)
                               .setHeader("eventType", "setChunkData")
                               .setBody("chunkServerData", chunkServerDataJson)
                               .build();
                networkManager_.sendResponse(clientSocket, networkManager_.generateResponseMessage("error", response));
                return;
            }
            } // if (!sameSocket): end of full catalog push
        }

        // Add the message to the response
        response = builder
                       .setHeader("message", "Joining chunk server success!")
                       .setHeader("hash", "")
                       .setHeader("clientId", clientID)
                       .setHeader("eventType", "setChunkData")
                       .setBody("id", chunkServerDataJson["id"])
                       .setBody("ip", chunkServerDataJson["ip"])
                       .setBody("port", chunkServerDataJson["port"])
                       .setBody("posX", chunkServerDataJson["posX"])
                       .setBody("posY", chunkServerDataJson["posY"])
                       .setBody("posZ", chunkServerDataJson["posZ"])
                       .setBody("sizeX", chunkServerDataJson["sizeX"])
                       .setBody("sizeY", chunkServerDataJson["sizeY"])
                       .setBody("sizeZ", chunkServerDataJson["sizeZ"])
                       .build();
        // Prepare a response message
        std::string responseData = networkManager_.generateResponseMessage("success", response);

        // Send the response to the client
        networkManager_.sendResponse(clientSocket, responseData);
    }
    catch (const std::bad_variant_access &ex)
    {
        gameServices_.getLogger().log("Error here: " + std::string(ex.what()));
    }
}

void
EventHandler::handleDisconnectChunkServerEvent(const Event &event)
{
    // Here we will disconnect the chunk server
    const auto data = event.getData();
    int clientID = event.getClientID();
    // get socket from the event
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();

    // Extract init data
    try
    {
        if (clientID == 0)
        {
            log_->info("Chunk Server ID is 0, so we will just remove chunk server from our list!");

            // Remove the chunk server data by socket
            gameServices_.getChunkManager().removeChunkServerDataBySocket(clientSocket);

            // Individual per-character disconnect events already handle is_online=false via
            // savePlayTime(isDisconnect:true). Bulk resetAllOnline() here was a false-positive
            // trigger that marked still-connected players as offline on any connection blip.

            return;
        }

        // Remove the chunk server data
        gameServices_.getChunkManager().removeChunkServerDataById(clientID);

        // Individual per-character disconnect events already handle is_online=false via
        // savePlayTime(isDisconnect:true). Bulk resetAllOnline() here was a false-positive
        // trigger that marked still-connected players as offline on any connection blip.

        // send the response to all clients
        nlohmann::json response;
        ResponseBuilder builder;
        response = builder
                       .setHeader("message", "Chunk Server disconnected!")
                       .setHeader("hash", "")
                       .setHeader("clientId", clientID)
                       .setHeader("eventType", "disconnectChunkServer")
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

// handle get mobs attributes event

// handle get mobs list event

// handle get mob data event

// handle get spawn zones event

// events dispatcher
void
EventHandler::dispatchEvent(const Event &event)
{
    switch (event.getType())
    {
    // character/client events
    case Event::PING_CLIENT:
        handlePingClientEvent(event);
        break;
    case Event::JOIN_PLAYER_CLIENT:
        handleJoinPlayerClientEvent(event);
        break;

    case Event::GET_CHARACTER_DATA:
        characterHandler_.handleGetCharacterDataEvent(event);
        break;
    case Event::MOVE_CHARACTER:
        handleMoveCharacterChunkEvent(event);
        break;
    case Event::DISCONNECT_CLIENT:
        handleDisconnectChunkEvent(event);
        break;

    // spawn zones events
    case Event::GET_SPAWN_ZONES:
        worldHandler_.handleGetSpawnZonesEvent(event);
        break;

    // mobs events
    case Event::GET_MOBS_LIST:
        worldHandler_.handleGetMobsListEvent(event);
        break;
    case Event::GET_MOBS_ATTRIBUTES:
        worldHandler_.handleGetMobsAttributesEvent(event);
        break;
    case Event::GET_MOB_DATA:
        worldHandler_.handleGetMobDataEvent(event);
        break;
    case Event::GET_CHARACTER_EXP_FOR_LEVEL:
        catalogHandler_.handleGetCharacterExpForLevelEvent(event);
        break;
    case Event::GET_EXP_LEVEL_TABLE:
        catalogHandler_.handleGetExpLevelTableEvent(event);
        break;

    // items events
    case Event::GET_ITEMS_LIST:
        worldHandler_.handleGetItemsListEvent(event);
        break;
    case Event::GET_MOB_LOOT_INFO:
        worldHandler_.handleGetMobLootInfoEvent(event);
        break;

    // NPC events
    case Event::GET_NPCS_LIST:
        worldHandler_.handleGetNPCsListEvent(event);
        break;
    case Event::GET_NPCS_ATTRIBUTES:
        worldHandler_.handleGetNPCsAttributesEvent(event);
        break;

    case Event::SAVE_POSITIONS:
        characterHandler_.handleSavePositionsEvent(event);
        break;

    case Event::SAVE_HP_MANA:
        characterHandler_.handleSaveHpManaEvent(event);
        break;

    case Event::SAVE_CHARACTER_PROGRESS:
        characterHandler_.handleSaveCharacterProgressEvent(event);
        break;

    case Event::SAVE_INVENTORY_CHANGE:
        inventoryHandler_.handleSaveInventoryChangeEvent(event);
        break;

    case Event::GET_PLAYER_INVENTORY:
        inventoryHandler_.handleGetPlayerInventoryEvent(event);
        break;

    // Dialogue & Quest events
    case Event::GET_DIALOGUES:
        dialogueQuestHandler_.handleGetDialoguesEvent(event);
        break;
    case Event::GET_QUESTS:
        dialogueQuestHandler_.handleGetQuestsEvent(event);
        break;
    case Event::GET_PLAYER_QUESTS:
        dialogueQuestHandler_.handleGetPlayerQuestsEvent(event);
        break;
    case Event::GET_PLAYER_FLAGS:
        dialogueQuestHandler_.handleGetPlayerFlagsEvent(event);
        break;
    case Event::GET_PLAYER_ACTIVE_EFFECTS:
        dialogueQuestHandler_.handleGetPlayerActiveEffectsEvent(event);
        break;
    case Event::GET_CHARACTER_ATTRIBUTES_REFRESH:
        dialogueQuestHandler_.handleGetCharacterAttributesRefreshEvent(event);
        break;
    case Event::UPDATE_PLAYER_QUEST_PROGRESS:
        dialogueQuestHandler_.handleUpdatePlayerQuestProgressEvent(event);
        break;
    case Event::UPDATE_PLAYER_FLAG:
        dialogueQuestHandler_.handleUpdatePlayerFlagEvent(event);
        break;
    case Event::GET_GAME_CONFIG:
        catalogHandler_.handleGetGameConfigEvent(event);
        break;
    case Event::GET_VENDOR_DATA:
        vendorHandler_.handleGetVendorDataEvent(event);
        break;
    case Event::GET_TRAINER_DATA:
        vendorHandler_.handleGetTrainerDataEvent(event);
        break;
    case Event::SAVE_DURABILITY_CHANGE:
        itemHandler_.handleSaveDurabilityChangeEvent(event);
        break;
    case Event::SAVE_CURRENCY_TRANSACTION:
        itemHandler_.handleSaveCurrencyTransactionEvent(event);
        break;
    case Event::SAVE_EQUIPMENT_CHANGE:
        itemHandler_.handleSaveEquipmentChangeEvent(event);
        break;
    case Event::GET_RESPAWN_ZONES:
        catalogHandler_.handleGetRespawnZonesEvent(event);
        break;
    case Event::GET_CLASS_SPAWN_ZONES:
        catalogHandler_.handleGetClassSpawnZonesEvent(event);
        break;
    case Event::GET_STATUS_EFFECT_TEMPLATES:
        catalogHandler_.handleGetStatusEffectTemplatesEvent(event);
        break;
    case Event::GET_GAME_ZONES:
        catalogHandler_.handleGetGameZonesEvent(event);
        break;
    case Event::SAVE_EXPERIENCE_DEBT:
        characterHandler_.handleSaveExperienceDebtEvent(event);
        break;
    case Event::SAVE_ACTIVE_EFFECT:
        handleSaveActiveEffectEvent(event);
        break;
    case Event::SAVE_ITEM_KILL_COUNT:
        itemHandler_.handleSaveItemKillCountEvent(event);
        break;
    case Event::TRANSFER_INVENTORY_ITEM:
        itemHandler_.handleTransferInventoryItemEvent(event);
        break;
    case Event::NULLIFY_ITEM_OWNER:
        itemHandler_.handleNullifyItemOwnerEvent(event);
        break;
    case Event::DELETE_INVENTORY_ITEM:
        itemHandler_.handleDeleteInventoryItemEvent(event);
        break;
    case Event::GET_PLAYER_PITY:
        handleGetPlayerPityEvent(event);
        break;
    case Event::GET_PLAYER_BESTIARY:
        handleGetPlayerBestiaryEvent(event);
        break;
    case Event::SAVE_PITY_COUNTER:
        handleSavePityCounterEvent(event);
        break;
    case Event::SAVE_BESTIARY_KILL:
        handleSaveBestiaryKillEvent(event);
        break;
    case Event::GET_TIMED_CHAMPION_TEMPLATES:
        handleGetTimedChampionTemplatesEvent(event);
        break;
    case Event::TIMED_CHAMPION_KILLED:
        handleTimedChampionKilledEvent(event);
        break;

    // Stage 4: Reputation & Mastery
    case Event::GET_PLAYER_REPUTATIONS:
        handleGetPlayerReputationsEvent(event);
        break;
    case Event::SAVE_REPUTATION:
        handleSaveReputationEvent(event);
        break;
    case Event::GET_PLAYER_MASTERIES:
        handleGetPlayerMasteriesEvent(event);
        break;
    case Event::SAVE_MASTERY:
        handleSaveMasteryEvent(event);
        break;
    case Event::SAVE_LEARNED_SKILL:
        handleSaveLearnedSkillEvent(event);
        break;
    case Event::SAVE_SKILL_BAR_SLOT:
        handleSaveSkillBarSlotEvent(event);
        break;
    case Event::GET_ZONE_EVENT_TEMPLATES:
        catalogHandler_.handleGetZoneEventTemplatesEvent(event);
        break;

    // Mastery definition system
    case Event::GET_MASTERY_DEFINITIONS:
        catalogHandler_.handleGetMasteryDefinitionsEvent(event);
        break;

    // Title system
    case Event::GET_TITLE_DEFINITIONS:
        handleGetTitleDefinitionsEvent(event);
        break;
    case Event::GET_PLAYER_TITLES:
        handleGetPlayerTitlesEvent(event);
        break;
    case Event::SAVE_PLAYER_TITLE:
        handleSavePlayerTitleEvent(event);
        break;

    // Emote system
    case Event::GET_EMOTE_DEFINITIONS:
        handleGetEmoteDefinitionsEvent(event);
        break;
    case Event::GET_PLAYER_EMOTES:
        handleGetPlayerEmotesEvent(event);
        break;

    // NPC Ambient Speech system
    case Event::GET_NPC_AMBIENT_SPEECH:
        catalogHandler_.handleGetNPCAmbientSpeechEvent(event);
        break;

    // World Interactive Objects (migration 043)
    case Event::GET_WORLD_OBJECTS:
        catalogHandler_.handleGetWorldObjectsEvent(event);
        break;

    // Skill cooldown persistence (migration 067)
    case Event::SAVE_SKILL_COOLDOWN:
        handleSaveSkillCooldownEvent(event);
        break;
    case Event::GET_PLAYER_SKILL_COOLDOWNS:
        handleGetPlayerSkillCooldownsEvent(event);
        break;

    // Analytics system (migration 058)
    case Event::SAVE_ANALYTICS_EVENT:
        handleSaveAnalyticsEventEvent(event);
        break;

    case Event::SAVE_PLAY_TIME:
        characterHandler_.handleSavePlayTimeEvent(event);
        break;

    case Event::MARK_CHARACTERS_ONLINE:
        characterHandler_.handleMarkCharactersOnlineEvent(event);
        break;

    // chunk server events
    case Event::JOIN_CHUNK_SERVER:
        handleJoinChunkServerEvent(event);
        break;
    case Event::DISCONNECT_CHUNK_SERVER:
        handleDisconnectChunkServerEvent(event);
        break;
    }
}

// handle get items list event

// handle get mob loot info event

// handle ping client event
void
EventHandler::handlePingClientEvent(const Event &event)
{
    // Retrieve the data from the event
    const auto &data = event.getData();
    int clientID = event.getClientID();

    // get socket from the event
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();

    log_->info("Handling PING event for client ID: " + std::to_string(clientID));

    if (!clientSocket || !clientSocket->is_open())
    {
        log_->info("Skipping ping - socket is closed for client ID: " + std::to_string(clientID));
        return;
    }

    try
    {
        // Try to extract the data
        if (std::holds_alternative<ClientDataStruct>(data))
        {
            ClientDataStruct passedClientData = std::get<ClientDataStruct>(data);

            // Check if we have timestamps (for request-response lag compensation)
            TimestampStruct timestamps;
            bool hasTimestamps = false;

            try
            {
                timestamps = event.getTimestamps();
                hasTimestamps = true;
            }
            catch (const std::exception &)
            {
                // Event doesn't have timestamps, create new ones
                timestamps = TimestampUtils::createTimestamp();
                hasTimestamps = true;
            }

            // Prepare a response message
            nlohmann::json response;
            ResponseBuilder builder;

            response = builder
                           .setHeader("message", "Pong!")
                           .setHeader("hash", passedClientData.hash)
                           .setHeader("clientId", passedClientData.clientId)
                           .setHeader("eventType", "pingClient")
                           .setTimestamps(timestamps)
                           .setBody("", "")
                           .build();

            // Prepare a response message with timestamps
            std::string responseData = networkManager_.generateResponseMessage("success", response, timestamps);

            // Send the response to the client
            networkManager_.sendResponse(clientSocket, responseData);

            log_->info("Sending PING response with timestamps to Client ID: " + std::to_string(clientID));
        }
        else
        {
            log_->error("Error extracting data from ping event for client ID: " + std::to_string(clientID));
        }
    }
    catch (const std::bad_variant_access &ex)
    {
        gameServices_.getLogger().logError("Variant access error in ping event for client ID " + std::to_string(clientID) + ": " + std::string(ex.what()));
    }
}



// handle get NPCs list event

// handle get NPCs attributes event


// ARCH-4: Handles the periodic HP/Mana snapshot from chunk-server.




// ---------------------------------------------------------------------------
// Dialogue & Quest handlers
// ---------------------------------------------------------------------------

void
EventHandler::handleSaveActiveEffectEvent(const Event &event)
{
    try
    {
        const auto &data = event.getData();
        if (!std::holds_alternative<nlohmann::json>(data))
            return;
        const auto &j = std::get<nlohmann::json>(data);

        int characterId = j.value("characterId", 0);
        std::string effectSlug = j.value("effectSlug", std::string(""));
        std::string attributeSlug = j.value("attributeSlug", std::string(""));
        std::string sourceType = j.value("sourceType", std::string("death"));
        double value = j.value("value", 0.0);
        int64_t expiresAt = j.value("expiresAt", int64_t(0));
        int tickMs = j.value("tickMs", 0);

        if (characterId <= 0 || effectSlug.empty())
            return;

        // $1=player_id $2=effect_slug $3=attribute_slug $4=source_type $5=value $6=expires_at $7=tick_ms
        // An empty attributeSlug results in NULL attribute_id (no match in entity_attributes).
        using DbParam = std::variant<int, int64_t, float, double, std::string>;
        std::vector<DbParam> params{
            characterId,
            effectSlug,
            attributeSlug, // '' → NULL attribute_id via SQL subquery
            sourceType,
            value,                          // double
            static_cast<double>(expiresAt), // int64 → double (unix sec, safe precision)
            tickMs};

        auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
        pqxx::work txn(_dbConn.get());
        // Outbox idempotency: claim first (atomic with the writes below).
        const std::string effFactKey = outboxFactKeyOf(j);
        if (!effFactKey.empty())
        {
            const int effClaimed = outboxClaimFactKey(
                gameServices_.getDatabase(), txn, effFactKey);
            if (effClaimed == 0)
            {
                outboxSendFactAck(networkManager_, event.getClientSocket(), effFactKey, "duplicate");
                return;
            }
            if (effClaimed < 0)
                return;
        }
        static_cast<void>(gameServices_.getDatabase().execChecked(
            txn, "insert_player_active_effect", params));
        txn.commit();
        if (!effFactKey.empty())
            outboxSendFactAck(networkManager_, event.getClientSocket(), effFactKey, "applied");

        log_->info("[SAVE_ACTIVE_EFFECT] char=" + std::to_string(characterId) + " effect=" + effectSlug);
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("handleSaveActiveEffectEvent error: " + std::string(ex.what()));
    }
}



// ── GET_PLAYER_PITY ────────────────────────────────────────────────────────
void
EventHandler::handleGetPlayerPityEvent(const Event &event)
{
    const auto &data = event.getData();
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();

    try
    {
        if (!std::holds_alternative<int>(data))
        {
            log_->error("handleGetPlayerPityEvent: unexpected data type");
            return;
        }
        int characterId = std::get<int>(data);

        auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
        pqxx::work txn(_dbConn.get());
        auto result = gameServices_.getDatabase().execChecked(
            txn, "get_player_pity", {characterId});

        nlohmann::json entriesJson = nlohmann::json::array();
        for (const auto &row : result)
        {
            nlohmann::json entry;
            entry["itemId"] = row["item_id"].as<int>();
            entry["killCount"] = row["kill_count"].as<int>();
            entriesJson.push_back(std::move(entry));
        }

        ResponseBuilder builder;
        nlohmann::json response = builder
                                      .setHeader("message", "Player pity data")
                                      .setHeader("hash", "")
                                      .setHeader("clientId", characterId)
                                      .setHeader("eventType", "setPlayerPityData")
                                      .setBody("characterId", characterId)
                                      .setBody("entries", entriesJson)
                                      .build();
        networkManager_.sendResponse(clientSocket,
            networkManager_.generateResponseMessage("success", response));

        log_->info("[EH] Sent " + std::to_string(entriesJson.size()) +
                   " pity counters for characterId=" + std::to_string(characterId));
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("handleGetPlayerPityEvent error: " + std::string(ex.what()));
    }
}

// ── GET_PLAYER_BESTIARY ────────────────────────────────────────────────────
void
EventHandler::handleGetPlayerBestiaryEvent(const Event &event)
{
    const auto &data = event.getData();
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();

    try
    {
        if (!std::holds_alternative<int>(data))
        {
            log_->error("handleGetPlayerBestiaryEvent: unexpected data type");
            return;
        }
        int characterId = std::get<int>(data);

        auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
        pqxx::work txn(_dbConn.get());
        auto result = gameServices_.getDatabase().execChecked(
            txn, "get_player_bestiary", {characterId});

        nlohmann::json entriesJson = nlohmann::json::array();
        for (const auto &row : result)
        {
            nlohmann::json entry;
            entry["mobTemplateId"] = row["mob_template_id"].as<int>();
            entry["killCount"] = row["kill_count"].as<int>();
            entriesJson.push_back(std::move(entry));
        }

        ResponseBuilder builder;
        nlohmann::json response = builder
                                      .setHeader("message", "Player bestiary data")
                                      .setHeader("hash", "")
                                      .setHeader("clientId", characterId)
                                      .setHeader("eventType", "setPlayerBestiaryData")
                                      .setBody("characterId", characterId)
                                      .setBody("entries", entriesJson)
                                      .build();
        networkManager_.sendResponse(clientSocket,
            networkManager_.generateResponseMessage("success", response));

        log_->info("[EH] Sent " + std::to_string(entriesJson.size()) +
                   " bestiary entries for characterId=" + std::to_string(characterId));
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("handleGetPlayerBestiaryEvent error: " + std::string(ex.what()));
    }
}

// ── SAVE_PITY_COUNTER ──────────────────────────────────────────────────────
void
EventHandler::handleSavePityCounterEvent(const Event &event)
{
    const auto &data = event.getData();

    try
    {
        if (!std::holds_alternative<nlohmann::json>(data))
        {
            log_->error("handleSavePityCounterEvent: unexpected data type");
            return;
        }
        const auto &j = std::get<nlohmann::json>(data);
        int characterId = j.value("characterId", 0);
        int itemId = j.value("itemId", 0);
        int killCount = j.value("killCount", 0);

        if (characterId <= 0 || itemId <= 0)
            return;

        auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
        pqxx::work txn(_dbConn.get());
        // Outbox idempotency: claim first (atomic with the writes below).
        const std::string pityFactKey = outboxFactKeyOf(j);
        if (!pityFactKey.empty())
        {
            const int pityClaimed = outboxClaimFactKey(
                gameServices_.getDatabase(), txn, pityFactKey);
            if (pityClaimed == 0)
            {
                outboxSendFactAck(networkManager_, event.getClientSocket(), pityFactKey, "duplicate");
                return;
            }
            if (pityClaimed < 0)
                return;
        }
        static_cast<void>(gameServices_.getDatabase().execChecked(
            txn, "upsert_pity_counter", {characterId, itemId, killCount}));
        txn.commit();
        if (!pityFactKey.empty())
            outboxSendFactAck(networkManager_, event.getClientSocket(), pityFactKey, "applied");

        log_->info("[SAVE_PITY] char=" + std::to_string(characterId) +
                   " item=" + std::to_string(itemId) +
                   " kills=" + std::to_string(killCount));
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("handleSavePityCounterEvent error: " + std::string(ex.what()));
    }
}

// ── SAVE_BESTIARY_KILL ─────────────────────────────────────────────────────
void
EventHandler::handleSaveBestiaryKillEvent(const Event &event)
{
    const auto &data = event.getData();

    try
    {
        if (!std::holds_alternative<nlohmann::json>(data))
        {
            log_->error("handleSaveBestiaryKillEvent: unexpected data type");
            return;
        }
        const auto &j = std::get<nlohmann::json>(data);
        int characterId = j.value("characterId", 0);
        int mobTemplateId = j.value("mobTemplateId", 0);
        int killCount = j.value("killCount", 0);

        if (characterId <= 0 || mobTemplateId <= 0)
            return;

        auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
        pqxx::work txn(_dbConn.get());
        // Outbox idempotency: claim first (atomic with the writes below).
        const std::string bestFactKey = outboxFactKeyOf(j);
        if (!bestFactKey.empty())
        {
            const int bestClaimed = outboxClaimFactKey(
                gameServices_.getDatabase(), txn, bestFactKey);
            if (bestClaimed == 0)
            {
                outboxSendFactAck(networkManager_, event.getClientSocket(), bestFactKey, "duplicate");
                return;
            }
            if (bestClaimed < 0)
                return;
        }
        static_cast<void>(gameServices_.getDatabase().execChecked(
            txn, "upsert_bestiary_kill", {characterId, mobTemplateId, killCount}));
        txn.commit();
        if (!bestFactKey.empty())
            outboxSendFactAck(networkManager_, event.getClientSocket(), bestFactKey, "applied");

        log_->info("[SAVE_BESTIARY] char=" + std::to_string(characterId) +
                   " mob=" + std::to_string(mobTemplateId) +
                   " kills=" + std::to_string(killCount));
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("handleSaveBestiaryKillEvent error: " + std::string(ex.what()));
    }
}

// ── GET_TIMED_CHAMPION_TEMPLATES ──────────────────────────────────────────────

void
EventHandler::handleGetTimedChampionTemplatesEvent(const Event &event)
{
    int clientID = event.getClientID();
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();

    try
    {
        auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
        pqxx::work txn(_dbConn.get());
        auto rows = gameServices_.getDatabase().execChecked(txn, "get_timed_champion_templates", {});
        txn.commit();

        nlohmann::json arr = nlohmann::json::array();
        for (const auto &row : rows)
        {
            nlohmann::json t;
            t["id"] = row["id"].as<int>();
            t["slug"] = row["slug"].as<std::string>();
            t["gameZoneId"] = row["game_zone_id"].as<int>();
            t["mobTemplateId"] = row["mob_template_id"].as<int>();
            t["intervalHours"] = row["interval_hours"].as<int>();
            t["windowMinutes"] = row["window_minutes"].as<int>();
            t["nextSpawnAt"] = row["next_spawn_at"].as<int64_t>();
            t["announceKey"] = row["announce_key"].as<std::string>();
            arr.push_back(std::move(t));
        }

        ResponseBuilder builder;
        nlohmann::json response = builder
                                      .setHeader("message", "Timed champion templates loaded")
                                      .setHeader("hash", "")
                                      .setHeader("clientId", clientID)
                                      .setHeader("eventType", "setTimedChampionTemplatesList")
                                      .setBody("timedChampionTemplates", arr)
                                      .build();

        std::string responseData = networkManager_.generateResponseMessage("success", response);
        networkManager_.sendResponse(clientSocket, responseData);

        log_->info("[TIMED_CHAMP] Sent {} timed champion templates to chunk server", arr.size());
    }
    catch (const DatabaseError &)
    {
        // Catalog entry: DB failure must reach dispatchCatalog
        // (catalogFailed -> incomplete JOIN), never a partial success.
        throw;
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("handleGetTimedChampionTemplatesEvent error: " + std::string(ex.what()));
    }
}

// ── TIMED_CHAMPION_KILLED ─────────────────────────────────────────────────────

void
EventHandler::handleTimedChampionKilledEvent(const Event &event)
{
    try
    {
        const auto &data = event.getData();
        if (!std::holds_alternative<nlohmann::json>(data))
        {
            log_->error("handleTimedChampionKilledEvent: unexpected data type");
            return;
        }
        const auto &j = std::get<nlohmann::json>(data);
        const std::string slug = j.value("slug", "");
        const int64_t killedAt = j.value("killedAt", int64_t{0});

        if (slug.empty())
            return;

        // Compute next_spawn_at = killedAt + interval_hours * 3600
        // We need the interval_hours for this slug. Query and then update.
        auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
        pqxx::work txn(_dbConn.get());
        // Outbox idempotency: claim first (atomic with the writes below).
        const std::string champFactKey = outboxFactKeyOf(j);
        if (!champFactKey.empty())
        {
            const int champClaimed = outboxClaimFactKey(
                gameServices_.getDatabase(), txn, champFactKey);
            if (champClaimed == 0)
            {
                outboxSendFactAck(networkManager_, event.getClientSocket(), champFactKey, "duplicate");
                return;
            }
            if (champClaimed < 0)
                return;
        }

        // Get interval_hours for this slug
        auto rows = txn.exec_params(
            "SELECT interval_hours FROM timed_champion_templates WHERE slug = $1", slug);
        if (!rows.empty())
        {
            int intervalHours = rows[0]["interval_hours"].as<int>();
            int64_t nextSpawnAt = killedAt + static_cast<int64_t>(intervalHours) * 3600;

            static_cast<void>(gameServices_.getDatabase().execChecked(
                txn, "update_timed_champion_next_spawn", {slug, nextSpawnAt}));
        }
        txn.commit();
        if (!champFactKey.empty())
            outboxSendFactAck(networkManager_, event.getClientSocket(), champFactKey, "applied");

        log_->info("[TIMED_CHAMP] Updated next_spawn for slug='{}' after kill", slug);
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("handleTimedChampionKilledEvent error: " + std::string(ex.what()));
    }
}

// ── GET_PLAYER_REPUTATIONS ─────────────────────────────────────────────────
void
EventHandler::handleGetPlayerReputationsEvent(const Event &event)
{
    const auto &data = event.getData();
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();

    try
    {
        if (!std::holds_alternative<int>(data))
        {
            log_->error("handleGetPlayerReputationsEvent: unexpected data type");
            return;
        }
        int characterId = std::get<int>(data);

        auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
        pqxx::work txn(_dbConn.get());
        auto result = gameServices_.getDatabase().execChecked(
            txn, "get_player_reputations", {characterId});

        nlohmann::json entriesJson = nlohmann::json::array();
        for (const auto &row : result)
        {
            nlohmann::json entry;
            entry["factionSlug"] = row["faction_slug"].as<std::string>();
            entry["value"] = row["value"].as<int>();
            entriesJson.push_back(std::move(entry));
        }

        ResponseBuilder builder;
        nlohmann::json response = builder
                                      .setHeader("message", "Player reputations")
                                      .setHeader("hash", "")
                                      .setHeader("clientId", characterId)
                                      .setHeader("eventType", "setPlayerReputationsData")
                                      .setBody("characterId", characterId)
                                      .setBody("entries", entriesJson)
                                      .build();
        networkManager_.sendResponse(clientSocket,
            networkManager_.generateResponseMessage("success", response));

        log_->info("[REPUTATION] Sent {} entries for characterId={}", entriesJson.size(), characterId);
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("handleGetPlayerReputationsEvent error: " + std::string(ex.what()));
    }
}

// ── SAVE_REPUTATION ────────────────────────────────────────────────────────
void
EventHandler::handleSaveReputationEvent(const Event &event)
{
    const auto &data = event.getData();

    try
    {
        if (!std::holds_alternative<nlohmann::json>(data))
        {
            log_->error("handleSaveReputationEvent: unexpected data type");
            return;
        }
        const auto &j = std::get<nlohmann::json>(data);
        int characterId = j.value("characterId", 0);
        std::string faction = j.value("factionSlug", "");
        int value = j.value("value", 0);

        if (characterId <= 0 || faction.empty())
            return;

        auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
        pqxx::work txn(_dbConn.get());
        // Outbox idempotency: claim first (atomic with the write below).
        const std::string repFactKey = outboxFactKeyOf(j);
        if (!repFactKey.empty())
        {
            const int claimed = outboxClaimFactKey(
                gameServices_.getDatabase(), txn, repFactKey);
            if (claimed == 0)
            {
                outboxSendFactAck(networkManager_, event.getClientSocket(), repFactKey, "duplicate");
                return;
            }
            if (claimed < 0)
                return;
        }
        // Prefer the delta when the sender provides one: concurrent changes
        // apply atomically instead of racing last-writer-wins on absolutes.
        // Legacy absolute-only senders keep the old upsert path.
        if (j.contains("delta") && j["delta"].is_number_integer() && j["delta"].get<int>() != 0)
        {
            static_cast<void>(gameServices_.getDatabase().execChecked(
                txn, "add_reputation", {characterId, faction, j["delta"].get<int>()}));
        }
        else
        {
            static_cast<void>(gameServices_.getDatabase().execChecked(
                txn, "upsert_reputation", {characterId, faction, value}));
        }
        txn.commit();

        if (!repFactKey.empty())
            outboxSendFactAck(networkManager_, event.getClientSocket(), repFactKey, "applied");

        log_->info("[REPUTATION] Saved char={} faction={} value={}", characterId, faction, value);
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("handleSaveReputationEvent error: " + std::string(ex.what()));
    }
}

// ── GET_PLAYER_MASTERIES ───────────────────────────────────────────────────
void
EventHandler::handleGetPlayerMasteriesEvent(const Event &event)
{
    const auto &data = event.getData();
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();

    try
    {
        if (!std::holds_alternative<int>(data))
        {
            log_->error("handleGetPlayerMasteriesEvent: unexpected data type");
            return;
        }
        int characterId = std::get<int>(data);

        auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
        pqxx::work txn(_dbConn.get());
        auto result = gameServices_.getDatabase().execChecked(
            txn, "get_player_masteries", {characterId});

        nlohmann::json entriesJson = nlohmann::json::array();
        for (const auto &row : result)
        {
            nlohmann::json entry;
            entry["masterySlug"] = row["mastery_slug"].as<std::string>();
            entry["value"] = row["value"].as<float>();
            entriesJson.push_back(std::move(entry));
        }

        ResponseBuilder builder;
        nlohmann::json response = builder
                                      .setHeader("message", "Player masteries")
                                      .setHeader("hash", "")
                                      .setHeader("clientId", characterId)
                                      .setHeader("eventType", "setPlayerMasteriesData")
                                      .setBody("characterId", characterId)
                                      .setBody("entries", entriesJson)
                                      .build();
        networkManager_.sendResponse(clientSocket,
            networkManager_.generateResponseMessage("success", response));

        log_->info("[MASTERY] Sent {} entries for characterId={}", entriesJson.size(), characterId);
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("handleGetPlayerMasteriesEvent error: " + std::string(ex.what()));
    }
}

// ── SAVE_MASTERY ───────────────────────────────────────────────────────────
void
EventHandler::handleSaveMasteryEvent(const Event &event)
{
    const auto &data = event.getData();

    try
    {
        if (!std::holds_alternative<nlohmann::json>(data))
        {
            log_->error("handleSaveMasteryEvent: unexpected data type");
            return;
        }
        const auto &j = std::get<nlohmann::json>(data);
        int characterId = j.value("characterId", 0);
        std::string masterySlug = j.value("masterySlug", "");
        float value = j.value("value", 0.0f);

        if (characterId <= 0 || masterySlug.empty())
            return;

        auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
        pqxx::work txn(_dbConn.get());
        // Outbox idempotency: claim first (atomic with the writes below).
        const std::string mastFactKey = outboxFactKeyOf(j);
        if (!mastFactKey.empty())
        {
            const int mastClaimed = outboxClaimFactKey(
                gameServices_.getDatabase(), txn, mastFactKey);
            if (mastClaimed == 0)
            {
                outboxSendFactAck(networkManager_, event.getClientSocket(), mastFactKey, "duplicate");
                return;
            }
            if (mastClaimed < 0)
                return;
        }
        static_cast<void>(gameServices_.getDatabase().execChecked(
            txn, "upsert_mastery", {characterId, masterySlug, value}));
        txn.commit();
        if (!mastFactKey.empty())
            outboxSendFactAck(networkManager_, event.getClientSocket(), mastFactKey, "applied");

        log_->info("[MASTERY] Saved char={} slug={} value={}", characterId, masterySlug, value);
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("handleSaveMasteryEvent error: " + std::string(ex.what()));
    }
}

// ── GET_MASTERY_DEFINITIONS ────────────────────────────────────────────────

// ── SAVE_LEARNED_SKILL ────────────────────────────────────────────────────
void
EventHandler::handleSaveLearnedSkillEvent(const Event &event)
{
    const auto &data = event.getData();
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();

    try
    {
        if (!std::holds_alternative<nlohmann::json>(data))
        {
            log_->error("handleSaveLearnedSkillEvent: unexpected data type");
            return;
        }
        const auto &j = std::get<nlohmann::json>(data);
        int characterId = j.value("characterId", 0);
        int clientId = j.value("clientId", 0);
        std::string skillSlug = j.value("skillSlug", "");

        if (characterId <= 0 || skillSlug.empty())
        {
            log_->error("handleSaveLearnedSkillEvent: invalid params char={} slug={}", characterId, skillSlug);
            return;
        }

        auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
        pqxx::work txn(_dbConn.get());

        // Outbox idempotency: claim the fact key atomically with the writes
        // below (duplicate deliveries skip + ack-duplicate; transport errors
        // abort without ack so the chunk retries).
        const std::string learnFactKey = outboxFactKeyOf(j);
        if (!learnFactKey.empty())
        {
            const int claimed = outboxClaimFactKey(
                gameServices_.getDatabase(), txn, learnFactKey);
            if (claimed == 0)
            {
                outboxSendFactAck(networkManager_, clientSocket, learnFactKey, "duplicate");
                return;
            }
            if (claimed < 0)
                return;
        }

        // Persist the skill
        static_cast<void>(gameServices_.getDatabase().execChecked(
            txn, "save_learned_skill", {characterId, skillSlug}));

        // Persist SP as the authoritative value decided by chunk (single
        // owner: chunk validates+deducts in-session, game stores the fact).
        // Repeating the same fact is a no-op (idempotent). Legacy senders
        // without the fact fall back to the old cost-table decrement.
        if (j.contains("freeSkillPoints") && j["freeSkillPoints"].is_number_integer())
        {
            static_cast<void>(gameServices_.getDatabase().execChecked(
                txn, "set_free_skill_points",
                {characterId, j["freeSkillPoints"].get<int>()}));
        }
        else
        {
            auto costRows = gameServices_.getDatabase().execChecked(
                txn, "get_skill_sp_cost", {skillSlug});
            if (!costRows.empty() && !costRows[0]["skill_point_cost"].is_null())
            {
                int spCost = costRows[0]["skill_point_cost"].as<int>(0);
                if (spCost > 0)
                    static_cast<void>(gameServices_.getDatabase().execChecked(
                        txn, "decrement_skill_points", {characterId, spCost}));
            }
        }

        // Query full skill data for the newly learned skill
        auto rows = gameServices_.getDatabase().execChecked(
            txn, "get_character_skills", {characterId});
        txn.commit();

        // Outbox ack: the fact is durable now.
        if (!learnFactKey.empty())
            outboxSendFactAck(networkManager_, clientSocket, learnFactKey, "applied");

        // Find the newly learned skill in the results
        nlohmann::json skillJson = nlohmann::json::object();
        for (const auto &row : rows)
        {
            if (row["skill_slug"].as<std::string>() == skillSlug)
            {
                skillJson["skillName"] = row["skill_name"].as<std::string>();
                skillJson["skillSlug"] = row["skill_slug"].as<std::string>();
                skillJson["scaleStat"] = row["scale_stat"].as<std::string>();
                skillJson["school"] = row["school"].as<std::string>();
                skillJson["skillEffectType"] = row["skill_effect_type"].as<std::string>();
                skillJson["skillLevel"] = row["skill_level"].is_null() ? 1 : row["skill_level"].as<int>();
                skillJson["coeff"] = row["coeff"].as<double>(0.0);
                skillJson["flatAdd"] = row["flat_add"].as<double>(0.0);
                skillJson["cooldownMs"] = row["cooldown_ms"].as<int>(0);
                skillJson["gcdMs"] = row["gcd_ms"].as<int>(0);
                skillJson["castMs"] = row["cast_ms"].as<int>(0);
                skillJson["costMp"] = row["cost_mp"].as<int>(0);
                skillJson["maxRange"] = row["max_range"].as<int>(0);
                skillJson["areaRadius"] = row["area_radius"].as<int>(0);
                skillJson["swingMs"] = row["swing_ms"].as<int>(300);
                skillJson["animationName"] = row["animation_name"].as<std::string>("");
                skillJson["isPassive"] = row["is_passive"].as<bool>();

                // Include active effects (from skill_active_effects for non-passive skills,
                // or from passive_skill_modifiers for passive skills)
                if (!row["active_effects"].is_null())
                {
                    try
                    {
                        skillJson["effects"] = nlohmann::json::parse(row["active_effects"].as<std::string>());
                    }
                    catch (const std::exception &)
                    {
                        skillJson["effects"] = nlohmann::json::array();
                    }
                }
                else
                {
                    skillJson["effects"] = nlohmann::json::array();
                }

                break;
            }
        }

        if (skillJson.empty())
        {
            log_->error("handleSaveLearnedSkillEvent: skill {} not found after save for char={}", skillSlug, characterId);
            return;
        }

        // For passive skills also include passive_skill_modifiers so the chunk server can
        // apply ActiveEffects immediately (permanent stat modifiers).
        if (skillJson.value("isPassive", false))
        {
            nlohmann::json effectsArr = nlohmann::json::array();
            try
            {
                // Fresh txn: the write txn above is already committed.
                // (Querying on it threw and always yielded empty effects.)
                auto _dbConnPm = gameServices_.getDatabase().getConnectionLocked();
                pqxx::work txnPm(_dbConnPm.get());
                auto effRows = gameServices_.getDatabase().execChecked(
                    txnPm, "get_passive_skill_modifiers_by_slug", {skillSlug});
                txnPm.commit();
                for (const auto &row : effRows)
                {
                    nlohmann::json eff;
                    eff["effectSlug"] = row["effect_slug"].as<std::string>();
                    eff["effectTypeSlug"] = std::string("passive");
                    eff["attributeSlug"] = row["attribute_slug"].as<std::string>();
                    eff["value"] = row["value"].as<float>();
                    eff["durationSeconds"] = 0;
                    eff["tickMs"] = 0;
                    effectsArr.push_back(std::move(eff));
                }
            }
            catch (const std::exception &effEx)
            {
                log_->warn("[SKILL] Could not load passive modifiers for skill {}: {}", skillSlug, effEx.what());
            }
            skillJson["effects"] = std::move(effectsArr);
        }

        ResponseBuilder builder;
        nlohmann::json response = builder
                                      .setHeader("message", "Skill learned")
                                      .setHeader("hash", "")
                                      .setHeader("clientId", clientId)
                                      .setHeader("eventType", "setLearnedSkill")
                                      .setBody("characterId", characterId)
                                      .setBody("skillData", skillJson)
                                      .build();

        // Send on the live chunk socket, not the captured one: the event
        // queued async and the chunk may have reconnected since (writes
        // into a half-open stale socket fail silently). resolveLiveSocket
        // prefers the current registration; sendResponse still guards
        // closed sockets with an error log.
        auto sendSocket =
            gameServices_.getChunkManager().resolveLiveSocket(clientSocket);
        if (!sendSocket)
        {
            log_->error("[GAME] setLearnedSkill: no live chunk socket for char={}", characterId);
            return;
        }
        networkManager_.sendResponse(sendSocket,
            networkManager_.generateResponseMessage("success", response));

        log_->info("[SKILL] Saved learned skill slug={} for char={}", skillSlug, characterId);
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("handleSaveLearnedSkillEvent error: " + std::string(ex.what()));
    }
}

// ── SAVE_SKILL_BAR_SLOT ────────────────────────────────────────────────────
void
EventHandler::handleSaveSkillBarSlotEvent(const Event &event)
{
    const auto &data = event.getData();

    try
    {
        if (!std::holds_alternative<nlohmann::json>(data))
        {
            log_->error("handleSaveSkillBarSlotEvent: unexpected data type");
            return;
        }
        const auto &j = std::get<nlohmann::json>(data);
        int characterId = j.value("characterId", 0);
        int slotIndex = j.value("slotIndex", -1);
        std::string skillSlug = j.value("skillSlug", "");

        if (characterId <= 0 || slotIndex < 0 || slotIndex >= 12)
        {
            log_->error("handleSaveSkillBarSlotEvent: invalid params char={} slot={}", characterId, slotIndex);
            return;
        }

        auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
        pqxx::work txn(_dbConn.get());
        // Outbox idempotency: claim first (atomic with the writes below).
        const std::string barFactKey = outboxFactKeyOf(j);
        if (!barFactKey.empty())
        {
            const int barClaimed = outboxClaimFactKey(
                gameServices_.getDatabase(), txn, barFactKey);
            if (barClaimed == 0)
            {
                outboxSendFactAck(networkManager_, event.getClientSocket(), barFactKey, "duplicate");
                return;
            }
            if (barClaimed < 0)
                return;
        }

        if (skillSlug.empty())
        {
            static_cast<void>(gameServices_.getDatabase().execChecked(
                txn, "clear_skill_bar_slot", {characterId, slotIndex}));
        }
        else
        {
            static_cast<void>(gameServices_.getDatabase().execChecked(
                txn, "save_skill_bar_slot", {characterId, slotIndex, skillSlug}));
        }
        txn.commit();
        if (!barFactKey.empty())
            outboxSendFactAck(networkManager_, event.getClientSocket(), barFactKey, "applied");

        log_->info("[SKILL_BAR] slot={} slug='{}' saved for char={}", slotIndex, skillSlug, characterId);
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("handleSaveSkillBarSlotEvent error: " + std::string(ex.what()));
    }
}

// ── GET_ZONE_EVENT_TEMPLATES ───────────────────────────────────────────────

// ── GET_TITLE_DEFINITIONS ──────────────────────────────────────────────────
// Sends the global title catalog to the requesting chunk-server connection.
// Called once on chunk-server startup (no characterId needed).
void
EventHandler::handleGetTitleDefinitionsEvent(const Event &event)
{
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();
    int clientID = event.getClientID();

    try
    {
        auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
        pqxx::work txn(_dbConn.get());
        auto result = gameServices_.getDatabase().execChecked(
            txn, "get_title_definitions", {});

        nlohmann::json titlesJson = nlohmann::json::array();
        for (const auto &row : result)
        {
            nlohmann::json t;
            t["id"] = row["id"].as<int>();
            t["slug"] = row["slug"].as<std::string>();
            t["displayName"] = row["display_name"].as<std::string>();
            t["description"] = row["description"].as<std::string>();
            t["earnCondition"] = row["earn_condition"].as<std::string>();
            // bonuses column is JSONB — parse it back to a JSON array.
            // Broken row content must not fail the whole static push (chunk
            // would boot without ANY titles); warn with the row id instead.
            try
            {
                t["bonuses"] = nlohmann::json::parse(row["bonuses"].as<std::string>());
            }
            catch (const std::exception &e)
            {
                log_->warn("[TITLE] title id={} has broken bonuses JSONB ({}), sending []",
                    row["id"].as<int>(), e.what());
                t["bonuses"] = nlohmann::json::array();
            }
            catch (...)
            {
                t["bonuses"] = nlohmann::json::array();
            }
            // conditionParams is JSONB — parse it back to a JSON object
            // (same broken-row policy as bonuses above).
            try
            {
                t["conditionParams"] = nlohmann::json::parse(row["condition_params"].as<std::string>());
            }
            catch (const std::exception &e)
            {
                log_->warn("[TITLE] title id={} has broken condition_params JSONB ({}), sending {{}}",
                    row["id"].as<int>(), e.what());
                t["conditionParams"] = nlohmann::json::object();
            }
            catch (...)
            {
                t["conditionParams"] = nlohmann::json::object();
            }
            titlesJson.push_back(std::move(t));
        }

        ResponseBuilder builder;
        nlohmann::json response = builder
                                      .setHeader("message", "Title definitions")
                                      .setHeader("hash", "")
                                      .setHeader("clientId", clientID)
                                      .setHeader("eventType", "setTitleDefinitionsData")
                                      .setBody("titles", titlesJson)
                                      .build();
        networkManager_.sendResponse(clientSocket,
            networkManager_.generateResponseMessage("success", response));

        log_->info("[TITLE] Sent {} title definitions to chunk server", titlesJson.size());
    }
    catch (const DatabaseError &)
    {
        // Catalog entry: DB failure must reach dispatchCatalog
        // (catalogFailed -> incomplete JOIN), never a partial success.
        throw;
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("handleGetTitleDefinitionsEvent error: " + std::string(ex.what()));
    }
}

// ── GET_PLAYER_TITLES ──────────────────────────────────────────────────────
void
EventHandler::handleGetPlayerTitlesEvent(const Event &event)
{
    const auto &data = event.getData();
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();

    try
    {
        if (!std::holds_alternative<int>(data))
        {
            log_->error("handleGetPlayerTitlesEvent: unexpected data type");
            return;
        }
        int characterId = std::get<int>(data);

        auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
        pqxx::work txn(_dbConn.get());
        auto result = gameServices_.getDatabase().execChecked(
            txn, "get_player_titles", {characterId});

        std::string equippedSlug;
        nlohmann::json earnedSlugs = nlohmann::json::array();
        for (const auto &row : result)
        {
            std::string slug = row["title_slug"].as<std::string>();
            earnedSlugs.push_back(slug);
            if (row["equipped"].as<bool>())
                equippedSlug = slug;
        }

        ResponseBuilder builder;
        nlohmann::json response = builder
                                      .setHeader("message", "Player titles")
                                      .setHeader("hash", "")
                                      .setHeader("clientId", characterId)
                                      .setHeader("eventType", "setPlayerTitlesData")
                                      .setBody("characterId", characterId)
                                      .setBody("earnedSlugs", earnedSlugs)
                                      .setBody("equippedSlug", equippedSlug)
                                      .build();
        networkManager_.sendResponse(clientSocket,
            networkManager_.generateResponseMessage("success", response));

        log_->info("[TITLE] Sent {} earned titles for characterId={}", earnedSlugs.size(), characterId);
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("handleGetPlayerTitlesEvent error: " + std::string(ex.what()));
    }
}

// ── SAVE_PLAYER_TITLE ──────────────────────────────────────────────────────
// Body: { "eventType":"savePlayerTitle", "characterId":7, "titleSlug":"wolf_slayer", "equipped":true }
void
EventHandler::handleSavePlayerTitleEvent(const Event &event)
{
    const auto &data = event.getData();

    try
    {
        if (!std::holds_alternative<nlohmann::json>(data))
        {
            log_->error("handleSavePlayerTitleEvent: unexpected data type");
            return;
        }
        const auto &j = std::get<nlohmann::json>(data);
        int characterId = j.value("characterId", 0);
        std::string equippedSlug = j.value("equippedSlug", "");
        nlohmann::json earnedSlugsArr = j.value("earnedSlugs", nlohmann::json::array());

        if (characterId <= 0)
        {
            log_->error("[TITLE] handleSavePlayerTitleEvent: invalid payload char={}", characterId);
            return;
        }

        auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
        pqxx::work txn(_dbConn.get());
        // Outbox idempotency: claim first (atomic with the writes below).
        const std::string titleFactKey = outboxFactKeyOf(j);
        if (!titleFactKey.empty())
        {
            const int titleClaimed = outboxClaimFactKey(
                gameServices_.getDatabase(), txn, titleFactKey);
            if (titleClaimed == 0)
            {
                outboxSendFactAck(networkManager_, event.getClientSocket(), titleFactKey, "duplicate");
                return;
            }
            if (titleClaimed < 0)
                return;
        }

        // Upsert all earned titles; equipped flag is true only for the currently selected one
        for (const auto &slugVal : earnedSlugsArr)
        {
            if (!slugVal.is_string())
                continue;
            std::string slug = slugVal.get<std::string>();
            bool equipped = (!equippedSlug.empty() && slug == equippedSlug);
            static_cast<void>(gameServices_.getDatabase().execChecked(
                txn, "upsert_player_title", {characterId, slug, equipped}));
        }

        // Ensure at most one title has equipped=true (clear all, then re-flag the chosen one)
        if (!equippedSlug.empty())
            static_cast<void>(gameServices_.getDatabase().execChecked(
                txn, "set_character_equipped_title", {characterId, equippedSlug}));

        txn.commit();
        if (!titleFactKey.empty())
            outboxSendFactAck(networkManager_, event.getClientSocket(), titleFactKey, "applied");
        log_->info("[TITLE] Saved char={} titles={} equipped='{}'",
            characterId,
            earnedSlugsArr.size(),
            equippedSlug);
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("handleSavePlayerTitleEvent error: " + std::string(ex.what()));
    }
}

// ── GET_EMOTE_DEFINITIONS ──────────────────────────────────────────────────
void
EventHandler::handleGetEmoteDefinitionsEvent(const Event &event)
{
    int clientID = event.getClientID();
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();

    try
    {
        auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
        pqxx::work txn(_dbConn.get());
        auto result = gameServices_.getDatabase().execChecked(
            txn, "get_emote_definitions", {});

        nlohmann::json emotesJson = nlohmann::json::array();
        for (const auto &row : result)
        {
            nlohmann::json e;
            e["id"] = row["id"].as<int>();
            e["slug"] = row["slug"].as<std::string>();
            e["displayName"] = row["display_name"].as<std::string>();
            e["animationName"] = row["animation_name"].as<std::string>();
            e["category"] = row["category"].as<std::string>();
            e["isDefault"] = row["is_default"].as<bool>();
            e["sortOrder"] = row["sort_order"].as<int>();
            emotesJson.push_back(std::move(e));
        }

        ResponseBuilder builder;
        nlohmann::json response = builder
                                      .setHeader("message", "Emote definitions")
                                      .setHeader("hash", "")
                                      .setHeader("clientId", clientID)
                                      .setHeader("eventType", "setEmoteDefinitionsData")
                                      .setBody("emotes", emotesJson)
                                      .build();
        networkManager_.sendResponse(clientSocket,
            networkManager_.generateResponseMessage("success", response));

        log_->info("[EMOTE] Sent {} emote definitions to chunk server", emotesJson.size());
    }
    catch (const DatabaseError &)
    {
        // Catalog entry: DB failure must reach dispatchCatalog
        // (catalogFailed -> incomplete JOIN), never a partial success.
        throw;
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("handleGetEmoteDefinitionsEvent error: " + std::string(ex.what()));
    }
}

// ── GET_PLAYER_EMOTES ──────────────────────────────────────────────────────
void
EventHandler::handleGetPlayerEmotesEvent(const Event &event)
{
    const auto &data = event.getData();
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();

    try
    {
        if (!std::holds_alternative<int>(data))
        {
            log_->error("handleGetPlayerEmotesEvent: unexpected data type");
            return;
        }
        int characterId = std::get<int>(data);

        auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
        pqxx::work txn(_dbConn.get());

        // Auto-grant default emotes for new characters (idempotent)
        static_cast<void>(gameServices_.getDatabase().execChecked(
            txn, "grant_default_emotes", {characterId}));

        auto result = gameServices_.getDatabase().execChecked(
            txn, "get_player_emotes", {characterId});

        nlohmann::json slugsJson = nlohmann::json::array();
        for (const auto &row : result)
            slugsJson.push_back(row["emote_slug"].as<std::string>());

        txn.commit();

        ResponseBuilder builder;
        nlohmann::json response = builder
                                      .setHeader("message", "Player emotes")
                                      .setHeader("hash", "")
                                      .setHeader("clientId", characterId)
                                      .setHeader("eventType", "setPlayerEmotesData")
                                      .setBody("characterId", characterId)
                                      .setBody("emotes", slugsJson)
                                      .build();
        networkManager_.sendResponse(clientSocket,
            networkManager_.generateResponseMessage("success", response));

        log_->info("[EMOTE] Sent {} emotes for charId={}", slugsJson.size(), characterId);
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("handleGetPlayerEmotesEvent error: " + std::string(ex.what()));
    }
}

// ── GET_NPC_AMBIENT_SPEECH ─────────────────────────────────────────────────

// ── GET_WORLD_OBJECTS (migration 043) ─────────────────────────────────────

// Skill cooldown persistence (migration 067)
// Upserts one cooldown row for a player skill.
// Body fields: characterId (int), skillSlug (string), cooldownEndsAtMs (int64 unix ms).
void
EventHandler::handleSaveSkillCooldownEvent(const Event &event)
{
    const auto &data = event.getData();
    try
    {
        if (!std::holds_alternative<nlohmann::json>(data))
        {
            log_->error("handleSaveSkillCooldownEvent: unexpected data type");
            return;
        }
        const auto &j = std::get<nlohmann::json>(data);
        int characterId = j.value("characterId", 0);
        std::string skillSlug = j.value("skillSlug", std::string(""));
        int64_t cooldownEndsAtMs = j.value("cooldownEndsAtMs", int64_t(0));

        if (characterId <= 0 || skillSlug.empty() || cooldownEndsAtMs <= 0)
            return;

        auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
        pqxx::work txn(_dbConn.get());
        // Outbox idempotency: claim first (atomic with the writes below).
        const std::string cdFactKey = outboxFactKeyOf(j);
        if (!cdFactKey.empty())
        {
            const int cdClaimed = outboxClaimFactKey(
                gameServices_.getDatabase(), txn, cdFactKey);
            if (cdClaimed == 0)
            {
                outboxSendFactAck(networkManager_, event.getClientSocket(), cdFactKey, "duplicate");
                return;
            }
            if (cdClaimed < 0)
                return;
        }
        using DbParam = std::variant<int, int64_t, float, double, std::string>;
        // Pass cooldownEndsAtMs as string to avoid std::to_string(double) producing
        // "1776627283138.000000" which PostgreSQL cannot cast with ::bigint.
        std::vector<DbParam> params{characterId, skillSlug, std::to_string(cooldownEndsAtMs)};
        static_cast<void>(gameServices_.getDatabase().execChecked(txn, "upsert_skill_cooldown", params));
        txn.commit();
        if (!cdFactKey.empty())
            outboxSendFactAck(networkManager_, event.getClientSocket(), cdFactKey, "applied");

        log_->debug("[SAVE_SKILL_COOLDOWN] char={} skill={} endsAt={}", characterId, skillSlug, cooldownEndsAtMs);
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("handleSaveSkillCooldownEvent error: " + std::string(ex.what()));
    }
}

// Loads all still-active cooldowns for a character and sends them back to chunk server.
// Body fields: characterId (int).
void
EventHandler::handleGetPlayerSkillCooldownsEvent(const Event &event)
{
    const auto &data = event.getData();
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();
    try
    {
        if (!std::holds_alternative<int>(data))
        {
            log_->error("handleGetPlayerSkillCooldownsEvent: unexpected data type");
            return;
        }
        int characterId = std::get<int>(data);

        auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
        pqxx::work txn(_dbConn.get());
        auto result = gameServices_.getDatabase().execChecked(
            txn, "get_active_skill_cooldowns", {characterId});
        txn.commit();

        nlohmann::json cooldownsJson = nlohmann::json::array();
        for (const auto &row : result)
        {
            nlohmann::json cd;
            cd["skillSlug"] = row["skill_slug"].as<std::string>();
            cd["remainingMs"] = row["remaining_ms"].as<int64_t>();
            cooldownsJson.push_back(std::move(cd));
        }

        ResponseBuilder builder;
        nlohmann::json response = builder
                                      .setHeader("message", "Player skill cooldowns")
                                      .setHeader("hash", "")
                                      .setHeader("clientId", characterId)
                                      .setHeader("eventType", "setPlayerSkillCooldowns")
                                      .setBody("characterId", characterId)
                                      .setBody("cooldowns", cooldownsJson)
                                      .build();
        networkManager_.sendResponse(clientSocket,
            networkManager_.generateResponseMessage("success", response));

        log_->debug("[GET_SKILL_COOLDOWNS] char={} active={}", characterId, cooldownsJson.size());
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("handleGetPlayerSkillCooldownsEvent error: " + std::string(ex.what()));
    }
}

// Analytics system (migration 058)
// Inserts one row into game_analytics. Fire-and-forget — no reply to chunk server.
// Body fields: analyticsType, characterId, sessionId, level, zoneId, payload (JSON object).
void
EventHandler::handleSaveAnalyticsEventEvent(const Event &event)
{
    const auto &data = event.getData();
    if (!std::holds_alternative<nlohmann::json>(data))
    {
        log_->error("handleSaveAnalyticsEventEvent: unexpected data type");
        return;
    }

    try
    {
        const auto &j = std::get<nlohmann::json>(data);
        std::string eventType = j.value("analyticsType", "");
        int charId = j.value("characterId", 0);
        std::string sessionId = j.value("sessionId", "");
        int level = j.value("level", 0);
        int zoneId = j.value("zoneId", 0);
        std::string payload = j.contains("payload") ? j["payload"].dump() : "{}";

        if (eventType.empty())
        {
            log_->warn("handleSaveAnalyticsEventEvent: empty analyticsType, skipping");
            return;
        }

        auto sc = gameServices_.getDatabase().getConnectionLocked();
        pqxx::work txn(sc.get());
        // Outbox idempotency: claim first (atomic with the writes below).
        const std::string anaFactKey = outboxFactKeyOf(j);
        if (!anaFactKey.empty())
        {
            const int anaClaimed = outboxClaimFactKey(
                gameServices_.getDatabase(), txn, anaFactKey);
            if (anaClaimed == 0)
            {
                outboxSendFactAck(networkManager_, event.getClientSocket(), anaFactKey, "duplicate");
                return;
            }
            if (anaClaimed < 0)
                return;
        }

        // Use exec_params with inline SQL — analytics INSERT does not need a reusable
        // prepared statement because each call is already serialised through the event queue.
        txn.exec_params(
            "INSERT INTO game_analytics (event_type, character_id, session_id, level, zone_id, payload) "
            "VALUES ($1, NULLIF($2, 0), $3, $4, $5, $6::jsonb)",
            eventType,
            charId,
            sessionId,
            level,
            zoneId,
            payload);

        txn.commit();
        if (!anaFactKey.empty())
            outboxSendFactAck(networkManager_, event.getClientSocket(), anaFactKey, "applied");

        log_->debug("[ANALYTICS] {} char={} session={} lvl={} zone={}", eventType, charId, sessionId, level, zoneId);
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("handleSaveAnalyticsEventEvent error: " + std::string(ex.what()));
    }
}


