# Event routing (game): route table + domain handlers

## Route table (`EventDispatcher.cpp:51-105`)

54 маршрута `eventType → handle*`, строится один раз в ctor (`buildRouteTable`),
lookup под `dispatchMutex_` (сериализация нужна только для батчинга
`eventsBatch_`, не для тред-сейфти хендлеров). Unknown event → error-лог,
без ответа. Сигнатура всех хендлеров одинакова: `(payload, socket)`.

Группы маршрутов:

| Группа | Маршруты |
|--------|----------|
| Сессии | `joinGame`, `joinGameClient`, `moveCharacter`, `disconnectClient`, `pingClient`, `getSpawnZones`, `getMobData` |
| Чанк-линк | `chunkServerConnection`, `chunkServerData` |
| Каталог (static reads) | `getCharacterExpForLevel`, `getExpLevelTable`, `getNPCsData`, `getMasteryDefinitionsData`, `getTitleDefinitionsData`, `getEmoteDefinitionsData` |
| Позиции/прогресс | `savePositions`, `saveHpMana`, `saveCharacterProgress`, `savePlayTime`, `markCharactersOnline` |
| Квесты/флаги/эффекты | `getPlayerQuests`, `getPlayerFlags`, `getPlayerActiveEffects`, `getCharacterAttributes`, `updatePlayerQuestProgress`, `updatePlayerFlag`, `saveActiveEffect`, `saveSkillCooldown`, `getPlayerSkillCooldowns` |
| Инвентарь/экономика | `saveInventoryChange`, `getPlayerInventory`, `saveEquipmentChange`, `saveDurabilityChange`, `saveItemKillCount`, `transferInventoryItem`, `nullifyItemOwner`, `deleteInventoryItem`, `saveCurrencyTransaction` |
| Прогрессия | `getPlayerPityData`, `getPlayerBestiaryData`, `savePityCounter`, `saveBestiaryKill`, `timedChampionKilled`, `saveExperienceDebt` |
| Скиллы/репутация | `getPlayerReputationsData`, `saveReputation`, `getPlayerMasteriesData`, `saveMastery`, `saveLearnedSkill`, `saveSkillBarSlot`, `getPlayerTitlesData`, `savePlayerTitle`, `getPlayerEmotesData` |
| Аналитика | `analyticsEvent` |

## Доменные хендлеры (`src/events/handlers/`, 12 файлов)

`EventHandler` — только ядро: `dispatchEvent`-switch → делегация,
JOIN/chunk-оркестрация (`join-catalog.md`), ping/join/move/disconnect сессий.
Вся предметка — в доменах (телá перенесены дословно при разносе 5643→1044):

| Хендлер | Отвечает за |
|---------|-------------|
| `BaseEventHandler` | Общая база (game-вариант chunk'овой): `NetworkManager&`, `GameServices&`, `log_` |
| `CatalogEventHandler` | Static reads каталога (rethrow в `dispatchCatalog` для `catalogFailed`) |
| `CharacterEventHandler` | Saves персонажа, debt, mark-online |
| `DialogueQuestEventHandler` | Диалоги/квесты (DQM-домен) |
| `EffectsEventHandler` | Active effects, cooldowns, analytics |
| `InventoryEventHandler` | Инвентарь (2 хендлера) |
| `ItemEventHandler` | Durability, currency, equipment, kill-counters, transfers, nullify, delete |
| `ProgressionEventHandler` | Pity, bestiary, champions |
| `SkillsEventHandler` | Reputation, mastery, learned skills, skillbar |
| `SocialEventHandler` | Titles, emotes |
| `VendorEventHandler` | Vendor/trainer данные |
| `WorldEventHandler` | World static reads (spawns/mobs/items/NPCs) |

Ноль кросс-вызовов между хендлерами (проверено скриптом при разносе) —
разбиение механическое. Новый `save*`/`get*` маршрут: добавить строку в
таблицу + метод (или в существующий домен по смыслу) + запись в `Outbox`
если нужен exactly-once (см. `outbox-game-side.md`).

## Парсинг (single-parse)

`MessageHandler::parseMessageWithTimestamps` парсит wire 1 раз, отдаёт
`shared_ptr<const json>` 8-м элементом tuple → `EventPayload::parsed`.
Все 38+2 body-хендлеров читают shared (копия локально, фолбэк — парс
`rawMessage` на defensive-пути). Новых `parse(payload.rawMessage)` не добавлять.
