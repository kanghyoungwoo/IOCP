#pragma once
// ================================================================
// Protocol.h - Packet protocol definitions
// Target Server: IOCPChatServer (Port 11021)
// ================================================================

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <cstdint>
#include <cstring>

enum class PACKET_ID : uint16_t
{
    // SYSTEM
    SYS_USER_CONNECT    = 11,
    SYS_USER_DISCONNECT = 12,

    SYS_PING = 21,
    SYS_PONG = 22,

    SYS_END = 30,

    // DB
    DB_END = 199,

    // Client
    LOGIN_REQUEST       = 201,
    LOGIN_RESPONSE      = 202,

    ROOM_ENTER_REQUEST  = 206,
    ROOM_ENTER_RESPONSE = 207,

    ROOM_LEAVE_REQUEST  = 215,
    ROOM_LEAVE_RESPONSE = 216,

    ROOM_CHAT_REQUEST   = 221,
    ROOM_CHAT_RESPONSE  = 222,
    ROOM_CHAT_NOTIFY    = 223,
};

#pragma pack(push, 1)

struct PACKET_HEADER
{
    uint16_t PacketLength;
    uint16_t PacketId;
    uint8_t  PacketType;
};

constexpr uint32_t PACKET_HEADER_LENGTH = sizeof(PACKET_HEADER);

constexpr int MAX_USER_ID_LENGTH = 32;
constexpr int MAX_USER_PW_LENGTH = 32;

struct LOGIN_REQUEST_PACKET : public PACKET_HEADER
{
    char UserID[MAX_USER_ID_LENGTH + 1];
    char UserPW[MAX_USER_PW_LENGTH + 1];
};

struct LOGIN_RESPONSE_PACKET : public PACKET_HEADER
{
    uint16_t Result;
};

struct ROOM_ENTER_REQUEST_PACKET : public PACKET_HEADER
{
    int32_t RoomNumber;
};

struct ROOM_ENTER_RESPONSE_PACKET : public PACKET_HEADER
{
    int16_t Result;
};

struct ROOM_LEAVE_REQUEST_PACKET : public PACKET_HEADER
{
    int32_t RoomNumber;
};

struct ROOM_LEAVE_RESPONSE_PACKET : public PACKET_HEADER
{
    int16_t Result;
};

constexpr int MAX_CHAT_MSG = 256;

struct ROOM_CHAT_REQUEST_PACKET : public PACKET_HEADER
{
    char Message[MAX_CHAT_MSG + 1] = {};
};

struct ROOM_CHAT_RESPONSE_PACKET : public PACKET_HEADER
{
    int16_t Result;
};

struct ROOM_CHAT_NOTIFY_PACKET : public PACKET_HEADER
{
    char UserID[MAX_USER_ID_LENGTH + 1] = {};
    char Message[MAX_CHAT_MSG + 1] = {};
};

struct PONG_PACKET : public PACKET_HEADER
{
};

#pragma pack(pop)

namespace PacketBuilder
{
    inline void BuildLoginRequest(LOGIN_REQUEST_PACKET& pkt, const char* userId, const char* userPw)
    {
        memset(&pkt, 0, sizeof(pkt));
        pkt.PacketLength = sizeof(LOGIN_REQUEST_PACKET);
        pkt.PacketId = static_cast<uint16_t>(PACKET_ID::LOGIN_REQUEST);
        pkt.PacketType = 0;
        strncpy_s(pkt.UserID, userId, MAX_USER_ID_LENGTH);
        strncpy_s(pkt.UserPW, userPw, MAX_USER_PW_LENGTH);
    }

    inline void BuildRoomEnterRequest(ROOM_ENTER_REQUEST_PACKET& pkt, int32_t roomNumber)
    {
        memset(&pkt, 0, sizeof(pkt));
        pkt.PacketLength = sizeof(ROOM_ENTER_REQUEST_PACKET);
        pkt.PacketId = static_cast<uint16_t>(PACKET_ID::ROOM_ENTER_REQUEST);
        pkt.PacketType = 0;
        pkt.RoomNumber = roomNumber;
    }

    inline void BuildRoomLeaveRequest(ROOM_LEAVE_REQUEST_PACKET& pkt, int32_t roomNumber)
    {
        memset(&pkt, 0, sizeof(pkt));
        pkt.PacketLength = sizeof(ROOM_LEAVE_REQUEST_PACKET);
        pkt.PacketId = static_cast<uint16_t>(PACKET_ID::ROOM_LEAVE_REQUEST);
        pkt.PacketType = 0;
        pkt.RoomNumber = roomNumber;
    }

    inline void BuildChatRequest(ROOM_CHAT_REQUEST_PACKET& pkt, const char* message)
    {
        memset(&pkt, 0, sizeof(pkt));
        pkt.PacketLength = sizeof(ROOM_CHAT_REQUEST_PACKET);
        pkt.PacketId = static_cast<uint16_t>(PACKET_ID::ROOM_CHAT_REQUEST);
        pkt.PacketType = 0;
        strncpy_s(pkt.Message, message, MAX_CHAT_MSG);
    }

    inline void BuildPongPacket(PONG_PACKET& pkt)
    {
        memset(&pkt, 0, sizeof(pkt));
        pkt.PacketLength = sizeof(PONG_PACKET);
        pkt.PacketId = static_cast<uint16_t>(PACKET_ID::SYS_PONG);
        pkt.PacketType = 0;
    }

    inline uint16_t WriteHeader(char* buffer, uint16_t length, uint16_t packetId, uint8_t type = 0)
    {
        auto* hdr = reinterpret_cast<PACKET_HEADER*>(buffer);
        hdr->PacketLength = length;
        hdr->PacketId = packetId;
        hdr->PacketType = type;
        return PACKET_HEADER_LENGTH;
    }
}
