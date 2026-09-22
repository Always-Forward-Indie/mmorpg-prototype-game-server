// Unit tests for game ClientManager (Logger only + unconnected sockets).
#include "services/ClientManager.hpp"

#include <boost/asio.hpp>
#include <gtest/gtest.h>

namespace
{

using boost::asio::ip::tcp;

ClientDataStruct makeClient(int id)
{
    ClientDataStruct c;
    c.clientId = id;
    c.characterId = 100 + id;
    c.hash = "h" + std::to_string(id);
    return c;
}

struct GameClientFixture : ::testing::Test
{
    Logger logger{"test"};
    ClientManager mgr{logger};
    boost::asio::io_context ioc;
};

} // namespace

TEST_F(GameClientFixture, SetGetRemove)
{
    mgr.setClientData(makeClient(1));
    mgr.setClientData(makeClient(2));
    EXPECT_EQ(mgr.getClientData(1).characterId, 101);
    EXPECT_EQ(mgr.getClientData(1).hash, "h1");
    EXPECT_EQ(mgr.getClientData(424242).clientId, 0); // miss sentinel
    EXPECT_EQ(mgr.getClientsList().size(), 2u);
    mgr.removeClientData(1);
    EXPECT_EQ(mgr.getClientData(1).clientId, 0);
    EXPECT_EQ(mgr.getClientsList().size(), 1u);
    mgr.removeClientData(424242); // safe no-op
}

TEST_F(GameClientFixture, SocketMapping)
{
    mgr.setClientData(makeClient(1));
    auto s = std::make_shared<tcp::socket>(ioc);
    mgr.setClientSocket(1, s);
    EXPECT_EQ(mgr.getClientSocket(1), s);
    mgr.removeClientDataBySocket(s);
    EXPECT_EQ(mgr.getClientSocket(1), nullptr);
    EXPECT_EQ(mgr.getClientData(1).clientId, 0);
}

TEST_F(GameClientFixture, OverwriteOnResend)
{
    mgr.setClientData(makeClient(1));
    ClientDataStruct upd = makeClient(1);
    upd.hash = "new";
    mgr.setClientData(upd);
    EXPECT_EQ(mgr.getClientData(1).hash, "new");
    EXPECT_EQ(mgr.getClientsList().size(), 1u);
}

TEST_F(GameClientFixture, StaleDisconnectKeepsReboundSession)
{
    // Old socket disconnect arriving after a re-bind must not evict.
    mgr.setClientData(makeClient(1));
    auto oldSock = std::make_shared<tcp::socket>(ioc);
    mgr.setClientSocket(1, oldSock);
    auto newSock = std::make_shared<tcp::socket>(ioc);
    mgr.setClientSocket(1, newSock);
    EXPECT_EQ(mgr.getClientSocket(1), newSock);
    mgr.removeClientDataBySocket(oldSock); // late FIN for the old link
    EXPECT_EQ(mgr.getClientData(1).clientId, 1);
    EXPECT_EQ(mgr.getClientSocket(1), newSock);
    mgr.removeClientDataBySocket(newSock); // live link disconnect evicts
    EXPECT_EQ(mgr.getClientData(1).clientId, 0);
}

TEST_F(GameClientFixture, SweepRespectsThreshold)
{
    mgr.setClientData(makeClient(1));
    mgr.setClientData(makeClient(2));
    // Huge threshold: nothing silent yet.
    EXPECT_TRUE(mgr.sweepSilentClients(3600000).empty());
    EXPECT_EQ(mgr.getClientsList().size(), 2u);
    // Zero threshold: everything is already silent.
    auto removed = mgr.sweepSilentClients(0);
    EXPECT_EQ(removed.size(), 2u);
    EXPECT_TRUE(mgr.getClientsList().empty());
}

TEST_F(GameClientFixture, TouchRefreshesLiveness)
{
    mgr.setClientData(makeClient(1));
    mgr.touchClientData(1);
    mgr.touchClientData(424242); // unknown id: safe no-op
    EXPECT_EQ(mgr.getClientData(1).clientId, 1);
    EXPECT_TRUE(mgr.sweepSilentClients(3600000).empty());
}
