#include <gtest/gtest.h>

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>

#include "../Room.h"
#include "../User.h"
#include "../Packet.h"
#include "../ErrorCode.h"

#include <vector>
#include <tuple>
#include <string>
#include <cstring>

// ==================== Test fixture ====================

class RoomTest : public ::testing::Test
{
protected:
    Room room;
    static constexpr INT32 MAX_ROOM_USERS = 3;

    // Mock capture for SendPacketFunc
    struct SendCall
    {
        UINT32 connIndex;
        UINT32 generation;
        UINT32 dataSize;
        std::vector<char> data;
    };
    std::vector<SendCall> sendCalls;

    // Users for testing
    User users[5];

    void SetUp() override
    {
        room.Init(1, MAX_ROOM_USERS);

        // Install mock SendPacketFunc
        room.SendPacketFunc = [this](UINT32 connIdx, UINT32 gen, UINT32 size, char* pData)
        {
            SendCall call;
            call.connIndex = connIdx;
            call.generation = gen;
            call.dataSize = size;
            call.data.assign(pData, pData + size);
            sendCalls.push_back(call);
        };

        // Install mock FreeJobFunc (no-op)
        room.FreeJobFunc = [](PacketJob*) {};

        // Initialize users with distinct indices
        for (int i = 0; i < 5; ++i)
        {
            users[i].Init(static_cast<UINT32>(i + 10));
            char id[32];
            sprintf_s(id, "user%d", i);
            users[i].SetLogin(id);
        }
    }
};

// ==================== Enter/Leave basics ====================

TEST_F(RoomTest, EnterUserSuccess)
{
    UINT16 result = room.EnterUser(&users[0]);
    EXPECT_EQ(result, (UINT16)ERROR_CODE::NONE);
}

TEST_F(RoomTest, EnterMultipleUsersUpToMax)
{
    for (int i = 0; i < MAX_ROOM_USERS; ++i)
    {
        SCOPED_TRACE(i);
        UINT16 result = room.EnterUser(&users[i]);
        EXPECT_EQ(result, (UINT16)ERROR_CODE::NONE);
    }
}

TEST_F(RoomTest, EnterUserFullRoomReturnsError)
{
    // Fill the room to capacity
    for (int i = 0; i < MAX_ROOM_USERS; ++i)
        room.EnterUser(&users[i]);

    // Next enter should fail
    UINT16 result = room.EnterUser(&users[3]);
    EXPECT_EQ(result, (UINT16)ERROR_CODE::ENTER_ROOM_FULL_USER);
}

TEST_F(RoomTest, EnterAfterLeaveSucceeds)
{
    // Fill room
    for (int i = 0; i < MAX_ROOM_USERS; ++i)
        room.EnterUser(&users[i]);

    // Remove one user
    room.LeaveUser(&users[1]);

    // Now a new user can enter
    UINT16 result = room.EnterUser(&users[3]);
    EXPECT_EQ(result, (UINT16)ERROR_CODE::NONE);
}

TEST_F(RoomTest, LeaveUserNotInRoomSafe)
{
    // Leaving a user that was never added should not crash
    room.EnterUser(&users[0]);
    room.LeaveUser(&users[1]);  // users[1] was never in the room
    // No crash = pass
}

TEST_F(RoomTest, LeaveUserDecrementsCount)
{
    room.EnterUser(&users[0]);
    room.EnterUser(&users[1]);
    room.LeaveUser(&users[0]);

    // After removing one, we should be able to add 2 more (capacity=3, current=1)
    UINT16 r1 = room.EnterUser(&users[2]);
    UINT16 r2 = room.EnterUser(&users[3]);
    EXPECT_EQ(r1, (UINT16)ERROR_CODE::NONE);
    EXPECT_EQ(r2, (UINT16)ERROR_CODE::NONE);

    // Now full again
    UINT16 r3 = room.EnterUser(&users[4]);
    EXPECT_EQ(r3, (UINT16)ERROR_CODE::ENTER_ROOM_FULL_USER);
}

// ==================== FindUserByClientIndex ====================

TEST_F(RoomTest, FindUserByClientIndexFound)
{
    room.EnterUser(&users[0]);
    User* found = room.FindUserByClientIndex(users[0].GetNetConnIndex());
    EXPECT_EQ(found, &users[0]);
}

TEST_F(RoomTest, FindUserByClientIndexNotFound)
{
    room.EnterUser(&users[0]);
    User* found = room.FindUserByClientIndex(999);
    EXPECT_EQ(found, nullptr);
}

TEST_F(RoomTest, FindUserAfterLeaveReturnsNull)
{
    room.EnterUser(&users[0]);
    room.LeaveUser(&users[0]);
    User* found = room.FindUserByClientIndex(users[0].GetNetConnIndex());
    EXPECT_EQ(found, nullptr);
}

// ==================== Broadcasting (NotifyChat) ====================

TEST_F(RoomTest, NotifyChatBroadcastsToAllUsers)
{
    room.EnterUser(&users[0]);
    room.EnterUser(&users[1]);
    room.EnterUser(&users[2]);

    ROOM_CHAT_REQUEST_PACKET chatReq = {};
    chatReq.PacketLength = sizeof(chatReq);
    chatReq.PacketId = (UINT16)PACKET_ID::ROOM_CHAT_REQUEST;
    strncpy_s(chatReq.Message, "hello world", _TRUNCATE);

    sendCalls.clear();
    room.NotifyChat(users[0].GetNetConnIndex(), "user0", &chatReq);

    // SendPacketFunc should be called for ALL users (skip_=false in NotifyChat)
    EXPECT_EQ(sendCalls.size(), 3u);
}

TEST_F(RoomTest, NotifyChatContainsCorrectPacketData)
{
    room.EnterUser(&users[0]);

    ROOM_CHAT_REQUEST_PACKET chatReq = {};
    chatReq.PacketLength = sizeof(chatReq);
    chatReq.PacketId = (UINT16)PACKET_ID::ROOM_CHAT_REQUEST;
    strncpy_s(chatReq.Message, "test msg", _TRUNCATE);

    sendCalls.clear();
    room.NotifyChat(users[0].GetNetConnIndex(), "user0", &chatReq);

    ASSERT_EQ(sendCalls.size(), 1u);

    // Verify the sent data is a valid ROOM_CHAT_NOTIFY_PACKET
    EXPECT_EQ(sendCalls[0].dataSize, sizeof(ROOM_CHAT_NOTIFY_PACKET));

    auto* notify = reinterpret_cast<ROOM_CHAT_NOTIFY_PACKET*>(sendCalls[0].data.data());
    EXPECT_EQ(notify->PacketId, (UINT16)PACKET_ID::ROOM_CHAT_NOTIFY);
    EXPECT_STREQ(notify->UserID, "user0");
    EXPECT_STREQ(notify->Message, "test msg");
}

TEST_F(RoomTest, NotifyChatSendsToCorrectConnIndices)
{
    room.EnterUser(&users[0]);  // connIndex = 10
    room.EnterUser(&users[1]);  // connIndex = 11
    room.EnterUser(&users[2]);  // connIndex = 12

    ROOM_CHAT_REQUEST_PACKET chatReq = {};
    chatReq.PacketLength = sizeof(chatReq);
    chatReq.PacketId = (UINT16)PACKET_ID::ROOM_CHAT_REQUEST;
    strncpy_s(chatReq.Message, "ping", _TRUNCATE);

    sendCalls.clear();
    room.NotifyChat(users[0].GetNetConnIndex(), "user0", &chatReq);

    // Collect all connection indices that received the broadcast
    std::set<UINT32> receivedIndices;
    for (auto& call : sendCalls)
        receivedIndices.insert(call.connIndex);

    EXPECT_EQ(receivedIndices.count(10u), 1u);
    EXPECT_EQ(receivedIndices.count(11u), 1u);
    EXPECT_EQ(receivedIndices.count(12u), 1u);
}

TEST_F(RoomTest, NotifyChatSessionGenerationCorrect)
{
    users[0].SetSessionGeneration(7);
    users[1].SetSessionGeneration(42);
    room.EnterUser(&users[0]);
    room.EnterUser(&users[1]);

    ROOM_CHAT_REQUEST_PACKET chatReq = {};
    chatReq.PacketLength = sizeof(chatReq);
    chatReq.PacketId = (UINT16)PACKET_ID::ROOM_CHAT_REQUEST;
    strncpy_s(chatReq.Message, "gen test", _TRUNCATE);

    sendCalls.clear();
    room.NotifyChat(users[0].GetNetConnIndex(), "user0", &chatReq);

    ASSERT_EQ(sendCalls.size(), 2u);

    // Find each user's call and verify generation
    for (auto& call : sendCalls)
    {
        if (call.connIndex == 10u)
            EXPECT_EQ(call.generation, 7u);
        else if (call.connIndex == 11u)
            EXPECT_EQ(call.generation, 42u);
    }
}

TEST_F(RoomTest, NotifyChatEmptyRoomNoCalls)
{
    ROOM_CHAT_REQUEST_PACKET chatReq = {};
    chatReq.PacketLength = sizeof(chatReq);
    chatReq.PacketId = (UINT16)PACKET_ID::ROOM_CHAT_REQUEST;
    strncpy_s(chatReq.Message, "nobody here", _TRUNCATE);

    sendCalls.clear();
    room.NotifyChat(0, "ghost", &chatReq);

    EXPECT_EQ(sendCalls.size(), 0u);
}

TEST_F(RoomTest, NotifyChatAfterLeaveSendsToRemaining)
{
    room.EnterUser(&users[0]);
    room.EnterUser(&users[1]);
    room.EnterUser(&users[2]);

    // Remove user[1]
    room.LeaveUser(&users[1]);

    ROOM_CHAT_REQUEST_PACKET chatReq = {};
    chatReq.PacketLength = sizeof(chatReq);
    chatReq.PacketId = (UINT16)PACKET_ID::ROOM_CHAT_REQUEST;
    strncpy_s(chatReq.Message, "after leave", _TRUNCATE);

    sendCalls.clear();
    room.NotifyChat(users[0].GetNetConnIndex(), "user0", &chatReq);

    // Only 2 users remain
    EXPECT_EQ(sendCalls.size(), 2u);

    std::set<UINT32> receivedIndices;
    for (auto& call : sendCalls)
        receivedIndices.insert(call.connIndex);

    EXPECT_EQ(receivedIndices.count(10u), 1u);  // user[0]
    EXPECT_EQ(receivedIndices.count(12u), 1u);  // user[2]
    EXPECT_EQ(receivedIndices.count(11u), 0u);  // user[1] left
}

// ==================== Room Reset ====================

TEST_F(RoomTest, ResetClearsAllUsers)
{
    room.EnterUser(&users[0]);
    room.EnterUser(&users[1]);

    room.Reset();

    // After reset, entering max users should succeed
    for (int i = 0; i < MAX_ROOM_USERS; ++i)
    {
        SCOPED_TRACE(i);
        UINT16 result = room.EnterUser(&users[i]);
        EXPECT_EQ(result, (UINT16)ERROR_CODE::NONE);
    }
}

TEST_F(RoomTest, ResetWithParamsChangesCapacity)
{
    room.Reset(2, 5);

    EXPECT_EQ(room.GetRoomNumber(), 2);

    // Can now hold 5 users
    for (int i = 0; i < 5; ++i)
    {
        SCOPED_TRACE(i);
        User u;
        u.Init(static_cast<UINT32>(100 + i));
        char id[32];
        sprintf_s(id, "tmp%d", i);
        u.SetLogin(id);
        EXPECT_EQ(room.EnterUser(&u), (UINT16)ERROR_CODE::NONE);
    }
}

// ==================== Room number ====================

TEST_F(RoomTest, GetRoomNumber)
{
    EXPECT_EQ(room.GetRoomNumber(), 1);
}

