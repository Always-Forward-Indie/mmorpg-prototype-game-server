# JOIN catalog orchestration (game → chunk static push)

> Топология деплоя: **single-chunk**. `ChunkManager` уже умеет реестр
> N чанков (`selectChunkForNewClient` lowest-id, `selectChunkForPosition`
> по боксам `pos/size`, generations против stale-disconnect), но межчанковой
> миграции сессии нет — `GET_CHARACTER_DATA` отвечает на тот же сокет.
> Это зафиксированное решение, не долг (см. wire `01`, §1.0).

Game — единственный, кто ходит в БД. Чанк при старте линка получает всю
статику одним пушем (`EventHandler.cpp:355-535`). Клиентские джойны в это
время ждут: чанк без каталога отвечает `chunkPort: 0` (см. `test_join_storm`).

## Триггер

`chunkServerConnection` от чанка. Дальше два пути:

- **Новый линк** (новый сокет = чанк рестартнул; unknown id = game рестартнул):
  полный пуш 24 записей каталога (см. ниже), затем ack `setChunkData`.
- **Heartbeat re-assert** (тот же сокет, чанк шлёт `chunkServerConnection`
  каждые 60с): только refresh регистрации + ack, **без пуша**. Полный пуш
  на каждый heartbeat вайпал бы chunk-side runtime (timed-champion
  spawned-флаги → бесконечный респаун каждую минуту).

## 24 записи каталога (порядок = порядок пуша)

`spawn_zones`, `mobs`, `mobs_attributes`, `npcs`, `npcs_attributes`,
`items`, `mob_loot`, `exp_table`, `dialogues`, `quests`, `game_config`,
`vendor`, `trainer`, `respawn_zones`, `class_spawn_zones`, `game_zones`,
`status_effects`, `timed_champions`, `zone_events`, `mastery_defs`,
`title_defs`, `emote_defs`, `ambient_speech`, `world_objects`.
(+JOIN-свита: `setCharacterData` per-client при `joinGameClient`, см. 01.)

## Учёт ошибок (`dispatchCatalog`)

Каждая запись диспатчится изолированно (`EventHandler.cpp:399-411`):
исключение = `++catalogFailed` + error-лог, остальные записи идут дальше.
Раньше любое исключение скипало остаток **и** ack — чанк бутался
частичным и висел до следующего heartbeat.

Итог — в логе game (root-логгер, всегда видно):
`[join] full catalog push chunk=1 entries=24 failed=N tookMs=M`.

- `failed == 0` → ack `setChunkData` success. Success-ack нового линка
  несёт `body.outboxGlobalSeq` (глобальный high-water `outbox_seq_watermark`,
  фазы B-D; heartbeat/error-ack'и — без поля). Чанк fast-forward'ит счётчик
  и шлёт `outboxUnacked` → получает `outboxDiff` (см. `outbox-game-side.md`).
- `failed > 0` → регистрация **сносится**
  (`removeChunkServerDataById`), ответ `ERR_JOIN_CATALOG_INCOMPLETE`
  (error + `body.errorCode`), следующий heartbeat трактуется как новый
  линк и ретраит полный пуш. Ack success на частичном каталоге запрещён —
  иначе мир бутается битым без ретрая.

Конкурентные JOIN'ы одного chunk id сериализованы (`joinLockFor`):
пуш — ~24 последовательных DB round-trip'а, два interleaved-пуша
дублировали бы весь каталог на чанке.

## Связь с остальным

- Heartbeat чанка (60с) + game-свип (`scheduler.chunk_sweep_sec`,
  порог `scheduler.chunk_silent_threshold_ms`) — см. `reconnect-outbox-ops.md` у чанка.
- `game_config` внутри пуша — см. `game-config.md`.
- Outbox-факты (`save*`) идут **после** каталога, по установленному линку —
  см. `outbox-game-side.md`.
