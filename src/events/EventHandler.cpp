#include "events/EventHandler.hpp"
#include "data/DataStructs.hpp"
#include "events/Outbox.hpp"
#include "utils/ProtocolLimits.hpp"
#include "utils/TerminalColors.hpp"
#include "utils/TimestampUtils.hpp"

#include "events/Event.hpp"
#include <chrono>
#include <cstdlib>
#include <spdlog/logger.h>
#include <unordered_set>

EventHandler::EventHandler(
    NetworkManager &networkManager,
    GameServices &gameServices)
    : networkManager_(networkManager),
      gameServices_(gameServices),
      vendorHandler_(networkManager, gameServices),
      itemHandler_(networkManager, gameServices),
      effectsHandler_(networkManager, gameServices),
      socialHandler_(networkManager, gameServices),
      skillsHandler_(networkManager, gameServices),
      progressionHandler_(networkManager, gameServices),
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
            // Prepare a response message
            nlohmann::json response;
            ResponseBuilder builder;

            auto sendJoinError = [&](const std::string &msg)
            {
                response = builder
                               .setHeader("message", msg)
                               .setHeader("hash", passedClientData.hash)
                               .setHeader("clientId", passedClientData.clientId)
                               .setHeader("eventType", "joinGameClient")
                               .setBody("", "")
                               .build();
                std::string responseData = networkManager_.generateResponseMessage("error", response);
                networkManager_.sendResponse(passedClientData.socket, responseData);
            };

            // Check if the authentication is not successful
            if (passedClientData.clientId == 0 || passedClientData.hash == "")
            {
                sendJoinError("Authentication failed for user!");
                return;
            }

            // Validate login session token against user_sessions (DB-backed).
            // search_user returns the user row only for live sessions
            // (revoked_at IS NULL AND expires_at > now()). Fail-closed on DB
            // outage with ERR_DB (never hangs); wrong session -> ERR_SESSION_INVALID.
            try
            {
                auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
                pqxx::work txn(_dbConn.get());
                auto rows = gameServices_.getDatabase().execChecked(
                    txn, "search_user", {passedClientData.hash});
                // Read-only probe: rollback to avoid holding the pooled slot dirty.
                txn.abort();
                if (rows.empty())
                {
                    sendJoinError("ERR_SESSION_INVALID");
                    return;
                }
                // search_user returns users row; column "id" is the account id.
                // Bind session to the joining account to prevent token replay across accounts.
                try
                {
                    const int sessionUserId = rows[0]["id"].as<int>();
                    if (sessionUserId != passedClientData.clientId)
                    {
                        sendJoinError("ERR_SESSION_INVALID");
                        return;
                    }
                }
                catch (...)
                {
                    // Schema without id column — accept live session (fail-open only here,
                    // ownership is still enforced by get_character owner_id check).
                }
            }
            catch (const std::exception &ex)
            {
                gameServices_.getLogger().logError(
                    std::string("[JOIN] session validation DB error: ") + ex.what());
                sendJoinError("ERR_DB");
                return;
            }

            // Session valid — now register client + route chunk.
            // Save the clientData object with the new init data
            gameServices_.getClientManager().setClientData(passedClientData);

            // Multi-chunk routing: no position yet at join time — deterministic
            // first chunk (lowest id). Position-routed handoff happens on
            // GET_CHARACTER_DATA via selectChunkForPosition (character pos).
            ChunkInfoStruct chunkServerData = gameServices_.getChunkManager().selectChunkForNewClient();

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
            // S14: echo the agreed framing for this socket (transport truth
            // from sniffing; v1 peers ignore the unknown body key).
            const int agreedFraming = networkManager_.isSocketFramingV2(passedClientData.socket)
                ? protocol::kFramingV2
                : 1;
            response = builder
                           .setHeader("message", "Authentication success for user!")
                           .setHeader("hash", passedClientData.hash)
                           .setHeader("clientId", passedClientData.clientId)
                           .setHeader("eventType", "joinGameClient")
                           .setBody("chunkServerData", chunkServerDataJson)
                           .setBody("framing", agreedFraming)
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
    // Character domain (extracted): live position mirror, persisted on disconnect.
    characterHandler_.handleMoveCharacterChunkEvent(event);
}

// disconnect the client
void
EventHandler::handleDisconnectChunkEvent(const Event &event)
{
    // Character domain (extracted): disconnect persist + client evict.
    characterHandler_.handleDisconnectChunkEvent(event);
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

    // Outbox phases B-D: new-link flag for the watermark attach below.
    // Set once chunkData is known (sameSocket lives in a narrower scope).
    // Defaults to new-link (fail-open: a missed set only adds a harmless
    // field; bootId keeps numbering safe regardless).
    bool isNewChunkLink = true;

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
            isNewChunkLink = !sameSocket;

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
        // Outbox phases B-D: attach the global high-water mark so the chunk
        // can fast-forward its seq_. NEW LINKS ONLY (gated on !sameSocket):
        // heartbeat re-asserts get the plain ack, otherwise the chunk would
        // reconcile (outboxUnacked storm) every 60s. The error path above
        // likewise carries no mark. DB failure here must never fail the
        // join: ack without the field (bootId keeps numbering safe) and let
        // the next link retry it.
        int64_t outboxGlobalSeq = -1;
        if (isNewChunkLink)
        {
            try
            {
                auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
                pqxx::work txn(_dbConn.get());
                auto wmRows = gameServices_.getDatabase().execChecked(
                    txn, "get_outbox_global_watermark", {1});
                txn.commit();
                if (!wmRows.empty() && !wmRows[0][0].is_null())
                    outboxGlobalSeq = wmRows[0][0].as<int64_t>(0);
            }
            catch (const std::exception &ex)
            {
                gameServices_.getLogger().logError(
                    "[join] outbox watermark unavailable, acking without it: " + std::string(ex.what()));
            }
        }
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
        if (outboxGlobalSeq >= 0)
            response["body"]["outboxGlobalSeq"] = outboxGlobalSeq;
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
EventHandler::handleOutboxUnackedEvent(const Event &event)
{
    // Outbox reconcile: chunk reports keys it never got acked for (new links
    // only — chunk sends this solely after a setChunkData ack carrying
    // outboxGlobalSeq). Split into applied (drop, we are durable) vs unknown
    // (resend now) with point PK lookups; answer on the live chunk socket.
    // On DB error send nothing (claim -1 semantics): the 5s chunk flush
    // covers it.
    const auto data = event.getData();
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();
    try
    {
        if (!std::holds_alternative<nlohmann::json>(data))
        {
            log_->error("handleOutboxUnackedEvent: unexpected data type");
            return;
        }
        const auto &body = std::get<nlohmann::json>(data);
        if (!body.contains("keys") || !body["keys"].is_array() || body["keys"].empty())
            return;

        std::vector<std::string> keys;
        keys.reserve(body["keys"].size());
        for (const auto &k : body["keys"])
        {
            if (!k.is_string() || k.get<std::string>().empty())
                continue;
            // Restricted charset (see design): real keys never contain
            // quotes/commas/braces, so the ANY array literal needs no escaping.
            const std::string &ks = k.get<std::string>();
            if (ks.find_first_of("\"',{}") == std::string::npos)
                keys.push_back(ks);
        }
        if (keys.empty())
            return;

        std::string arrayLit = "{";
        for (size_t i = 0; i < keys.size(); ++i)
            arrayLit += (i ? "," : "") + keys[i];
        arrayLit += "}";

        auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
        pqxx::work txn(_dbConn.get());
        auto rows = gameServices_.getDatabase().execChecked(txn, "get_outbox_diff_keys", {arrayLit});
        txn.commit();

        std::unordered_set<std::string> appliedSet;
        for (const auto &row : rows)
            appliedSet.insert(row[0].as<std::string>());
        std::vector<std::string> applied;
        std::vector<std::string> unknown;
        for (const auto &k : keys)
            (appliedSet.count(k) ? applied : unknown).push_back(k);

        auto sendSocket = gameServices_.getChunkManager().resolveLiveSocket(clientSocket);
        if (!sendSocket)
        {
            log_->error("[GAME] outboxDiff: no live chunk socket");
            return;
        }
        outboxSendDiff(networkManager_, sendSocket, applied, unknown);
        log_->info("[GAME] outboxDiff: applied={} unknown={}",
            applied.size(), unknown.size());
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("handleOutboxUnackedEvent error: " + std::string(ex.what()));
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
    case Event::DELETE_PLAYER_QUEST:
        dialogueQuestHandler_.handleDeletePlayerQuestEvent(event);
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
        effectsHandler_.handleSaveActiveEffectEvent(event);
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
        progressionHandler_.handleGetPlayerPityEvent(event);
        break;
    case Event::GET_PLAYER_BESTIARY:
        progressionHandler_.handleGetPlayerBestiaryEvent(event);
        break;
    case Event::SAVE_PITY_COUNTER:
        progressionHandler_.handleSavePityCounterEvent(event);
        break;
    case Event::SAVE_BESTIARY_KILL:
        progressionHandler_.handleSaveBestiaryKillEvent(event);
        break;
    case Event::GET_TIMED_CHAMPION_TEMPLATES:
        progressionHandler_.handleGetTimedChampionTemplatesEvent(event);
        break;
    case Event::TIMED_CHAMPION_KILLED:
        progressionHandler_.handleTimedChampionKilledEvent(event);
        break;

    // Stage 4: Reputation & Mastery
    case Event::GET_PLAYER_REPUTATIONS:
        skillsHandler_.handleGetPlayerReputationsEvent(event);
        break;
    case Event::SAVE_REPUTATION:
        skillsHandler_.handleSaveReputationEvent(event);
        break;
    case Event::GET_PLAYER_MASTERIES:
        skillsHandler_.handleGetPlayerMasteriesEvent(event);
        break;
    case Event::SAVE_MASTERY:
        skillsHandler_.handleSaveMasteryEvent(event);
        break;
    case Event::SAVE_LEARNED_SKILL:
        skillsHandler_.handleSaveLearnedSkillEvent(event);
        break;
    case Event::SAVE_SKILL_BAR_SLOT:
        skillsHandler_.handleSaveSkillBarSlotEvent(event);
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
        socialHandler_.handleGetTitleDefinitionsEvent(event);
        break;
    case Event::GET_PLAYER_TITLES:
        socialHandler_.handleGetPlayerTitlesEvent(event);
        break;
    case Event::SAVE_PLAYER_TITLE:
        socialHandler_.handleSavePlayerTitleEvent(event);
        break;
    case Event::SAVE_GM_ACTION:
        socialHandler_.handleSaveGmActionEvent(event);
        break;
    case Event::SAVE_USER_BAN:
        socialHandler_.handleSaveUserBanEvent(event);
        break;

    // Emote system
    case Event::GET_EMOTE_DEFINITIONS:
        socialHandler_.handleGetEmoteDefinitionsEvent(event);
        break;
    case Event::GET_PLAYER_EMOTES:
        socialHandler_.handleGetPlayerEmotesEvent(event);
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
        effectsHandler_.handleSaveSkillCooldownEvent(event);
        break;
    case Event::GET_PLAYER_SKILL_COOLDOWNS:
        effectsHandler_.handleGetPlayerSkillCooldownsEvent(event);
        break;

    // Analytics system (migration 058)
    case Event::SAVE_ANALYTICS_EVENT:
        effectsHandler_.handleSaveAnalyticsEventEvent(event);
        break;

    case Event::SAVE_PLAY_TIME:
        characterHandler_.handleSavePlayTimeEvent(event);
        break;

    case Event::MARK_CHARACTERS_ONLINE:
        characterHandler_.handleMarkCharactersOnlineEvent(event);
        break;
    case Event::OUTBOX_UNACKED:
        handleOutboxUnackedEvent(event);
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

    // Liveness for sweepSilentClients: a ping proves the session is alive.
    gameServices_.getClientManager().touchClientData(clientID);

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




// ── GET_PLAYER_PITY ────────────────────────────────────────────────────────

// ── GET_PLAYER_BESTIARY ────────────────────────────────────────────────────

// ── SAVE_PITY_COUNTER ──────────────────────────────────────────────────────

// ── SAVE_BESTIARY_KILL ─────────────────────────────────────────────────────

// ── GET_TIMED_CHAMPION_TEMPLATES ──────────────────────────────────────────────


// ── TIMED_CHAMPION_KILLED ─────────────────────────────────────────────────────


// ── GET_PLAYER_REPUTATIONS ─────────────────────────────────────────────────

// ── SAVE_REPUTATION ────────────────────────────────────────────────────────

// ── GET_PLAYER_MASTERIES ───────────────────────────────────────────────────

// ── SAVE_MASTERY ───────────────────────────────────────────────────────────

// ── GET_MASTERY_DEFINITIONS ────────────────────────────────────────────────

// ── SAVE_LEARNED_SKILL ────────────────────────────────────────────────────

// ── SAVE_SKILL_BAR_SLOT ────────────────────────────────────────────────────

// ── GET_ZONE_EVENT_TEMPLATES ───────────────────────────────────────────────

// ── GET_TITLE_DEFINITIONS ──────────────────────────────────────────────────
// Sends the global title catalog to the requesting chunk-server connection.
// Called once on chunk-server startup (no characterId needed).

// ── GET_PLAYER_TITLES ──────────────────────────────────────────────────────

// ── SAVE_PLAYER_TITLE ──────────────────────────────────────────────────────
// Body: { "eventType":"savePlayerTitle", "characterId":7, "titleSlug":"wolf_slayer", "equipped":true }

// ── GET_EMOTE_DEFINITIONS ──────────────────────────────────────────────────

// ── GET_PLAYER_EMOTES ──────────────────────────────────────────────────────

// ── GET_NPC_AMBIENT_SPEECH ─────────────────────────────────────────────────

// ── GET_WORLD_OBJECTS (migration 043) ─────────────────────────────────────

// Skill cooldown persistence (migration 067)
// Upserts one cooldown row for a player skill.
// Body fields: characterId (int), skillSlug (string), cooldownEndsAtMs (int64 unix ms).

// Loads all still-active cooldowns for a character and sends them back to chunk server.
// Body fields: characterId (int).

// Analytics system (migration 058)
// Inserts one row into game_analytics. Fire-and-forget — no reply to chunk server.
// Body fields: analyticsType, characterId, sessionId, level, zoneId, payload (JSON object).


