# Session registry (game): generations + sweeps

Проблема: поздний disconnect старого линка сносил свежую сессию
(стейл-сокет приходит позже переподключения). Решение — поколения,
как у chunk `ChunkManager`/`ClientManager`.

## ClientManager (`src/services/ClientManager.cpp`)

- `(Re)bind`: новый линк = новая генерация сокета. Поздний disconnect
  старого поколения дропает только себя, переподвязанную сессию не трогает
  (проверка генераций, `ClientManager.cpp:15,81`). Пин:
  `StaleDisconnectKeepsReboundSession`.
- `lastSeenMs` обновляется на bind/ping (`:12,38,94`, `steady_clock`).
- `sweepSilentClients(thresholdMs)` (`:98-112`): выселяет молчунов.
  Период и порог — `scheduler.client_sweep_sec` / 
  `scheduler.client_silent_threshold_ms` (default 60с / 600000мс = 10 мин).

## ChunkManager (`src/services/ChunkManager.cpp`)

- Та же схема поколений для регистраций чанков (`:121`): стейл-disconnect
  и heartbeat-гонки не сносят живой линк.
- `sweepSilentChunks(thresholdMs)` (`:170-187`): дроп чанков без heartbeat.
  Период и порог — `scheduler.chunk_sweep_sec` /
  `scheduler.chunk_silent_threshold_ms` (default 60с / 180000мс = 3 heartbeat'а).
- Heartbeat чанка каждые 60с (`reconnect-outbox-ops.md` у чанка).

## Инварианты

- Никогда не удалять по сокету без сверки поколения.
- `steady_clock` везде (не wall — NTP-скачки не должны выселять живых).
- Свипы только выселяют мёртвых (пороги консервативные); живые сессии
  трогать запрещено даже при переполнении.
