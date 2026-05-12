#include <gtest/gtest.h>
#include "../User.h"

class UserTest : public ::testing::Test
{
protected:
    User user;

    void SetUp() override
    {
        user.Init(0);
    }
};

// ==================== Initial state ====================

TEST_F(UserTest, InitialStateIsNone)
{
    EXPECT_EQ(user.GetDomainState(), User::DOMAIN_STATE::NONE);
}

TEST_F(UserTest, InitialRoomIndexIsInvalid)
{
    EXPECT_EQ(user.GetRoomIndex(), -1);
}

TEST_F(UserTest, InitialNetConnIndex)
{
    EXPECT_EQ(user.GetNetConnIndex(), 0);
}

TEST_F(UserTest, InitialUserIDIsEmpty)
{
    EXPECT_EQ(user.GetUserID(), "");
}

// ==================== State transitions ====================

TEST_F(UserTest, SetLoginChangesState)
{
    char id[] = "player1";
    user.SetLogin(id);
    EXPECT_EQ(user.GetDomainState(), User::DOMAIN_STATE::LOGIN);
    EXPECT_EQ(user.GetUserID(), "player1");
}

TEST_F(UserTest, EnterRoomChangesState)
{
    char id[] = "player1";
    user.SetLogin(id);
    user.EnterRoom(5);

    EXPECT_EQ(user.GetDomainState(), User::DOMAIN_STATE::ROOM);
    EXPECT_EQ(user.GetRoomIndex(), 5);
}

TEST_F(UserTest, ResetRoomClearsRoomIndex)
{
    char id[] = "player1";
    user.SetLogin(id);
    user.EnterRoom(3);
    user.ResetRoom();

    EXPECT_EQ(user.GetRoomIndex(), -1);
    // Note: ResetRoom does NOT change domain state
}

TEST_F(UserTest, ClearResetsEverything)
{
    char id[] = "player1";
    user.SetLogin(id);
    user.EnterRoom(7);
    user.Clear();

    EXPECT_EQ(user.GetDomainState(), User::DOMAIN_STATE::NONE);
    EXPECT_EQ(user.GetRoomIndex(), -1);
    EXPECT_EQ(user.GetUserID(), "");
}

// ==================== Session generation ====================

TEST_F(UserTest, SessionGenerationDefault)
{
    EXPECT_EQ(user.GetSessionGeneration(), 0u);
}

TEST_F(UserTest, SetSessionGeneration)
{
    user.SetSessionGeneration(42);
    EXPECT_EQ(user.GetSessionGeneration(), 42u);
}

// ==================== Disconnecting flag ====================

TEST_F(UserTest, IsDisconnectingDefault)
{
    EXPECT_FALSE(user.IsDisconnecting());
}

TEST_F(UserTest, SetDisconnecting)
{
    user.SetDisconnecting();
    EXPECT_TRUE(user.IsDisconnecting());
}

TEST_F(UserTest, ClearResetsDisconnecting)
{
    user.SetDisconnecting();
    user.Clear();
    EXPECT_FALSE(user.IsDisconnecting());
}

// ==================== Packet RingBuffer ====================

TEST_F(UserTest, SetPacketDataAndGetPacket)
{
    // Build a minimal valid packet
    LOGIN_REQUEST_PACKET pkt = {};
    pkt.PacketLength = sizeof(LOGIN_REQUEST_PACKET);
    pkt.PacketId = (UINT16)PACKET_ID::LOGIN_REQUEST;
    pkt.PacketType = 0;
    strncpy_s(pkt.UserID, "tester", _TRUNCATE);
    strncpy_s(pkt.UserPW, "pw123", _TRUNCATE);

    // Write packet data to user buffer
    bool ok = user.SetPacketData(sizeof(pkt), reinterpret_cast<char*>(&pkt));
    EXPECT_TRUE(ok);

    // Read packet back
    char outBuf[MAX_SINGLE_PACKET_SIZE] = {};
    PacketInfo info = user.GetPacket(outBuf, sizeof(outBuf));

    EXPECT_EQ(info.PacketId, (UINT16)PACKET_ID::LOGIN_REQUEST);
    EXPECT_EQ(info.DataSize, sizeof(LOGIN_REQUEST_PACKET));

    auto* parsed = reinterpret_cast<LOGIN_REQUEST_PACKET*>(outBuf);
    EXPECT_STREQ(parsed->UserID, "tester");
    EXPECT_STREQ(parsed->UserPW, "pw123");
}

TEST_F(UserTest, GetPacketFromEmptyBuffer)
{
    char outBuf[MAX_SINGLE_PACKET_SIZE] = {};
    PacketInfo info = user.GetPacket(outBuf, sizeof(outBuf));
    EXPECT_EQ(info.PacketId, 0u);
    EXPECT_EQ(info.DataSize, 0u);
}

TEST_F(UserTest, SetPacketDataNullptr)
{
    // nullptr should not crash, returns true (no-op)
    bool ok = user.SetPacketData(10, nullptr);
    EXPECT_TRUE(ok);
}

TEST_F(UserTest, MultiplePacketsInBuffer)
{
    // Write two packets
    ROOM_ENTER_REQUEST_PACKET pkt1 = {};
    pkt1.PacketLength = sizeof(pkt1);
    pkt1.PacketId = (UINT16)PACKET_ID::ROOM_ENTER_REQUEST;
    pkt1.RoomNumber = 7;

    ROOM_LEAVE_REQUEST_PACKET pkt2 = {};
    pkt2.PacketLength = sizeof(pkt2);
    pkt2.PacketId = (UINT16)PACKET_ID::ROOM_LEAVE_REQUEST;
    pkt2.RoomNumber = 7;

    user.SetPacketData(sizeof(pkt1), reinterpret_cast<char*>(&pkt1));
    user.SetPacketData(sizeof(pkt2), reinterpret_cast<char*>(&pkt2));

    // Read first packet
    char outBuf[MAX_SINGLE_PACKET_SIZE] = {};
    PacketInfo info1 = user.GetPacket(outBuf, sizeof(outBuf));
    EXPECT_EQ(info1.PacketId, (UINT16)PACKET_ID::ROOM_ENTER_REQUEST);

    // Read second packet
    PacketInfo info2 = user.GetPacket(outBuf, sizeof(outBuf));
    EXPECT_EQ(info2.PacketId, (UINT16)PACKET_ID::ROOM_LEAVE_REQUEST);

    // No more packets
    PacketInfo info3 = user.GetPacket(outBuf, sizeof(outBuf));
    EXPECT_EQ(info3.PacketId, 0u);
}

// ==================== TCP Fragmentation reassembly ====================

TEST_F(UserTest, FragmentedPacketReassembly)
{
    // Build a 71-byte LOGIN_REQUEST_PACKET
    LOGIN_REQUEST_PACKET pkt = {};
    pkt.PacketLength = sizeof(LOGIN_REQUEST_PACKET);
    pkt.PacketId = (UINT16)PACKET_ID::LOGIN_REQUEST;
    pkt.PacketType = 0;
    strncpy_s(pkt.UserID, "fraguser", _TRUNCATE);
    strncpy_s(pkt.UserPW, "fragpw", _TRUNCATE);

    char* raw = reinterpret_cast<char*>(&pkt);
    const UINT32 totalSize = sizeof(LOGIN_REQUEST_PACKET);  // 71 bytes

    // Fragment 1: first 30 bytes (incomplete packet)
    const UINT32 frag1 = 30;
    bool ok1 = user.SetPacketData(frag1, raw);
    EXPECT_TRUE(ok1);

    // Attempt to read — should return empty (not enough data)
    char outBuf[MAX_SINGLE_PACKET_SIZE] = {};
    PacketInfo info1 = user.GetPacket(outBuf, sizeof(outBuf));
    EXPECT_EQ(info1.PacketId, 0u);
    EXPECT_EQ(info1.DataSize, 0u);

    // Fragment 2: remaining 41 bytes
    const UINT32 frag2 = totalSize - frag1;
    bool ok2 = user.SetPacketData(frag2, raw + frag1);
    EXPECT_TRUE(ok2);

    // Now the full packet should be readable
    PacketInfo info2 = user.GetPacket(outBuf, sizeof(outBuf));
    EXPECT_EQ(info2.PacketId, (UINT16)PACKET_ID::LOGIN_REQUEST);
    EXPECT_EQ(info2.DataSize, totalSize);

    auto* parsed = reinterpret_cast<LOGIN_REQUEST_PACKET*>(outBuf);
    EXPECT_STREQ(parsed->UserID, "fraguser");
    EXPECT_STREQ(parsed->UserPW, "fragpw");
}

TEST_F(UserTest, FragmentedHeaderOnly)
{
    // Send only the 5-byte header without body — GetPacket should wait
    LOGIN_REQUEST_PACKET pkt = {};
    pkt.PacketLength = sizeof(LOGIN_REQUEST_PACKET);  // claims 71 bytes
    pkt.PacketId = (UINT16)PACKET_ID::LOGIN_REQUEST;

    char* raw = reinterpret_cast<char*>(&pkt);

    // Write only the header (5 bytes)
    user.SetPacketData(PACKET_HEADER_LENGTH, raw);

    char outBuf[MAX_SINGLE_PACKET_SIZE] = {};
    PacketInfo info = user.GetPacket(outBuf, sizeof(outBuf));
    EXPECT_EQ(info.PacketId, 0u);  // header says 71 bytes but only 5 arrived

    // Write the remaining body
    user.SetPacketData(sizeof(LOGIN_REQUEST_PACKET) - PACKET_HEADER_LENGTH,
                       raw + PACKET_HEADER_LENGTH);

    PacketInfo info2 = user.GetPacket(outBuf, sizeof(outBuf));
    EXPECT_EQ(info2.PacketId, (UINT16)PACKET_ID::LOGIN_REQUEST);
    EXPECT_EQ(info2.DataSize, sizeof(LOGIN_REQUEST_PACKET));
}

TEST_F(UserTest, ThreeFragmentsReassembly)
{
    // Split a 71-byte packet into 3 fragments: 10 + 25 + 36
    LOGIN_REQUEST_PACKET pkt = {};
    pkt.PacketLength = sizeof(LOGIN_REQUEST_PACKET);
    pkt.PacketId = (UINT16)PACKET_ID::LOGIN_REQUEST;
    strncpy_s(pkt.UserID, "threefrag", _TRUNCATE);
    strncpy_s(pkt.UserPW, "pass3", _TRUNCATE);

    char* raw = reinterpret_cast<char*>(&pkt);
    char outBuf[MAX_SINGLE_PACKET_SIZE] = {};

    // Fragment 1: 10 bytes
    user.SetPacketData(10, raw);
    EXPECT_EQ(user.GetPacket(outBuf, sizeof(outBuf)).PacketId, 0u);

    // Fragment 2: 25 bytes (total 35)
    user.SetPacketData(25, raw + 10);
    EXPECT_EQ(user.GetPacket(outBuf, sizeof(outBuf)).PacketId, 0u);

    // Fragment 3: 36 bytes (total 71 — complete)
    user.SetPacketData(36, raw + 35);
    PacketInfo info = user.GetPacket(outBuf, sizeof(outBuf));
    EXPECT_EQ(info.PacketId, (UINT16)PACKET_ID::LOGIN_REQUEST);
    EXPECT_EQ(info.DataSize, sizeof(LOGIN_REQUEST_PACKET));

    auto* parsed = reinterpret_cast<LOGIN_REQUEST_PACKET*>(outBuf);
    EXPECT_STREQ(parsed->UserID, "threefrag");
    EXPECT_STREQ(parsed->UserPW, "pass3");
}
