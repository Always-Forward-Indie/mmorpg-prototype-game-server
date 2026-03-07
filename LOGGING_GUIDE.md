# Logging Guide

Все три сервера используют [spdlog](https://github.com/gabime/spdlog) с единой конфигурацией.  
Логи пишутся одновременно в **консоль** (с цветами) и в **файл** `logs/<server-name>.log`.

---

## Уровни логов

| Уровень    | Цвет в терминале | Когда использовать                              |
|------------|------------------|-------------------------------------------------|
| `trace`    | серый            | Очень детальный трейс потока выполнения         |
| `debug`    | голубой (cyan)   | Отладочная информация в процессе разработки     |
| `info`     | зелёный          | Нормальные события (старт, подключение, и т.д.) |
| `warn`     | жёлтый           | Ненормальная, но не фатальная ситуация          |
| `error`    | красный          | Ошибка, которую сервер пережил                  |
| `critical` | ярко-красный     | Фатальная ошибка, сервер падает                 |

---

## Глобальный уровень логов

Задаётся через переменную окружения `LOG_LEVEL` в `docker-compose.yml`:

```yaml
environment:
  - LOG_LEVEL=info   # показывать info / warn / error / critical
```

Возможные значения: `trace`, `debug`, `info`, `warn`, `error`, `critical`  
По умолчанию: `info`

**Примеры:**

```yaml
# Тихий режим — только ошибки
- LOG_LEVEL=error

# Разработка — всё подряд
- LOG_LEVEL=debug

# Почти тихий, но предупреждения видны
- LOG_LEVEL=warn
```

---

## Фильтрация по подсистемам

Каждая подсистема может иметь **свой уровень**, независимо от глобального.

Имя переменной формируется так:
```
LOG_LEVEL_<getSystem() аргумент в верхнем регистре>
```

Например:
| Вызов в коде                      | Переменная окружения          |
|-----------------------------------|-------------------------------|
| `logger.getSystem("combat")`      | `LOG_LEVEL_COMBAT`            |
| `logger.getSystem("mob_movement")`| `LOG_LEVEL_MOB_MOVEMENT`      |
| `logger.getSystem("network")`     | `LOG_LEVEL_NETWORK`           |
| `logger.getSystem("auth")`        | `LOG_LEVEL_AUTH`              |

> **Важно:** переменная влияет **только** на логи, которые пишутся через логгер этой подсистемы.  
> Код, использующий основной `logger_`, под неё не подпадает.

> **Важно:** имена подсистем нужно использовать **с подчёркиванием** (не точкой): `"mob_movement"`, а не `"mob.movement"` — иначе env var имя будет содержать точку и не будет корректно найдено.

```yaml
environment:
  - LOG_LEVEL=warn                # глобально: тихо
  - LOG_LEVEL_COMBAT=debug        # combat: подробно
  - LOG_LEVEL_NETWORK=error       # network: только ошибки
  - LOG_LEVEL_AUTH=debug          # auth: подробно
```

Если переменная для подсистемы не задана — она наследует глобальный `LOG_LEVEL`.

---

## Использование в коде

### Основной логгер (для сервисов с инжектированным Logger)

```cpp
logger_.debug("message");
logger_.info("message");
logger_.warn("message");
logger_.error("message");
logger_.critical("message");
```

### Логгер подсистемы

Создаётся один раз — обычно в конструкторе класса.  
`getSystem()` возвращает существующий логгер или создаёт новый (регистрирует в spdlog).

```cpp
#include <spdlog/logger.h>   // нужен для типа возвращаемого значения

class CombatSystem {
    std::shared_ptr<spdlog::logger> log_;
public:
    CombatSystem(Logger& logger) {
        log_ = logger.getSystem("combat");
        // имя в выводе: "game-server.combat"
    }

    void handleAttack(int attackerId, int targetId, int damage) {
        log_->debug("Player {} hit mob {} for {} dmg", attackerId, targetId, damage);
        if (damage > 9000) {
            log_->warn("Abnormally high damage: {}", damage);
        }
    }
};
```

**Поддерживается fmt-форматирование** — не нужно вручную собирать строки:

```cpp
log_->info("Mob {} spawned at ({:.1f}, {:.1f})", mobId, x, y);
log_->error("DB query failed [{}ms]: {}", elapsed, errorMsg);
log_->debug("Inventory slot {} → item {} x{}", slot, itemId, count);
```

### Примеры названий подсистем

```cpp
logger.getSystem("combat")        // → game-server.combat    → LOG_LEVEL_COMBAT
logger.getSystem("mob_movement")  // → chunk-server.mob_movement → LOG_LEVEL_MOB_MOVEMENT
logger.getSystem("auth")          // → login-server.auth     → LOG_LEVEL_AUTH
logger.getSystem("network")       // → game-server.network   → LOG_LEVEL_NETWORK
logger.getSystem("inventory")     // → game-server.inventory → LOG_LEVEL_INVENTORY
logger.getSystem("aoe")           // → game-server.aoe       → LOG_LEVEL_AOE
```

Имя подсистемы всегда видно в консоли, например:
```
[12:34:56.123] [game-server.combat] [debug] Player 42 hit mob 7 for 150 dmg
[12:34:56.124] [game-server.combat] [warn]  Abnormally high damage: 99999
[12:34:56.125] [game-server.network] [error] Client 5 disconnected unexpectedly
```

---

## Изменение уровня прямо во время работы

Без перезапуска сервера (из кода):

```cpp
logger_.setLevel("debug");   // включить дебаг глобально
logger_.setLevel("warn");    // вернуть тишину
```

---

## Типичный сценарий отладки

> Хочу видеть только логи боевой системы и системы движения мобов, всё остальное молчит.

Сначала убеждаешься, что в коде эти классы используют `getSystem()`:
```cpp
// CombatSystem.cpp
log_ = logger.getSystem("combat");      // → LOG_LEVEL_COMBAT

// MobMovementManager.cpp
log_ = logger.getSystem("mob_movement"); // → LOG_LEVEL_MOB_MOVEMENT
```

Затем в `docker-compose.yml`:
```yaml
environment:
  - LOG_LEVEL=error                  # всё остальное — молчит
  - LOG_LEVEL_COMBAT=debug           # combat — подробно
  - LOG_LEVEL_MOB_MOVEMENT=debug     # движение мобов — подробно
```

Пересобирать не нужно — переменные окружения применяются при старте контейнера:
```bash
docker-compose up
```

---

## Лог-файлы

Файлы пишутся в директорию `logs/` внутри контейнера:

| Сервер       | Файл                         |
|--------------|------------------------------|
| game-server  | `logs/game-server.log`       |
| chunk-server | `logs/chunk-server.log`      |
| login-server | `logs/login-server.log`      |

Ротация: до **5 файлов по 10 МБ** каждый. Старые файлы удаляются автоматически.
