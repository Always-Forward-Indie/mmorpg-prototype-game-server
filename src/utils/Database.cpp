#include "utils/Database.hpp"
#include "utils/Config.hpp"
#include <iostream>

Database::Database(std::tuple<DatabaseConfig, GameServerConfig> &configs, Logger &logger)
    : logger_(logger)
{
    connect(configs);
    prepareDefaultQueries();
}

void
Database::connect(std::tuple<DatabaseConfig, GameServerConfig> &configs)
{
    try
    {
        short port = std::get<0>(configs).port;
        std::string host = std::get<0>(configs).host;
        std::string databaseName = std::get<0>(configs).dbname;
        std::string user = std::get<0>(configs).user;
        std::string password = std::get<0>(configs).password;

        logger_.log("Connecting to database...", YELLOW);
        logger_.log("Database name: " + databaseName, BLUE);
        // logger_.log("User: " + user, BLUE);
        logger_.log("Host: " + host, BLUE);
        logger_.log("Port: " + std::to_string(port), BLUE);

        connection_ = std::make_unique<pqxx::connection>(
            "dbname=" + databaseName + " user=" + user + " password=" + password + " host=" + host + " port=" + std::to_string(port));

        if (connection_->is_open())
        {
            logger_.log("Database connection established!", GREEN);
        }
        else
        {
            logger_.logError("Database connection failed!");
        }
    }
    catch (const std::exception &e)
    {
        handleDatabaseError(e);
    }
}

void
Database::prepareDefaultQueries()
{
    if (connection_->is_open())
    {
        connection_->prepare("search_user", "SELECT * FROM users WHERE session_key = $1 LIMIT 1;");

        connection_->prepare("get_character", "SELECT characters.id as character_id, characters.level as character_lvl, "
                                              "characters.name as character_name, character_class.name as character_class, race.name as race_name, "
                                              "characters.experience_points as character_exp, characters.current_health as character_current_health, "
                                              "characters.current_mana as character_current_mana "
                                              "FROM characters "
                                              "JOIN character_class ON characters.class_id = character_class.id "
                                              "JOIN race on characters.race_id = race.id "
                                              "WHERE characters.owner_id = $1 AND characters.id = $2 LIMIT 1;");

        // get character attributes
        connection_->prepare("get_character_attributes", "SELECT character_attributes.*, character_attributes_mapping.value FROM character_attributes_mapping "
                                                         "JOIN character_attributes ON character_attributes_mapping.attribute_id = character_attributes.id "
                                                         "WHERE character_attributes_mapping.character_id = $1;");

        // get character exp for level
        connection_->prepare("get_character_exp_for_next_level", "SELECT experience_points FROM exp_for_level WHERE level = $1 + 1;");

        connection_->prepare("set_basic_character_data", "UPDATE characters "
                                                         "SET level = $2, experience_points = $3, current_health = $4, current_mana = $5 "
                                                         "WHERE id = $1;");
        connection_->prepare("set_character_level", "UPDATE characters "
                                                    "SET level = $2 WHERE id = $1;");
        connection_->prepare("set_character_health", "UPDATE characters "
                                                     "SET current_health = $2 WHERE id = $1;");
        connection_->prepare("set_character_mana", "UPDATE characters "
                                                   "SET current_mana = $2 WHERE id = $1;");
        connection_->prepare("set_character_exp", "UPDATE characters "
                                                  "SET experience_points = $2 WHERE id = $1;");

        connection_->prepare("get_character_position", "SELECT x, y, z FROM character_position WHERE character_id = $1 LIMIT 1;");
        connection_->prepare("set_character_position", "UPDATE character_position SET x = $1, y = $2, z = $3 WHERE character_id = $4;");

        // get mob spawn zone data
        connection_->prepare("get_mob_spawn_zone_data", "SELECT spawn_zones.*, mob.name as mob_name, mob.level, mob_race.name as race FROM spawn_zones "
                                                        "JOIN mob ON spawn_zones.mob_id = mob.id "
                                                        "JOIN mob_race ON mob.race_id = mob_race.id "
                                                        ";");

        // get mobs list
        connection_->prepare("get_mobs", "SELECT mob.*, mob_race.name as race FROM mob "
                                         "JOIN mob_race ON mob.race_id = mob_race.id "
                                         ";");

        // get mob attributes
        connection_->prepare("get_mob_attributes", "SELECT mob_attributes.*, mob_attributes_mapping.value FROM mob_attributes_mapping "
                                                   "JOIN mob_attributes ON mob_attributes_mapping.attribute_id = mob_attributes.id "
                                                   "WHERE mob_attributes_mapping.mob_id = $1;");
    }
    else
    {
        logger_.logError("Cannot prepare queries: Database connection is not open.");
    }
}

pqxx::connection &
Database::getConnection()
{
    if (connection_->is_open())
    {
        return *connection_;
    }
    else
    {
        throw std::runtime_error("Database connection is not open.");
    }
}

// Function to handle database errors
void
Database::handleDatabaseError(const std::exception &e)
{
    // Handle database connection or query errors
    logger_.logError("Database error: " + std::string(e.what()));
}

// Function to execute a query with a transaction
pqxx::result
Database::executeQueryWithTransaction(
    pqxx::work &transaction,
    const std::string &preparedQueryName,
    const std::vector<std::variant<int, float, double, std::string>> &parameters)
{
    try
    {
        // Convert all parameters to strings
        std::vector<std::string> paramStrings;
        for (const auto &param : parameters)
        {
            paramStrings.push_back(std::visit([](const auto &value) -> std::string
                {
                if constexpr (std::is_same_v<std::decay_t<decltype(value)>, std::string>)
                    return value;
                else
                    return std::to_string(value); },
                param));
        }

        // Convert to a vector of raw C-style strings (needed for exec_prepared)
        std::vector<const char *> cstrParams;
        for (auto &param : paramStrings)
        {
            cstrParams.push_back(param.c_str());
        }

        // Use the parameter pack expansion to pass all arguments dynamically
        pqxx::result result = transaction.exec_prepared(preparedQueryName, pqxx::prepare::make_dynamic_params(cstrParams.begin(), cstrParams.end()));

        return result;
    }
    catch (const std::exception &e)
    {
        transaction.abort(); // Rollback transaction
        handleDatabaseError(e);
        return pqxx::result();
    }
}
