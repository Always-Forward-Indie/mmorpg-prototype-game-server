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
            "COALESCE(ccs.current_mana, 1) as character_current_mana, "
            "character_class.id as class_id, "
            "COALESCE(characters.experience_debt, 0) as experience_debt, "
            "COALESCE(characters.free_skill_points, 0) as free_skill_points, "
            "COALESCE(cg.name, '') as gender_slug, "
            "COALESCE(characters.total_play_time_sec, 0) as total_play_time_sec, "
            "COALESCE(characters.last_session_play_time_sec, 0) as last_session_play_time_sec "
            "FROM characters "
            "JOIN character_class ON characters.class_id = character_class.id "
            "JOIN race on characters.race_id = race.id "
            "LEFT JOIN character_current_state ccs ON ccs.character_id = characters.id "
            "LEFT JOIN character_genders cg ON cg.id = characters.gender "
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
            "dura_config AS ( "
            "  SELECT "
            "    (SELECT value::float FROM game_config WHERE key = 'durability.tier1_threshold_pct') AS t1, "
            "    (SELECT value::float FROM game_config WHERE key = 'durability.tier1_penalty_pct')   AS p1, "
            "    (SELECT value::float FROM game_config WHERE key = 'durability.tier2_threshold_pct') AS t2, "
            "    (SELECT value::float FROM game_config WHERE key = 'durability.tier2_penalty_pct')   AS p2, "
            "    (SELECT value::float FROM game_config WHERE key = 'durability.tier3_threshold_pct') AS t3, "
            "    (SELECT value::float FROM game_config WHERE key = 'durability.tier3_penalty_pct')   AS p3 "
            "), "
            "equip_bonus AS ( "
            "  SELECT iam.attribute_id, "
            "    SUM( "
            "      CASE "
            "        WHEN i.is_durable = false THEN iam.value "
            "        WHEN i.durability_max = 0 THEN iam.value "
            "        WHEN COALESCE(pi.durability_current, i.durability_max) = 0 THEN 0 "
            "        WHEN COALESCE(pi.durability_current, i.durability_max)::float / i.durability_max < dc.t3 "
            "          THEN ROUND(iam.value::float * (1.0 - dc.p3))::int "
            "        WHEN COALESCE(pi.durability_current, i.durability_max)::float / i.durability_max < dc.t2 "
            "          THEN ROUND(iam.value::float * (1.0 - dc.p2))::int "
            "        WHEN COALESCE(pi.durability_current, i.durability_max)::float / i.durability_max < dc.t1 "
            "          THEN ROUND(iam.value::float * (1.0 - dc.p1))::int "
            "        ELSE iam.value "
            "      END "
            "    )::int AS bonus "
            "  FROM character_equipment ce "
            "  JOIN player_inventory pi ON pi.id = ce.inventory_item_id "
            "  JOIN items i ON i.id = pi.item_id "
            "  JOIN item_attributes_mapping iam "
            "    ON iam.item_id = pi.item_id AND iam.apply_on = 'equip' "
            "  CROSS JOIN dura_config dc "
            "  WHERE ce.character_id = $1 "
            "  GROUP BY iam.attribute_id "
            "), "
            "set_bonus AS ( "
            "  SELECT isb.attribute_id, SUM(isb.bonus_value)::int AS bonus "
            "  FROM ( "
            "    SELECT ism.set_id, COUNT(*)::int AS equipped_count "
            "    FROM character_equipment ce "
            "    JOIN player_inventory pi ON pi.id = ce.inventory_item_id "
            "    JOIN item_set_members ism ON ism.item_id = pi.item_id "
            "    WHERE ce.character_id = $1 "
            "    GROUP BY ism.set_id "
            "  ) equipped_sets "
            "  JOIN item_set_bonuses isb "
            "    ON isb.set_id = equipped_sets.set_id "
            "    AND equipped_sets.equipped_count >= isb.pieces_required "
            "  GROUP BY isb.attribute_id "
            ") "
            "SELECT ba.id, ba.name, ba.slug, "
            "  (ba.value + COALESCE(pm.bonus, 0) + COALESCE(eb.bonus, 0) + COALESCE(sb.bonus, 0))::int AS value "
            "FROM base_attrs ba "
            "LEFT JOIN perm_mods pm ON pm.attribute_id = ba.id "
            "LEFT JOIN equip_bonus eb ON eb.attribute_id = ba.id "
            "LEFT JOIN set_bonus sb ON sb.attribute_id = ba.id "
            "ORDER BY ba.id;");

        // get character skills
        // NOTE: damage-formula tables use LEFT JOINs so that passive skills (which have no
        // skill_effect_instances / skill_damage_formulas rows) are still returned.
        // COALESCE guards nullable columns that become NULL when those joins miss.
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
                                                     "COALESCE(seft.slug, '') as skill_effect_type, "
                                                     "COALESCE(spm.skill_level, cs.current_level) as skill_level, "

                                                     "coalesce(MAX(CASE WHEN se.slug IN ('coeff','heal_coeff') THEN sem.value END),0) AS coeff, "
                                                     "coalesce(MAX(CASE WHEN se.slug IN ('flat_add','heal_flat') THEN sem.value END),0) AS flat_add, "

                                                     "coalesce(MAX(CASE WHEN sp.slug='cooldown_ms' THEN spm.property_value END),0) AS cooldown_ms, "
                                                     "coalesce(MAX(CASE WHEN sp.slug='gcd_ms'      THEN spm.property_value END),0) AS gcd_ms, "
                                                     "coalesce(MAX(CASE WHEN sp.slug='cast_ms'     THEN spm.property_value END),0) AS cast_ms, "
                                                     "coalesce(MAX(CASE WHEN sp.slug='cost_mp'     THEN spm.property_value END),0) AS cost_mp, "
                                                     "coalesce(MAX(CASE WHEN sp.slug='max_range'   THEN spm.property_value END),0) AS max_range, "
                                                     "coalesce(MAX(CASE WHEN sp.slug='area_radius' THEN spm.property_value END),0) AS area_radius, "
                                                     "coalesce(MAX(CASE WHEN sp.slug='swing_ms'    THEN spm.property_value END),300)::integer AS swing_ms, "
                                                     "COALESCE(s.animation_name, '') AS animation_name, "
                                                     "COALESCE(s.is_passive, FALSE) AS is_passive, "
                                                     "COALESCE("
                                                     "  (SELECT json_agg("
                                                     "      json_build_object("
                                                     "        'effectSlug',      sae.effect_slug,"
                                                     "        'effectTypeSlug',  sae.effect_type_slug,"
                                                     "        'attributeSlug',   sae.attribute_slug,"
                                                     "        'value',           sae.value::float,"
                                                     "        'durationSeconds', sae.duration_seconds,"
                                                     "        'tickMs',          sae.tick_ms"
                                                     "      ) ORDER BY sae.id"
                                                     "    )"
                                                     "    FROM skill_active_effects sae"
                                                     "    WHERE sae.skill_id = s.id"
                                                     "  ), '[]'::json"
                                                     ") AS active_effects "

                                                     "FROM cs "
                                                     "JOIN skills s ON s.id=cs.skill_id "
                                                     "JOIN skill_scale_type sst ON sst.id = s.scale_stat_id "
                                                     "JOIN skill_school ss ON ss.id = s.school_id "
                                                     "LEFT JOIN skill_effect_instances sei ON sei.skill_id = s.id "
                                                     "LEFT JOIN skill_effects_mapping sem ON sem.effect_instance_id = sei.id AND sem.level=cs.current_level "
                                                     "LEFT JOIN skill_damage_formulas se ON se.id = sem.effect_id "
                                                     "LEFT JOIN skill_damage_types seft ON seft.id = se.effect_type_id "
                                                     "LEFT JOIN skill_properties_mapping spm ON spm.skill_id = s.id AND spm.skill_level=cs.current_level "
                                                     "LEFT JOIN skill_properties sp ON sp.id = spm.property_id "
                                                     "GROUP BY s.id, s.name, s.slug, s.animation_name, s.is_passive, sst.slug, ss.slug, seft.slug, COALESCE(spm.skill_level, cs.current_level);");

        // Passive skill modifiers for a character's passive skills.
        // Returns one row per (passive_skill, attribute) pair.
        // id is negated to avoid collisions with player_active_effect PKs.
        connection_->prepare("get_player_passive_skill_effects",
            "SELECT (-psm.id)::bigint AS id, "
            "s.slug AS effect_slug, "
            "psm.attribute_slug, "
            "psm.value::float AS value, "
            "psm.modifier_type "
            "FROM character_skills cs "
            "JOIN skills s ON cs.skill_id = s.id AND s.is_passive = TRUE "
            "JOIN passive_skill_modifiers psm ON psm.skill_id = s.id "
            "WHERE cs.character_id = $1 "
            "ORDER BY psm.id;");

        // passive modifiers for a single skill by skill slug (used when returning setLearnedSkill response)
        connection_->prepare("get_passive_skill_modifiers_by_slug",
            "SELECT s.slug AS effect_slug, "
            "psm.attribute_slug, "
            "psm.value::float AS value, "
            "psm.modifier_type "
            "FROM skills s "
            "JOIN passive_skill_modifiers psm ON psm.skill_id = s.id "
            "WHERE s.slug = $1 "
            "ORDER BY psm.id;");

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
        connection_->prepare("set_character_exp_level",
            "UPDATE characters "
            "SET experience_points = $2, level = $3, "
            "free_skill_points = free_skill_points + GREATEST(0, $3::integer - level) "
            "WHERE id = $1;");

        connection_->prepare("save_learned_skill",
            "INSERT INTO character_skills (character_id, skill_id, current_level) "
            "SELECT $1, s.id, 1 FROM skills s WHERE s.slug = $2 "
            "ON CONFLICT (character_id, skill_id) DO NOTHING;");

        connection_->prepare("decrement_skill_points",
            "UPDATE characters SET free_skill_points = GREATEST(0, free_skill_points - $2) WHERE id = $1;");

        // ── Skill Bar (migration 051) ─────────────────────────────────────────
        connection_->prepare("get_character_skill_bar",
            "SELECT slot_index, skill_slug "
            "FROM character_skill_bar "
            "WHERE character_id = $1 "
            "ORDER BY slot_index;");

        connection_->prepare("save_skill_bar_slot",
            "INSERT INTO character_skill_bar (character_id, slot_index, skill_slug) "
            "VALUES ($1, $2, $3) "
            "ON CONFLICT (character_id, slot_index) DO UPDATE SET skill_slug = EXCLUDED.skill_slug;");

        connection_->prepare("clear_skill_bar_slot",
            "DELETE FROM character_skill_bar "
            "WHERE character_id = $1 AND slot_index = $2;");

        connection_->prepare("set_character_experience_debt",
            "UPDATE characters SET experience_debt = $2 WHERE id = $1;");

        connection_->prepare("get_character_position", "SELECT x, y, z, rot_z FROM character_position WHERE character_id = $1 LIMIT 1;");
        connection_->prepare("set_character_position", "UPDATE character_position SET x = $1, y = $2, z = $3, rot_z = $4 WHERE character_id = $5;");

        connection_->prepare("reset_all_online",
            "UPDATE characters SET is_online = false WHERE is_online = true;");
        connection_->prepare("set_character_online",
            "UPDATE characters SET is_online = true WHERE id = $1;");
        connection_->prepare("save_character_playtime",
            "UPDATE characters SET total_play_time_sec = total_play_time_sec + $2 WHERE id = $1;");
        connection_->prepare("save_character_playtime_disconnect",
            "UPDATE characters SET total_play_time_sec = total_play_time_sec + $2, "
            "last_session_play_time_sec = $3, last_online_at = NOW(), is_online = false "
            "WHERE id = $1;");

        // get mob spawn zone data — join spawn_zone_mobs (many-to-many) for mob_id, spawn_count, respawn_time
        // One row per (zone, mob) pair; szm_id is the unique key for the map.
        // Old schema had mob_id directly in spawn_zones; new schema uses spawn_zone_mobs link table.
        connection_->prepare("get_mob_spawn_zone_data",
            "SELECT szm.id AS szm_id, sz.zone_id, sz.zone_name, "
            "sz.min_spawn_x, sz.max_spawn_x, sz.min_spawn_y, sz.max_spawn_y, sz.min_spawn_z, sz.max_spawn_z, "
            "sz.shape_type, sz.center_x, sz.center_y, sz.inner_radius, sz.outer_radius, sz.exclusion_game_zone_id, "
            "szm.mob_id, szm.spawn_count, szm.respawn_time, "
            "m.name AS mob_name, m.level, mr.name AS race "
            "FROM spawn_zones sz "
            "JOIN spawn_zone_mobs szm ON szm.spawn_zone_id = sz.zone_id "
            "JOIN mob m ON m.id = szm.mob_id "
            "JOIN mob_race mr ON mr.id = m.race_id;");

        // get mobs list — include base_xp, rank_id, mob_ranks multiplier, AI config, flee/archetype, Stage 3/4 fields,
        //                  and bestiary metadata (migration 040)
        connection_->prepare("get_mobs",
            "SELECT m.id, m.name, m.slug, m.level, m.spawn_health, m.spawn_mana, "
            "m.is_aggressive, m.is_dead, m.radius, m.base_xp, m.rank_id, "
            "mr.name AS race, mrk.code AS rank_code, mrk.mult AS rank_mult, "
            "m.aggro_range, m.attack_range, m.attack_cooldown, m.chase_multiplier, m.patrol_speed, m.patrol_radius, "
            "m.is_social, m.chase_duration, "
            "m.flee_hp_threshold, m.ai_archetype, "
            "COALESCE(m.can_evolve, FALSE) AS can_evolve, "
            "COALESCE(m.is_rare, FALSE) AS is_rare, "
            "COALESCE(m.rare_spawn_chance, 0.0) AS rare_spawn_chance, "
            "COALESCE(m.rare_spawn_condition, '') AS rare_spawn_condition, "
            "COALESCE(m.faction_slug, '') AS faction_slug, "
            "COALESCE(m.rep_delta_per_kill, 0) AS rep_delta_per_kill, "
            "COALESCE(m.biome_slug, '') AS biome_slug, "
            "COALESCE(m.mob_type_slug, 'beast') AS mob_type_slug, "
            "COALESCE("
            "  CASE WHEN ms_hp.multiplier IS NOT NULL "
            "    THEN GREATEST(0, ROUND(ms_hp.flat_value + ms_hp.multiplier * POWER(m.level::numeric, ms_hp.exponent)))::int "
            "    ELSE ms_hp.flat_value::int "
            "  END, m.spawn_health"
            ") AS hp_min, "
            "COALESCE("
            "  CASE WHEN ms_hp.multiplier IS NOT NULL "
            "    THEN GREATEST(0, ROUND(ms_hp.flat_value + ms_hp.multiplier * POWER(m.level::numeric, ms_hp.exponent)))::int "
            "    ELSE ms_hp.flat_value::int "
            "  END, m.spawn_health"
            ") AS hp_max "
            "FROM mob m "
            "JOIN mob_race mr ON mr.id = m.race_id "
            "JOIN mob_ranks mrk ON mrk.rank_id = m.rank_id "
            "LEFT JOIN mob_stat ms_hp ON ms_hp.mob_id = m.id AND ms_hp.attribute_id = 1;");

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

                                               "coalesce(MAX(CASE WHEN se.slug IN ('coeff','heal_coeff') THEN sem.value END),0) AS coeff, "
                                               "coalesce(MAX(CASE WHEN se.slug IN ('flat_add','heal_flat') THEN sem.value END),0) AS flat_add, "

                                               "coalesce(MAX(CASE WHEN sp.slug='cooldown_ms' THEN spm.property_value END),0) AS cooldown_ms, "
                                               "coalesce(MAX(CASE WHEN sp.slug='gcd_ms'      THEN spm.property_value END),0) AS gcd_ms, "
                                               "coalesce(MAX(CASE WHEN sp.slug='cast_ms'     THEN spm.property_value END),0) AS cast_ms, "
                                               "coalesce(MAX(CASE WHEN sp.slug='cost_mp'     THEN spm.property_value END),0) AS cost_mp, "
                                               "coalesce(MAX(CASE WHEN sp.slug='max_range'   THEN spm.property_value END),0) AS max_range, "
                                               "coalesce(MAX(CASE WHEN sp.slug='area_radius' THEN spm.property_value END),0) AS area_radius, "
                                               "coalesce(MAX(CASE WHEN sp.slug='swing_ms'    THEN spm.property_value END),300)::integer AS swing_ms, "
                                               "COALESCE(s.animation_name, '') AS animation_name "

                                               "FROM cs "
                                               "join "
                                               "skills s ON s.id=cs.skill_id "
                                               "JOIN skill_scale_type sst ON sst.id = s.scale_stat_id "
                                               "JOIN skill_school ss ON ss.id = s.school_id "
                                               "JOIN skill_effect_instances sei ON sei.skill_id = s.id "
                                               "JOIN skill_effects_mapping sem ON sem.effect_instance_id = sei.id AND sem.level=cs.current_level "
                                               "JOIN skill_damage_formulas se ON se.id = sem.effect_id "
                                               "JOIN skill_damage_types seft ON seft.id = se.effect_type_id "
                                               "LEFT JOIN skill_properties_mapping spm ON spm.skill_id = s.id AND spm.skill_level=cs.current_level "
                                               "LEFT JOIN skill_properties sp ON sp.id = spm.property_id "
                                               "GROUP BY s.id, s.name, s.slug, s.animation_name, sst.slug, ss.slug, seft.slug, spm.skill_level;");

        // get items list
        connection_->prepare("get_items", "SELECT items.*, item_types.name as item_type_name, item_types.slug as item_type_slug, ir.name as rarity_name, ir.slug as rarity_slug,  COALESCE(es.name, '') as equip_slot_name, COALESCE(es.slug, '') as equip_slot_slug, COALESCE(items.mastery_slug, '') as mastery_slug "
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

        // get per-class item restrictions (all rows; loaded once at startup)
        connection_->prepare("get_item_class_restrictions",
            "SELECT item_id, class_id FROM item_class_restrictions ORDER BY item_id;");

        // get item-set memberships (all rows; loaded once at startup)
        connection_->prepare("get_item_set_memberships",
            "SELECT ism.item_id, s.id AS set_id, s.slug AS set_slug "
            "FROM item_set_members ism "
            "JOIN item_sets s ON s.id = ism.set_id "
            "ORDER BY ism.item_id;");

        // get use-effects for a single item (migration 034)
        connection_->prepare("get_item_use_effects",
            "SELECT effect_slug, attribute_slug, value, is_instant, "
            "duration_seconds, tick_ms, cooldown_seconds "
            "FROM item_use_effects WHERE item_id = $1 ORDER BY id;");

        // get mobs loot info — include is_harvest_only, quantity range, and loot_tier (migration 040)
        connection_->prepare("get_mobs_loot",
            "SELECT id, mob_id, item_id, drop_chance, "
            "COALESCE(is_harvest_only, false) AS is_harvest_only, "
            "COALESCE(min_quantity, 1) AS min_quantity, "
            "COALESCE(max_quantity, 1) AS max_quantity, "
            "COALESCE(loot_tier, 'common') AS loot_tier "
            "FROM mob_loot_info;");

        // Bestiary: mob weaknesses and resistances — loaded once at startup (migration 040)
        connection_->prepare("get_mob_weaknesses_all",
            "SELECT mob_id, element_slug FROM mob_weaknesses ORDER BY mob_id;");

        connection_->prepare("get_mob_resistances_all",
            "SELECT mob_id, element_slug FROM mob_resistances ORDER BY mob_id;");

        // get npc position from npc_placements (single source of truth)
        connection_->prepare("get_npc_position",
            "SELECT COALESCE(np.x, 0) AS x, "
            "COALESCE(np.y, 0) AS y, "
            "COALESCE(np.z, 0) AS z, "
            "COALESCE(np.rot_z, 0) AS rot_z, "
            "np.zone_id AS zone_id "
            "FROM npc n "
            "LEFT JOIN npc_placements np ON np.npc_id = n.id "
            "WHERE n.id = $1 "
            "LIMIT 1;");

        // get npc list
        connection_->prepare("get_npcs", "SELECT npc.id, npc.name, npc.slug, npc.level, npc.current_health, npc.current_mana, "
                                         "npc.is_dead, npc.radius, npc.is_interactable, "
                                         "race.slug as race, nt.slug as npc_type, "
                                         "COALESCE(npc.faction_slug, '') as faction_slug "
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

                                               "coalesce(MAX(CASE WHEN se.slug IN ('coeff','heal_coeff') THEN sem.value END),0) AS coeff, "
                                               "coalesce(MAX(CASE WHEN se.slug IN ('flat_add','heal_flat') THEN sem.value END),0) AS flat_add, "

                                               "coalesce(MAX(CASE WHEN sp.slug='cooldown_ms' THEN spm.property_value END),0) AS cooldown_ms, "
                                               "coalesce(MAX(CASE WHEN sp.slug='gcd_ms'      THEN spm.property_value END),0) AS gcd_ms, "
                                               "coalesce(MAX(CASE WHEN sp.slug='cast_ms'     THEN spm.property_value END),0) AS cast_ms, "
                                               "coalesce(MAX(CASE WHEN sp.slug='cost_mp'     THEN spm.property_value END),0) AS cost_mp, "
                                               "coalesce(MAX(CASE WHEN sp.slug='max_range'   THEN spm.property_value END),0) AS max_range, "
                                               "coalesce(MAX(CASE WHEN sp.slug='area_radius' THEN spm.property_value END),0) AS area_radius, "
                                               "coalesce(MAX(CASE WHEN sp.slug='swing_ms'    THEN spm.property_value END),300)::integer AS swing_ms, "
                                               "COALESCE(s.animation_name, '') AS animation_name "

                                               "FROM cs "
                                               "join "
                                               "skills s ON s.id=cs.skill_id "
                                               "JOIN skill_scale_type sst ON sst.id = s.scale_stat_id "
                                               "JOIN skill_school ss ON ss.id = s.school_id "
                                               "JOIN skill_effect_instances sei ON sei.skill_id = s.id "
                                               "JOIN skill_effects_mapping sem ON sem.effect_instance_id = sei.id AND sem.level=cs.current_level "
                                               "JOIN skill_damage_formulas se ON se.id = sem.effect_id "
                                               "JOIN skill_damage_types seft ON seft.id = se.effect_type_id "
                                               "LEFT JOIN skill_properties_mapping spm ON spm.skill_id = s.id AND spm.skill_level=cs.current_level "
                                               "LEFT JOIN skill_properties sp ON sp.id = spm.property_id "
                                               "GROUP BY s.id, s.name, s.slug, s.animation_name, sst.slug, ss.slug, seft.slug, spm.skill_level;");

        // get quest slugs for a given NPC (both giver and turn-in roles)
        connection_->prepare("get_npc_quests",
            "SELECT slug FROM quest "
            "WHERE giver_npc_id = $1 OR turnin_npc_id = $1 "
            "ORDER BY id;");

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
            "COALESCE(client_quest_key, '') AS client_quest_key, "
            "COALESCE(reputation_faction_slug, '') AS reputation_faction_slug, "
            "COALESCE(reputation_on_complete, 0) AS reputation_on_complete, "
            "COALESCE(reputation_on_fail, 0) AS reputation_on_fail "
            "FROM quest ORDER BY id;");

        connection_->prepare("get_quest_steps",
            "SELECT id, quest_id, step_index, step_type::text AS step_type, "
            "params::text AS params, "
            "COALESCE(client_step_key, '') AS client_step_key, "
            "COALESCE(completion_mode, 'auto') AS completion_mode "
            "FROM quest_step ORDER BY quest_id, step_index;");

        connection_->prepare("get_quest_rewards",
            "SELECT id, quest_id, reward_type, "
            "COALESCE(item_id, 0) AS item_id, quantity, amount "
            "FROM quest_reward ORDER BY quest_id;");

        // --- Player quest queries ---
        connection_->prepare("get_player_quests",
            "SELECT pq.quest_id, q.slug, pq.state::text AS state, pq.current_step, "
            "COALESCE(pq.progress::text, '{}') AS progress "
            "FROM player_quest pq "
            "JOIN quest q ON q.id = pq.quest_id "
            "WHERE pq.player_id = $1 "
            "AND pq.state NOT IN ('turned_in', 'failed');");

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
        // Purge expired rows first (keeps the table lean; safe to run each load).
        connection_->prepare("cleanup_expired_active_effects",
            "DELETE FROM player_active_effect "
            "WHERE expires_at IS NOT NULL AND expires_at < NOW();");

        connection_->prepare("get_player_active_effects",
            "SELECT pae.id, pae.status_effect_id AS effect_id, se.slug AS effect_slug, "
            "se.category AS effect_type_slug, "
            "COALESCE(pae.attribute_id, 0) AS attribute_id, "
            "COALESCE(ea.slug, '') AS attribute_slug, "
            "pae.value::float AS value, pae.source_type, "
            "pae.tick_ms, "
            "COALESCE(EXTRACT(EPOCH FROM pae.expires_at)::bigint, 0) AS expires_at_unix "
            "FROM player_active_effect pae "
            "JOIN status_effects se ON se.id = pae.status_effect_id "
            "LEFT JOIN entity_attributes ea ON ea.id = pae.attribute_id "
            "WHERE pae.player_id = $1 "
            "  AND (pae.expires_at IS NULL OR pae.expires_at > NOW()) "
            "ORDER BY pae.id;");

        // insert a named status effect instance on a player
        // $1=player_id $2=effect_slug $3=attribute_slug ('' = no attribute) $4=source_type $5=value $6=expires_at(unix sec) $7=tick_ms
        connection_->prepare("insert_player_active_effect",
            "INSERT INTO player_active_effect "
            "  (player_id, status_effect_id, attribute_id, source_type, value, expires_at, tick_ms) "
            "VALUES ($1, "
            "  (SELECT id FROM status_effects WHERE slug = $2), "
            "  (SELECT id FROM entity_attributes WHERE slug = $3), "
            "  $4, $5::numeric, "
            "  to_timestamp($6), "
            "  $7) "
            "RETURNING id;");

        // --- Skill cooldown persistence (migration 067) ---
        // Upsert a cooldown row when a player uses a skill.
        // $1=character_id, $2=skill_slug, $3=cooldown_ends_at (unix ms)
        connection_->prepare("upsert_skill_cooldown",
            "INSERT INTO player_skill_cooldown (character_id, skill_slug, cooldown_ends_at) "
            "VALUES ($1, $2, to_timestamp($3::bigint / 1000.0)) "
            "ON CONFLICT (character_id, skill_slug) DO UPDATE "
            "  SET cooldown_ends_at = EXCLUDED.cooldown_ends_at;");

        // Load still-active cooldowns for a character on login; also prunes expired rows.
        // $1=character_id
        connection_->prepare("get_active_skill_cooldowns",
            "WITH cleanup AS ( "
            "  DELETE FROM player_skill_cooldown "
            "  WHERE character_id = $1 AND cooldown_ends_at <= NOW() "
            ") "
            "SELECT skill_slug, "
            "  GREATEST(0, EXTRACT(EPOCH FROM (cooldown_ends_at - NOW()))::bigint * 1000)::bigint AS remaining_ms "
            "FROM player_skill_cooldown "
            "WHERE character_id = $1 AND cooldown_ends_at > NOW();");
        ;

        connection_->prepare("upsert_player_flag",
            "INSERT INTO player_flag (player_id, flag_key, int_value, bool_value, updated_at) "
            "VALUES ($1, $2, $3, $4, now()) "
            "ON CONFLICT (player_id, flag_key) DO UPDATE SET "
            "int_value = EXCLUDED.int_value, bool_value = EXCLUDED.bool_value, "
            "updated_at = now();");

        connection_->prepare("upsert_player_inventory_item",
            "INSERT INTO player_inventory (character_id, item_id, quantity) "
            "VALUES ($1, $2, $3) "
            "ON CONFLICT (character_id, item_id) "
            "DO UPDATE SET quantity = player_inventory.quantity + EXCLUDED.quantity "
            "RETURNING id;");

        // Update quantity of an existing inventory row by its primary key.
        // $1=quantity, $2=inventory_item_id, $3=character_id (safety check)
        connection_->prepare("update_player_inventory_quantity",
            "UPDATE player_inventory SET quantity = $1 "
            "WHERE id = $2 AND character_id = $3;");

        connection_->prepare("delete_player_inventory_item",
            "DELETE FROM player_inventory WHERE character_id = $1 AND item_id = $2;");

        // Delete a specific stackable inventory row by id (used when qty reaches 0 and id known).
        // $1=inventory_item_id, $2=character_id (safety check)
        connection_->prepare("delete_player_inventory_item_by_char_id",
            "DELETE FROM player_inventory WHERE id = $1 AND character_id = $2;");

        connection_->prepare("get_player_inventory",
            "SELECT pi.id, pi.item_id, pi.quantity, "
            "COALESCE(pi.slot_index, -1) AS slot_index, "
            "COALESCE(pi.durability_current, 0) AS durability_current, "
            "COALESCE(pi.kill_count, 0) AS kill_count, "
            "CASE WHEN ce.inventory_item_id IS NOT NULL THEN true ELSE false END AS is_equipped "
            "FROM player_inventory pi "
            "LEFT JOIN character_equipment ce ON ce.inventory_item_id = pi.id "
            "WHERE pi.character_id = $1 ORDER BY pi.id;");

        // --- Game config queries ---
        // Загружает все геймплейные константы. Ответ читает GameConfigService::loadConfig().
        connection_->prepare("get_game_config",
            "SELECT key, value, value_type FROM public.game_config ORDER BY key;");

        // --- Vendor queries ---
        connection_->prepare("get_vendor_npcs",
            "SELECT DISTINCT vn.npc_id "
            "FROM vendor_inventory vi "
            "JOIN vendor_npc vn ON vn.id = vi.vendor_npc_id;");

        connection_->prepare("get_vendor_inventory",
            "SELECT vi.item_id, "
            "COALESCE(vi.stock_count, -1) AS stock_current, "
            "COALESCE(vi.stock_max, -1) AS stock_max, "
            "COALESCE(vi.restock_amount, 0) AS restock_amount, "
            "COALESCE(vi.restock_interval_sec, 3600) AS restock_interval_sec, "
            "COALESCE(vi.price_override, 0) AS price_override_buy, "
            "COALESCE(vi.price_override, 0) AS price_override_sell "
            "FROM vendor_inventory vi "
            "JOIN vendor_npc vn ON vn.id = vi.vendor_npc_id "
            "WHERE vn.npc_id = $1;");

        // --- Trainer queries ---
        connection_->prepare("get_trainer_npcs",
            "SELECT npc_id, class_id FROM public.npc_trainer_class ORDER BY npc_id;");

        connection_->prepare("get_trainer_skills",
            "SELECT "
            "  s.id          AS skill_id, "
            "  s.slug        AS skill_slug, "
            "  s.name        AS skill_name, "
            "  s.is_passive  AS is_passive, "
            "  COALESCE(cst.required_level, 1) AS required_level, "
            "  COALESCE(cst.skill_point_cost, 1) AS sp_cost, "
            "  COALESCE(cst.gold_cost, 0) AS gold_cost, "
            "  COALESCE(cst.requires_book, false) AS requires_book, "
            "  COALESCE(cst.skill_book_item_id, 0) AS book_item_id, "
            "  COALESCE(prereq.slug, '') AS prerequisite_skill_slug "
            "FROM public.npc_trainer_class ntc "
            "JOIN public.class_skill_tree cst ON cst.class_id = ntc.class_id "
            "JOIN public.skills s ON s.id = cst.skill_id "
            "LEFT JOIN public.skills prereq ON prereq.id = cst.prerequisite_skill_id "
            "WHERE ntc.npc_id = $1 "
            "ORDER BY cst.required_level, s.id;");

        // Look up SP cost for a given skill slug (used when persisting skill purchase)
        connection_->prepare("get_skill_sp_cost",
            "SELECT COALESCE(cst.skill_point_cost, 1) AS skill_point_cost "
            "FROM public.class_skill_tree cst "
            "JOIN public.skills s ON s.id = cst.skill_id "
            "WHERE s.slug = $1 "
            "LIMIT 1;");

        // Update durability_current for a specific inventory item
        connection_->prepare("update_durability_current",
            "UPDATE player_inventory SET durability_current = $1 "
            "WHERE id = $2 AND character_id = $3;");

        // Update kill_count for Item Soul system
        connection_->prepare("update_item_kill_count",
            "UPDATE player_inventory SET kill_count = $1 "
            "WHERE id = $2 AND character_id = $3;");

        // Transfer item instance to another character (preserves all per-instance data).
        // When picking up a ground item, character_id IS NULL in DB — use IS NULL check.
        // $1=to_character_id, $2=inventory_item_id
        connection_->prepare("transfer_item_ownership",
            "UPDATE player_inventory SET character_id = $1, slot_index = NULL "
            "WHERE id = $2 AND character_id IS NULL;");

        // P2P trade: transfer item between two live characters.
        // $1=to_character_id, $2=inventory_item_id, $3=from_character_id
        connection_->prepare("transfer_item_between_chars",
            "UPDATE player_inventory SET character_id = $1, slot_index = NULL "
            "WHERE id = $2 AND character_id = $3;");

        // Nullify owner: item dropped to ground (character_id = NULL = on ground)
        // $1=inventory_item_id, $2=from_character_id
        connection_->prepare("nullify_item_owner",
            "UPDATE player_inventory SET character_id = NULL, slot_index = NULL "
            "WHERE id = $1 AND character_id = $2;");

        // Delete a specific inventory row by instance ID (ground item expired)
        // $1=inventory_item_id
        connection_->prepare("delete_inventory_item_by_id",
            "DELETE FROM player_inventory WHERE id = $1 AND character_id IS NULL;");

        // Insert a vendor/repair transaction log entry
        connection_->prepare("insert_currency_transaction",
            "INSERT INTO currency_transactions "
            "(character_id, source_id, amount, reason_type) "
            "VALUES ($1, $2, $3, $4);");

        // Equipment: equip / unequip
        // $1=character_id, $2=equip_slot_slug, $3=inventory_item_id
        connection_->prepare("insert_character_equipment",
            "INSERT INTO character_equipment (character_id, equip_slot_id, inventory_item_id) "
            "VALUES ($1, (SELECT id FROM equip_slot WHERE slug = $2), $3) "
            "ON CONFLICT ON CONSTRAINT uq_character_equip_slot "
            "DO UPDATE SET inventory_item_id = EXCLUDED.inventory_item_id;");
        connection_->prepare("delete_character_equipment",
            "DELETE FROM character_equipment "
            "WHERE character_id = $1 AND inventory_item_id = $2;");

        // Respawn zones (with area bounds for random point selection)
        connection_->prepare("get_respawn_zones",
            "SELECT id, name, x, y, z, zone_id, is_default, "
            "min_x, max_x, min_y, max_y, min_z, max_z, "
            "shape_type, center_x, center_y, inner_radius, outer_radius "
            "FROM respawn_zones ORDER BY id;");

        // Class spawn zones (starting zones for new characters by class)
        connection_->prepare("get_class_spawn_zones",
            "SELECT csz.id, csz.class_id, cc.name AS class_name, csz.zone_id, "
            "csz.min_x, csz.max_x, csz.min_y, csz.max_y, csz.min_z, csz.max_z, "
            "csz.shape_type, csz.center_x, csz.center_y, csz.inner_radius, csz.outer_radius "
            "FROM class_spawn_zones csz "
            "JOIN character_class cc ON cc.id = csz.class_id "
            "ORDER BY csz.id;");

        // Game zones with shape-aware world bounds (for zone detection / exploration rewards)
        connection_->prepare("get_game_zones",
            "SELECT id, slug, name, min_level, max_level, is_pvp, is_safe_zone, "
            "       min_x, max_x, min_y, max_y, "
            "       shape_type, center_x, center_y, inner_radius, outer_radius, "
            "       exploration_xp_reward, "
            "       COALESCE(champion_threshold_kills, 100) AS champion_threshold_kills "
            "FROM   zones "
            "ORDER BY id;");

        // Status effect templates (data-driven buff/debuff configuration)
        connection_->prepare("get_status_effect_templates",
            "SELECT se.slug                              AS effect_slug, "
            "       se.category::TEXT                   AS category, "
            "       COALESCE(se.duration_sec, 0)        AS duration_sec, "
            "       sem.modifier_type::TEXT             AS modifier_type, "
            "       sem.value::FLOAT                    AS value, "
            "       COALESCE(ea.slug, '')               AS attribute_slug "
            "FROM   status_effects se "
            "LEFT JOIN status_effect_modifiers sem ON sem.status_effect_id = se.id "
            "LEFT JOIN entity_attributes        ea  ON ea.id = sem.attribute_id "
            "ORDER BY se.id, sem.id;");

        // Pity kill counters
        connection_->prepare("get_player_pity",
            "SELECT item_id, kill_count "
            "FROM character_pity "
            "WHERE character_id = $1;");

        connection_->prepare("upsert_pity_counter",
            "INSERT INTO character_pity(character_id, item_id, kill_count) "
            "VALUES($1, $2, $3) "
            "ON CONFLICT(character_id, item_id) DO UPDATE SET kill_count = $3;");

        // Bestiary kill counts
        connection_->prepare("get_player_bestiary",
            "SELECT mob_template_id, kill_count "
            "FROM character_bestiary "
            "WHERE character_id = $1;");

        connection_->prepare("upsert_bestiary_kill",
            "INSERT INTO character_bestiary(character_id, mob_template_id, kill_count) "
            "VALUES($1, $2, $3) "
            "ON CONFLICT(character_id, mob_template_id) DO UPDATE SET kill_count = $3;");

        // Timed champion templates (Stage 3)
        connection_->prepare("get_timed_champion_templates",
            "SELECT t.id, t.slug, z.id AS game_zone_id, t.mob_template_id, "
            "t.interval_hours, t.window_minutes, "
            "COALESCE(t.next_spawn_at, 0) AS next_spawn_at, "
            "COALESCE(t.announcement_key, '') AS announce_key "
            "FROM timed_champion_templates t "
            "JOIN zones z ON z.id = t.zone_id;");

        connection_->prepare("update_timed_champion_next_spawn",
            "UPDATE timed_champion_templates "
            "SET next_spawn_at = to_timestamp($2), last_killed_at = NOW() "
            "WHERE slug = $1;");

        // Stage 4: Reputation
        connection_->prepare("get_player_reputations",
            "SELECT faction_slug, value "
            "FROM character_reputation "
            "WHERE character_id = $1;");

        connection_->prepare("upsert_reputation",
            "INSERT INTO character_reputation(character_id, faction_slug, value) "
            "VALUES($1, $2, $3) "
            "ON CONFLICT(character_id, faction_slug) DO UPDATE SET value = EXCLUDED.value;");

        // Stage 4: Mastery
        connection_->prepare("get_player_masteries",
            "SELECT mastery_slug, value "
            "FROM character_skill_mastery "
            "WHERE character_id = $1;");

        connection_->prepare("upsert_mastery",
            "INSERT INTO character_skill_mastery(character_id, mastery_slug, value) "
            "VALUES($1, $2, $3) "
            "ON CONFLICT(character_id, mastery_slug) DO UPDATE SET value = EXCLUDED.value;");

        // Title system
        connection_->prepare("get_title_definitions",
            "SELECT id, slug, display_name, description, earn_condition, bonuses::text, condition_params::text "
            "FROM title_definitions "
            "ORDER BY id;");

        connection_->prepare("get_player_titles",
            "SELECT title_slug, equipped "
            "FROM character_titles "
            "WHERE character_id = $1;");

        connection_->prepare("upsert_player_title",
            "INSERT INTO character_titles(character_id, title_slug, equipped) "
            "VALUES($1, $2, $3) "
            "ON CONFLICT(character_id, title_slug) DO UPDATE SET equipped = EXCLUDED.equipped;");

        connection_->prepare("set_character_equipped_title",
            "UPDATE character_titles SET equipped = (title_slug = $2) "
            "WHERE character_id = $1;");

        // Emote system
        connection_->prepare("get_emote_definitions",
            "SELECT id, slug, display_name, animation_name, category, is_default, sort_order "
            "FROM emote_definitions "
            "ORDER BY sort_order, id;");

        connection_->prepare("get_player_emotes",
            "SELECT emote_slug FROM character_emotes WHERE character_id = $1;");

        connection_->prepare("grant_default_emotes",
            "INSERT INTO character_emotes(character_id, emote_slug) "
            "SELECT $1, slug FROM emote_definitions WHERE is_default = TRUE "
            "ON CONFLICT DO NOTHING;");

        // NPC Ambient Speech system
        connection_->prepare("get_npc_ambient_speech",
            "SELECT c.npc_id, c.min_interval_sec, c.max_interval_sec, "
            "       l.id AS line_id, l.line_key, l.trigger_type, l.trigger_radius, "
            "       l.priority, l.weight, l.cooldown_sec, l.condition_group "
            "FROM npc_ambient_speech_configs c "
            "JOIN npc_ambient_speech_lines l ON l.npc_id = c.npc_id "
            "ORDER BY c.npc_id, l.priority DESC, l.id;");

        // Stage 4: Zone event templates
        connection_->prepare("get_zone_event_templates",
            "SELECT id, slug, COALESCE(game_zone_id, 0) AS game_zone_id, trigger_type, "
            "COALESCE(duration_sec, 0) AS duration_sec, "
            "COALESCE(loot_multiplier, 1.0) AS loot_multiplier, "
            "COALESCE(spawn_rate_multiplier, 1.0) AS spawn_rate_multiplier, "
            "COALESCE(mob_speed_multiplier, 1.0) AS mob_speed_multiplier, "
            "COALESCE(announce_key, '') AS announce_key, "
            "COALESCE(interval_hours, 0) AS interval_hours, "
            "COALESCE(random_chance_per_hour, 0.0) AS random_chance_per_hour, "
            "COALESCE(has_invasion_wave, false) AS has_invasion_wave, "
            "invasion_mob_template_id, "
            "COALESCE(invasion_wave_count, 0) AS invasion_wave_count "
            "FROM zone_event_templates;");

        // World Interactive Objects (migration 043)
        connection_->prepare("get_world_objects",
            "SELECT wo.id, wo.slug, wo.name_key, wo.object_type, wo.scope, "
            "  wo.pos_x, wo.pos_y, wo.pos_z, wo.rot_z, "
            "  COALESCE(wo.zone_id, 0) AS zone_id, "
            "  COALESCE(wo.dialogue_id, 0) AS dialogue_id, "
            "  COALESCE(wo.loot_table_id, 0) AS loot_table_id, "
            "  COALESCE(wo.required_item_id, 0) AS required_item_id, "
            "  wo.interaction_radius, wo.channel_time_sec, wo.respawn_sec, "
            "  wo.is_active_by_default, wo.min_level, "
            "  COALESCE(wo.condition_group::text, 'null') AS condition_group, "
            "  COALESCE(s.state, 'active') AS current_state "
            "FROM world_objects wo "
            "LEFT JOIN world_object_states s ON s.object_id = wo.id;");
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
    const std::vector<std::variant<int, int64_t, float, double, std::string>> &parameters)
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
