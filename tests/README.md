# Game unit tests (gtest)

Same pattern as chunk `tests/`: one file per unit (`test_<name>.cpp`),
built as `unit_tests` by the normal server build.

## Run (from inside the dev container)

```bash
ctest --test-dir /usr/src/app/build --output-on-failure
```

## Add a test

1. Create `tests/test_<name>.cpp` with `TEST`/`TEST_F` cases.
2. Append the file plus the needed `../src/**/*.cpp` to `tests/CMakeLists.txt`.
3. Reconfigure happens automatically via watchexec; for a manual check use a
   separate build dir (never a second `make` inside `/usr/src/app/build`
   while watchexec watches it).

## Rules

- Unit-test managers/services only (pure logic + Logger). Anything needing
  asio sockets, DB, or live game state belongs to contract tests
  (`Tests/Contract`) or bots (`Tools/Bots`), not here.
- `tests/` is mounted into the container but ignored by `watch_and_run.sh`,
  so editing tests never restarts the server.

## Sanitizers (on demand, no CI)

ASan is on by default in Debug builds (`-fsanitize=address` in the root
`CMakeLists.txt`). TSan uses a throwaway build dir in the live dev
container — never add sanitizer flags to `CMakeLists.txt`, and never run a
second `make` inside the watched `/usr/src/app/build` (the watchexec
watcher builds there; parallel makes corrupt the link step).

```bash
# TSan unit run (game-server-1 container, separate build dir).
# Two environment quirks, same as chunk (see chunk tests/README.md):
#  1. TSan is incompatible with ASan: use a custom build type (e.g. -DCMAKE_BUILD_TYPE=TSan)
#     so the hardcoded Debug ASan flags do not apply.
#  2. This toolchain+Docker needs non-PIE binaries (-fno-pie -no-pie) plus
#     setarch, otherwise TSan dies at startup with "unexpected memory mapping".
docker exec mmorpg-prototype-game-server-game-server-1 cmake -S /usr/src/app -B /tmp/gbuild-tsan \
  -DCMAKE_BUILD_TYPE=TSan \
  -DCMAKE_CXX_FLAGS="-fsanitize=thread -fno-omit-frame-pointer -g -fno-pie" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=thread -no-pie"
docker exec mmorpg-prototype-game-server-game-server-1 cmake --build /tmp/gbuild-tsan --target unit_tests -j4
docker exec mmorpg-prototype-game-server-game-server-1 \
  env TSAN_OPTIONS="suppressions=/usr/src/app/tests/TSanSuppressions.txt" \
  setarch x86_64 -R /tmp/gbuild-tsan/tests/unit_tests
```

Gate: zero non-suppressed reports. Suppressions (`tests/TSanSuppressions.txt`)
cover only the triaged spdlog-teardown artifact (ported from chunk); any new
stack shape is guilty until proven otherwise.
