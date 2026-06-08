#include <gtest/gtest.h>

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>

#include "../RoomManager.h"
#include "../Room.h"
#include "../ErrorCode.h"

class RoomManagerTest : public ::testing::Test
{
protected:
    RoomManager roomMgr;

    static constexpr INT32 BEGIN_ROOM = 0;
    static constexpr INT32 MAX_ROOMS = 10;
    static constexpr INT32 MAX_ROOM_USERS = 50;

    void SetUp() override
    {
        // Install mock callbacks before Init (Init copies them to each Room)
        roomMgr.SendPacketFunc = [](UINT32, UINT32, UINT32, char*) {};
        roomMgr.FreeJobFunc = [](PacketJob*) {};
        roomMgr.Init(BEGIN_ROOM, MAX_ROOMS, MAX_ROOM_USERS);
    }
};

// ==================== GetRoomByNumber bounds ====================

TEST_F(RoomManagerTest, ValidRoomNumberReturnsNonNull)
{
    for (INT32 i = BEGIN_ROOM; i < BEGIN_ROOM + MAX_ROOMS; ++i)
    {
        SCOPED_TRACE(i);
        Room* room = roomMgr.GetRoomByNumber(i);
        EXPECT_NE(room, nullptr);
    }
}

TEST_F(RoomManagerTest, RoomNumberMatchesInit)
{
    for (INT32 i = BEGIN_ROOM; i < BEGIN_ROOM + MAX_ROOMS; ++i)
    {
        SCOPED_TRACE(i);
        Room* room = roomMgr.GetRoomByNumber(i);
        ASSERT_NE(room, nullptr);
        EXPECT_EQ(room->GetRoomNumber(), i);
    }
}

TEST_F(RoomManagerTest, NegativeRoomNumberReturnsNull)
{
    EXPECT_EQ(roomMgr.GetRoomByNumber(-1), nullptr);
}

TEST_F(RoomManagerTest, ExceedMaxRoomNumberReturnsNull)
{
    // Room numbers are [0, 10), so 10 is out of range
    EXPECT_EQ(roomMgr.GetRoomByNumber(MAX_ROOMS), nullptr);
    EXPECT_EQ(roomMgr.GetRoomByNumber(MAX_ROOMS + 100), nullptr);
}

TEST_F(RoomManagerTest, BoundaryRoomNumbers)
{
    // First valid room
    EXPECT_NE(roomMgr.GetRoomByNumber(BEGIN_ROOM), nullptr);

    // Last valid room
    EXPECT_NE(roomMgr.GetRoomByNumber(BEGIN_ROOM + MAX_ROOMS - 1), nullptr);

    // One before first
    EXPECT_EQ(roomMgr.GetRoomByNumber(BEGIN_ROOM - 1), nullptr);

    // One after last
    EXPECT_EQ(roomMgr.GetRoomByNumber(BEGIN_ROOM + MAX_ROOMS), nullptr);
}

// ==================== Non-zero begin room number ====================

TEST(RoomManagerOffset, OffsetRoomNumbers)
{
    RoomManager mgr;
    mgr.SendPacketFunc = [](UINT32, UINT32, UINT32, char*) {};
    mgr.FreeJobFunc = [](PacketJob*) {};
    mgr.Init(100, 5, 10);  // rooms 100~104

    EXPECT_EQ(mgr.GetRoomByNumber(99), nullptr);   // below range
    EXPECT_NE(mgr.GetRoomByNumber(100), nullptr);   // first valid
    EXPECT_NE(mgr.GetRoomByNumber(104), nullptr);   // last valid
    EXPECT_EQ(mgr.GetRoomByNumber(105), nullptr);   // above range

    Room* room = mgr.GetRoomByNumber(102);
    ASSERT_NE(room, nullptr);
    EXPECT_EQ(room->GetRoomNumber(), 102);
}

// ==================== Room functionality via RoomManager ====================

TEST_F(RoomManagerTest, EnterUserViaRoomFromManager)
{
    Room* room = roomMgr.GetRoomByNumber(3);
    ASSERT_NE(room, nullptr);

    User user;
    user.Init(0);
    char id[] = "rmtest";
    user.SetLogin(id);

    UINT16 result = room->EnterUser(&user);
    EXPECT_EQ(result, (UINT16)ERROR_CODE::NONE);
}
