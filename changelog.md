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
