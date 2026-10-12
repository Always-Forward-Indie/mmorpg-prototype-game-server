# Outbox, game side: claim + ack

Чанк-шлейф (`FactOutbox`, ретраи, ключи) — в `protocol/13-reliability-delivery.md`.
Здесь — что делает game при приёме факта.

## Claim (`src/events/Outbox.cpp:17-31`, `QueryRegistry.cpp:67`)

```sql
WITH ins AS (
  INSERT INTO fact_keys_applied(key) VALUES($1)
  ON CONFLICT DO NOTHING RETURNING key)
SELECT COUNT(*) FROM ins;
```

Выполняется **в той же транзакции**, что и бизнес-запись (атомарно):

| `COUNT(*)` | Смысл | Хендлер |
|------------|-------|---------|
| `1` | applied | Бизнес-логика → `commit` → `factAck{applied}` |
| `0` | duplicate | `return` без бизнес-логики → `factAck{duplicate}` |
| throw (`DatabaseError`) | transport error | `-1`: без ack и без commit → чанк ретраит |

Таблица `fact_keys_applied(key, applied_at)` — миграция `083`.
Ключ — `{characterId}:{factType}:{bootId}:{seq}` (минтит чанк).

## High-water (`outbox_seq_watermark`, миграция `088`)

Каждый fresh-claim дописывает `last_seq` (GREATEST-wins) в той же txn —
см. `outboxClaimFactKey`: upsert идёт после claim'а, throw роняет всё
вместе. Парсинг ключа — `outboxParseFactKey` (reject 3-part legacy и
`test-*` ключей — они клеймятся, но watermark не трогают; `characterId 0`
принимается). Читается один раз на новый линк (`get_outbox_global_watermark`
= `MAX(last_seq)`), отдаётся в `setChunkData`-ack'е. Пустая таблица → 0,
безвредно (bootId).

## Reconcile (`outboxUnacked` → `outboxDiff`)

Маршрут `EventDispatcher` + событие `OUTBOX_UNACKED` + `handleOutboxUnackedEvent`:
`get_outbox_diff_keys` (`= ANY($1::text[])`, только PK-lookups) делит ключи
на applied/unknown; ответ `outboxSendDiff` — на живой chunk-сокет
(`resolveLiveSocket`). Ключи валидируются по charset (`"',[{}`
отбрасываются — array-литерал без экранирования безопасен). DB-ошибка →
молчание (chunk ретраит флашом).

## Ретеншн

`delete_old_fact_keys(days, limit)` — game-свип (id 3, cadence
`scheduler.chunk_sweep_sec`), сейчас (30, 1000). Индекс
`ix_fact_keys_applied(applied_at)` — миграция `088`.

## Ack (`Outbox.cpp:33-49`)

`factAck{key, status}` в `status:success`-конверте на **тот же сокет**,
откуда пришёл факт (chunk-линк, не клиентский). `generateResponseMessage`
добавляет `version`/`timestamp`; `errorCode` тут нет (это не ошибка).

## Паттерн в хендлере (6 строк, копировать дословно)

```cpp
const std::string factKey = outboxFactKeyOf(j);          // "" если нет ключа (legacy)
if (!factKey.empty())
{
    const int claimed = outboxClaimFactKey(db, txn, factKey);
    if (claimed == 0) { outboxSendFactAck(net, socket, factKey, "duplicate"); return; }
    if (claimed < 0) return;                             // без ack → ретрай чанка
}
// ... бизнес-записи через execChecked ...
txn.commit();
if (!factKey.empty()) outboxSendFactAck(net, socket, factKey, "applied");
```

Покрыты 17 хендлеров (Skills: rep/mastery/learned/skillbar; Item: durability/
currency/equipment/killCount; Effects; Character debt; Social titles;
Inventory; Progression: pity/bestiary/timed). Legacy keyless-сендеры идут
старым путём без claim. Новый `save*` с exactly-once: добавить claim
**строго до** бизнес-записей в том же `txn`; widen allowlist чанка
одновременно (иначе без ack — вечные ретраи в DLQ-шум).

## Без claim — осознанно (idempotent-by-state)

Три группы не клеймятся, и это решение, а не долг:

1. **Quest/flag** (`DialogueQuestEventHandler: updatePlayerQuestProgress/Flag`):
   last-write-wins перезапись состояния; повторная доставка пишет то же
   состояние. Наградные выдачи идут отдельными claimed-путями.
2. **Transfer-трио** (`ItemEventHandler: transfer/nullify/delete`): повторный
   `transfer_item_between_chars`/`transfer_item_ownership` — no-op (владелец
   уже сменён), `nullify`/`delete by id` — no-op по отсутствующей строке.
3. **Typed-vector снапшоты** (`SAVE_POSITIONS/HP_MANA/PROGRESS/PLAYTIME`,
   `MARK_ONLINE`): `vector<CharacterDataStruct>`, не объект — `factKeyOf`
   всегда `""` (legacy by design); self-heal следующим периодом (30с/10с/60с).

Строгий claim для 1–2 потребовал бы `factKey` с чанка (widen `FactOutbox`
allowlist 16+) ради нулевого выигрыша — не делать без новой причины.

## Дельты vs absolute

Где конкурентные записи возможны — дельта (`add_reputation`: атомарный
`value = value + EXCLUDED.value`), а не absolute-upsert: конкурентные
абсолюты гоняются last-writer-wins. Исключение — 60s снапшот репутации
чанк шлёт absolute осознанно (staleness-bound, см. 13-й протокол).

Обратное исключение — `upsert_player_inventory_item` теперь absolute-SET
(`quantity = EXCLUDED.quantity`, было аддитивное `+`): чанк всегда шлёт
абсолютные остатки, writer один, а аддитив дрейфовал при быстрых повторах
и дублировал каждый sync-ретрай. Пин: `test_inventory_sync_sim.py`.

Дальше (096): строки владеют пер-инстанс токеном (`sync_token` UNIQUE
NULL) — путь токена (SELECT → adopt virgin legacy → INSERT) вместо пары
шаблона; template-pair unique снят (дубли нестакаемых живут, стакаемые
остаются одной строкой через adopt). Все записи абсолютным SET, ретраи
сходятся. Legacy без токена: SELECT-first без ON CONFLICT. Пин:
`test_duplicate_nonstackables_get_distinct_ids` + echo/convergence.
