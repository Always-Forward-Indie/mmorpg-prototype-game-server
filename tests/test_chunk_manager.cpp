// Unit tests for game ChunkManager generation stamps (gtest).
//
// A stale async disconnect (old socket, processed after a reconnect) must
// never wipe the live chunk registration — this exact race caused a total
// join outage (game returned chunkId 0) under load.
// Migrated from the hand-rolled CHECK-macro style to gtest; same 6 cases,
// now independent (each builds its own fixture state).
#include "services/ChunkManager.hpp"

#include <boost/asio.hpp>
#include <gtest/gtest.h>

namespace
{

using boost::asio::ip::tcp;

struct ChunkManagerFixture : ::testing::Test
{
    Logger logger{"test"};
    ChunkManager cm{logger};
    boost::asio::io_context ioc;

    std::shared_ptr<tcp::socket> makeSocket()
    {
        return std::make_shared<tcp::socket>(ioc);
    }

    ChunkInfoStruct makeInfo(std::shared_ptr<tcp::socket> s)
    {
        ChunkInfoStruct info;
        info.id = 1;
        info.ip = "127.0.0.1";
        info.port = 27017;
        info.socket = s;
        return info;
    }
};

} // namespace

TEST_F(ChunkManagerFixture, RegisterResolvesByIdAndSocket)
{
    auto s1 = makeSocket();
    cm.addChunkInfo(makeInfo(s1));
    EXPECT_EQ(cm.getChunkById(1).port, 27017);
    EXPECT_EQ(cm.getChunkBySocket(s1).id, 1);
}

TEST_F(ChunkManagerFixture, ReconnectReplacesSocket)
{
    auto s1 = makeSocket();
    auto s2 = makeSocket();
    cm.addChunkInfo(makeInfo(s1));
    cm.addChunkInfo(makeInfo(s2));
    EXPECT_EQ(cm.getChunkById(1).socket, s2);
    EXPECT_EQ(cm.getChunkBySocket(s2).id, 1);
}

TEST_F(ChunkManagerFixture, StaleDisconnectKeepsLiveRegistration)
{
    auto s1 = makeSocket();
    auto s2 = makeSocket();
    cm.addChunkInfo(makeInfo(s1));
    cm.addChunkInfo(makeInfo(s2)); // reconnect
    cm.removeChunkServerDataBySocket(s1); // late disconnect for the STALE socket
    EXPECT_EQ(cm.getChunkById(1).port, 27017);
    EXPECT_EQ(cm.getChunkBySocket(s2).id, 1);
}

TEST_F(ChunkManagerFixture, LiveDisconnectRemoves)
{
    auto s1 = makeSocket();
    auto s2 = makeSocket();
    cm.addChunkInfo(makeInfo(s1));
    cm.addChunkInfo(makeInfo(s2));
    cm.removeChunkServerDataBySocket(s2);
    EXPECT_EQ(cm.getChunkById(1).id, 0);
}

TEST_F(ChunkManagerFixture, UnknownSocketAndIdAreSafeNoOps)
{
    auto s1 = makeSocket();
    cm.addChunkInfo(makeInfo(s1));
    auto s3 = makeSocket();
    cm.removeChunkServerDataBySocket(s3);
    cm.removeChunkServerDataById(999);
    cm.removeChunkServerDataBySocket(nullptr);
    EXPECT_EQ(cm.getChunkById(1).port, 27017); // registration untouched
}

TEST_F(ChunkManagerFixture, RemoveByIdWorks)
{
    auto s1 = makeSocket();
    cm.addChunkInfo(makeInfo(s1));
    cm.removeChunkServerDataById(1);
    EXPECT_EQ(cm.getChunkById(1).id, 0);
}

TEST_F(ChunkManagerFixture, ZeroIdHandshakeNeverRegisters)
{
    // Wave 1.7: id 0 is the "missing header.id" default, never a real chunk.
    // Registering it poisoned joinGameClient with CHUNKID_0 until a manual
    // chunk reboot — now rejected at both layers (handler + manager).
    auto s1 = makeSocket();
    ChunkInfoStruct bad = makeInfo(s1);
    bad.id = 0;
    cm.addChunkInfo(bad);
    // Nothing stored at all: even a sweep-everything finds no id-0 entry...
    EXPECT_TRUE(cm.sweepSilentChunks(0).empty());
    EXPECT_EQ(cm.getChunkBySocket(s1).id, 0); // ...and no reverse mapping.
    std::vector<ChunkInfoStruct> batch{bad};
    cm.addListOfAllChunks(batch);
    EXPECT_TRUE(cm.sweepSilentChunks(0).empty());
    // A valid handshake on the same socket still registers fine.
    cm.addChunkInfo(makeInfo(s1));
    EXPECT_EQ(cm.getChunkById(1).port, 27017);
}

TEST_F(ChunkManagerFixture, SweepRemovesOnlySilentChunks)
{
    // Fresh registration survives a generous threshold...
    auto s1 = makeSocket();
    cm.addChunkInfo(makeInfo(s1));
    EXPECT_TRUE(cm.sweepSilentChunks(60000).empty());
    EXPECT_EQ(cm.getChunkById(1).port, 27017);
    // ...but a zero threshold (silent since forever, in test terms) sweeps.
    // Reverse mappings go with it, so a later disconnect is a safe no-op.
    auto removed = cm.sweepSilentChunks(0);
    ASSERT_EQ(removed.size(), 1u);
    EXPECT_EQ(removed[0], 1);
    EXPECT_EQ(cm.getChunkById(1).id, 0);
    EXPECT_EQ(cm.getChunkBySocket(s1).id, 0);
    cm.removeChunkServerDataBySocket(s1);
    cm.removeChunkServerDataById(1);
    EXPECT_EQ(cm.getChunkById(1).id, 0);
}

TEST_F(ChunkManagerFixture, ReRegisterRefreshesHeartbeat)
{
    // Re-registration (chunk heartbeat, every 60s) restamps the entry, so a
    // live chunk is never swept: add, sweep-all, re-add, sweep-all again.
    auto s1 = makeSocket();
    cm.addChunkInfo(makeInfo(s1));
    EXPECT_EQ(cm.sweepSilentChunks(0).size(), 1u);
    auto s2 = makeSocket();
    cm.addChunkInfo(makeInfo(s2)); // heartbeat re-assert
    EXPECT_EQ(cm.getChunkById(1).socket, s2);
    EXPECT_TRUE(cm.sweepSilentChunks(60000).empty());
}

TEST_F(ChunkManagerFixture, ResolveLiveSocketPrefersCurrentRegistration)
{
    // Event queued on s1, chunk reconnected on s2 before the game answers:
    // answering on s1 would vanish into a half-open stale socket.
    auto s1 = makeSocket();
    auto s2 = makeSocket();
    cm.addChunkInfo(makeInfo(s1));
    cm.addChunkInfo(makeInfo(s2)); // reconnect
    EXPECT_EQ(cm.resolveLiveSocket(s1), s2);
}

TEST_F(ChunkManagerFixture, ResolveLiveSocketFallsBackToHint)
{
    // Hint socket never registered (unknown chunk): nothing better known,
    // return the hint itself; sendResponse still guards closed sockets.
    auto s1 = makeSocket();
    EXPECT_EQ(cm.resolveLiveSocket(s1), s1);
}

TEST_F(ChunkManagerFixture, ResolveLiveSocketNullWhenNothingKnown)
{
    EXPECT_EQ(cm.resolveLiveSocket(nullptr), nullptr);
}
