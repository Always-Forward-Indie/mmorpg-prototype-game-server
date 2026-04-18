v0.2.4
18.04.2026
================
New:

**Zone Shape System — поддержка форм RECT/CIRCLE/ANNULUS для спавн-зон и игровых зон.**
- `SpawnZoneStruct` (DataStructs.hpp) — синхронизирован с chunk-сервером: добавлены `ZoneShape shape`, `centerX/Y`, `innerRadius`, `outerRadius`; `SpawnZoneMobEntry` заменяет плоские поля моба.
- `SpawnZoneManager` — обновлён под новую структуру `SpawnZoneStruct` и `SpawnZoneMobEntry`.
- `Database::get_game_zones` — расширен SELECT: добавлены `shape_type`, `center_x`, `center_y`, `inner_radius`, `outer_radius`.
- `Database::get_spawn_zones` — расширен SELECT: добавлены `shape_type`, `center_x`, `center_y`, `inner_radius`, `outer_radius`, `mob_entries` (JSON-аггрегат вместо JOIN-строк).
- `EventHandler::handleGetGameZonesEvent` — сериализует новые поля: `shape`, `centerX`, `centerY`, `innerRadius`, `outerRadius` в пакет `setGameZones` на chunk-сервер.
- `EventHandler::handleGetSpawnZonesEvent` — сериализует новые поля аналогично в пакет `setSpawnZones`.

---

v0.2.3
17.04.2026
================
New:

**Игровая аналитика — сохранение событий в БД.**
- `Event::SAVE_ANALYTICS_EVENT` — новый тип события.
- `EventDispatcher::handleSaveAnalyticsEvent` — принимает пакет `analyticsEvent` от chunk-сервера, создаёт событие `SAVE_ANALYTICS_EVENT` и передаёт в очередь.
- `EventHandler::handleSaveAnalyticsEventEvent` — выполняет `txn.exec_params` с `INSERT INTO game_analytics (event_type, character_id, session_id, level, zone_id, payload) VALUES ($1, NULLIF($2,0), $3, $4, $5, $6::jsonb)`. `character_id = 0` → NULL через `NULLIF` (FK безопасен). Fire-and-forget, ответ chunk-серверу не отправляется.
- Роутинг `"analyticsEvent"` добавлен в `EventDispatcher.cpp`.

---

v0.2.2
16.04.2026
================
New:

**WIO — World Interactive Objects (загрузка и форвардинг на chunk-server).**
- `WorldObjectDataStruct` — новая структура данных (аналог `NpcTemplateStruct`): все поля таблицы `world_objects` включая `conditionGroup` (JSONB → `nlohmann::json`). Поле `meshId` отсутствует — UE5 binds mesh по `slug` самостоятельно.
- `Database::get_world_objects` — новый prepared statement: `SELECT wo.id, wo.slug, wo.name_key, wo.object_type, wo.scope, wo.pos_x, wo.pos_y, wo.pos_z, wo.rot_z, wo.zone_id, wo.dialogue_id, wo.loot_table_id, wo.required_item_id, wo.interaction_radius, wo.channel_time_sec, wo.respawn_sec, wo.is_active_by_default, wo.min_level, wo.condition_group::text, COALESCE(ws.state, CASE WHEN wo.is_active_by_default THEN 'active' ELSE 'disabled' END) AS current_state FROM world_objects wo LEFT JOIN world_object_states ws ON ws.object_id = wo.id WHERE wo.id > 0`.
- `EventHandler::handleGetWorldObjectsEvent` — новый обработчик: выполняет `get_world_objects`, парсит JSONB `condition_group`, собирает вектор `WorldObjectDataStruct` и отправляет chunk-серверу событие `SET_ALL_WORLD_OBJECTS`.
- Вызов `GET_WORLD_OBJECTS` добавлен в startup-последовательность `JOIN_CHUNK_SERVER` — после `GET_TITLE_DEFINITIONS` и до завершения инициализации.

---

v0.2.1
17.04.2026
================
New:

**Поддержка `condition_params` в системе титулов.**
- `Database::get_title_definitions` — добавлена колонка `condition_params::text` в SELECT-запрос.
- `EventHandler::handleGetTitleDefinitionsEvent` — парсит `condition_params` из результата запроса и передаёт в пакете `setTitleDefinitions` на chunk-сервер. При ошибке парсинга JSONB подставляется пустой объект `{}`.

---

v0.2.0
16.04.2026
================
New:

**Migration 041 — Weapon Mastery, Quest Reputation, 3-tier Durability.**
- `Database::get_quests` — добавлены три колонки: `reputation_faction_slug`, `reputation_on_complete`, `reputation_on_fail`.
- `Database::get_character_attributes` — CTE `dura_config` заменена: вместо одного `threshold`/`penalty` теперь шесть параметров `t1/p1`, `t2/p2`, `t3/p3`. CTE `equip_bonus` использует трёхуровневый CASE по соотношению `durability_current/durability_max`:
  - ratio < t3 (0.25): `value * (1 - p3)` = −30%
  - ratio < t2 (0.50): `value * (1 - p2)` = −15%
  - ratio < t1 (0.75): `value * (1 - p1)` = −5%
  - иначе: без штрафа
  Клиент получает атрибуты уже с применённым штрафом — не нужно рассчитывать на стороне клиента.
- `DialogueQuestManager::getAllQuestsJson` — сериализует три новых поля репутации в квест JSON для chunk-сервера.
- DB: `items.mastery_slug` заполнен для `iron_sworld` (sword_mastery) и `wooden_staff` (staff_mastery).
- DB: `quest` — три новых колонки (`reputation_faction_slug VARCHAR(64)`, `reputation_on_complete INT`, `reputation_on_fail INT`).
- DB: конфигурация `durability.warning_threshold_pct` и `durability.warning_penalty_pct` удалены; добавлены `durability.tier1_threshold_pct=0.75`, `tier1_penalty_pct=0.05`, `tier2_threshold_pct=0.50`, `tier2_penalty_pct=0.15`, `tier3_threshold_pct=0.25`, `tier3_penalty_pct=0.30`.

---

v0.1.9
15.04.2026
================
New:

`GET_NPC_AMBIENT_SPEECH` / `setNPCAmbientSpeech` event — при подключении chunk-сервера game-сервер выполняет запрос `get_npc_ambient_speech` к БД и отправляет chunk-серверу пакет `setNPCAmbientSpeech` со всеми конфигурациями ambient speech. Запускается в той же последовательности что `GET_EMOTE_DEFINITIONS`, `GET_TITLE_DEFINITIONS` и прочие startup-события при `JOIN_CHUNK_SERVER`.
Database — новый prepared statement `get_npc_ambient_speech`: `SELECT c.npc_id, c.min_interval_sec, c.max_interval_sec, l.id AS line_id, l.line_key, l.trigger_type, l.trigger_radius, l.priority, l.weight, l.cooldown_sec, l.condition_group FROM npc_ambient_speech_configs c JOIN npc_ambient_speech_lines l ON l.npc_id = c.npc_id ORDER BY c.npc_id, l.priority DESC, l.id`.
`GET_NPC_AMBIENT_SPEECH` — новый тип события в `Event.hpp`.
`EventHandler::handleGetNPCAmbientSpeechEvent()` — новый обработчик: читает результат запроса, группирует строки по `npc_id` в map, парсит `condition_group` как JSONB→JSON, собирает JSON-массив `ambientSpeech[]` и отправляет chunk-серверу пакет `setNPCAmbientSpeech`.

---

v0.1.8
14.04.2026
================
Bug Fixes:

Пассивные скилы не возвращались в `get_character_skills` — запрос использовал только `JOIN` к таблицам `skill_effect_instances`, `skill_effects_mapping`, `skill_damage_formulas`, `skill_damage_types`, которые для пассивных скилов пусты. Passive-скилы полностью пропадали из `SET_PLAYER_SKILLS` после перезапуска. Фикс: все четыре `JOIN` переведены в `LEFT JOIN`; `COALESCE(seft.slug, '')` защищает nullable `skill_effect_type`; `COALESCE(spm.skill_level, cs.current_level)` как значение по умолчанию для `skill_level` когда `skill_properties_mapping` пуст; `GROUP BY` дополнен соответствующим `COALESCE`-выражением.

`handleGetCharacterDataEvent` зажимал `characterCurrentMana` до базового `characterMaxMana` — это уничтожало ману, накопленную выше базового предела за счёт пассивного скила `mana_shield` (постоянный `max_mana`-бонус). Фикс: клэмп по `characterMaxMana` для маны убран; HP-клэмп сохранён как защита от мусора из БД (у HP нет пассивных бонусов выше базового).

`handleSaveLearnedSkillEvent` не включал пассивные модификаторы в ответ чанк-серверу — chunk-сервер получал `setLearnedSkill` без поля `effects` для пассивных скилов и не мог применить `ActiveEffectStruct` немедленно (только после реконнекта). Фикс: если `skillJson["isPassive"] == true`, game-сервер дополнительно выполняет запрос `get_passive_skill_modifiers_by_slug` и вкладывает результат в `skillJson["effects"]` перед отправкой ответа.

New:

Новый prepared statement `get_passive_skill_modifiers_by_slug` — возвращает строки `(effect_slug, attribute_slug, value, modifier_type)` для пассивного скила по его `slug` из таблицы `passive_skill_modifiers`; используется в `handleSaveLearnedSkillEvent` при сохранении изученного пассивного скила.

---

v0.1.7
13.04.2026
================
New:
`GET_EMOTE_DEFINITIONS` / `SET_EMOTE_DEFINITIONS_DATA` event — при подключении chunk-сервера game-сервер выполняет запрос `get_emote_definitions` к БД и отправляет chunk-серверу пакет `setEmoteDefinitionsData` с полным каталогом эмоций (id, slug, display_name, animation_name, category, is_default, sort_order). Запускается вместе с `GET_TITLE_DEFINITIONS` при подключении.
`GET_PLAYER_EMOTES` / `SET_PLAYER_EMOTES_DATA` event — по запросу chunk-сервера (`getPlayerEmotesData`) автоматически выдаёт дефолтные эмоции новым персонажам (`grant_default_emotes` — идемпотентный INSERT ON CONFLICT DO NOTHING), читает `character_emotes` для персонажа, возвращает `setPlayerEmotesData` пакет со списком разблокированных slugs.
Database — три новых prepared statement: `get_emote_definitions` (`SELECT ... FROM emote_definitions ORDER BY sort_order, id`), `get_player_emotes` (`SELECT emote_slug FROM character_emotes WHERE character_id = $1`), `grant_default_emotes` (INSERT дефолтных эмоций для персонажа ON CONFLICT DO NOTHING).
`GET_EMOTE_DEFINITIONS`, `GET_PLAYER_EMOTES` — два новых типа событий в `Event.hpp` (game-server).
`EventDispatcher` (game-server) — роутинг `getEmoteDefinitionsData` → `handleGetEmoteDefinitionsData()` и `getPlayerEmotesData` → `handleGetPlayerEmotesData()`.

---

v0.1.6
11.04.2026
================
Bug Fixes:
`SAVE_LEARNED_SKILL` — SP теперь списывается атомарно в той же транзакции, что и вставка скила (`save_learned_skill` + `get_skill_sp_cost` + `decrement_skill_points`). Ранее `free_skill_points` убывало только в памяти chunk-сервера; рестарт после изучения скила возвращал SP без отката самого скила.
`SAVE_PLAYER_TITLE` — полная переработка логики сохранения: новый формат пейлоада `{ earnedSlugs[], equippedSlug }` вместо одиночного `{ titleSlug, equipped }`. Все заработанные тайтлы bulk-upsert-ятся в одной транзакции, затем атомарно сбрасывается флаг `equipped` у всех и выставляется только у выбранного. Ранее сохранялся только последний экипированный тайтл — все остальные заработанные терялись при рестарте.

New:
`decrement_skill_points` prepared statement — `UPDATE characters SET free_skill_points = GREATEST(0, free_skill_points - $2) WHERE id = $1`. Защита от ухода в отрицательные значения через GREATEST(0, ...).
`get_skill_sp_cost` prepared statement — получает `skill_point_cost` из `class_skill_tree JOIN skills WHERE slug = $1`. Используется при сохранении изученного скила для атомарного списания SP.

---

v0.1.5
08.04.2026
================
Improvements:
Skill timing data — в БД исправлены значения `cast_ms` и `swing_ms` для всех активных скилов через `skill_properties_mapping`. Данные загружаются game-сервером при старте и передаются chunk-серверу; актуальные значения обеспечивают корректную работу каст-блокировки и отложенного выполнения скилов.

---

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
