v0.1.4
07.04.2026
================
New:
`GET_TITLE_DEFINITIONS_DATA` / `SET_TITLE_DEFINITIONS_DATA` event — при подключении chunk-сервера game-сервер выполняет запрос `get_title_definitions` к БД и отправляет chunk-серверу пакет `setTitleDefinitionsData` с полным каталогом титулов (slug, display_name, description, earn_condition, bonuses JSONB). Запускается вместе с остальными статическими данными (зоны, квесты, тренеры).
`GET_PLAYER_TITLES_DATA` / `SET_PLAYER_TITLES_DATA` event — по запросу chunk-сервера читает `character_titles` для персонажа (title_slug, equipped), возвращает `setPlayerTitlesData` пакет со списком заработанных титулов.
`SAVE_PLAYER_TITLE` event — upsert записи в `character_titles`: `INSERT ... ON CONFLICT (character_id, title_slug) DO UPDATE SET equipped = EXCLUDED.equipped`. Используется при `equipTitle` от клиента.
Database — `get_title_definitions` prepared statement: `SELECT id, slug, display_name, description, earn_condition, bonuses::text FROM title_definitions ORDER BY id`.
Database — `get_player_titles` prepared statement: `SELECT title_slug, equipped FROM character_titles WHERE character_id = $1`.
Database — `save_player_title` prepared statement: upsert по (character_id, title_slug) с обновлением `equipped`.
DB Migration 049 — таблицы `title_definitions` (id, slug, display_name, description, earn_condition, bonuses JSONB) и `character_titles` (character_id FK → characters, title_slug FK → title_definitions, equipped, earned_at); индекс `idx_char_titles_char`; seed из 4 тестовых титулов. Миграция применена; дамп обновлён.

---

v0.1.3
06.04.2026
================
New:
`GET_TRAINER_DATA` event — `EventDispatcher::handleGetTrainerData()` → `EventHandler::handleGetTrainerDataEvent()`: при подключении chunk-сервера выполняет два запроса к БД — `get_trainer_npcs` (список NPC-тренеров с привязкой к классу) и `get_trainer_skills` (полный `class_skill_tree` для каждого тренера: slug, name, required_level, sp_cost, gold_cost, requires_book, book_item_id, prerequisite_skill_slug). Формирует JSON-массив `trainers[]` и отправляет chunk-серверу пакет `setTrainerData`.
Database — `get_trainer_npcs` prepared statement: `SELECT n.id AS npc_id, n.slug AS npc_slug, nct.class_id FROM npcs n JOIN npc_trainer_class nct ON nct.npc_id = n.id`.
Database — `get_trainer_skills` prepared statement: запрашивает все записи `class_skill_tree` с JOIN на `skills` и `items` (книга навыка), возвращает поля `npc_id`, `skill_slug`, `skill_name`, `required_level`, `skill_point_cost`, `gold_cost`, `requires_book`, `book_item_id`, `prerequisite_skill_slug`.
`GET_TRAINER_DATA` dispatch — вызывается при подключении chunk-сервера сразу после `GET_VENDOR_DATA`, гарантируя что данные тренеров загружаются вместе с остальными статическими данными.

---

v0.1.2
================
New:
SAVE_LEARNED_SKILL event — `EventDispatcher::handleSaveLearnedSkill()` → `EventHandler::handleSaveLearnedSkillEvent()`: persists a newly learned skill via `save_learned_skill` prepared statement, then queries `get_character_skills` for the full skill row and returns a `setLearnedSkill` response packet to the chunk-server client socket. Includes all skill fields required to build `SkillStruct` on the chunk server.
Database — `save_learned_skill` prepared statement: `INSERT INTO character_skills (character_id, skill_id, current_level) SELECT $1, s.id, 1 FROM skills s WHERE s.slug = $2 ON CONFLICT DO NOTHING`.
Database — `get_npc_quests` prepared statement: returns all quest slugs where the NPC is giver or turn-in target — `SELECT slug FROM quest WHERE giver_npc_id=$1 OR turnin_npc_id=$1 ORDER BY id`.
Database — `get_character_position` prepared statement: `SELECT x, y, z, rot_z FROM character_position WHERE character_id=$1 LIMIT 1`.
Database — `set_character_position` prepared statement: `UPDATE character_position SET x=$1, y=$2, z=$3, rot_z=$4 WHERE character_id=$5` (replaces the previous variant that did not persist `rot_z`).
NPCManager — `loadNPCQuests(txn, npcId)`: loads quest slugs for giver/turn-in NPC roles and stores them in `NPCDataStruct::questSlugs`. Called during NPC data load at startup. `questSlugs` included in the NPC data packet sent to chunk server.
NPCDataStruct — new field `questSlugs` (`std::vector<std::string>`).

Improvements:
Database — `set_character_exp_level` prepared statement updated: now atomically awards skill points on level-up — `free_skill_points = free_skill_points + GREATEST(0, new_level - old_level)`. Characters gain exactly 1 SP per level gained, even when multiple levels are crossed in one grant.
CharacterManager — character data load now reads `free_skill_points` column from DB and stores it in `CharacterDataStruct::freeSkillPoints`. `freeSkillPoints` is included in the character data response body sent to the chunk server (`"freeSkillPoints"` field).
config.json — server host changed to `0.0.0.0` (listen on all interfaces, not only loopback).

---

v0.1.1
21.03.2026
================
Improvements:
EventHandler (SAVE_INVENTORY_CHANGE) — branched logic on `inventoryItemId`:
  - `inventoryItemId > 0` and `quantity > 0`: performs UPDATE on the existing row (instead of upsert), preventing duplicate row creation.
  - `quantity == 0` and `inventoryItemId > 0`: deletes by exact id (instead of character_id + item_id), preventing accidental removal of other stacks of the same item.
  - Upsert and id-sync are kept for the `inventoryItemId == 0` case (new item with no known DB id).
Database — 2 new prepared queries:
  - `update_player_inventory_quantity` — UPDATE player_inventory SET quantity = $1 WHERE id = $2 AND character_id = $3.
  - `delete_player_inventory_item_by_char_id` — DELETE FROM player_inventory WHERE id = $1 AND character_id = $2 (delete by exact id with character safety check).
Database (GET_TIMED_CHAMPION_TEMPLATES) — query extended with `next_spawn_at` field.
ItemManager — parses and loads `equip_slot_name`, `equip_slot_slug`, `mastery_slug` fields from item query results.

Bug Fixes:
Fixed duplicate rows being created in player_inventory when updating quantity for an item with a known DB id (upsert was inserting a new row instead of updating the existing one).
Fixed wrong item stack being deleted when removing by item_id for a character with multiple stacks of the same item.

---

v0.1.0
15.03.2026
================
New:
EventHandler — 30+ new event types handled: SAVE_INVENTORY_CHANGE, GET_PLAYER_INVENTORY, GET_VENDOR_DATA, SAVE_DURABILITY_CHANGE, SAVE_CURRENCY_TRANSACTION, SAVE_EQUIPMENT_CHANGE, GET_RESPAWN_ZONES, GET_STATUS_EFFECT_TEMPLATES, GET_GAME_ZONES, SAVE_EXPERIENCE_DEBT, SAVE_ACTIVE_EFFECT, SAVE_ITEM_KILL_COUNT, TRANSFER_INVENTORY_ITEM, NULLIFY_ITEM_OWNER, DELETE_INVENTORY_ITEM, GET_PLAYER_PITY, GET_PLAYER_BESTIARY, SAVE_PITY_COUNTER, SAVE_BESTIARY_KILL, GET_TIMED_CHAMPION_TEMPLATES, TIMED_CHAMPION_KILLED, GET_PLAYER_REPUTATIONS, SAVE_REPUTATION, GET_PLAYER_MASTERIES, SAVE_MASTERY, GET_ZONE_EVENT_TEMPLATES, INVENTORY_ITEM_ID_SYNC, SET_CHARACTER_ATTRIBUTES_REFRESH.
Database — 35+ new prepared queries: get_player_passive_skill_effects, set_character_experience_debt, get_item_class_restrictions, get_item_set_memberships, get_item_use_effects, get_mob_weaknesses_all, get_mob_resistances_all, insert_player_active_effect, upsert_player_inventory_item, delete_player_inventory_item, get_player_inventory, get_vendor_npcs, get_vendor_inventory, update_durability_current, update_item_kill_count, transfer_item_ownership, transfer_item_between_chars, nullify_item_owner, delete_inventory_item_by_id, insert_currency_transaction, insert_character_equipment, delete_character_equipment, get_respawn_zones, get_game_zones, get_status_effect_templates, get_player_pity, upsert_pity_counter, get_player_bestiary, upsert_bestiary_kill, get_timed_champion_templates, update_timed_champion_next_spawn, get_player_reputations, upsert_reputation, get_player_masteries, upsert_mastery, get_zone_event_templates.

Improvements:
Database — character attribute query now joins item_set_bonuses so set bonuses are reflected in effective attributes; skills query extended with swing_ms, animation_name, is_passive fields; skill tables renamed from skill_effects/skill_effects_type to skill_damage_formulas/skill_damage_types; mob query extended with rare/champion fields (is_rare, rare_spawn_chance, can_evolve), faction/biome/mob_type_slug, and formula-based HP min/max; items query extended with mastery_slug, max_quantity, loot_tier, class restrictions, set memberships, and use effects; quest query now excludes turned_in/failed entries; passive skill modifier query added.
ItemManager — updated to load new item fields (mastery_slug, max_quantity, loot_tier, class restrictions, item set memberships, use effects).
MobManager — extended to load rare-mob, faction, biome, mob_type_slug fields.
NPCManager — loads faction_slug field.
CharacterManager — minor improvements to character data handling.
DialogueQuestManager — minor fixes.

Removed:
LOGGING_GUIDE.md — moved to chunk server docs/ folder.
NPC_SYSTEM_DOCUMENTATION.md — moved to chunk server docs/ folder.
NPC_SYSTEM_GUIDE.md — moved to chunk server docs/ folder.
SKILL_SYSTEM_GUIDE.md — moved to chunk server docs/ folder.

---

v0.0.3
07.03.2026
================
New:
Per-subsystem logging via spdlog — each component (combat, mob, skill, character, item, npc, chunk, client, spawn, quest, config, experience, inventory, network, events, gameloop) now logs to its own named channel. Each subsystem level can be set independently via environment variables (LOG_LEVEL_<NAME>).
GameConfigService — new service for managing game configuration.
Added TimestampUtils utility.

Improvements:
Database — major refactor of the database layer.
EventHandler / EventDispatcher — significantly extended, new event types added.
NetworkManager — extended and improved.
MobManager — extended, improved logic.
NPCManager — improved.
CharacterManager — extended, improved character data handling.
DialogueQuestManager — improved.
Dockerfile — minor update.
docker-compose: LOG_LEVEL=info by default; network, gameloop, events set to warn to reduce log noise.

---

v0.0.2
28.02.2026
================
New:
Add changelog file to track changes and updates in the project.
Save current experience and level to game server DB immediately upon grant
Save current player position to game server DB periodically and upon player logout
