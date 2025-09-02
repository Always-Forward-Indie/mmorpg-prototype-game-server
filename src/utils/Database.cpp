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
        connection_->prepare("get_character_attributes", "SELECT entity_attributes.*, character_attributes.value FROM character_attributes "
                                                         "JOIN entity_attributes ON character_attributes.attribute_id = entity_attributes.id "
                                                         "WHERE character_attributes.character_id = $1;");

        // get character skills
        connection_->prepare("get_character_skills", "WITH cs as ( "
                                                     "SELECT skill_id, current_level "
                                                     "FROM character_skills "
                                                     "WHERE character_id = $1 "
                                                     ")"

                                                     "SELECT "
                                                     "s.name as skill_name, "
                                                     "s.slug as skill_slug, "
                                                     "sst.slug as scale_stat, "
                                                     "ss.slug  as school,"
                                                     "seft.slug as skill_effect_type, "
                                                     "spm.skill_level, "

                                                     "coalesce(MAX(CASE WHEN se.slug='coeff'     THEN sem.value END),0) AS coeff, "
                                                     "coalesce(MAX(CASE WHEN se.slug='flat_add'  THEN sem.value END),0) AS flat_add, "

                                                     "coalesce(MAX(CASE WHEN sp.slug='cooldown_ms' THEN spm.property_value END),0) AS cooldown_ms, "
                                                     "coalesce(MAX(CASE WHEN sp.slug='gcd_ms'      THEN spm.property_value END),0) AS gcd_ms, "
                                                     "coalesce(MAX(CASE WHEN sp.slug='cast_ms'     THEN spm.property_value END),0) AS cast_ms, "
                                                     "coalesce(MAX(CASE WHEN sp.slug='cost_mp'     THEN spm.property_value END),0) AS cost_mp, "
                                                     "coalesce(MAX(CASE WHEN sp.slug='max_range'   THEN spm.property_value END),0) AS max_range "

                                                     "FROM cs "
                                                     "join "
                                                     "skills s ON s.id=cs.skill_id "
                                                     "JOIN skill_scale_type sst ON sst.id = s.scale_stat_id "
                                                     "JOIN skill_school ss ON ss.id = s.school_id "
                                                     "JOIN skill_effect_instances sei ON sei.skill_id = s.id "
                                                     "JOIN skill_effects_mapping sem ON sem.effect_instance_id = sei.id AND sem.level=cs.current_level "
                                                     "JOIN skill_effects se ON se.id = sem.effect_id "
                                                     "JOIN skill_effects_type seft ON seft.id = se.effect_type_id "
                                                     "LEFT JOIN skill_properties_mapping spm ON spm.skill_id = s.id AND spm.skill_level=cs.current_level "
                                                     "LEFT JOIN skill_properties sp ON sp.id = spm.property_id "
                                                     "GROUP BY s.id, s.name, s.slug, sst.slug, ss.slug, seft.slug, spm.skill_level;");

        // get character exp for level
        connection_->prepare("get_character_exp_for_next_level", "SELECT experience_points FROM exp_for_level WHERE level = $1 + 1;");

        // get experience for level data
        connection_->prepare("get_exp_level_table", "SELECT experience_points, level FROM exp_for_level;");

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
        connection_->prepare("get_mob_attributes", "SELECT entity_attributes.*, mob_attributes.value FROM mob_attributes "
                                                   "JOIN entity_attributes ON mob_attributes.attribute_id = entity_attributes.id "
                                                   "WHERE mob_attributes.mob_id = $1;");

        // get mob skills
        connection_->prepare("get_mob_skills", "WITH cs as ( "
                                               "SELECT skill_id, current_level "
                                               "FROM mob_skills "
                                               "WHERE mob_id = $1 "
                                               ")"

                                               "SELECT "
                                               "s.name as skill_name, "
                                               "s.slug as skill_slug, "
                                               "sst.slug as scale_stat, "
                                               "ss.slug  as school,"
                                               "seft.slug as skill_effect_type, "
                                               "spm.skill_level, "

                                               "coalesce(MAX(CASE WHEN se.slug='coeff'     THEN sem.value END),0) AS coeff, "
                                               "coalesce(MAX(CASE WHEN se.slug='flat_add'  THEN sem.value END),0) AS flat_add, "

                                               "coalesce(MAX(CASE WHEN sp.slug='cooldown_ms' THEN spm.property_value END),0) AS cooldown_ms, "
                                               "coalesce(MAX(CASE WHEN sp.slug='gcd_ms'      THEN spm.property_value END),0) AS gcd_ms, "
                                               "coalesce(MAX(CASE WHEN sp.slug='cast_ms'     THEN spm.property_value END),0) AS cast_ms, "
                                               "coalesce(MAX(CASE WHEN sp.slug='cost_mp'     THEN spm.property_value END),0) AS cost_mp, "
                                               "coalesce(MAX(CASE WHEN sp.slug='max_range'   THEN spm.property_value END),0) AS max_range "

                                               "FROM cs "
                                               "join "
                                               "skills s ON s.id=cs.skill_id "
                                               "JOIN skill_scale_type sst ON sst.id = s.scale_stat_id "
                                               "JOIN skill_school ss ON ss.id = s.school_id "
                                               "JOIN skill_effect_instances sei ON sei.skill_id = s.id "
                                               "JOIN skill_effects_mapping sem ON sem.effect_instance_id = sei.id AND sem.level=cs.current_level "
                                               "JOIN skill_effects se ON se.id = sem.effect_id "
                                               "JOIN skill_effects_type seft ON seft.id = se.effect_type_id "
                                               "LEFT JOIN skill_properties_mapping spm ON spm.skill_id = s.id AND spm.skill_level=cs.current_level "
                                               "LEFT JOIN skill_properties sp ON sp.id = spm.property_id "
                                               "GROUP BY s.id, s.name, s.slug, sst.slug, ss.slug, seft.slug, spm.skill_level;");

        // get items list
        connection_->prepare("get_items", "SELECT items.*, item_types.name as item_type_name, item_types.slug as item_type_slug, ir.name as rarity_name, ir.slug as rarity_slug,  COALESCE(es.name, '') as equip_slot_name, COALESCE(es.slug, '') as equip_slot_slug "
                                          "FROM items "
                                          "join items_rarity ir on ir.id = items.rarity_id "
                                          "left join equip_slot es on es.id = items.equip_slot "
                                          "JOIN item_types ON items.item_type = item_types.id "
                                          ";");

        // get items attributes
        connection_->prepare("get_item_attributes", "SELECT item_attributes.*, item_attributes_mapping.value FROM item_attributes_mapping "
                                                    "JOIN item_attributes ON item_attributes_mapping.attribute_id = item_attributes.id "
                                                    "WHERE item_attributes_mapping.item_id = $1;");

        // get mobs loot info
        connection_->prepare("get_mobs_loot", "SELECT mob_loot_info.* FROM mob_loot_info; ");
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
