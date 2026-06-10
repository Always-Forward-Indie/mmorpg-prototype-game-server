# MMOGameServer — Game logic + DB proxy

Port: `27016`

## Prod (Release, без hot-reload)

```bash
cd mmorpg-prototype-game-server
docker-compose up --build -d
```

Собирает образ с `CMAKE_BUILD_TYPE=Release`, стрипает бинарник.

## Dev (Debug, hot-reload через watchexec)

```bash
cd mmorpg-prototype-game-server
docker-compose -f docker-compose.dev.yml up --build -d
```

Собирает с `CMAKE_BUILD_TYPE=Debug`, включает watchexec.

## Порядок запуска

Запускать **после** логин-сервера (нужна сеть `mmo_network` и PostgreSQL).

После гейм-сервера запускай chunk-server.

## Конфигурация

`config.json`:
- `max_clients` — лимит одновременных подключений
- `database` — доступы к PostgreSQL (host: `db`)
