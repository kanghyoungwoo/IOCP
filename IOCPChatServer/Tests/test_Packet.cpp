#include <gtest/gtest.h>

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>

#include "../Packet.h"
#include "../ErrorCode.h"

// ==================== Struct size validation ====================

TEST(PacketStructSize, HeaderIs5Bytes)
{
    EXPECT_EQ(sizeof(PACKET_HEADER), 5u);
    EXPECT_EQ(PACKET_HEADER_LENGTH, 5u);
}

TEST(PacketStructSize, LoginRequestPacket)
{
    // Header(5) + UserID(33) + UserPW(33) = 71
    EXPECT_EQ(sizeof(LOGIN_REQUEST_PACKET), 71u);
    EXPECT_EQ(LOGIN_REQUEST_PACKET_SIZE, 71u);
}

TEST(PacketStructSize, LoginResponsePacket)
{
    // Header(5) + Result(2) = 7
    EXPECT_EQ(sizeof(LOGIN_RESPONSE_PACKET), 7u);
}

TEST(PacketStructSize, RoomEnterRequest)
{
    // Header(5) + RoomNumber(4) = 9
    EXPECT_EQ(sizeof(ROOM_ENTER_REQUEST_PACKET), 9u);
}

TEST(PacketStructSize, RoomEnterResponse)
{
    // Header(5) + Result(2) = 7
    EXPECT_EQ(sizeof(ROOM_ENTER_RESPONSE_PACKET), 7u);
}

TEST(PacketStructSize, RoomLeaveRequest)
{
    // Header(5) + RoomNumber(4) = 9
    EXPECT_EQ(sizeof(ROOM_LEAVE_REQUEST_PACKET), 9u);
}

TEST(PacketStructSize, RoomChatRequest)
{
    // Header(5) + Message(257) = 262
    EXPECT_EQ(sizeof(ROOM_CHAT_REQUEST_PACKET), 5u + MAX_CHAT_MSG + 1u);
}

TEST(PacketStructSize, RoomChatNotify)
{
    // Header(5) + UserID(33) + Message(257) = 295
    EXPECT_EQ(sizeof(ROOM_CHAT_NOTIFY_PACKET),
        5u + (MAX_USER_ID_LENGTH + 1u) + (MAX_CHAT_MSG + 1u));
}

// ==================== Header parsing ====================

TEST(PacketParsing, ByteArrayToHeader)
{
    // Simulate raw bytes received from network
    char raw[5] = {};
    UINT16 length = 71;
    UINT16 id = (UINT16)PACKET_ID::LOGIN_REQUEST;
    UINT8 type = 0;

    memcpy(raw, &length, 2);
    memcpy(raw + 2, &id, 2);
    memcpy(raw + 4, &type, 1);

    auto* header = reinterpret_cast<PACKET_HEADER*>(raw);
    EXPECT_EQ(header->PacketLength, 71u);
    EXPECT_EQ(header->PacketId, (UINT16)PACKET_ID::LOGIN_REQUEST);
    EXPECT_EQ(header->PacketType, 0u);
}

TEST(PacketParsing, LoginRequestFields)
{
    LOGIN_REQUEST_PACKET pkt = {};
    pkt.PacketLength = sizeof(LOGIN_REQUEST_PACKET);
    pkt.PacketId = (UINT16)PACKET_ID::LOGIN_REQUEST;
    pkt.PacketType = 0;
    strncpy_s(pkt.UserID, "testuser", _TRUNCATE);
    strncpy_s(pkt.UserPW, "pass1234", _TRUNCATE);

    // Cast back from raw bytes
    char* raw = reinterpret_cast<char*>(&pkt);
    auto* parsed = reinterpret_cast<LOGIN_REQUEST_PACKET*>(raw);

    EXPECT_EQ(parsed->PacketLength, sizeof(LOGIN_REQUEST_PACKET));
    EXPECT_STREQ(parsed->UserID, "testuser");
    EXPECT_STREQ(parsed->UserPW, "pass1234");
}

// ==================== Constants validation ====================

TEST(PacketConstants, RingBufferSize)
{
    EXPECT_EQ(RING_BUFFER_SIZE, 4096u);
}

TEST(PacketConstants, MaxSinglePacketSize)
{
    EXPECT_EQ(MAX_SINGLE_PACKET_SIZE, 512u);
}

TEST(PacketConstants, MaxUserIDLength)
{
    EXPECT_EQ(MAX_USER_ID_LENGTH, 32);
}

TEST(PacketConstants, MaxChatMsg)
{
    EXPECT_EQ(MAX_CHAT_MSG, 256);
}

// ==================== Packet ID uniqueness ====================

TEST(PacketID, NoCollisions)
{
    // All packet IDs must be unique
    std::set<UINT16> ids;
    ids.insert((UINT16)PACKET_ID::SYS_USER_CONNECT);
    ids.insert((UINT16)PACKET_ID::SYS_USER_DISCONNECT);
    ids.insert((UINT16)PACKET_ID::SYS_PING);
    ids.insert((UINT16)PACKET_ID::SYS_PONG);
    ids.insert((UINT16)PACKET_ID::SYS_END);
    ids.insert((UINT16)PACKET_ID::DB_END);
    ids.insert((UINT16)PACKET_ID::LOGIN_REQUEST);
    ids.insert((UINT16)PACKET_ID::LOGIN_RESPONSE);
    ids.insert((UINT16)PACKET_ID::ROOM_ENTER_REQUEST);
    ids.insert((UINT16)PACKET_ID::ROOM_ENTER_RESPONSE);
    ids.insert((UINT16)PACKET_ID::ROOM_LEAVE_REQUEST);
    ids.insert((UINT16)PACKET_ID::ROOM_LEAVE_RESPONSE);
    ids.insert((UINT16)PACKET_ID::ROOM_CHAT_REQUEST);
    ids.insert((UINT16)PACKET_ID::ROOM_CHAT_RESPONSE);
    ids.insert((UINT16)PACKET_ID::ROOM_CHAT_NOTIFY);

    // 15 IDs inserted, set size should also be 15 (no duplicates)
    EXPECT_EQ(ids.size(), 15u);
}

// ==================== Error code validation ====================

TEST(ErrorCode, NoneIsZero)
{
    EXPECT_EQ((unsigned short)ERROR_CODE::NONE, 0u);
}

TEST(ErrorCode, EnterRoomFullUser)
{
    EXPECT_EQ((unsigned short)ERROR_CODE::ENTER_ROOM_FULL_USER, 54u);
}
