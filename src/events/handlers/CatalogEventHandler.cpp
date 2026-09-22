#include "events/handlers/CatalogEventHandler.hpp"

#include "data/DataStructs.hpp"
#include "events/Outbox.hpp"
#include "utils/ResponseBuilder.hpp"
#include "utils/TerminalColors.hpp"

CatalogEventHandler::CatalogEventHandler(
    NetworkManager &networkManager,
    GameServices &gameServices)
    : BaseEventHandler(networkManager, gameServices)
{
}

void
CatalogEventHandler::handleGetCharacterExpForLevelEvent(const Event &event)
{
    const auto &data = event.getData();
    int clientID = event.getClientID();
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();

    try
    {
        log_->debug("Processing getCharacterExpForLevel request from chunk server");

        // Извлекаем данные клиента из события
        if (std::holds_alternative<ClientDataStruct>(data))
        {
            ClientDataStruct clientData = std::get<ClientDataStruct>(data);

            // Парсим JSON данные из запроса, чтобы получить уровень
            std::string requestData = clientData.hash; // Используем hash поле для передачи JSON данных
            nlohmann::json requestJson = nlohmann::json::parse(requestData);

            int level = requestJson["body"]["level"].get<int>();

            log_->debug("Requesting experience points for level: " + std::to_string(level));

            // Получаем опыт для указанного уровня из базы данных
            int experiencePoints = 0;
            try
            {
                auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
                pqxx::work txn(_dbConn.get());
                auto result = gameServices_.getDatabase().execChecked(txn, "get_character_exp_for_next_level", {level});

                if (!result.empty())
                {
                    experiencePoints = result[0][0].as<int>();
                    gameServices_.getLogger().log("Found experience points for level " + std::to_string(level) + ": " +
                                                      std::to_string(experiencePoints),
                        GREEN);
                }
                else
                {
                    log_->error("No experience data found for level " + std::to_string(level));
                    experiencePoints = 0;
                }

                txn.commit();
            }
            catch (const DatabaseError &)
            {
                // DB transport failure: no partial/empty success here.
                // JOIN entries are counted by dispatchCatalog; runtime
                // reads fall through to the error response below.
                throw;
            }
            catch (const std::exception &e)
            {
                gameServices_.getLogger().logError("Database error getting experience for level " +
                                                   std::to_string(level) + ": " + std::string(e.what()));
                experiencePoints = 0;
            }

            // Подготавливаем ответ
            ResponseBuilder builder;
            nlohmann::json response = builder
                                          .setHeader("message", "Experience for level retrieved successfully!")
                                          .setHeader("hash", clientData.hash)
                                          .setHeader("clientId", clientData.clientId)
                                          .setHeader("eventType", "getCharacterExpForLevel")
                                          .setBody("level", level)
                                          .setBody("experiencePoints", experiencePoints)
                                          .build();

            // Отправляем ответ чанк-серверу
            std::string responseData = networkManager_.generateResponseMessage("success", response);
            networkManager_.sendResponse(clientSocket, responseData);

            log_->info("Sent experience data for level " + std::to_string(level) +
                       " to chunk server");
        }
        else
        {
            log_->error("Error extracting client data from getCharacterExpForLevel event");

            // Отправляем ошибку
            ResponseBuilder builder;
            nlohmann::json errorResponse = builder
                                               .setHeader("message", "Error processing getCharacterExpForLevel request!")
                                               .setHeader("eventType", "getCharacterExpForLevel")
                                               .build();

            std::string responseData = networkManager_.generateResponseMessage("error", errorResponse);
            networkManager_.sendResponse(clientSocket, responseData);
        }
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("Exception in handleGetCharacterExpForLevelEvent: " + std::string(ex.what()));

        // Отправляем ошибку
        ResponseBuilder builder;
        nlohmann::json errorResponse = builder
                                           .setHeader("message", "Server error processing getCharacterExpForLevel request!")
                                           .setHeader("eventType", "getCharacterExpForLevel")
                                           .build();

        std::string responseData = networkManager_.generateResponseMessage("error", errorResponse);
        networkManager_.sendResponse(clientSocket, responseData);
    }
}

void
CatalogEventHandler::handleGetExpLevelTableEvent(const Event &event)
{
    const auto &data = event.getData();
    int clientID = event.getClientID();
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();

    try
    {
        log_->debug("Processing getExpLevelTable request from chunk server");

        // Извлекаем данные клиента из события
        if (std::holds_alternative<ClientDataStruct>(data))
        {
            ClientDataStruct clientData = std::get<ClientDataStruct>(data);

            log_->debug("Requesting experience level table from database");

            // Получаем всю таблицу опыта из базы данных
            nlohmann::json expLevelTable = nlohmann::json::array();
            try
            {
                auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
                pqxx::work txn(_dbConn.get());
                auto result = gameServices_.getDatabase().execChecked(txn, "get_exp_level_table", {});

                for (const auto &row : result)
                {
                    nlohmann::json levelEntry;
                    levelEntry["level"] = row["level"].as<int>();
                    levelEntry["experiencePoints"] = row["experience_points"].as<int>();
                    expLevelTable.push_back(levelEntry);
                }

                txn.commit();

                gameServices_.getLogger().log("Retrieved " + std::to_string(expLevelTable.size()) +
                                                  " experience level entries from database",
                    GREEN);
            }
            catch (const DatabaseError &)
            {
                // Catalog entry: DB failure must reach dispatchCatalog
                // (catalogFailed -> incomplete JOIN), never a partial success.
                throw;
            }
            catch (const std::exception &e)
            {
                gameServices_.getLogger().logError("Database error getting experience level table: " + std::string(e.what()));
                expLevelTable = nlohmann::json::array(); // Пустой массив в случае ошибки
            }

            // Подготавливаем ответ
            ResponseBuilder builder;
            nlohmann::json response = builder
                                          .setHeader("message", "Experience level table retrieved successfully!")
                                          .setHeader("hash", clientData.hash)
                                          .setHeader("clientId", clientData.clientId)
                                          .setHeader("eventType", "getExpLevelTable")
                                          .setBody("expLevelTable", expLevelTable)
                                          .build();

            // Отправляем ответ чанк-серверу
            std::string responseData = networkManager_.generateResponseMessage("success", response);
            networkManager_.sendResponse(clientSocket, responseData);

            gameServices_.getLogger().log("Sent experience level table (" + std::to_string(expLevelTable.size()) +
                                              " entries) to chunk server",
                GREEN);
        }
        else
        {
            log_->error("Error extracting client data from getExpLevelTable event");

            // Отправляем ошибку
            ResponseBuilder builder;
            nlohmann::json errorResponse = builder
                                               .setHeader("message", "Error processing getExpLevelTable request!")
                                               .setHeader("eventType", "getExpLevelTable")
                                               .build();

            std::string responseData = networkManager_.generateResponseMessage("error", errorResponse);
            networkManager_.sendResponse(clientSocket, responseData);
        }
    }
    catch (const DatabaseError &)
    {
        // Catalog entry: DB failure must reach dispatchCatalog
        // (catalogFailed -> incomplete JOIN), never a partial success.
        throw;
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("Exception in handleGetExpLevelTableEvent: " + std::string(ex.what()));

        // Отправляем ошибку
        ResponseBuilder builder;
        nlohmann::json errorResponse = builder
                                           .setHeader("message", "Server error processing getExpLevelTable request!")
                                           .setHeader("eventType", "getExpLevelTable")
                                           .build();

        std::string responseData = networkManager_.generateResponseMessage("error", errorResponse);
        networkManager_.sendResponse(clientSocket, responseData);
    }
}

void
CatalogEventHandler::handleGetGameConfigEvent(const Event &event)
{
    int clientID = event.getClientID();
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();

    try
    {
        // Получаем snapshot конфига (уже загружен loadConfig() выше)
        auto configMap = gameServices_.getGameConfigService().getAll();

        nlohmann::json configListJson = nlohmann::json::array();
        for (const auto &[key, value] : configMap)
        {
            nlohmann::json entry;
            entry["key"] = key;
            entry["value"] = value;
            configListJson.push_back(entry);
        }

        nlohmann::json response = ResponseBuilder()
                                      .setHeader("message", "Game config loaded")
                                      .setHeader("hash", "")
                                      .setHeader("clientId", clientID)
                                      .setHeader("eventType", "setGameConfig")
                                      .setBody("configList", configListJson)
                                      .build();

        std::string responseData = networkManager_.generateResponseMessage("success", response);
        networkManager_.sendResponse(clientSocket, responseData);

        gameServices_.getLogger().log("Sent game config (" +
                                      std::to_string(configMap.size()) + " entries) to chunk-server.");
    }
    catch (const DatabaseError &)
    {
        // DB transport failure: no partial/empty success here.
        // JOIN entries are counted by dispatchCatalog; runtime
        // reads fall through to the error response below.
        throw;
    }
    catch (const std::exception &e)
    {
        gameServices_.getLogger().logError("handleGetGameConfigEvent error: " + std::string(e.what()));
    }
}

void
CatalogEventHandler::handleGetRespawnZonesEvent(const Event &event)
{
    int clientID = event.getClientID();
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();

    try
    {
        auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
        pqxx::work txn(_dbConn.get());
        auto rows = gameServices_.getDatabase().execChecked(txn, "get_respawn_zones", {});
        txn.commit();

        nlohmann::json zonesJson = nlohmann::json::array();
        for (const auto &row : rows)
        {
            nlohmann::json z;
            z["id"] = row["id"].as<int>();
            z["name"] = row["name"].as<std::string>();
            z["x"] = row["x"].as<float>();
            z["y"] = row["y"].as<float>();
            z["z"] = row["z"].as<float>();
            z["zoneId"] = row["zone_id"].as<int>();
            z["isDefault"] = row["is_default"].as<bool>();
            // Area bounds for random point selection
            z["minX"] = row["min_x"].as<float>();
            z["maxX"] = row["max_x"].as<float>();
            z["minY"] = row["min_y"].as<float>();
            z["maxY"] = row["max_y"].as<float>();
            z["minZ"] = row["min_z"].as<float>();
            z["maxZ"] = row["max_z"].as<float>();
            z["shapeType"] = row["shape_type"].as<std::string>();
            z["centerX"] = row["center_x"].as<float>();
            z["centerY"] = row["center_y"].as<float>();
            z["innerRadius"] = row["inner_radius"].as<float>();
            z["outerRadius"] = row["outer_radius"].as<float>();
            zonesJson.push_back(z);
        }

        ResponseBuilder builder;
        nlohmann::json response = builder
                                      .setHeader("message", "Respawn zones loaded")
                                      .setHeader("hash", "")
                                      .setHeader("clientId", clientID)
                                      .setHeader("eventType", "setRespawnZonesList")
                                      .setBody("respawnZonesData", zonesJson)
                                      .build();
        std::string responseData = networkManager_.generateResponseMessage("success", response);
        networkManager_.sendResponse(clientSocket, responseData);

        log_->info("[RESPAWN_ZONES] Sent " + std::to_string(zonesJson.size()) + " respawn zones to chunk server");
    }
    catch (const DatabaseError &)
    {
        // Catalog entry: DB failure must reach dispatchCatalog
        // (catalogFailed -> incomplete JOIN), never a partial success.
        throw;
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("handleGetRespawnZonesEvent error: " + std::string(ex.what()));
    }
}

void
CatalogEventHandler::handleGetClassSpawnZonesEvent(const Event &event)
{
    int clientID = event.getClientID();
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();

    try
    {
        const auto &zones = gameServices_.getClassSpawnZoneManager().getAllClassSpawnZones();

        nlohmann::json zonesJson = nlohmann::json::array();
        for (const auto &pair : zones)
        {
            const auto &zone = pair.second;
            nlohmann::json z;
            z["id"] = zone.id;
            z["classId"] = zone.classId;
            z["className"] = zone.className;
            z["zoneId"] = zone.zoneId;
            z["minX"] = zone.minX;
            z["maxX"] = zone.maxX;
            z["minY"] = zone.minY;
            z["maxY"] = zone.maxY;
            z["minZ"] = zone.minZ;
            z["maxZ"] = zone.maxZ;
            z["shapeType"] = (zone.shape == ZoneShape::CIRCLE ? "CIRCLE" :
                              zone.shape == ZoneShape::ANNULUS ? "ANNULUS" : "RECT");
            z["centerX"] = zone.centerX;
            z["centerY"] = zone.centerY;
            z["innerRadius"] = zone.innerRadius;
            z["outerRadius"] = zone.outerRadius;
            zonesJson.push_back(z);
        }

        ResponseBuilder builder;
        nlohmann::json response = builder
                                      .setHeader("message", "Class spawn zones loaded")
                                      .setHeader("hash", "")
                                      .setHeader("clientId", clientID)
                                      .setHeader("eventType", "setClassSpawnZonesList")
                                      .setBody("classSpawnZonesData", zonesJson)
                                      .build();
        std::string responseData = networkManager_.generateResponseMessage("success", response);
        networkManager_.sendResponse(clientSocket, responseData);

        log_->info("[CLASS_SPAWN_ZONES] Sent " + std::to_string(zonesJson.size()) + " class spawn zones to chunk server");
    }
    catch (const DatabaseError &)
    {
        // DB transport failure: no partial/empty success here.
        // JOIN entries are counted by dispatchCatalog; runtime
        // reads fall through to the error response below.
        throw;
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("handleGetClassSpawnZonesEvent error: " + std::string(ex.what()));
    }
}

void
CatalogEventHandler::handleGetStatusEffectTemplatesEvent(const Event &event)
{
    int clientID = event.getClientID();
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();

    try
    {
        auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
        pqxx::work txn(_dbConn.get());
        auto rows = gameServices_.getDatabase().execChecked(txn, "get_status_effect_templates", {});
        txn.commit();

        // Group rows by effect_slug into a nested JSON structure:
        // { "effectSlug": "resurrection_sickness", "category": "debuff",
        //   "durationSec": 120,
        //   "modifiers": [{"modifierType":"percent_all","attributeSlug":"","value":-20}] }
        nlohmann::json effectsJson = nlohmann::json::array();
        std::string currentSlug;
        nlohmann::json currentEffect;

        auto pushCurrentEffect = [&]()
        {
            if (!currentSlug.empty())
                effectsJson.push_back(std::move(currentEffect));
        };

        for (const auto &row : rows)
        {
            std::string slug = row["effect_slug"].as<std::string>();

            if (slug != currentSlug)
            {
                pushCurrentEffect();
                currentEffect = nlohmann::json::object();
                currentEffect["effectSlug"] = slug;
                currentEffect["category"] = row["category"].as<std::string>();
                currentEffect["durationSec"] = row["duration_sec"].as<int>();
                currentEffect["modifiers"] = nlohmann::json::array();
                currentSlug = slug;
            }

            // A LEFT JOIN row with no modifier columns means the effect has no modifiers yet.
            if (!row["modifier_type"].is_null())
            {
                nlohmann::json mod;
                mod["modifierType"] = row["modifier_type"].as<std::string>();
                mod["attributeSlug"] = row["attribute_slug"].as<std::string>(); // '' for percent_all
                mod["value"] = row["value"].as<double>();
                currentEffect["modifiers"].push_back(std::move(mod));
            }
        }
        pushCurrentEffect(); // push the last effect

        ResponseBuilder builder;
        nlohmann::json response = builder
                                      .setHeader("message", "Status effect templates loaded")
                                      .setHeader("hash", "")
                                      .setHeader("clientId", clientID)
                                      .setHeader("eventType", "setStatusEffectTemplates")
                                      .setBody("templates", effectsJson)
                                      .build();
        std::string responseData = networkManager_.generateResponseMessage("success", response);
        networkManager_.sendResponse(clientSocket, responseData);

        log_->info("[STATUS_EFFECT_TEMPLATES] Sent " + std::to_string(effectsJson.size()) + " templates to chunk server");
    }
    catch (const DatabaseError &)
    {
        // Catalog entry: DB failure must reach dispatchCatalog
        // (catalogFailed -> incomplete JOIN), never a partial success.
        throw;
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("handleGetStatusEffectTemplatesEvent error: " + std::string(ex.what()));
    }
}

void
CatalogEventHandler::handleGetGameZonesEvent(const Event &event)
{
    int clientID = event.getClientID();
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();

    try
    {
        auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
        pqxx::work txn(_dbConn.get());
        auto rows = gameServices_.getDatabase().execChecked(txn, "get_game_zones", {});
        txn.commit();

        nlohmann::json zonesJson = nlohmann::json::array();
        for (const auto &row : rows)
        {
            nlohmann::json z;
            z["id"] = row["id"].as<int>();
            z["slug"] = row["slug"].as<std::string>();
            z["name"] = row["name"].as<std::string>();
            z["minLevel"] = row["min_level"].as<int>();
            z["maxLevel"] = row["max_level"].as<int>();
            z["isPvp"] = row["is_pvp"].as<bool>();
            z["isSafeZone"] = row["is_safe_zone"].as<bool>();
            z["minX"] = row["min_x"].as<float>();
            z["maxX"] = row["max_x"].as<float>();
            z["minY"] = row["min_y"].as<float>();
            z["maxY"] = row["max_y"].as<float>();
            z["shape"] = row["shape_type"].as<std::string>("RECT");
            z["centerX"] = row["center_x"].as<float>(0.0f);
            z["centerY"] = row["center_y"].as<float>(0.0f);
            z["innerRadius"] = row["inner_radius"].as<float>(0.0f);
            z["outerRadius"] = row["outer_radius"].as<float>(0.0f);
            z["explorationXpReward"] = row["exploration_xp_reward"].as<int>();
            z["championThresholdKills"] = row["champion_threshold_kills"].as<int>();
            zonesJson.push_back(z);
        }

        ResponseBuilder builder;
        nlohmann::json response = builder
                                      .setHeader("message", "Game zones loaded")
                                      .setHeader("hash", "")
                                      .setHeader("clientId", clientID)
                                      .setHeader("eventType", "setGameZonesList")
                                      .setBody("gameZonesData", zonesJson)
                                      .build();
        std::string responseData = networkManager_.generateResponseMessage("success", response);
        networkManager_.sendResponse(clientSocket, responseData);

        log_->info("[GAME_ZONES] Sent " + std::to_string(zonesJson.size()) + " game zones to chunk server");
    }
    catch (const DatabaseError &)
    {
        // Catalog entry: DB failure must reach dispatchCatalog
        // (catalogFailed -> incomplete JOIN), never a partial success.
        throw;
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("handleGetGameZonesEvent error: " + std::string(ex.what()));
    }
}

void
CatalogEventHandler::handleGetMasteryDefinitionsEvent(const Event &event)
{
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();
    int clientID = event.getClientID();

    try
    {
        auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
        pqxx::work txn(_dbConn.get());
        auto result = gameServices_.getDatabase().execChecked(
            txn, "get_mastery_definitions", {});

        nlohmann::json defsJson = nlohmann::json::array();
        for (const auto &row : result)
        {
            nlohmann::json d;
            d["slug"] = row["slug"].as<std::string>();
            d["name"] = row["name"].as<std::string>();
            d["weaponTypeSlug"] = row["weapon_type_slug"].is_null()
                                       ? ""
                                       : row["weapon_type_slug"].as<std::string>();
            d["maxValue"] = row["max_value"].as<double>();
            d["targetAttributeSlug"] = row["target_attribute_slug"].as<std::string>();
            defsJson.push_back(std::move(d));
        }

        ResponseBuilder builder;
        nlohmann::json response = builder
                                      .setHeader("message", "Mastery definitions")
                                      .setHeader("hash", "")
                                      .setHeader("clientId", clientID)
                                      .setHeader("eventType", "setMasteryDefinitionsData")
                                      .setBody("definitions", defsJson)
                                      .build();
        networkManager_.sendResponse(clientSocket,
            networkManager_.generateResponseMessage("success", response));

        log_->info("[MASTERY] Sent {} mastery definitions to chunk server", defsJson.size());
    }
    catch (const DatabaseError &)
    {
        // Catalog entry: DB failure must reach dispatchCatalog
        // (catalogFailed -> incomplete JOIN), never a partial success.
        throw;
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("handleGetMasteryDefinitionsEvent error: " + std::string(ex.what()));
    }
}

void
CatalogEventHandler::handleGetZoneEventTemplatesEvent(const Event &event)
{
    int clientID = event.getClientID();
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();

    try
    {
        auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
        pqxx::work txn(_dbConn.get());
        auto rows = gameServices_.getDatabase().execChecked(
            txn, "get_zone_event_templates", {});

        nlohmann::json arr = nlohmann::json::array();
        for (const auto &row : rows)
        {
            nlohmann::json t;
            t["id"] = row["id"].as<int>();
            t["slug"] = row["slug"].as<std::string>();
            t["gameZoneId"] = row["game_zone_id"].as<int>();
            t["triggerType"] = row["trigger_type"].as<std::string>();
            t["durationSec"] = row["duration_sec"].as<int>();
            t["lootMultiplier"] = row["loot_multiplier"].as<float>();
            t["spawnRateMultiplier"] = row["spawn_rate_multiplier"].as<float>();
            t["mobSpeedMultiplier"] = row["mob_speed_multiplier"].as<float>();
            t["announceKey"] = row["announce_key"].as<std::string>();
            t["intervalHours"] = row["interval_hours"].as<int>();
            t["randomChancePerHour"] = row["random_chance_per_hour"].as<float>();
            t["hasInvasionWave"] = row["has_invasion_wave"].as<bool>();
            t["invasionMobTemplateId"] = row["invasion_mob_template_id"].is_null()
                                             ? 0
                                             : row["invasion_mob_template_id"].as<int>();
            t["invasionWaveCount"] = row["invasion_wave_count"].as<int>();
            arr.push_back(std::move(t));
        }

        ResponseBuilder builder;
        nlohmann::json response = builder
                                      .setHeader("message", "Zone event templates loaded")
                                      .setHeader("hash", "")
                                      .setHeader("clientId", clientID)
                                      .setHeader("eventType", "setZoneEventTemplatesList")
                                      .setBody("templates", arr)
                                      .build();
        networkManager_.sendResponse(clientSocket,
            networkManager_.generateResponseMessage("success", response));

        log_->info("[ZONE_EVENT] Sent {} zone event templates to chunk server", arr.size());
    }
    catch (const DatabaseError &)
    {
        // Catalog entry: DB failure must reach dispatchCatalog
        // (catalogFailed -> incomplete JOIN), never a partial success.
        throw;
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("handleGetZoneEventTemplatesEvent error: " + std::string(ex.what()));
    }
}

void
CatalogEventHandler::handleGetNPCAmbientSpeechEvent(const Event &event)
{
    int clientID = event.getClientID();
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();

    try
    {
        auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
        pqxx::work txn(_dbConn.get());
        auto result = gameServices_.getDatabase().execChecked(
            txn, "get_npc_ambient_speech", {});
        txn.commit();

        // Group rows by npc_id into a map to build the nested JSON structure
        // map<npc_id, {minSec, maxSec, lines[]}>
        std::map<int, nlohmann::json> configMap;

        for (const auto &row : result)
        {
            int npcId = row["npc_id"].as<int>();
            if (configMap.find(npcId) == configMap.end())
            {
                nlohmann::json cfg;
                cfg["npcId"] = npcId;
                cfg["minIntervalSec"] = row["min_interval_sec"].as<int>();
                cfg["maxIntervalSec"] = row["max_interval_sec"].as<int>();
                cfg["lines"] = nlohmann::json::array();
                configMap[npcId] = std::move(cfg);
            }

            nlohmann::json line;
            line["id"] = row["line_id"].as<int>();
            line["lineKey"] = row["line_key"].as<std::string>();
            line["triggerType"] = row["trigger_type"].as<std::string>();
            line["triggerRadius"] = row["trigger_radius"].as<int>();
            line["priority"] = row["priority"].as<int>();
            line["weight"] = row["weight"].as<int>();
            line["cooldownSec"] = row["cooldown_sec"].as<int>();

            // condition_group is stored as JSON text in DB; broken content
            // falls back to null for that line only (same policy as titles:
            // never fail the whole static push over one bad row).
            if (!row["condition_group"].is_null())
            {
                try
                {
                    line["conditionGroup"] = nlohmann::json::parse(
                        row["condition_group"].as<std::string>());
                }
                catch (const std::exception &e)
                {
                    log_->warn("[AMBIENT] npc={} line={} has broken condition_group ({}), sending null",
                        npcId, row["line_id"].as<int>(), e.what());
                    line["conditionGroup"] = nullptr;
                }
                catch (...)
                {
                    line["conditionGroup"] = nullptr;
                }
            }
            else
            {
                line["conditionGroup"] = nullptr;
            }

            configMap[npcId]["lines"].push_back(std::move(line));
        }

        nlohmann::json ambientArray = nlohmann::json::array();
        for (auto &[npcId, cfg] : configMap)
            ambientArray.push_back(std::move(cfg));

        ResponseBuilder builder;
        nlohmann::json response = builder
                                      .setHeader("message", "NPC ambient speech configs")
                                      .setHeader("hash", "")
                                      .setHeader("clientId", clientID)
                                      .setHeader("eventType", "setNPCAmbientSpeech")
                                      .setBody("ambientSpeech", ambientArray)
                                      .build();
        networkManager_.sendResponse(clientSocket,
            networkManager_.generateResponseMessage("success", response));

        log_->info("[AMBIENT] Sent ambient speech for {} NPCs to chunk server", ambientArray.size());
    }
    catch (const DatabaseError &)
    {
        // Catalog entry: DB failure must reach dispatchCatalog
        // (catalogFailed -> incomplete JOIN), never a partial success.
        throw;
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("handleGetNPCAmbientSpeechEvent error: " + std::string(ex.what()));
    }
}

void
CatalogEventHandler::handleGetWorldObjectsEvent(const Event &event)
{
    int clientID = event.getClientID();
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();

    try
    {
        auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
        pqxx::work txn(_dbConn.get());
        auto result = gameServices_.getDatabase().execChecked(
            txn, "get_world_objects", {});
        txn.commit();

        nlohmann::json objectsArray = nlohmann::json::array();
        for (const auto &row : result)
        {
            nlohmann::json obj;
            obj["id"] = row["id"].as<int>();
            obj["slug"] = row["slug"].as<std::string>();
            obj["nameKey"] = row["name_key"].as<std::string>();
            obj["objectType"] = row["object_type"].as<std::string>();
            obj["scope"] = row["scope"].as<std::string>();
            obj["posX"] = row["pos_x"].as<float>();
            obj["posY"] = row["pos_y"].as<float>();
            obj["posZ"] = row["pos_z"].as<float>();
            obj["rotZ"] = row["rot_z"].as<float>();
            obj["zoneId"] = row["zone_id"].as<int>();
            obj["dialogueId"] = row["dialogue_id"].as<int>();
            obj["lootTableId"] = row["loot_table_id"].as<int>();
            obj["requiredItemId"] = row["required_item_id"].as<int>();
            obj["interactionRadius"] = row["interaction_radius"].as<float>();
            obj["channelTimeSec"] = row["channel_time_sec"].as<int>();
            obj["respawnSec"] = row["respawn_sec"].as<int>();
            obj["isActiveByDefault"] = row["is_active_by_default"].as<bool>();
            obj["minLevel"] = row["min_level"].as<int>();
            obj["currentState"] = row["current_state"].as<std::string>();

            const std::string cgText = row["condition_group"].as<std::string>();
            try
            {
                obj["conditionGroup"] = nlohmann::json::parse(cgText);
            }
            catch (...)
            {
                obj["conditionGroup"] = nullptr;
            }

            objectsArray.push_back(std::move(obj));
        }

        ResponseBuilder builder;
        nlohmann::json response = builder
                                      .setHeader("message", "World objects list")
                                      .setHeader("hash", "")
                                      .setHeader("clientId", clientID)
                                      .setHeader("eventType", "setWorldObjects")
                                      .setBody("worldObjects", objectsArray)
                                      .build();
        networkManager_.sendResponse(clientSocket,
            networkManager_.generateResponseMessage("success", response));

        log_->info("[WIO] Sent {} world objects to chunk server", objectsArray.size());
    }
    catch (const DatabaseError &)
    {
        // Catalog entry: DB failure must reach dispatchCatalog
        // (catalogFailed -> incomplete JOIN), never a partial success.
        throw;
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("handleGetWorldObjectsEvent error: " + std::string(ex.what()));
    }
}
