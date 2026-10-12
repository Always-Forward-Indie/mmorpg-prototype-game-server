# game_config: единый реестр тюнинга

Таблица `game_config(key, value, value_type, description)` — единственный
источник чисел, которые раньше были литералами. Game читает при старте
(`GameServices::loadStaticData` → `GameConfigService::loadConfig`, fail-fast),
пушит **целиком** в чанк при JOIN (`GET_GAME_CONFIG`, см. `join-catalog.md`),
GM-команда `reload` перечитывает без рестарта.

## Чтение

- Game: `GameConfigService::getInt(key, fallback)` (не бросает; нет ключа
  или мусор → fallback). Других typed-геттеров нет — добавлять по мере нужды.
- Chunk: `getInt/getFloat/getBool/getString(key, default)` (`shared_mutex`,
  не бросает, лог + fallback).
- Правило фолбэка: **fallback = предыдущий литерал**. Миграция, меняющая
  поведение дефолтом, запрещена — только новые ключи со старыми значениями.
- Пустая таблица не вайпает последний хороший конфиг (fail-safe в `loadConfig`).

## Группы ключей (канонические значения — в дампе + миграциях 085/086/087)

| Группа | Ключи | Семплирование |
|--------|-------|---------------|
| `scheduler.*_ms` (24) | `mob_tick_ms 50` … `spawn_safety_ms 300000` + `inv_sync_retry_ms 5000` | Чанк: один раз при планировании |
| `scheduler.*_sec` (2) | `chunk_sweep_sec`, `client_sweep_sec` = 60 | Game `main`: один раз при старте |
| Пороги свипов (2) | `chunk_silent_threshold_ms 180000`, `client_silent_threshold_ms 600000` | Game `main`: один раз |
| `scheduler.inv_sync_retry_ms 5000` | Ретрай id-sync инвентаря (миграция `095`) | Чанк: один раз при планировании |
| `interest.*` (6) | `cell_size 1500.0`, `view_radius 1`, `rehome_margin_frac 0.15`, `enabled/snapshots true`, `snapshot_cooldown_ms 2000` | Чанк: при старте (+reload) |
| `regen.*` | `tickIntervalMs 4000`, `baseHpRegen 2`, `baseMpRegen 1`, `disableInCombatMs 8000`, коэффы | Интервал — при старте; amounts — живьём каждый тик |
| `durability.*` | `tier{1,2,3}_threshold_pct` 75/50/25 + `tier{1,2,3}_penalty_pct` 5/15/30 (SQL `COALESCE`, живьём), `death_penalty_pct` 0.05, `warning_threshold_pct` 30 | Живьём |
| `drop.ttl_sec 300`, `corpse.ttl_ms 60000` | TTL чисток (миграция `089`; раньше только фолбэки) | Живьём каждый тик |
| `reputation.*` | Тиры `enemy/neutral/friendly/ally` −500/0/200/500 (чанк `setThresholds`), `vendor_discount_threshold` 200 + `vendor_discount_pct` 0.05 | При JOIN-каталоге |
| `combat.evasion_cap` 75, `combat.dot_min_tick_ms` 500, `combat.dot_max_ticks` 20 | Кап уворота + нормализация DoT-тиков (`clampDotTick`, мигр. 105) | Живьём |
| `item_soul.*` | Тиры 50/200/500 + flat 1/2/3 + `tier2/3_crit_pct` 0.05/0.08 (крит теперь применяется) | Живьём |
| `champion.*`, `survival_champion.*` | Множители HP/урона, деспавн, пре-анонс (мигр. 105; раньше только фолбэки) | Живьём |
| `economy.*`, `mastery.*`, `bestiary.*`, `movement.*`, `combat.*` (остальные), `admin.*`, `carry_weight.*`, `fellowship.*`, `pity.*` | См. дамп | По месту (интересует — grep `getInt/getFloat` в chunk `src/`) |

Полный перечень — `SELECT key, value FROM game_config ORDER BY key`
(миграции `085/086/087` + дамп; live = dump). Новый ключ: миграция
`INSERT ... ON CONFLICT DO NOTHING` + строка в дамп с live-timestamp +
`get*()` в коде с прежним литералом как фолбэк (прецеденты: 085, 086, 087).
