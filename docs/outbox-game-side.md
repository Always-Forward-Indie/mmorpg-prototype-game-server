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

## Дельты vs absolute

Где конкурентные записи возможны — дельта (`add_reputation`: атомарный
`value = value + EXCLUDED.value`), а не absolute-upsert: конкурентные
абсолюты гоняются last-writer-wins. Исключение — 60s снапшот репутации
чанк шлёт absolute осознанно (staleness-bound, см. 13-й протокол).
