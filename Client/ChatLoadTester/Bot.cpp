#include "Bot.h"
#include "IOCPClient.h"
#include "MetricsCollector.h"
#include "DebugLog.h"
#include <cstring>

void Bot::Init(uint32_t index, const std::string& userID, int32_t roomNumber)
{
	mIndex = index;
	mUserID = userID;
	mRoomNumber = roomNumber;
	mState = BotState::IDLE;
	mAssemblySize = 0;
}

void Bot::OnConnectComplete(bool success)
{
	if (success)
	{
		mState = BotState::CONNECTED;
		if (mMetrics) mMetrics->OnConnect();
	}
	else
	{
		mState = BotState::ERROR_STATE;
		if (mMetrics) mMetrics->OnConnectFail();
	}
}

void Bot::OnRecvData(uint32_t size, char* data)
{
	if (mMetrics) mMetrics->OnRecv(size);

	// 패킷 조립 버퍼에 추가
	if (mAssemblySize + size > sizeof(mAssemblyBuffer))
	{
		DEBUG_LOG("[Bot %u] 조립 버퍼 오버플로우, 리셋\n", mIndex);
		mAssemblySize = 0;
		return;
	}

	memcpy(mAssemblyBuffer + mAssemblySize, data, size);
	mAssemblySize += size;

	// 완성된 패킷 처리
	while (mAssemblySize >= PACKET_HEADER_LENGTH)
	{
		auto* header = reinterpret_cast<PACKET_HEADER*>(mAssemblyBuffer);
		uint16_t packetLen = header->PacketLength;

		if (packetLen > sizeof(mAssemblyBuffer) || packetLen < PACKET_HEADER_LENGTH)
		{
			DEBUG_LOG("[Bot %u] 잘못된 패킷 길이: %u, 리셋\n", mIndex, packetLen);
			mAssemblySize = 0;
			break;
		}

		if (mAssemblySize < packetLen)
			break; // 아직 데이터 부족

		ProcessPacket(header->PacketId, mAssemblyBuffer, packetLen);

		// 처리한 패킷 제거
		uint32_t remaining = mAssemblySize - packetLen;
		if (remaining > 0)
			memmove(mAssemblyBuffer, mAssemblyBuffer + packetLen, remaining);
		mAssemblySize = remaining;
	}
}

void Bot::OnDisconnect()
{
	mState = BotState::DISCONNECTED;
	mAssemblySize = 0;
	if (mMetrics) mMetrics->OnDisconnect();
}

void Bot::ProcessPacket(uint16_t packetId, char* data, uint16_t dataSize)
{
	switch ((CYCPACKET_ID)packetId)
	{
	case CYCPACKET_ID::LOGIN_RESPONSE:
		HandleLoginResponse(data, dataSize);
		break;
	case CYCPACKET_ID::ROOM_ENTER_RESPONSE:
		HandleRoomEnterResponse(data, dataSize);
		break;
	case CYCPACKET_ID::ROOM_LEAVE_RESPONSE:
		HandleRoomLeaveResponse(data, dataSize);
		break;
	case CYCPACKET_ID::ROOM_CHAT_RESPONSE:
		HandleChatResponse(data, dataSize);
		break;
	case CYCPACKET_ID::ROOM_CHAT_NOTIFY:
		HandleChatNotify(data, dataSize);
		break;
	case CYCPACKET_ID::SYS_PING:
		HandlePing();
		break;
	default:
		break;
	}
}

// --- 패킷 전송 ---

void Bot::DoSendLogin()
{
	if (mState != BotState::CONNECTED)
		return;

	LOGIN_REQUEST_PACKET pkt;
	PacketCodec::BuildLoginRequest(pkt, mUserID.c_str(), "test_pw");

	if (mIOCPClient->AsyncSend(mIndex, (char*)&pkt, sizeof(pkt)))
	{
		mState = BotState::LOGIN_SENT;
		if (mMetrics) mMetrics->OnSend(sizeof(pkt));
	}
}

void Bot::DoEnterRoom()
{
	if (mState != BotState::LOGGED_IN)
		return;

	ROOM_ENTER_REQUEST_PACKET pkt;
	PacketCodec::BuildRoomEnterRequest(pkt, mRoomNumber);

	if (mIOCPClient->AsyncSend(mIndex, (char*)&pkt, sizeof(pkt)))
	{
		mState = BotState::ENTER_ROOM_SENT;
		if (mMetrics) mMetrics->OnSend(sizeof(pkt));
	}
}

void Bot::DoSendChat()
{
	if (mState != BotState::IN_ROOM)
		return;

	ROOM_CHAT_REQUEST_PACKET pkt;

	char msg[64];
	snprintf(msg, sizeof(msg), "Hello from %s", mUserID.c_str());
	PacketCodec::BuildChatRequestWithTimestamp(pkt, msg);

	mLastChatSendTime = std::chrono::steady_clock::now();

	if (mIOCPClient->AsyncSend(mIndex, (char*)&pkt, sizeof(pkt)))
	{
		mState = BotState::CHATTING;
		if (mMetrics) mMetrics->OnSend(sizeof(pkt));
	}
}

void Bot::DoLeaveRoom()
{
	if (mState != BotState::IN_ROOM && mState != BotState::CHATTING)
		return;

	ROOM_LEAVE_REQUEST_PACKET pkt;
	PacketCodec::BuildRoomLeaveRequest(pkt, mRoomNumber);

	if (mIOCPClient->AsyncSend(mIndex, (char*)&pkt, sizeof(pkt)))
	{
		mState = BotState::LEAVE_ROOM_SENT;
		if (mMetrics) mMetrics->OnSend(sizeof(pkt));
	}
}

// --- 응답 처리 ---

void Bot::HandleLoginResponse(char* data, uint16_t size)
{
	if (size < sizeof(LOGIN_RESPONSE_PACKET))
		return;

	auto* pkt = reinterpret_cast<LOGIN_RESPONSE_PACKET*>(data);

	if (pkt->Result == 0)
	{
		mState = BotState::LOGGED_IN;
	}
	else
	{
		DEBUG_LOG("[Bot %u] 로그인 실패 (Result: %u)\n", mIndex, pkt->Result);
		mState = BotState::ERROR_STATE;
	}
}

void Bot::HandleRoomEnterResponse(char* data, uint16_t size)
{
	if (size < sizeof(ROOM_ENTER_RESPONSE_PACKET))
		return;

	auto* pkt = reinterpret_cast<ROOM_ENTER_RESPONSE_PACKET*>(data);

	if (pkt->Result == 0)
	{
		mState = BotState::IN_ROOM;
	}
	else
	{
		DEBUG_LOG("[Bot %u] 방 입장 실패 (Room: %d, Result: %d)\n", mIndex, mRoomNumber, pkt->Result);
		mState = BotState::ERROR_STATE;
	}
}

void Bot::HandleRoomLeaveResponse(char* data, uint16_t size)
{
	if (size < sizeof(ROOM_LEAVE_RESPONSE_PACKET))
		return;

	auto* pkt = reinterpret_cast<ROOM_LEAVE_RESPONSE_PACKET*>(data);

	if (pkt->Result == 0)
	{
		mState = BotState::LOGGED_IN; // 방 나간 후 다시 LOGGED_IN
	}
	else
	{
		DEBUG_LOG("[Bot %u] 방 퇴장 실패 (Result: %d)\n", mIndex, pkt->Result);
		mState = BotState::ERROR_STATE;
	}
}

void Bot::HandleChatResponse(char* data, uint16_t size)
{
	if (size < sizeof(ROOM_CHAT_RESPONSE_PACKET))
		return;

	auto* pkt = reinterpret_cast<ROOM_CHAT_RESPONSE_PACKET*>(data);

	if (pkt->Result == 0)
	{
		mState = BotState::IN_ROOM; // 채팅 후 다시 IN_ROOM
	}
	else
	{
		DEBUG_LOG("[Bot %u] 채팅 실패 (Result: %d)\n", mIndex, pkt->Result);
		mState = BotState::IN_ROOM; // 실패해도 IN_ROOM으로 복귀 → 다음 채팅 가능
	}
}

void Bot::HandleChatNotify(char* data, uint16_t size)
{
	if (size < sizeof(ROOM_CHAT_NOTIFY_PACKET))
		return;

	auto* pkt = reinterpret_cast<ROOM_CHAT_NOTIFY_PACKET*>(data);

	// 자신이 보낸 메시지인지 확인하여 RTT 측정
	if (strcmp(pkt->UserID, mUserID.c_str()) == 0)
	{
		int64_t sendTimestamp = 0;
		if (PacketCodec::ExtractTimestamp(pkt->Message, sendTimestamp))
		{
			auto now = std::chrono::steady_clock::now();
			auto nowUs = std::chrono::duration_cast<std::chrono::microseconds>(
				now.time_since_epoch()).count();

			double latencyMs = (nowUs - sendTimestamp) / 1000.0;

			if (latencyMs >= 0.0 && mMetrics)
				mMetrics->RecordLatency(latencyMs);
		}
	}
}

void Bot::HandlePing()
{
	// PONG 응답
	PACKET_HEADER pongPkt;
	PacketCodec::BuildPongPacket(pongPkt);

	if (mIOCPClient)
	{
		mIOCPClient->AsyncSend(mIndex, (char*)&pongPkt, sizeof(pongPkt));
		if (mMetrics) mMetrics->OnSend(sizeof(pongPkt));
	}
}
