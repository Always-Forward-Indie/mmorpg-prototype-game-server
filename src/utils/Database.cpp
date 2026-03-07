#include "utils/Database.hpp"
#include "utils/Config.hpp"
#include <iostream>
#include <spdlog/logger.h>

Database::Database(std::tuple<DatabaseConfig, GameServerConfig> &configs, Logger &logger)
    : logger_(logger)
{
    log_ = logger.getSystem("db");
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

        log_->info("Connecting to database...");
        log_->debug("Database name: " + databaseName);
        // log_->debug("User: " + user);
        log_->debug("Host: " + host);
        log_->debug("Port: " + std::to_string(port));

        connectionString_ = "dbname=" + databaseName + " user=" + user + " password=" + password + " host=" + host + " port=" + std::to_string(port);
        connection_ = std::make_unique<pqxx::connection>(connectionString_);

        if (connection_->is_open())
        {
            log_->info("Database connection established!");
        }
        else
        {
            log_->error("Database connection failed!");
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
        connection_->prepare("search_user",
            "SELECT u.* FROM users u "
            "JOIN user_sessions s ON s.user_id = u.id "
            "WHERE s.token_hash = $1 "
            "AND s.revoked_at IS NULL "
            "AND s.expires_at > now() "
            "LIMIT 1;");

        connection_->prepare("get_character",
            "SELECT characters.id as character_id, characters.level as character_lvl, "
            "characters.name as character_name, character_class.name as character_class, race.name as race_name, "
            "characters.experience_points as character_exp, "
            "COALESCE(ccs.current_health, 1) as character_current_health, "
            "COALESCE(ccs.current_mana, 1) as character_current_mana "
            "FROM characters "
            "JOIN character_class ON characters.class_id = character_class.id "
            "JOIN race on characters.race_id = race.id "
            "LEFT JOIN character_current_state ccs ON ccs.character_id = characters.id "
            "WHERE characters.owner_id = $1 AND characters.id = $2 LIMIT 1;");

        // get character attributes — base (class formula) + permanent modifiers + equipment bonuses
        // Formula: ROUND(base_value + multiplier * level^exponent)
        // Permanent modifiers (from quests/GM/events) are summed and added on top.
        // Equipment bonuses: all items in character_equipment → player_inventory → item_attributes_mapping
        //   where apply_on = 'equip'. Keyed by entity_attributes.id, same as base stats.
        connection_->prepare("get_character_attributes",
            "WITH char_info AS ( "
            "  SELECT c.id, c.class_id, c.level "
            "  FROM characters c WHERE c.id = $1 "
            "), "
            "base_attrs AS ( "
            "  SELECT ea.id, ea.name, ea.slug, "
            "    ROUND(csf.base_value + csf.multiplier * POWER(ci.level, csf.exponent))::int AS value "
            "  FROM char_info ci "
            "  JOIN class_stat_formula csf ON csf.class_id = ci.class_id "
            "  JOIN entity_attributes ea ON ea.id = csf.attribute_id "
            "), "
            "perm_mods AS ( "
            "  SELECT attribute_id, SUM(value)::int AS bonus "
            "  FROM character_permanent_modifiers "
            "  WHERE character_id = $1 "
            "  GROUP BY attribute_id "
            "), "
            "equip_bonus AS ( "
            "  SELECT iam.attribute_id, SUM(iam.value)::int AS bonus "
            "  FROM character_equipment ce "
            "  JOIN player_inventory pi ON pi.id = ce.inventory_item_id "
            "  JOIN item_attributes_mapping iam "
            "    ON iam.item_id = pi.item_id AND iam.apply_on = 'equip' "
            "  WHERE ce.character_id = $1 "
            "  GROUP BY iam.attribute_id "
            ") "
            "SELECT ba.id, ba.name, ba.slug, "
            "  (ba.value + COALESCE(pm.bonus, 0) + COALESCE(eb.bonus, 0))::int AS value "
            "FROM base_attrs ba "
            "LEFT JOIN perm_mods pm ON pm.attribute_id = ba.id "
            "LEFT JOIN equip_bonus eb ON eb.attribute_id = ba.id "
            "ORDER BY ba.id;");

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
                                                     "coalesce(MAX(CASE WHEN sp.slug='max_range'   THEN spm.property_value END),0) AS max_range, "
                                                     "coalesce(MAX(CASE WHEN sp.slug='area_radius' THEN spm.property_value END),0) AS area_radius "

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

        connection_->prepare("set_basic_character_data",
            "UPDATE characters "
            "SET level = $2, experience_points = $3 "
            "WHERE id = $1;");
        connection_->prepare("upsert_character_current_state",
            "INSERT INTO character_current_state (character_id, current_health, current_mana) "
            "VALUES ($1, $2, $3) "
            "ON CONFLICT (character_id) DO UPDATE SET "
            "current_health = EXCLUDED.current_health, "
            "current_mana = EXCLUDED.current_mana, "
            "updated_at = now();");
        connection_->prepare("set_character_level", "UPDATE characters "
                                                    "SET level = $2 WHERE id = $1;");
        connection_->prepare("set_character_health",
            "INSERT INTO character_current_state (character_id, current_health) "
            "VALUES ($1, $2) "
            "ON CONFLICT (character_id) DO UPDATE SET "
            "current_health = EXCLUDED.current_health, updated_at = now();");
        connection_->prepare("set_character_mana",
            "INSERT INTO character_current_state (character_id, current_mana) "
            "VALUES ($1, $2) "
            "ON CONFLICT (character_id) DO UPDATE SET "
            "current_mana = EXCLUDED.current_mana, updated_at = now();");
        connection_->prepare("set_character_exp", "UPDATE characters "
                                                  "SET experience_points = $2 WHERE id = $1;");
        connection_->prepare("set_character_exp_level", "UPDATE characters "
                                                        "SET experience_points = $2, level = $3 WHERE id = $1;");

        connection_->prepare("get_character_position", "SELECT x, y, z FROM character_position WHERE character_id = $1 LIMIT 1;");
        connection_->prepare("set_character_position", "UPDATE character_position SET x = $1, y = $2, z = $3 WHERE character_id = $4;");

        // get mob spawn zone data — join spawn_zone_mobs (many-to-many) for mob_id, spawn_count, respawn_time
        // One row per (zone, mob) pair; szm_id is the unique key for the map.
        // Old schema had mob_id directly in spawn_zones; new schema uses spawn_zone_mobs link table.
        connection_->prepare("get_mob_spawn_zone_data",
            "SELECT szm.id AS szm_id, sz.zone_id, sz.zone_name, "
            "sz.min_spawn_x, sz.max_spawn_x, sz.min_spawn_y, sz.max_spawn_y, sz.min_spawn_z, sz.max_spawn_z, "
            "szm.mob_id, szm.spawn_count, szm.respawn_time, "
            "m.name AS mob_name, m.level, mr.name AS race "
            "FROM spawn_zones sz "
            "JOIN spawn_zone_mobs szm ON szm.spawn_zone_id = sz.zone_id "
            "JOIN mob m ON m.id = szm.mob_id "
            "JOIN mob_race mr ON mr.id = m.race_id;");

        // get mobs list — include base_xp, rank_id, mob_ranks multiplier, AI config, flee/archetype
        connection_->prepare("get_mobs",
            "SELECT m.id, m.name, m.slug, m.level, m.spawn_health, m.spawn_mana, "
            "m.is_aggressive, m.is_dead, m.radius, m.base_xp, m.rank_id, "
            "mr.name AS race, mrk.code AS rank_code, mrk.mult AS rank_mult, "
            "m.aggro_range, m.attack_range, m.attack_cooldown, m.chase_multiplier, m.patrol_speed, "
            "m.is_social, m.chase_duration, "
            "m.flee_hp_threshold, m.ai_archetype "
            "FROM mob m "
            "JOIN mob_race mr ON mr.id = m.race_id "
            "JOIN mob_ranks mrk ON mrk.rank_id = m.rank_id;");

        // get mob attributes — unified mob_stat table (migration 015)
        // multiplier IS NULL → use flat_value directly
        // multiplier IS NOT NULL → ROUND(flat_value + multiplier * level^exponent)
        connection_->prepare("get_mob_attributes",
            "SELECT ea.id, ea.name, ea.slug, "
            "  CASE "
            "    WHEN ms.multiplier IS NOT NULL "
            "      THEN GREATEST(0, ROUND(ms.flat_value + ms.multiplier * POWER(m.level::numeric, ms.exponent)))::int "
            "    ELSE ms.flat_value::int "
            "  END AS value "
            "FROM mob_stat ms "
            "JOIN entity_attributes ea ON ea.id = ms.attribute_id "
            "JOIN mob m                ON m.id  = ms.mob_id "
            "WHERE ms.mob_id = $1 "
            "ORDER BY ea.id;");

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
                                               "coalesce(MAX(CASE WHEN sp.slug='max_range'   THEN spm.property_value END),0) AS max_range, "
                                               "coalesce(MAX(CASE WHEN sp.slug='area_radius' THEN spm.property_value END),0) AS area_radius "

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

        // get items attributes — joined via entity_attributes (unified stat dictionary)
        connection_->prepare("get_item_attributes", "SELECT ea.id, ea.name, ea.slug, iam.value, iam.apply_on "
                                                    "FROM item_attributes_mapping iam "
                                                    "JOIN entity_attributes ea ON ea.id = iam.attribute_id "
                                                    "WHERE iam.item_id = $1;");

        // get mobs loot info — include is_harvest_only and quantity range (migrations 013, 014)
        connection_->prepare("get_mobs_loot",
            "SELECT id, mob_id, item_id, drop_chance, "
            "COALESCE(is_harvest_only, false) AS is_harvest_only, "
            "COALESCE(min_quantity, 1) AS min_quantity, "
            "COALESCE(max_quantity, 1) AS max_quantity "
            "FROM mob_loot_info;");

        // get npc position — prefer npc_placements (static placement data), fallback to npc_position
        connection_->prepare("get_npc_position",
            "SELECT COALESCE(np.x, npos.x, 0) AS x, "
            "COALESCE(np.y, npos.y, 0) AS y, "
            "COALESCE(np.z, npos.z, 0) AS z, "
            "COALESCE(np.rot_z, npos.rot_z, 0) AS rot_z, "
            "COALESCE(np.zone_id, npos.zone_id) AS zone_id "
            "FROM npc n "
            "LEFT JOIN npc_placements np ON np.npc_id = n.id "
            "LEFT JOIN npc_position npos ON npos.npc_id = n.id "
            "WHERE n.id = $1 "
            "LIMIT 1;");

        // get npc list
        connection_->prepare("get_npcs", "SELECT npc.id, npc.name, npc.slug, npc.level, npc.current_health, npc.current_mana, "
                                         "npc.is_dead, npc.radius, npc.is_interactable, "
                                         "race.slug as race, nt.slug as npc_type "
                                         "FROM npc "
                                         "JOIN race ON npc.race_id = race.id "
                                         "JOIN npc_type nt ON npc.npc_type = nt.id "
                                         ";");

        // get npc attributes
        connection_->prepare("get_npc_attributes", "SELECT entity_attributes.*, npc_attributes.value FROM npc_attributes "
                                                   "JOIN entity_attributes ON npc_attributes.attribute_id = entity_attributes.id "
                                                   "WHERE npc_attributes.npc_id = $1;");

        // get npc skills
        connection_->prepare("get_npc_skills", "WITH cs as ( "
                                               "SELECT skill_id, current_level "
                                               "FROM npc_skills "
                                               "WHERE npc_id = $1 "
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
                                               "coalesce(MAX(CASE WHEN sp.slug='max_range'   THEN spm.property_value END),0) AS max_range, "
                                               "coalesce(MAX(CASE WHEN sp.slug='area_radius' THEN spm.property_value END),0) AS area_radius "

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

        // --- Dialogue queries ---
        connection_->prepare("get_dialogues",
            "SELECT id, slug, version, start_node_id FROM dialogue ORDER BY id;");

        connection_->prepare("get_dialogue_nodes",
            "SELECT id, dialogue_id, type::text AS type, speaker_npc_id, client_node_key, "
            "COALESCE(condition_group::text, '') AS condition_group, "
            "COALESCE(action_group::text, '') AS action_group, "
            "COALESCE(jump_target_node_id, 0) AS jump_target_node_id "
            "FROM dialogue_node ORDER BY dialogue_id, id;");

        connection_->prepare("get_dialogue_edges",
            "SELECT id, from_node_id, to_node_id, order_index, "
            "COALESCE(client_choice_key, '') AS client_choice_key, "
            "COALESCE(condition_group::text, '') AS condition_group, "
            "COALESCE(action_group::text, '') AS action_group, "
            "hide_if_locked "
            "FROM dialogue_edge ORDER BY from_node_id, order_index;");

        connection_->prepare("get_npc_dialogue_mappings",
            "SELECT npc_id, dialogue_id, priority, "
            "COALESCE(condition_group::text, '') AS condition_group "
            "FROM npc_dialogue ORDER BY npc_id, priority DESC;");

        // --- Quest queries ---
        connection_->prepare("get_quests",
            "SELECT id, slug, min_level, repeatable, cooldown_sec, "
            "COALESCE(giver_npc_id, 0) AS giver_npc_id, "
            "COALESCE(turnin_npc_id, 0) AS turnin_npc_id, "
            "COALESCE(client_quest_key, '') AS client_quest_key "
            "FROM quest ORDER BY id;");

        connection_->prepare("get_quest_steps",
            "SELECT id, quest_id, step_index, step_type::text AS step_type, "
            "params::text AS params, "
            "COALESCE(client_step_key, '') AS client_step_key "
            "FROM quest_step ORDER BY quest_id, step_index;");

        connection_->prepare("get_quest_rewards",
            "SELECT id, quest_id, reward_type, "
            "COALESCE(item_id, 0) AS item_id, quantity, amount "
            "FROM quest_reward ORDER BY quest_id;");

        // --- Player quest queries ---
        connection_->prepare("get_player_quests",
            "SELECT quest_id, state::text AS state, current_step, "
            "COALESCE(progress::text, '{}') AS progress "
            "FROM player_quest WHERE player_id = $1 "
            "AND state NOT IN ('turned_in', 'failed');");

        connection_->prepare("upsert_player_quest",
            "INSERT INTO player_quest (player_id, quest_id, state, current_step, progress, updated_at) "
            "VALUES ($1, $2, $3::quest_state, $4, $5::jsonb, now()) "
            "ON CONFLICT (player_id, quest_id) DO UPDATE SET "
            "state = EXCLUDED.state, current_step = EXCLUDED.current_step, "
            "progress = EXCLUDED.progress, updated_at = now();");

        // --- Player flag queries ---
        connection_->prepare("get_player_flags",
            "SELECT flag_key, COALESCE(int_value, 0) AS int_value, "
            "COALESCE(bool_value, false) AS bool_value "
            "FROM player_flag WHERE player_id = $1;");

        // --- Player active effects ---
        connection_->prepare("get_player_active_effects",
            "SELECT pae.id, pae.effect_id, se.slug AS effect_slug, "
            "seft.slug AS effect_type_slug, "
            "COALESCE(pae.attribute_id, 0) AS attribute_id, "
            "COALESCE(ea.slug, '') AS attribute_slug, "
            "pae.value::float AS value, pae.source_type, "
            "pae.tick_ms, "
            "COALESCE(EXTRACT(EPOCH FROM pae.expires_at)::bigint, 0) AS expires_at_unix "
            "FROM player_active_effect pae "
            "JOIN skill_effects se ON se.id = pae.effect_id "
            "JOIN skill_effects_type seft ON seft.id = se.effect_type_id "
            "LEFT JOIN entity_attributes ea ON ea.id = pae.attribute_id "
            "WHERE pae.player_id = $1 "
            "  AND (pae.expires_at IS NULL OR pae.expires_at > NOW()) "
            "ORDER BY pae.id;");

        connection_->prepare("upsert_player_flag",
            "INSERT INTO player_flag (player_id, flag_key, int_value, bool_value, updated_at) "
            "VALUES ($1, $2, $3, $4, now()) "
            "ON CONFLICT (player_id, flag_key) DO UPDATE SET "
            "int_value = EXCLUDED.int_value, bool_value = EXCLUDED.bool_value, "
            "updated_at = now();");

        // --- Game config queries ---
        // Загружает все геймплейные константы. Ответ читает GameConfigService::loadConfig().
        connection_->prepare("get_game_config",
            "SELECT key, value, value_type FROM public.game_config ORDER BY key;");
    }
    else
    {
        log_->error("Cannot prepare queries: Database connection is not open.");
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

// CRITICAL-6 + HIGH-10: Returns ScopedConnection that holds dbMutex_ for the lifetime of the caller's transaction.
// Automatically reconnects if the connection was dropped.
Database::ScopedConnection
Database::getConnectionLocked()
{
    std::unique_lock<std::mutex> lock(dbMutex_);
    // HIGH-10: reconnect if the connection was lost
    if (!connection_ || !connection_->is_open())
    {
        log_->info("Database connection lost — reconnecting...");
        try
        {
            connection_ = std::make_unique<pqxx::connection>(connectionString_);
            if (connection_->is_open())
            {
                log_->info("Database reconnected successfully.");
                // Re-register prepared statements so pqxx::work can use them
                prepareDefaultQueries();
            }
            else
            {
                throw std::runtime_error("Reconnect attempt failed: connection not open.");
            }
        }
        catch (const std::exception &e)
        {
            throw std::runtime_error("Database reconnect failed: " + std::string(e.what()));
        }
    }
    // Transfer ownership of the lock into ScopedConnection (lock is adopted, not re-locked)
    return ScopedConnection(std::move(lock), *connection_);
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
