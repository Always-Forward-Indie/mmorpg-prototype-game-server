#include "utils/QueryRegistry.hpp"

namespace {

// 108 prepared statements.
const QueryDef kQueries[] = {
    {"search_user",
     R"QDEF(SELECT u.* FROM users u JOIN user_sessions s ON s.user_id = u.id WHERE s.token_hash = $1 AND s.revoked_at IS NULL AND s.expires_at > now() LIMIT 1;)QDEF"},
    {"get_character",
     R"QDEF(SELECT characters.id as character_id, characters.level as character_lvl, characters.name as character_name, character_class.name as character_class, race.name as race_name, characters.experience_points as character_exp, COALESCE(ccs.current_health, 1) as character_current_health, COALESCE(ccs.current_mana, 1) as character_current_mana, character_class.id as class_id, COALESCE(characters.experience_debt, 0) as experience_debt, COALESCE(characters.free_skill_points, 0) as free_skill_points, COALESCE(cg.name, '') as gender_slug, COALESCE(characters.total_play_time_sec, 0) as total_play_time_sec, COALESCE(characters.last_session_play_time_sec, 0) as last_session_play_time_sec FROM characters JOIN character_class ON characters.class_id = character_class.id JOIN race on characters.race_id = race.id LEFT JOIN character_current_state ccs ON ccs.character_id = characters.id LEFT JOIN character_genders cg ON cg.id = characters.gender WHERE characters.owner_id = $1 AND characters.id = $2 LIMIT 1;)QDEF"},
            // get character attributes — base (class formula) + permanent modifiers + equipment bonuses
            // Formula: ROUND(base_value + multiplier * level^exponent)
            // Permanent modifiers (from quests/GM/events) are summed and added on top.
            // Equipment bonuses: all items in character_equipment → player_inventory → item_attributes_mapping
            //   where apply_on = 'equip'. Keyed by entity_attributes.id, same as base stats.
    {"get_character_attributes",
     R"QDEF(WITH char_info AS (   SELECT c.id, c.class_id, c.level   FROM characters c WHERE c.id = $1 ), base_attrs AS (   SELECT ea.id, ea.name, ea.slug,     ROUND(csf.base_value + csf.multiplier * POWER(ci.level, csf.exponent))::int AS value   FROM char_info ci   JOIN class_stat_formula csf ON csf.class_id = ci.class_id   JOIN entity_attributes ea ON ea.id = csf.attribute_id ), perm_mods AS (   SELECT attribute_id, SUM(value)::int AS bonus   FROM character_permanent_modifiers   WHERE character_id = $1   GROUP BY attribute_id ), dura_config AS (   SELECT     (SELECT value::float FROM game_config WHERE key = 'durability.tier1_threshold_pct') AS t1,     (SELECT value::float FROM game_config WHERE key = 'durability.tier1_penalty_pct')   AS p1,     (SELECT value::float FROM game_config WHERE key = 'durability.tier2_threshold_pct') AS t2,     (SELECT value::float FROM game_config WHERE key = 'durability.tier2_penalty_pct')   AS p2,     (SELECT value::float FROM game_config WHERE key = 'durability.tier3_threshold_pct') AS t3,     (SELECT value::float FROM game_config WHERE key = 'durability.tier3_penalty_pct')   AS p3 ), equip_bonus AS (   SELECT iam.attribute_id,     SUM(       CASE         WHEN i.is_durable = false THEN iam.value         WHEN i.durability_max = 0 THEN iam.value         WHEN COALESCE(pi.durability_current, i.durability_max) = 0 THEN 0         WHEN COALESCE(pi.durability_current, i.durability_max)::float / i.durability_max < dc.t3           THEN ROUND(iam.value::float * (1.0 - dc.p3))::int         WHEN COALESCE(pi.durability_current, i.durability_max)::float / i.durability_max < dc.t2           THEN ROUND(iam.value::float * (1.0 - dc.p2))::int         WHEN COALESCE(pi.durability_current, i.durability_max)::float / i.durability_max < dc.t1           THEN ROUND(iam.value::float * (1.0 - dc.p1))::int         ELSE iam.value       END     )::int AS bonus   FROM character_equipment ce   JOIN player_inventory pi ON pi.id = ce.inventory_item_id   JOIN items i ON i.id = pi.item_id   JOIN item_attributes_mapping iam     ON iam.item_id = pi.item_id AND iam.apply_on = 'equip'   CROSS JOIN dura_config dc   WHERE ce.character_id = $1   GROUP BY iam.attribute_id ), set_bonus AS (   SELECT isb.attribute_id, SUM(isb.bonus_value)::int AS bonus   FROM (     SELECT ism.set_id, COUNT(*)::int AS equipped_count     FROM character_equipment ce     JOIN player_inventory pi ON pi.id = ce.inventory_item_id     JOIN item_set_members ism ON ism.item_id = pi.item_id     WHERE ce.character_id = $1     GROUP BY ism.set_id   ) equipped_sets   JOIN item_set_bonuses isb     ON isb.set_id = equipped_sets.set_id     AND equipped_sets.equipped_count >= isb.pieces_required   GROUP BY isb.attribute_id ) SELECT ba.id, ba.name, ba.slug,   (ba.value + COALESCE(pm.bonus, 0) + COALESCE(eb.bonus, 0) + COALESCE(sb.bonus, 0))::int AS value FROM base_attrs ba LEFT JOIN perm_mods pm ON pm.attribute_id = ba.id LEFT JOIN equip_bonus eb ON eb.attribute_id = ba.id LEFT JOIN set_bonus sb ON sb.attribute_id = ba.id ORDER BY ba.id;)QDEF"},
            // get character skills
            // NOTE: damage-formula tables use LEFT JOINs so that passive skills (which have no
            // skill_effect_instances / skill_damage_formulas rows) are still returned.
            // COALESCE guards nullable columns that become NULL when those joins miss.
    {"get_character_skills",
     R"QDEF(WITH cs as ( SELECT skill_id, current_level FROM character_skills WHERE character_id = $1 )SELECT s.name as skill_name, s.slug as skill_slug, sst.slug as scale_stat, ss.slug  as school,COALESCE(seft.slug, '') as skill_effect_type, COALESCE(spm.skill_level, cs.current_level) as skill_level, coalesce(MAX(CASE WHEN se.slug IN ('coeff','heal_coeff') THEN sem.value END),0) AS coeff, coalesce(MAX(CASE WHEN se.slug IN ('flat_add','heal_flat') THEN sem.value END),0) AS flat_add, coalesce(MAX(CASE WHEN sp.slug='cooldown_ms' THEN spm.property_value END),0) AS cooldown_ms, coalesce(MAX(CASE WHEN sp.slug='gcd_ms'      THEN spm.property_value END),0) AS gcd_ms, coalesce(MAX(CASE WHEN sp.slug='cast_ms'     THEN spm.property_value END),0) AS cast_ms, coalesce(MAX(CASE WHEN sp.slug='cost_mp'     THEN spm.property_value END),0) AS cost_mp, coalesce(MAX(CASE WHEN sp.slug='max_range'   THEN spm.property_value END),0) AS max_range, coalesce(MAX(CASE WHEN sp.slug='area_radius' THEN spm.property_value END),0) AS area_radius, coalesce(MAX(CASE WHEN sp.slug='swing_ms'    THEN spm.property_value END),300)::integer AS swing_ms, COALESCE(s.animation_name, '') AS animation_name, COALESCE(s.is_passive, FALSE) AS is_passive, COALESCE(  (SELECT json_agg(      json_build_object(        'effectSlug',      sae.effect_slug,        'effectTypeSlug',  sae.effect_type_slug,        'attributeSlug',   sae.attribute_slug,        'value',           sae.value::float,        'durationSeconds', sae.duration_seconds,        'tickMs',          sae.tick_ms      ) ORDER BY sae.id    )    FROM skill_active_effects sae    WHERE sae.skill_id = s.id  ), '[]'::json) AS active_effects FROM cs JOIN skills s ON s.id=cs.skill_id JOIN skill_scale_type sst ON sst.id = s.scale_stat_id JOIN skill_school ss ON ss.id = s.school_id LEFT JOIN skill_effect_instances sei ON sei.skill_id = s.id LEFT JOIN skill_effects_mapping sem ON sem.effect_instance_id = sei.id AND sem.level=cs.current_level LEFT JOIN skill_damage_formulas se ON se.id = sem.effect_id LEFT JOIN skill_damage_types seft ON seft.id = se.effect_type_id LEFT JOIN skill_properties_mapping spm ON spm.skill_id = s.id AND spm.skill_level=cs.current_level LEFT JOIN skill_properties sp ON sp.id = spm.property_id GROUP BY s.id, s.name, s.slug, s.animation_name, s.is_passive, sst.slug, ss.slug, seft.slug, COALESCE(spm.skill_level, cs.current_level);)QDEF"},
            // Passive skill modifiers for a character's passive skills.
            // Returns one row per (passive_skill, attribute) pair.
            // id is negated to avoid collisions with player_active_effect PKs.
    {"get_player_passive_skill_effects",
     R"QDEF(SELECT (-psm.id)::bigint AS id, s.slug AS effect_slug, psm.attribute_slug, psm.value::float AS value, psm.modifier_type FROM character_skills cs JOIN skills s ON cs.skill_id = s.id AND s.is_passive = TRUE JOIN passive_skill_modifiers psm ON psm.skill_id = s.id WHERE cs.character_id = $1 ORDER BY psm.id;)QDEF"},
            // passive modifiers for a single skill by skill slug (used when returning setLearnedSkill response)
    {"get_passive_skill_modifiers_by_slug",
     R"QDEF(SELECT s.slug AS effect_slug, psm.attribute_slug, psm.value::float AS value, psm.modifier_type FROM skills s JOIN passive_skill_modifiers psm ON psm.skill_id = s.id WHERE s.slug = $1 ORDER BY psm.id;)QDEF"},
            // get character exp for level
    {"get_character_exp_for_next_level",
     R"QDEF(SELECT experience_points FROM exp_for_level WHERE level = $1 + 1;)QDEF"},
            // get experience for level data
    {"get_exp_level_table",
     R"QDEF(SELECT experience_points, level FROM exp_for_level;)QDEF"},
    {"set_basic_character_data",
     R"QDEF(UPDATE characters SET level = $2, experience_points = $3 WHERE id = $1;)QDEF"},
    {"upsert_character_current_state",
     R"QDEF(INSERT INTO character_current_state (character_id, current_health, current_mana) VALUES ($1, $2, $3) ON CONFLICT (character_id) DO UPDATE SET current_health = EXCLUDED.current_health, current_mana = EXCLUDED.current_mana, updated_at = now();)QDEF"},
    {"set_character_level",
     R"QDEF(UPDATE characters SET level = $2 WHERE id = $1;)QDEF"},
    {"set_character_health",
     R"QDEF(INSERT INTO character_current_state (character_id, current_health) VALUES ($1, $2) ON CONFLICT (character_id) DO UPDATE SET current_health = EXCLUDED.current_health, updated_at = now();)QDEF"},
    {"set_character_mana",
     R"QDEF(INSERT INTO character_current_state (character_id, current_mana) VALUES ($1, $2) ON CONFLICT (character_id) DO UPDATE SET current_mana = EXCLUDED.current_mana, updated_at = now();)QDEF"},
    {"set_character_exp",
     R"QDEF(UPDATE characters SET experience_points = $2 WHERE id = $1;)QDEF"},
    {"set_character_exp_level",
     R"QDEF(UPDATE characters SET experience_points = $2, level = $3, free_skill_points = free_skill_points + GREATEST(0, $3::integer - level) WHERE id = $1;)QDEF"},
    {"save_learned_skill",
     R"QDEF(INSERT INTO character_skills (character_id, skill_id, current_level) SELECT $1, s.id, $3 FROM skills s WHERE s.slug = $2 ON CONFLICT (character_id, skill_id) DO UPDATE SET current_level = GREATEST(character_skills.current_level, EXCLUDED.current_level);)QDEF"},
    {"decrement_skill_points",
     R"QDEF(UPDATE characters SET free_skill_points = GREATEST(0, free_skill_points - $2) WHERE id = $1;)QDEF"},
            // Absolute SP persist for chunk-decided facts (single owner: chunk
            // validates+deducts, game stores the value as-is; idempotent).
    {"set_free_skill_points",
     R"QDEF(UPDATE characters SET free_skill_points = GREATEST(0, $2::integer) WHERE id = $1;)QDEF"},
            // Outbox idempotency guard (see Tools/Tests/OUTBOX_PLAN.md): claim a
            // fact key atomically inside the caller's txn; duplicates skip the
            // business writes and get an ack-duplicate instead. COUNT(*) wrapper
            // (not bare RETURNING key): callers read an int (1 = fresh, 0 =
            // duplicate); an empty result unambiguously means transport error.
            // (A bare RETURNING key breaks .as<int>() readers with
            // "Unexpected text after integer" and rolls everything back.)
    {"claim_fact_key",
     R"QDEF(WITH ins AS (INSERT INTO fact_keys_applied(key) VALUES($1) ON CONFLICT DO NOTHING RETURNING key) SELECT COUNT(*) FROM ins;)QDEF"},
            // Outbox high-water (migration 088): max applied fact seq per
            // character. Written atomically with each applied fact inside the
            // caller's txn (GREATEST-wins, never backward); read once per NEW
            // chunk link for seq fast-forward. bootId stays in keys, so a
            // stale/low watermark is harmless (no cross-boot collisions).
    {"upsert_outbox_watermark",
     R"QDEF(INSERT INTO outbox_seq_watermark(chunk_id, character_id, last_seq, updated_at) VALUES($1, $2, $3, NOW()) ON CONFLICT (chunk_id, character_id) DO UPDATE SET last_seq = GREATEST(outbox_seq_watermark.last_seq, EXCLUDED.last_seq), updated_at = NOW();)QDEF"},
    {"get_outbox_global_watermark",
     R"QDEF(SELECT COALESCE(MAX(last_seq), 0) FROM outbox_seq_watermark WHERE chunk_id = $1;)QDEF"},
    {"get_outbox_diff_keys",
     R"QDEF(SELECT key FROM fact_keys_applied WHERE key = ANY($1::text[]);)QDEF"},
    {"delete_old_fact_keys",
     R"QDEF(DELETE FROM fact_keys_applied WHERE ctid IN (SELECT ctid FROM fact_keys_applied WHERE applied_at < NOW() - ($1 || ' days')::interval ORDER BY applied_at LIMIT $2);)QDEF"},
            // ── Skill Bar (migration 051) ─────────────────────────────────────────
    {"get_character_skill_bar",
     R"QDEF(SELECT slot_index, skill_slug FROM character_skill_bar WHERE character_id = $1 ORDER BY slot_index;)QDEF"},
    {"save_skill_bar_slot",
     R"QDEF(INSERT INTO character_skill_bar (character_id, slot_index, skill_slug) VALUES ($1, $2, $3) ON CONFLICT (character_id, slot_index) DO UPDATE SET skill_slug = EXCLUDED.skill_slug;)QDEF"},
    {"clear_skill_bar_slot",
     R"QDEF(DELETE FROM character_skill_bar WHERE character_id = $1 AND slot_index = $2;)QDEF"},
    {"set_character_experience_debt",
     R"QDEF(UPDATE characters SET experience_debt = $2 WHERE id = $1;)QDEF"},
    {"get_character_position",
     R"QDEF(SELECT x, y, z, rot_z FROM character_position WHERE character_id = $1 LIMIT 1;)QDEF"},
    {"set_character_position",
     R"QDEF(UPDATE character_position SET x = $1, y = $2, z = $3, rot_z = $4 WHERE character_id = $5;)QDEF"},
    {"reset_all_online",
     R"QDEF(UPDATE characters SET is_online = false WHERE is_online = true;)QDEF"},
    {"set_character_online",
     R"QDEF(UPDATE characters SET is_online = true WHERE id = $1;)QDEF"},
    {"save_character_playtime",
     R"QDEF(UPDATE characters SET total_play_time_sec = total_play_time_sec + $2 WHERE id = $1;)QDEF"},
    {"save_character_playtime_disconnect",
     R"QDEF(UPDATE characters SET total_play_time_sec = total_play_time_sec + $2, last_session_play_time_sec = $3, last_online_at = NOW(), is_online = false WHERE id = $1;)QDEF"},
            // get mob spawn zone data — join spawn_zone_mobs (many-to-many) for mob_id, spawn_count, respawn_time
            // One row per (zone, mob) pair; szm_id is the unique key for the map.
            // Old schema had mob_id directly in spawn_zones; new schema uses spawn_zone_mobs link table.
    {"get_mob_spawn_zone_data",
     R"QDEF(SELECT szm.id AS szm_id, sz.zone_id, sz.zone_name, sz.game_zone_id, sz.min_spawn_x, sz.max_spawn_x, sz.min_spawn_y, sz.max_spawn_y, sz.min_spawn_z, sz.max_spawn_z, sz.shape_type, sz.center_x, sz.center_y, sz.inner_radius, sz.outer_radius, sz.exclusion_game_zone_id, szm.mob_id, szm.spawn_count, szm.respawn_time, m.name AS mob_name, m.level, mr.name AS race FROM spawn_zones sz JOIN spawn_zone_mobs szm ON szm.spawn_zone_id = sz.zone_id JOIN mob m ON m.id = szm.mob_id JOIN mob_race mr ON mr.id = m.race_id;)QDEF"},
            // get mobs list — include base_xp, rank_id, mob_ranks multiplier, AI config, flee/archetype, Stage 3/4 fields,
            //                  and bestiary metadata (migration 040)
    {"get_mobs",
     R"QDEF(SELECT m.id, m.name, m.slug, m.level, m.spawn_health, m.spawn_mana, m.is_aggressive, m.is_dead, m.radius, m.base_xp, m.rank_id, mr.name AS race, mrk.code AS rank_code, mrk.mult AS rank_mult, m.aggro_range, m.attack_range, m.attack_cooldown, m.chase_multiplier, m.patrol_speed, m.patrol_radius, m.is_social, m.chase_duration, m.flee_hp_threshold, m.ai_archetype, COALESCE(m.can_evolve, FALSE) AS can_evolve, COALESCE(m.is_rare, FALSE) AS is_rare, COALESCE(m.rare_spawn_chance, 0.0) AS rare_spawn_chance, COALESCE(m.rare_spawn_condition, '') AS rare_spawn_condition, COALESCE(m.faction_slug, '') AS faction_slug, COALESCE(m.rep_delta_per_kill, 0) AS rep_delta_per_kill, COALESCE(m.biome_slug, '') AS biome_slug, COALESCE(m.mob_type_slug, 'beast') AS mob_type_slug, COALESCE(  CASE WHEN ms_hp.multiplier IS NOT NULL     THEN GREATEST(0, ROUND(ms_hp.flat_value + ms_hp.multiplier * POWER(m.level::numeric, ms_hp.exponent)))::int     ELSE ms_hp.flat_value::int   END, m.spawn_health) AS hp_min, COALESCE(  CASE WHEN ms_hp.multiplier IS NOT NULL     THEN GREATEST(0, ROUND(ms_hp.flat_value + ms_hp.multiplier * POWER(m.level::numeric, ms_hp.exponent)))::int     ELSE ms_hp.flat_value::int   END, m.spawn_health) AS hp_max FROM mob m JOIN mob_race mr ON mr.id = m.race_id JOIN mob_ranks mrk ON mrk.rank_id = m.rank_id LEFT JOIN mob_stat ms_hp ON ms_hp.mob_id = m.id AND ms_hp.attribute_id = 1;)QDEF"},
            // get mob attributes — unified mob_stat table (migration 015)
            // multiplier IS NULL → use flat_value directly
            // multiplier IS NOT NULL → ROUND(flat_value + multiplier * level^exponent)
    {"get_mob_attributes",
     R"QDEF(SELECT ea.id, ea.name, ea.slug,   CASE     WHEN ms.multiplier IS NOT NULL       THEN GREATEST(0, ROUND(ms.flat_value + ms.multiplier * POWER(m.level::numeric, ms.exponent)))::int     ELSE ms.flat_value::int   END AS value FROM mob_stat ms JOIN entity_attributes ea ON ea.id = ms.attribute_id JOIN mob m                ON m.id  = ms.mob_id WHERE ms.mob_id = $1 ORDER BY ea.id;)QDEF"},
            // get mob skills
    {"get_mob_skills",
     R"QDEF(WITH cs as ( SELECT skill_id, current_level FROM mob_skills WHERE mob_id = $1 )SELECT s.name as skill_name, s.slug as skill_slug, sst.slug as scale_stat, ss.slug  as school,seft.slug as skill_effect_type, spm.skill_level, coalesce(MAX(CASE WHEN se.slug IN ('coeff','heal_coeff') THEN sem.value END),0) AS coeff, coalesce(MAX(CASE WHEN se.slug IN ('flat_add','heal_flat') THEN sem.value END),0) AS flat_add, coalesce(MAX(CASE WHEN sp.slug='cooldown_ms' THEN spm.property_value END),0) AS cooldown_ms, coalesce(MAX(CASE WHEN sp.slug='gcd_ms'      THEN spm.property_value END),0) AS gcd_ms, coalesce(MAX(CASE WHEN sp.slug='cast_ms'     THEN spm.property_value END),0) AS cast_ms, coalesce(MAX(CASE WHEN sp.slug='cost_mp'     THEN spm.property_value END),0) AS cost_mp, coalesce(MAX(CASE WHEN sp.slug='max_range'   THEN spm.property_value END),0) AS max_range, coalesce(MAX(CASE WHEN sp.slug='area_radius' THEN spm.property_value END),0) AS area_radius, coalesce(MAX(CASE WHEN sp.slug='swing_ms'    THEN spm.property_value END),300)::integer AS swing_ms, COALESCE(s.animation_name, '') AS animation_name FROM cs join skills s ON s.id=cs.skill_id JOIN skill_scale_type sst ON sst.id = s.scale_stat_id JOIN skill_school ss ON ss.id = s.school_id JOIN skill_effect_instances sei ON sei.skill_id = s.id JOIN skill_effects_mapping sem ON sem.effect_instance_id = sei.id AND sem.level=cs.current_level JOIN skill_damage_formulas se ON se.id = sem.effect_id JOIN skill_damage_types seft ON seft.id = se.effect_type_id LEFT JOIN skill_properties_mapping spm ON spm.skill_id = s.id AND spm.skill_level=cs.current_level LEFT JOIN skill_properties sp ON sp.id = spm.property_id GROUP BY s.id, s.name, s.slug, s.animation_name, sst.slug, ss.slug, seft.slug, spm.skill_level;)QDEF"},
            // get items list
    {"get_items",
     R"QDEF(SELECT items.*, item_types.name as item_type_name, item_types.slug as item_type_slug, ir.name as rarity_name, ir.slug as rarity_slug,  COALESCE(es.name, '') as equip_slot_name, COALESCE(es.slug, '') as equip_slot_slug, COALESCE(items.mastery_slug, '') as mastery_slug FROM items join items_rarity ir on ir.id = items.rarity_id left join equip_slot es on es.id = items.equip_slot JOIN item_types ON items.item_type = item_types.id ;)QDEF"},
            // get items attributes — joined via entity_attributes (unified stat dictionary)
    {"get_item_attributes",
     R"QDEF(SELECT ea.id, ea.name, ea.slug, iam.value, iam.apply_on FROM item_attributes_mapping iam JOIN entity_attributes ea ON ea.id = iam.attribute_id WHERE iam.item_id = $1;)QDEF"},
            // get per-class item restrictions (all rows; loaded once at startup)
    {"get_item_class_restrictions",
     R"QDEF(SELECT item_id, class_id FROM item_class_restrictions ORDER BY item_id;)QDEF"},
            // get item-set memberships (all rows; loaded once at startup)
    {"get_item_set_memberships",
     R"QDEF(SELECT ism.item_id, s.id AS set_id, s.slug AS set_slug FROM item_set_members ism JOIN item_sets s ON s.id = ism.set_id ORDER BY ism.item_id;)QDEF"},
            // get use-effects for a single item (migration 034)
    {"get_item_use_effects",
     R"QDEF(SELECT effect_slug, attribute_slug, value, is_instant, duration_seconds, tick_ms, cooldown_seconds FROM item_use_effects WHERE item_id = $1 ORDER BY id;)QDEF"},
            // get mobs loot info — include is_harvest_only, quantity range, and loot_tier (migration 040)
    {"get_mobs_loot",
     R"QDEF(SELECT id, mob_id, item_id, drop_chance, COALESCE(is_harvest_only, false) AS is_harvest_only, COALESCE(min_quantity, 1) AS min_quantity, COALESCE(max_quantity, 1) AS max_quantity, COALESCE(loot_tier, 'common') AS loot_tier FROM mob_loot_info;)QDEF"},
            // Bestiary: mob weaknesses and resistances — loaded once at startup (migration 040)
    {"get_mob_weaknesses_all",
     R"QDEF(SELECT mob_id, element_slug FROM mob_weaknesses ORDER BY mob_id;)QDEF"},
    {"get_mob_resistances_all",
     R"QDEF(SELECT mob_id, element_slug FROM mob_resistances ORDER BY mob_id;)QDEF"},
            // get npc position from npc_placements (single source of truth)
    {"get_npc_position",
     R"QDEF(SELECT COALESCE(np.x, 0) AS x, COALESCE(np.y, 0) AS y, COALESCE(np.z, 0) AS z, COALESCE(np.rot_z, 0) AS rot_z, np.zone_id AS zone_id FROM npc n LEFT JOIN npc_placements np ON np.npc_id = n.id WHERE n.id = $1 LIMIT 1;)QDEF"},
            // get npc list
    {"get_npcs",
     R"QDEF(SELECT npc.id, npc.name, npc.slug, npc.level, npc.current_health, npc.current_mana, npc.is_dead, npc.radius, npc.is_interactable, race.slug as race, nt.slug as npc_type, COALESCE(npc.faction_slug, '') as faction_slug FROM npc JOIN race ON npc.race_id = race.id JOIN npc_type nt ON npc.npc_type = nt.id ;)QDEF"},
            // get npc attributes
    {"get_npc_attributes",
     R"QDEF(SELECT entity_attributes.*, npc_attributes.value FROM npc_attributes JOIN entity_attributes ON npc_attributes.attribute_id = entity_attributes.id WHERE npc_attributes.npc_id = $1;)QDEF"},
            // get npc skills
    {"get_npc_skills",
     R"QDEF(WITH cs as ( SELECT skill_id, current_level FROM npc_skills WHERE npc_id = $1 )SELECT s.name as skill_name, s.slug as skill_slug, sst.slug as scale_stat, ss.slug  as school,seft.slug as skill_effect_type, spm.skill_level, coalesce(MAX(CASE WHEN se.slug IN ('coeff','heal_coeff') THEN sem.value END),0) AS coeff, coalesce(MAX(CASE WHEN se.slug IN ('flat_add','heal_flat') THEN sem.value END),0) AS flat_add, coalesce(MAX(CASE WHEN sp.slug='cooldown_ms' THEN spm.property_value END),0) AS cooldown_ms, coalesce(MAX(CASE WHEN sp.slug='gcd_ms'      THEN spm.property_value END),0) AS gcd_ms, coalesce(MAX(CASE WHEN sp.slug='cast_ms'     THEN spm.property_value END),0) AS cast_ms, coalesce(MAX(CASE WHEN sp.slug='cost_mp'     THEN spm.property_value END),0) AS cost_mp, coalesce(MAX(CASE WHEN sp.slug='max_range'   THEN spm.property_value END),0) AS max_range, coalesce(MAX(CASE WHEN sp.slug='area_radius' THEN spm.property_value END),0) AS area_radius, coalesce(MAX(CASE WHEN sp.slug='swing_ms'    THEN spm.property_value END),300)::integer AS swing_ms, COALESCE(s.animation_name, '') AS animation_name FROM cs join skills s ON s.id=cs.skill_id JOIN skill_scale_type sst ON sst.id = s.scale_stat_id JOIN skill_school ss ON ss.id = s.school_id JOIN skill_effect_instances sei ON sei.skill_id = s.id JOIN skill_effects_mapping sem ON sem.effect_instance_id = sei.id AND sem.level=cs.current_level JOIN skill_damage_formulas se ON se.id = sem.effect_id JOIN skill_damage_types seft ON seft.id = se.effect_type_id LEFT JOIN skill_properties_mapping spm ON spm.skill_id = s.id AND spm.skill_level=cs.current_level LEFT JOIN skill_properties sp ON sp.id = spm.property_id GROUP BY s.id, s.name, s.slug, s.animation_name, sst.slug, ss.slug, seft.slug, spm.skill_level;)QDEF"},
            // get quest slugs for a given NPC (both giver and turn-in roles)
    {"get_npc_quests",
     R"QDEF(SELECT slug FROM quest WHERE giver_npc_id = $1 OR turnin_npc_id = $1 ORDER BY id;)QDEF"},
            // --- Dialogue queries ---
    {"get_dialogues",
     R"QDEF(SELECT id, slug, version, start_node_id FROM dialogue ORDER BY id;)QDEF"},
    {"get_dialogue_nodes",
     R"QDEF(SELECT id, dialogue_id, type::text AS type, speaker_npc_id, client_node_key, COALESCE(condition_group::text, '') AS condition_group, COALESCE(action_group::text, '') AS action_group, COALESCE(jump_target_node_id, 0) AS jump_target_node_id FROM dialogue_node ORDER BY dialogue_id, id;)QDEF"},
    {"get_dialogue_edges",
     R"QDEF(SELECT id, from_node_id, to_node_id, order_index, COALESCE(client_choice_key, '') AS client_choice_key, COALESCE(condition_group::text, '') AS condition_group, COALESCE(action_group::text, '') AS action_group, hide_if_locked FROM dialogue_edge ORDER BY from_node_id, order_index;)QDEF"},
    {"get_npc_dialogue_mappings",
     R"QDEF(SELECT npc_id, dialogue_id, priority, COALESCE(condition_group::text, '') AS condition_group FROM npc_dialogue ORDER BY npc_id, priority DESC;)QDEF"},
            // --- Quest queries ---
    {"get_quests",
     R"QDEF(SELECT id, slug, min_level, repeatable, cooldown_sec, COALESCE(giver_npc_id, 0) AS giver_npc_id, COALESCE(turnin_npc_id, 0) AS turnin_npc_id, COALESCE(client_quest_key, '') AS client_quest_key, COALESCE(reputation_faction_slug, '') AS reputation_faction_slug, COALESCE(reputation_on_complete, 0) AS reputation_on_complete, COALESCE(reputation_on_fail, 0) AS reputation_on_fail FROM quest ORDER BY id;)QDEF"},
    {"get_quest_steps",
     R"QDEF(SELECT id, quest_id, step_index, step_type::text AS step_type, params::text AS params, COALESCE(client_step_key, '') AS client_step_key, COALESCE(completion_mode, 'auto') AS completion_mode FROM quest_step ORDER BY quest_id, step_index;)QDEF"},
    {"get_quest_rewards",
     R"QDEF(SELECT id, quest_id, reward_type, COALESCE(item_id, 0) AS item_id, quantity, amount FROM quest_reward ORDER BY quest_id;)QDEF"},
            // --- Player quest queries ---
    {"get_player_quests",
     R"QDEF(SELECT pq.quest_id, q.slug, pq.state::text AS state, pq.current_step, COALESCE(pq.progress::text, '{}') AS progress FROM player_quest pq JOIN quest q ON q.id = pq.quest_id WHERE pq.player_id = $1 AND pq.state NOT IN ('turned_in', 'failed');)QDEF"},
    {"upsert_player_quest",
     R"QDEF(INSERT INTO player_quest (player_id, quest_id, state, current_step, progress, updated_at) VALUES ($1, $2, $3::quest_state, $4, $5::jsonb, now()) ON CONFLICT (player_id, quest_id) DO UPDATE SET state = EXCLUDED.state, current_step = EXCLUDED.current_step, progress = EXCLUDED.progress, updated_at = now();)QDEF"},
            // --- Player flag queries ---
    {"get_player_flags",
     R"QDEF(SELECT flag_key, COALESCE(int_value, 0) AS int_value, COALESCE(bool_value, false) AS bool_value FROM player_flag WHERE player_id = $1;)QDEF"},
            // --- Player active effects ---
            // Purge expired rows first (keeps the table lean; safe to run each load).
    {"cleanup_expired_active_effects",
     R"QDEF(DELETE FROM player_active_effect WHERE expires_at IS NOT NULL AND expires_at < NOW();)QDEF"},
    {"get_player_active_effects",
     R"QDEF(SELECT pae.id, pae.status_effect_id AS effect_id, se.slug AS effect_slug, se.category AS effect_type_slug, COALESCE(pae.attribute_id, 0) AS attribute_id, COALESCE(ea.slug, '') AS attribute_slug, pae.value::float AS value, pae.source_type, pae.tick_ms, COALESCE(EXTRACT(EPOCH FROM pae.expires_at)::bigint, 0) AS expires_at_unix FROM player_active_effect pae JOIN status_effects se ON se.id = pae.status_effect_id LEFT JOIN entity_attributes ea ON ea.id = pae.attribute_id WHERE pae.player_id = $1   AND (pae.expires_at IS NULL OR pae.expires_at > NOW()) ORDER BY pae.id;)QDEF"},
            // insert a named status effect instance on a player
            // $1=player_id $2=effect_slug $3=attribute_slug ('' = no attribute) $4=source_type $5=value $6=expires_at(unix sec) $7=tick_ms
    {"insert_player_active_effect",
     R"QDEF(INSERT INTO player_active_effect   (player_id, status_effect_id, attribute_id, source_type, value, expires_at, tick_ms) VALUES ($1,   (SELECT id FROM status_effects WHERE slug = $2),   (SELECT id FROM entity_attributes WHERE slug = $3),   $4, $5::numeric,   to_timestamp($6),   $7) RETURNING id;)QDEF"},
            // --- Skill cooldown persistence (migration 067) ---
            // Upsert a cooldown row when a player uses a skill.
            // $1=character_id, $2=skill_slug, $3=cooldown_ends_at (unix ms)
    {"upsert_skill_cooldown",
     R"QDEF(INSERT INTO player_skill_cooldown (character_id, skill_slug, cooldown_ends_at) VALUES ($1, $2, to_timestamp($3::bigint / 1000.0)) ON CONFLICT (character_id, skill_slug) DO UPDATE   SET cooldown_ends_at = EXCLUDED.cooldown_ends_at;)QDEF"},
            // Load still-active cooldowns for a character on login; also prunes expired rows.
            // $1=character_id
    {"get_active_skill_cooldowns",
     R"QDEF(WITH cleanup AS (   DELETE FROM player_skill_cooldown   WHERE character_id = $1 AND cooldown_ends_at <= NOW() ) SELECT skill_slug,   GREATEST(0, EXTRACT(EPOCH FROM (cooldown_ends_at - NOW()))::bigint * 1000)::bigint AS remaining_ms FROM player_skill_cooldown WHERE character_id = $1 AND cooldown_ends_at > NOW();)QDEF"},
    {"upsert_player_flag",
     R"QDEF(INSERT INTO player_flag (player_id, flag_key, int_value, bool_value, updated_at) VALUES ($1, $2, $3, $4, now()) ON CONFLICT (player_id, flag_key) DO UPDATE SET int_value = EXCLUDED.int_value, bool_value = EXCLUDED.bool_value, updated_at = now();)QDEF"},
            // Legacy token-less write (chunk pre-token versions only): SELECT
            // first, then UPDATE or plain INSERT. No ON CONFLICT — the
            // (character_id, item_id) unique index is gone by design (096):
            // instances are keyed by token now, and a lost race merely
            // duplicates a legacy row (chunk consolidates stackables).
            // $1=character_id, $2=item_id
    {"get_inventory_id_by_char_item",
     R"QDEF(SELECT id FROM player_inventory WHERE character_id = $1 AND item_id = $2 ORDER BY id LIMIT 1;)QDEF"},
            // $1=character_id, $2=item_id, $3=quantity
    {"insert_player_inventory_item",
     R"QDEF(INSERT INTO player_inventory (character_id, item_id, quantity) VALUES ($1, $2, $3) RETURNING id;)QDEF"},
            // Per-instance idempotency (migration 096): chunk stamps a random-start
            // syncToken per new row; resends converge on it instead of the
            // (character_id, item_id) pair (which collapses non-stackable dupes).
            // $1=character_id, $2=sync_token
    {"get_inventory_id_by_token",
     R"QDEF(SELECT id FROM player_inventory WHERE character_id = $1 AND sync_token = $2;)QDEF"},
            // Adopt a virgin legacy row (same template, never tokened) into
            // the token world: absolute qty + stamp. Keeps stackables on one
            // row; token-owned rows are invisible here, so real duplicates
            // always fall through to INSERT below.
            // $1=quantity, $2=sync_token, $3=character_id, $4=item_id
    {"adopt_legacy_inventory_row",
     R"QDEF(UPDATE player_inventory SET quantity = $1, sync_token = $2 WHERE id = (SELECT id FROM player_inventory WHERE character_id = $3 AND item_id = $4 AND sync_token IS NULL ORDER BY id LIMIT 1) RETURNING id;)QDEF"},
            // $1=character_id, $2=item_id, $3=quantity, $4=sync_token (nonzero).
            // ON CONFLICT DO NOTHING (never throws: execChecked aborts the
            // whole txn on any error, so the race must resolve to an empty
            // result + re-SELECT, not an exception).
    {"insert_player_inventory_with_token",
     R"QDEF(INSERT INTO player_inventory (character_id, item_id, quantity, sync_token) VALUES ($1, $2, $3, $4) ON CONFLICT (sync_token) DO NOTHING RETURNING id;)QDEF"},
            // Update quantity of an existing inventory row by its primary key.
            // $1=quantity, $2=inventory_item_id, $3=character_id (safety check)
    {"update_player_inventory_quantity",
     R"QDEF(UPDATE player_inventory SET quantity = $1 WHERE id = $2 AND character_id = $3;)QDEF"},
    {"delete_player_inventory_item",
     R"QDEF(DELETE FROM player_inventory WHERE character_id = $1 AND item_id = $2;)QDEF"},
            // Delete a specific stackable inventory row by id (used when qty reaches 0 and id known).
            // $1=inventory_item_id, $2=character_id (safety check)
    {"delete_player_inventory_item_by_char_id",
     R"QDEF(DELETE FROM player_inventory WHERE id = $1 AND character_id = $2;)QDEF"},
    {"get_player_inventory",
     R"QDEF(SELECT pi.id, pi.item_id, pi.quantity, COALESCE(pi.slot_index, -1) AS slot_index, COALESCE(pi.durability_current, 0) AS durability_current, COALESCE(pi.kill_count, 0) AS kill_count, CASE WHEN ce.inventory_item_id IS NOT NULL THEN true ELSE false END AS is_equipped FROM player_inventory pi LEFT JOIN character_equipment ce ON ce.inventory_item_id = pi.id WHERE pi.character_id = $1 ORDER BY pi.id;)QDEF"},
            // --- Game config queries ---
            // Загружает все геймплейные константы. Ответ читает GameConfigService::loadConfig().
    {"get_game_config",
     R"QDEF(SELECT key, value, value_type FROM public.game_config ORDER BY key;)QDEF"},
            // --- Vendor queries ---
    {"get_vendor_npcs",
     R"QDEF(SELECT DISTINCT vn.npc_id FROM vendor_inventory vi JOIN vendor_npc vn ON vn.id = vi.vendor_npc_id;)QDEF"},
    {"get_vendor_inventory",
     R"QDEF(SELECT vi.item_id, COALESCE(vi.stock_count, -1) AS stock_current, COALESCE(vi.stock_max, -1) AS stock_max, COALESCE(vi.restock_amount, 0) AS restock_amount, COALESCE(vi.restock_interval_sec, 3600) AS restock_interval_sec, COALESCE(vi.price_override, 0) AS price_override_buy, COALESCE(vi.price_override, 0) AS price_override_sell FROM vendor_inventory vi JOIN vendor_npc vn ON vn.id = vi.vendor_npc_id WHERE vn.npc_id = $1;)QDEF"},
            // --- Trainer queries ---
    {"get_trainer_npcs",
     R"QDEF(SELECT npc_id, class_id FROM public.npc_trainer_class ORDER BY npc_id;)QDEF"},
    {"get_trainer_skills",
     R"QDEF(SELECT   s.id          AS skill_id,   s.slug        AS skill_slug,   s.name        AS skill_name,   s.is_passive  AS is_passive,   COALESCE(cst.required_level, 1) AS required_level,   COALESCE(cst.skill_point_cost, 1) AS sp_cost,   COALESCE(cst.gold_cost, 0) AS gold_cost,   COALESCE(cst.requires_book, false) AS requires_book,   COALESCE(cst.skill_book_item_id, 0) AS book_item_id,   COALESCE(prereq.slug, '') AS prerequisite_skill_slug FROM public.npc_trainer_class ntc JOIN public.class_skill_tree cst ON cst.class_id = ntc.class_id JOIN public.skills s ON s.id = cst.skill_id LEFT JOIN public.skills prereq ON prereq.id = cst.prerequisite_skill_id WHERE ntc.npc_id = $1 ORDER BY cst.required_level, s.id;)QDEF"},
            // Look up SP cost for a given skill slug (used when persisting skill purchase)
    {"get_skill_sp_cost",
     R"QDEF(SELECT COALESCE(cst.skill_point_cost, 1) AS skill_point_cost FROM public.class_skill_tree cst JOIN public.skills s ON s.id = cst.skill_id WHERE s.slug = $1 LIMIT 1;)QDEF"},
            // Update durability_current for a specific inventory item
    {"update_durability_current",
     R"QDEF(UPDATE player_inventory SET durability_current = $1 WHERE id = $2 AND character_id = $3;)QDEF"},
            // Update kill_count for Item Soul system
    {"update_item_kill_count",
     R"QDEF(UPDATE player_inventory SET kill_count = $1 WHERE id = $2 AND character_id = $3;)QDEF"},
            // Transfer item instance to another character (preserves all per-instance data).
            // When picking up a ground item, character_id IS NULL in DB — use IS NULL check.
            // $1=to_character_id, $2=inventory_item_id
    {"transfer_item_ownership",
     R"QDEF(UPDATE player_inventory SET character_id = $1, slot_index = NULL WHERE id = $2 AND character_id IS NULL;)QDEF"},
            // P2P trade: transfer item between two live characters.
            // $1=to_character_id, $2=inventory_item_id, $3=from_character_id
    {"transfer_item_between_chars",
     R"QDEF(UPDATE player_inventory SET character_id = $1, slot_index = NULL WHERE id = $2 AND character_id = $3;)QDEF"},
            // Nullify owner: item dropped to ground (character_id = NULL = on ground)
            // $1=inventory_item_id, $2=from_character_id
    {"nullify_item_owner",
     R"QDEF(UPDATE player_inventory SET character_id = NULL, slot_index = NULL WHERE id = $1 AND character_id = $2;)QDEF"},
            // Delete a specific inventory row by instance ID (ground item expired)
            // $1=inventory_item_id
    {"delete_inventory_item_by_id",
     R"QDEF(DELETE FROM player_inventory WHERE id = $1 AND character_id IS NULL;)QDEF"},
            // Insert a vendor/repair transaction log entry
    {"insert_currency_transaction",
     R"QDEF(INSERT INTO currency_transactions (character_id, source_id, amount, reason_type) VALUES ($1, $2, $3, $4);)QDEF"},
            // Equipment: equip / unequip
            // $1=character_id, $2=equip_slot_slug, $3=inventory_item_id
    {"insert_character_equipment",
     R"QDEF(INSERT INTO character_equipment (character_id, equip_slot_id, inventory_item_id) VALUES ($1, (SELECT id FROM equip_slot WHERE slug = $2), $3) ON CONFLICT ON CONSTRAINT uq_character_equip_slot DO UPDATE SET inventory_item_id = EXCLUDED.inventory_item_id;)QDEF"},
    {"delete_character_equipment",
     R"QDEF(DELETE FROM character_equipment WHERE character_id = $1 AND inventory_item_id = $2;)QDEF"},
            // Respawn zones (with area bounds for random point selection)
    {"get_respawn_zones",
     R"QDEF(SELECT id, name, x, y, z, zone_id, is_default, min_x, max_x, min_y, max_y, min_z, max_z, shape_type, center_x, center_y, inner_radius, outer_radius FROM respawn_zones ORDER BY id;)QDEF"},
            // Class spawn zones (starting zones for new characters by class)
    {"get_class_spawn_zones",
     R"QDEF(SELECT csz.id, csz.class_id, cc.name AS class_name, csz.zone_id, csz.min_x, csz.max_x, csz.min_y, csz.max_y, csz.min_z, csz.max_z, csz.shape_type, csz.center_x, csz.center_y, csz.inner_radius, csz.outer_radius FROM class_spawn_zones csz JOIN character_class cc ON cc.id = csz.class_id ORDER BY csz.id;)QDEF"},
            // Game zones with shape-aware world bounds (for zone detection / exploration rewards)
    {"get_game_zones",
     R"QDEF(SELECT id, slug, name, min_level, max_level, is_pvp, is_safe_zone,        min_x, max_x, min_y, max_y,        shape_type, center_x, center_y, inner_radius, outer_radius,        exploration_xp_reward,        COALESCE(champion_threshold_kills, 100) AS champion_threshold_kills FROM   zones ORDER BY id;)QDEF"},
            // Status effect templates (data-driven buff/debuff configuration)
    {"get_status_effect_templates",
     R"QDEF(SELECT se.slug                              AS effect_slug,        se.category::TEXT                   AS category,        COALESCE(se.duration_sec, 0)        AS duration_sec,        sem.modifier_type::TEXT             AS modifier_type,        sem.value::FLOAT                    AS value,        COALESCE(ea.slug, '')               AS attribute_slug FROM   status_effects se LEFT JOIN status_effect_modifiers sem ON sem.status_effect_id = se.id LEFT JOIN entity_attributes        ea  ON ea.id = sem.attribute_id ORDER BY se.id, sem.id;)QDEF"},
            // Pity kill counters
    {"get_player_pity",
     R"QDEF(SELECT item_id, kill_count FROM character_pity WHERE character_id = $1;)QDEF"},
    {"upsert_pity_counter",
     R"QDEF(INSERT INTO character_pity(character_id, item_id, kill_count) VALUES($1, $2, $3) ON CONFLICT(character_id, item_id) DO UPDATE SET kill_count = $3;)QDEF"},
            // Bestiary kill counts
    {"get_player_bestiary",
     R"QDEF(SELECT mob_template_id, kill_count FROM character_bestiary WHERE character_id = $1;)QDEF"},
    {"upsert_bestiary_kill",
     R"QDEF(INSERT INTO character_bestiary(character_id, mob_template_id, kill_count) VALUES($1, $2, $3) ON CONFLICT(character_id, mob_template_id) DO UPDATE SET kill_count = $3;)QDEF"},
            // Timed champion templates (Stage 3)
    {"get_timed_champion_templates",
     R"QDEF(SELECT t.id, t.slug, z.id AS game_zone_id, t.mob_template_id, t.interval_hours, t.window_minutes, COALESCE(t.next_spawn_at, 0) AS next_spawn_at, COALESCE(t.announcement_key, '') AS announce_key FROM timed_champion_templates t JOIN zones z ON z.id = t.zone_id;)QDEF"},
    {"update_timed_champion_next_spawn",
     R"QDEF(UPDATE timed_champion_templates SET next_spawn_at = $2::bigint, last_killed_at = NOW() WHERE slug = $1;)QDEF"},
            // Stage 4: Reputation
    {"get_player_reputations",
     R"QDEF(SELECT faction_slug, value FROM character_reputation WHERE character_id = $1;)QDEF"},
    {"upsert_reputation",
     R"QDEF(INSERT INTO character_reputation(character_id, faction_slug, value) VALUES($1, $2, $3) ON CONFLICT(character_id, faction_slug) DO UPDATE SET value = EXCLUDED.value;)QDEF"},
            // Atomic delta apply (concurrent changes must add, not last-write).
            // Idempotent only per unique fact; same delta twice adds twice, so
            // chunk sends each change once (at-least-once retries need keys).
    {"add_reputation",
     R"QDEF(INSERT INTO character_reputation(character_id, faction_slug, value) VALUES($1, $2, $3) ON CONFLICT(character_id, faction_slug) DO UPDATE SET value = character_reputation.value + EXCLUDED.value;)QDEF"},
            // Stage 4: Mastery
    {"get_player_masteries",
     R"QDEF(SELECT mastery_slug, value FROM character_skill_mastery WHERE character_id = $1;)QDEF"},
    {"upsert_mastery",
     R"QDEF(INSERT INTO character_skill_mastery(character_id, mastery_slug, value) VALUES($1, $2, $3) ON CONFLICT(character_id, mastery_slug) DO UPDATE SET value = EXCLUDED.value;)QDEF"},
    {"get_mastery_definitions",
     R"QDEF(SELECT slug, name, weapon_type_slug, max_value, target_attribute_slug FROM mastery_definitions ORDER BY slug;)QDEF"},
            // Title system
    {"get_title_definitions",
     R"QDEF(SELECT id, slug, display_name, description, earn_condition, bonuses::text, condition_params::text FROM title_definitions ORDER BY id;)QDEF"},
    {"get_player_titles",
     R"QDEF(SELECT title_slug, equipped FROM character_titles WHERE character_id = $1;)QDEF"},
    {"upsert_player_title",
     R"QDEF(INSERT INTO character_titles(character_id, title_slug, equipped) VALUES($1, $2, $3) ON CONFLICT(character_id, title_slug) DO UPDATE SET equipped = EXCLUDED.equipped;)QDEF"},
    {"set_character_equipped_title",
     R"QDEF(UPDATE character_titles SET equipped = (title_slug = $2) WHERE character_id = $1;)QDEF"},
            // Emote system
    {"get_emote_definitions",
     R"QDEF(SELECT id, slug, display_name, animation_name, category, is_default, sort_order FROM emote_definitions ORDER BY sort_order, id;)QDEF"},
    {"get_player_emotes",
     R"QDEF(SELECT emote_slug FROM character_emotes WHERE character_id = $1;)QDEF"},
    {"grant_default_emotes",
     R"QDEF(INSERT INTO character_emotes(character_id, emote_slug) SELECT $1, slug FROM emote_definitions WHERE is_default = TRUE ON CONFLICT DO NOTHING;)QDEF"},
            // NPC Ambient Speech system
    {"get_npc_ambient_speech",
     R"QDEF(SELECT c.npc_id, c.min_interval_sec, c.max_interval_sec,        l.id AS line_id, l.line_key, l.trigger_type, l.trigger_radius,        l.priority, l.weight, l.cooldown_sec, l.condition_group FROM npc_ambient_speech_configs c JOIN npc_ambient_speech_lines l ON l.npc_id = c.npc_id ORDER BY c.npc_id, l.priority DESC, l.id;)QDEF"},
            // Stage 4: Zone event templates
    {"get_zone_event_templates",
     R"QDEF(SELECT id, slug, COALESCE(game_zone_id, 0) AS game_zone_id, trigger_type, COALESCE(duration_sec, 0) AS duration_sec, COALESCE(loot_multiplier, 1.0) AS loot_multiplier, COALESCE(spawn_rate_multiplier, 1.0) AS spawn_rate_multiplier, COALESCE(mob_speed_multiplier, 1.0) AS mob_speed_multiplier, COALESCE(announce_key, '') AS announce_key, COALESCE(interval_hours, 0) AS interval_hours, COALESCE(random_chance_per_hour, 0.0) AS random_chance_per_hour, COALESCE(has_invasion_wave, false) AS has_invasion_wave, invasion_mob_template_id, COALESCE(invasion_wave_count, 0) AS invasion_wave_count FROM zone_event_templates;)QDEF"},
            // World Interactive Objects (migration 043)
    {"get_world_objects",
     R"QDEF(SELECT wo.id, wo.slug, wo.name_key, wo.object_type, wo.scope,   wo.pos_x, wo.pos_y, wo.pos_z, wo.rot_z,   COALESCE(wo.zone_id, 0) AS zone_id,   COALESCE(wo.dialogue_id, 0) AS dialogue_id,   COALESCE(wo.loot_table_id, 0) AS loot_table_id,   COALESCE(wo.required_item_id, 0) AS required_item_id,   wo.interaction_radius, wo.channel_time_sec, wo.respawn_sec,   wo.is_active_by_default, wo.min_level,   COALESCE(wo.condition_group::text, 'null') AS condition_group,   COALESCE(s.state, 'active') AS current_state FROM world_objects wo LEFT JOIN world_object_states s ON s.object_id = wo.id;)QDEF"},
};

} // namespace

const QueryDef *queryRegistryBegin() { return kQueries; }
const QueryDef *queryRegistryEnd()
{
    return kQueries + sizeof(kQueries) / sizeof(kQueries[0]);
}
