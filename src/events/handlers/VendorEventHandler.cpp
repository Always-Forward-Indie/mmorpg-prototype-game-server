#include "events/handlers/VendorEventHandler.hpp"

#include "data/DataStructs.hpp"
#include "utils/ResponseBuilder.hpp"

VendorEventHandler::VendorEventHandler(
    NetworkManager &networkManager,
    GameServices &gameServices)
    : BaseEventHandler(networkManager, gameServices)
{
}

void
VendorEventHandler::handleGetVendorDataEvent(const Event &event)
{
    int clientID = event.getClientID();
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();

    try
    {
        auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
        pqxx::work txn(_dbConn.get());

        // Get all vendor NPC IDs
        auto npcRows = gameServices_.getDatabase().execChecked(txn, "get_vendor_npcs", {});

        nlohmann::json vendorList = nlohmann::json::array();
        for (const auto &npcRow : npcRows)
        {
            int npcId = npcRow["npc_id"].as<int>();

            auto itemRows = gameServices_.getDatabase().execChecked(
                txn, "get_vendor_inventory", {npcId});

            nlohmann::json items = nlohmann::json::array();
            for (const auto &row : itemRows)
            {
                nlohmann::json item;
                item["itemId"] = row["item_id"].as<int>();
                item["stockCurrent"] = row["stock_current"].as<int>();
                item["stockMax"] = row["stock_max"].as<int>();
                item["restockAmount"] = row["restock_amount"].as<int>();
                item["restockIntervalSec"] = row["restock_interval_sec"].as<int>();
                item["priceOverrideBuy"] = row["price_override_buy"].as<int>();
                item["priceOverrideSell"] = row["price_override_sell"].as<int>();
                items.push_back(std::move(item));
            }

            nlohmann::json vendor;
            vendor["npcId"] = npcId;
            vendor["items"] = std::move(items);
            vendorList.push_back(std::move(vendor));
        }
        txn.commit();

        ResponseBuilder builder;
        nlohmann::json response = builder
                                      .setHeader("message", "Vendor data loaded")
                                      .setHeader("hash", "")
                                      .setHeader("clientId", clientID)
                                      .setHeader("eventType", "setVendorData")
                                      .setBody("vendors", vendorList)
                                      .build();

        networkManager_.sendResponse(clientSocket,
            networkManager_.generateResponseMessage("success", response));

        gameServices_.getLogger().log("[EH] Sent vendor data: " +
                                      std::to_string(vendorList.size()) + " vendors.");
    }
    catch (const DatabaseError &)
    {
        // Catalog entry: DB failure must reach dispatchCatalog
        // (catalogFailed -> incomplete JOIN), never a partial success.
        throw;
    }
    catch (const std::exception &e)
    {
        gameServices_.getLogger().logError("handleGetVendorDataEvent error: " + std::string(e.what()));
    }
}

void
VendorEventHandler::handleGetTrainerDataEvent(const Event &event)
{
    auto clientSocket = event.getClientSocket();
    int clientID = event.getClientID();

    try
    {
        auto _dbConn = gameServices_.getDatabase().getConnectionLocked();
        pqxx::work txn(_dbConn.get());

        auto npcRows = gameServices_.getDatabase().execChecked(txn, "get_trainer_npcs", {});

        nlohmann::json trainerList = nlohmann::json::array();
        for (const auto &npcRow : npcRows)
        {
            int npcId = npcRow["npc_id"].as<int>();
            int classId = npcRow["class_id"].as<int>(0);

            auto skillRows = gameServices_.getDatabase().execChecked(
                txn, "get_trainer_skills", {npcId});

            nlohmann::json skills = nlohmann::json::array();
            for (const auto &row : skillRows)
            {
                nlohmann::json skill;
                skill["skillId"] = row["skill_id"].as<int>();
                skill["skillSlug"] = row["skill_slug"].as<std::string>();
                skill["skillName"] = row["skill_name"].as<std::string>();
                skill["isPassive"] = row["is_passive"].as<bool>();
                skill["requiredLevel"] = row["required_level"].as<int>();
                skill["spCost"] = row["sp_cost"].as<int>();
                skill["goldCost"] = row["gold_cost"].as<int>();
                skill["requiresBook"] = row["requires_book"].as<bool>();
                skill["bookItemId"] = row["book_item_id"].as<int>();
                skill["prerequisiteSkillSlug"] = row["prerequisite_skill_slug"].as<std::string>();
                skills.push_back(std::move(skill));
            }

            nlohmann::json trainer;
            trainer["npcId"] = npcId;
            trainer["classId"] = classId;
            trainer["skills"] = std::move(skills);
            trainerList.push_back(std::move(trainer));
        }
        txn.commit();

        ResponseBuilder builder;
        nlohmann::json response = builder
                                      .setHeader("message", "Trainer data loaded")
                                      .setHeader("hash", "")
                                      .setHeader("clientId", clientID)
                                      .setHeader("eventType", "setTrainerData")
                                      .setBody("trainers", trainerList)
                                      .build();

        networkManager_.sendResponse(clientSocket,
            networkManager_.generateResponseMessage("success", response));

        gameServices_.getLogger().log("[EH] Sent trainer data: " +
                                      std::to_string(trainerList.size()) + " trainers.");
    }
    catch (const DatabaseError &)
    {
        // Catalog entry: DB failure must reach dispatchCatalog
        // (catalogFailed -> incomplete JOIN), never a partial success.
        throw;
    }
    catch (const std::exception &e)
    {
        gameServices_.getLogger().logError("handleGetTrainerDataEvent error: " + std::string(e.what()));
    }
}
