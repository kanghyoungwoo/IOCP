#pragma once

#include "BotState.h"
#include "PacketCodec.h"
#include <cstdint>
#include <string>
#include <chrono>
#include <atomic>

class IOCPClient;
class MetricsCollector;

class Bot
{
public:
	Bot() = default;
	~Bot() = default;

	void Init(uint32_t index, const std::string& userID, int32_t roomNumber);

	// 상태 머신 이벤트 처리
	void OnConnectComplete(bool success);
	void OnRecvData(uint32_t size, char* data);
	void OnDisconnect();

	// 타이머 이벤트로 호출
	void DoSendLogin();
	void DoEnterRoom();
	void DoSendChat();
	void DoLeaveRoom();

	// Getter
	uint32_t GetIndex() const { return mIndex; }
	BotState GetState() const { return mState; }
	int32_t GetRoomNumber() const { return mRoomNumber; }
	const std::string& GetUserID() const { return mUserID; }

	void SetIOCPClient(IOCPClient* client) { mIOCPClient = client; }
	void SetMetrics(MetricsCollector* metrics) { mMetrics = metrics; }
	void SetState(BotState state) { mState = state; }

private:
	void ProcessPacket(uint16_t packetId, char* data, uint16_t dataSize);
	void HandleLoginResponse(char* data, uint16_t size);
	void HandleRoomEnterResponse(char* data, uint16_t size);
	void HandleRoomLeaveResponse(char* data, uint16_t size);
	void HandleChatResponse(char* data, uint16_t size);
	void HandleChatNotify(char* data, uint16_t size);
	void HandlePing();

	uint32_t	mIndex = 0;
	BotState	mState = BotState::IDLE;
	std::string	mUserID;
	int32_t		mRoomNumber = 0;

	IOCPClient*		 mIOCPClient = nullptr;
	MetricsCollector* mMetrics = nullptr;

	// 수신 버퍼 (패킷 조립용)
	char	mAssemblyBuffer[4096] = {};
	uint32_t mAssemblySize = 0;

	// Latency 측정용
	std::chrono::steady_clock::time_point mLastChatSendTime;
};
