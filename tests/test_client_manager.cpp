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
