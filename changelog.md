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
