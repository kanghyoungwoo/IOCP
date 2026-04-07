#pragma once

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <string>
#include <chrono>

// 서버와 동일한 패킷 구조 (pragma pack(push,1))
#pragma pack(push, 1)

struct PACKET_HEADER
{
	uint16_t PacketLength;
	uint16_t PacketId;
	uint8_t  PacketType;
};

const uint32_t PACKET_HEADER_LENGTH = sizeof(PACKET_HEADER);

// 패킷 ID (서버와 동일)
enum class CYCPACKET_ID : uint16_t
{
	SYS_PING = 21,
	SYS_PONG = 22,

	LOGIN_REQUEST = 201,
	LOGIN_RESPONSE = 202,

	ROOM_ENTER_REQUEST = 206,
	ROOM_ENTER_RESPONSE = 207,

	ROOM_LEAVE_REQUEST = 215,
	ROOM_LEAVE_RESPONSE = 216,

	ROOM_CHAT_REQUEST = 221,
	ROOM_CHAT_RESPONSE = 222,
	ROOM_CHAT_NOTIFY = 223,
};

const int MAX_USER_ID_LENGTH = 32;
const int MAX_USER_PW_LENGTH = 32;
const int MAX_CHAT_MSG = 256;

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

struct ROOM_CHAT_REQUEST_PACKET : public PACKET_HEADER
{
	char Message[MAX_CHAT_MSG + 1];
};

struct ROOM_CHAT_RESPONSE_PACKET : public PACKET_HEADER
{
	int16_t Result;
};

struct ROOM_CHAT_NOTIFY_PACKET : public PACKET_HEADER
{
	char UserID[MAX_USER_ID_LENGTH + 1];
	char Message[MAX_CHAT_MSG + 1];
};

#pragma pack(pop)

// 패킷 빌더 유틸리티
namespace PacketCodec
{
	inline void BuildLoginRequest(LOGIN_REQUEST_PACKET& pkt, const char* userID, const char* userPW)
	{
		memset(&pkt, 0, sizeof(pkt));
		pkt.PacketLength = sizeof(LOGIN_REQUEST_PACKET);
		pkt.PacketId = (uint16_t)CYCPACKET_ID::LOGIN_REQUEST;
		pkt.PacketType = 0;
		strncpy_s(pkt.UserID, userID, MAX_USER_ID_LENGTH);
		strncpy_s(pkt.UserPW, userPW, MAX_USER_PW_LENGTH);
	}

	inline void BuildRoomEnterRequest(ROOM_ENTER_REQUEST_PACKET& pkt, int32_t roomNumber)
	{
		memset(&pkt, 0, sizeof(pkt));
		pkt.PacketLength = sizeof(ROOM_ENTER_REQUEST_PACKET);
		pkt.PacketId = (uint16_t)CYCPACKET_ID::ROOM_ENTER_REQUEST;
		pkt.PacketType = 0;
		pkt.RoomNumber = roomNumber;
	}

	inline void BuildRoomLeaveRequest(ROOM_LEAVE_REQUEST_PACKET& pkt, int32_t roomNumber)
	{
		memset(&pkt, 0, sizeof(pkt));
		pkt.PacketLength = sizeof(ROOM_LEAVE_REQUEST_PACKET);
		pkt.PacketId = (uint16_t)CYCPACKET_ID::ROOM_LEAVE_REQUEST;
		pkt.PacketType = 0;
		pkt.RoomNumber = roomNumber;
	}

	inline void BuildChatRequest(ROOM_CHAT_REQUEST_PACKET& pkt, const char* message)
	{
		memset(&pkt, 0, sizeof(pkt));
		pkt.PacketLength = sizeof(ROOM_CHAT_REQUEST_PACKET);
		pkt.PacketId = (uint16_t)CYCPACKET_ID::ROOM_CHAT_REQUEST;
		pkt.PacketType = 0;
		strncpy_s(pkt.Message, message, MAX_CHAT_MSG);
	}

	inline void BuildPongPacket(PACKET_HEADER& pkt)
	{
		pkt.PacketLength = sizeof(PACKET_HEADER);
		pkt.PacketId = (uint16_t)CYCPACKET_ID::SYS_PONG;
		pkt.PacketType = 0;
	}

	// Latency 측정용: 채팅 메시지에 타임스탬프 삽입
	// 형식: "[TS:1234567890123]actual message"
	inline void BuildChatRequestWithTimestamp(ROOM_CHAT_REQUEST_PACKET& pkt, const char* message)
	{
		memset(&pkt, 0, sizeof(pkt));
		pkt.PacketLength = sizeof(ROOM_CHAT_REQUEST_PACKET);
		pkt.PacketId = (uint16_t)CYCPACKET_ID::ROOM_CHAT_REQUEST;
		pkt.PacketType = 0;

		auto now = std::chrono::steady_clock::now();
		auto ms = std::chrono::duration_cast<std::chrono::microseconds>(
			now.time_since_epoch()).count();

		char buf[MAX_CHAT_MSG + 1] = {};
		snprintf(buf, sizeof(buf), "[TS:%lld]%s", ms, message);
		strncpy_s(pkt.Message, buf, MAX_CHAT_MSG);
	}

	// 수신한 NOTIFY 메시지에서 타임스탬프 추출 (microseconds)
	inline bool ExtractTimestamp(const char* message, int64_t& outTimestamp)
	{
		if (strncmp(message, "[TS:", 4) != 0)
			return false;

		const char* end = strchr(message + 4, ']');
		if (!end)
			return false;

		// #5 스택 버퍼 사용 - std::string 임시 객체 제거
		size_t len = end - (message + 4);
		if (len >= 24) return false;
		char buf[24];
		memcpy(buf, message + 4, len);
		buf[len] = '\0';
		outTimestamp = _atoi64(buf);
		return true;
	}
}
