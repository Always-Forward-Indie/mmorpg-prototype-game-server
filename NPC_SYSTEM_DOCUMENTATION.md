# NPC System Documentation

## Overview

Система NPC (Non-Player Characters) реализована для MMORPG Game Server с использованием архитектуры SOLID принципов. Система обеспечивает загрузку, управление и отправку данных NPC между серверами.

## Архитектура

### Основные компоненты:

1. **NPCDataStruct** - структура данных для NPC
2. **NPCManager** - менеджер для управления NPC 
3. **EventHandler** - обработчик событий NPC
4. **Database integration** - интеграция с базой данных

## Структуры данных

### NPCDataStruct
```cpp
struct NPCDataStruct {
    int id;                    // Уникальный ID NPC
    std::string name;          // Имя NPC
    std::string slug;          // Slug для идентификации
    std::string raceName;      // Раса NPC
    int level;                 // Уровень NPC
    int currentHealth;         // Текущее здоровье
    int currentMana;           // Текущая мана
    int maxHealth;             // Максимальное здоровье
    int maxMana;               // Максимальная мана
    std::vector<NPCAttributeStruct> attributes; // Атрибуты
    std::vector<SkillStruct> skills;           // Навыки
    PositionStruct position;   // Позиция в мире
    
    // NPC специфичные свойства
    std::string npcType;       // Тип NPC (vendor, quest_giver, general)
    bool isInteractable;       // Можно ли взаимодействовать
    std::string dialogueId;    // ID диалога
    std::string questId;       // ID квеста
};
```

### NPCAttributeStruct
```cpp
struct NPCAttributeStruct {
    int id;                    // ID атрибута
    int npc_id;               // ID NPC
    std::string name;         // Название атрибута
    std::string slug;         // Slug атрибута
    int value;                // Значение атрибута
};
```

## NPCManager

### Функциональность:
- **Thread-safe операции** - все операции защищены мьютексами
- **Lazy loading** - загрузка NPCs только при необходимости
- **Chunk-based queries** - получение NPC по координатам чанка
- **Memory management** - правильное управление памятью

### Основные методы:

```cpp
void loadNPCs();                                              // Загрузка всех NPC из БД
std::map<int, NPCDataStruct> getNPCs() const;               // Получить все NPC
NPCDataStruct getNPCById(int npcId) const;                  // Получить NPC по ID
std::vector<NPCDataStruct> getNPCsByChunk(float x, float y, float z) const; // NPC в чанке
bool isLoaded() const;                                       // Проверка загрузки
size_t getNPCCount() const;                                 // Количество NPC
```

## События (Events)

### Типы событий NPC:
- **GET_NPCS_LIST** - получение списка всех NPC
- **GET_NPCS_ATTRIBUTES** - получение атрибутов NPC
- **SPAWN_NPCS_IN_CHUNK** - спавн NPC в чанке при входе игрока

### Обработчики событий:
```cpp
void handleGetNPCsListEvent(const Event &event);          // Отправка списка NPC
void handleGetNPCsAttributesEvent(const Event &event);    // Отправка атрибутов NPC
void handleSpawnNPCsInChunkEvent(const Event &event);     // Спавн NPC в чанке
```

## Интеграция с базой данных

### Подготовленные запросы:
- **get_npcs** - получение основных данных NPC
- **get_npc_attributes** - получение атрибутов NPC
- **get_npc_skills** - получение навыков NPC
- **get_npc_position** - получение позиции NPC

### Пример запроса:
```sql
SELECT npc.*, race.name as race 
FROM npc 
JOIN race ON npc.race_id = race.id;
```

## Сетевые пакеты

### Формат ответов:
```json
{
  "header": {
    "message": "Getting NPCs list success!",
    "clientId": 123,
    "eventType": "setNPCsList"
  },
  "body": {
    "npcsList": [
      {
        "id": 1,
        "name": "Village Elder",
        "slug": "village_elder",
        "race": "Human",
        "level": 50,
        "npcType": "quest_giver",
        "isInteractable": true,
        "posX": 100.5,
        "posY": 200.0,
        "posZ": 0.0
      }
    ]
  }
}
```

## Система спавна

### При входе игрока в чанк:
1. Отправляется событие спавна NPC
2. Загружаются ВСЕ NPC из базы данных
3. Отправляется пакет со всеми NPC клиенту
4. Chunk server обрабатывает спавн всех NPC

### Получение всех NPC:
```cpp
auto allNPCs = gameServices.getNPCManager().getNPCsByChunk(0, 0, 0); // параметры игнорируются
```

## Безопасность и производительность

### Thread Safety:
- Все операции в NPCManager защищены мьютексами
- Использование `std::lock_guard` для автоматического освобождения блокировок
- Безопасный доступ к данным из нескольких потоков

### Memory Management:
- RAII принципы для управления ресурсами
- Умные указатели для автоматического управления памятью
- Минимизация копирования данных через `std::move`

### Обработка ошибок:
- Проверка null значений из базы данных
- Graceful handling исключений
- Логирование всех ошибок

## Использование

### Загрузка NPC:
```cpp
gameServices.getNPCManager().loadNPCs();
```

### Получение всех NPC:
```cpp
auto allNPCs = gameServices.getNPCManager().getNPCsByChunk(0, 0, 0); // параметры игнорируются
```

### Отправка данных NPC:
```cpp
Event npcEvent(Event::GET_NPCS_LIST, clientID, NPCDataStruct(), clientSocket);
eventHandler.dispatchEvent(npcEvent);
```

## Расширение системы

Система спроектирована для легкого расширения:
- Добавление новых типов NPC
- Расширение атрибутов и навыков
- Добавление новых событий взаимодействия
- Интеграция с системой квестов и торговли

## Troubleshooting

### Частые проблемы:
1. **NPCs не загружаются** - проверить подключение к БД и структуру таблиц
2. **Ошибки компиляции** - убедиться что все заголовочные файлы включены
3. **Memory leaks** - проверить правильность освобождения ресурсов
4. **Race conditions** - убедиться в использовании мьютексов

### Логирование:
Система логирует все важные операции:
- Загрузку NPC из базы данных
- Отправку данных клиентам
- Ошибки и исключения