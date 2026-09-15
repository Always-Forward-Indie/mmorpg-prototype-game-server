# Game unit tests (no gtest, no CMake changes)

Same pattern as chunk `tests/`: one file per unit, `main` returns 0/1,
compile & run inside the dev container:

```bash
C=mmorpg-prototype-game-server-game-server-1
cd ~/projects/mmorpg-prototype/mmorpg-prototype-game-server
docker cp tests/test_chunk_manager.cpp $C:/tmp/test_chunk_manager.cpp
docker exec $C g++ -std=c++17 -I/usr/src/app/include /tmp/test_chunk_manager.cpp \
  /usr/src/app/src/services/ChunkManager.cpp \
  /usr/src/app/src/utils/Logger.cpp \
  -o /tmp/test_chunk_manager -lspdlog -lfmt -pthread \
  && docker exec $C /tmp/test_chunk_manager
```

Expected: `ALL OK`.
