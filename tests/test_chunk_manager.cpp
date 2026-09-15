// Unit tests for game ChunkManager generation stamps.
//
// A stale async disconnect (old socket, processed after a reconnect) must
// never wipe the live chunk registration — this exact race caused a total
// join outage (game returned chunkId 0) under load. See SERVER_BUGS.md.
#include "services/ChunkManager.hpp"

#include <boost/asio.hpp>
#include <cstdio>

static int failures = 0;

#define CHECK(cond, msg)                                                \
    do                                                                  \
    {                                                                   \
        if (!(cond))                                                    \
        {                                                               \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, (msg)); \
            ++failures;                                                 \
        }                                                               \
    } while (0)

int main()
{
    Logger logger("test");
    ChunkManager cm(logger);
    boost::asio::io_context ioc;
    using boost::asio::ip::tcp;

    auto s1 = std::make_shared<tcp::socket>(ioc);
    auto s2 = std::make_shared<tcp::socket>(ioc);

    ChunkInfoStruct info;
    info.id = 1;
    info.ip = "127.0.0.1";
    info.port = 27017;

    // 1. Register, resolve by id and by socket.
    info.socket = s1;
    cm.addChunkInfo(info);
    CHECK(cm.getChunkById(1).port == 27017, "registered chunk resolves by id");
    CHECK(cm.getChunkBySocket(s1).id == 1, "registered chunk resolves by socket");

    // 2. Reconnect (new socket, same id) replaces the mapping.
    info.socket = s2;
    cm.addChunkInfo(info);
    CHECK(cm.getChunkById(1).socket == s2, "reconnect replaces socket");
    CHECK(cm.getChunkBySocket(s2).id == 1, "reconnect resolves by new socket");

    // 3. THE BUG: late disconnect for the STALE socket must not wipe live.
    cm.removeChunkServerDataBySocket(s1);
    CHECK(cm.getChunkById(1).port == 27017, "stale disconnect keeps live registration");
    CHECK(cm.getChunkBySocket(s2).id == 1, "live socket still resolves");

    // 4. Genuine disconnect for the live socket removes.
    cm.removeChunkServerDataBySocket(s2);
    CHECK(cm.getChunkById(1).id == 0, "live disconnect removes");

    // 5. Unknown socket / id are safe no-ops.
    auto s3 = std::make_shared<tcp::socket>(ioc);
    cm.removeChunkServerDataBySocket(s3);
    cm.removeChunkServerDataById(999);
    cm.removeChunkServerDataBySocket(nullptr);

    // 6. removeChunkServerDataById still works.
    info.socket = s1;
    cm.addChunkInfo(info);
    cm.removeChunkServerDataById(1);
    CHECK(cm.getChunkById(1).id == 0, "remove by id works");

    if (failures == 0)
        std::printf("ALL OK\n");
    else
        std::printf("%d FAILURES\n", failures);
    return failures == 0 ? 0 : 1;
}
