#pragma once

#include "Bot.h"
#include "IOCPClient.h"
#include "TimerScheduler.h"
#include "MetricsCollector.h"
#include "Config.h"

#include <vector>
#include <random>

class BotManager
{
public:
	BotManager() = default;
	~BotManager() = default;

	bool Init(const Config& config);
	void Start();
	void Stop();

private:
	// IOCP 콜백
	void OnConnectComplete(uint32_t botIndex, bool success);
	void OnRecvComplete(uint32_t botIndex, uint32_t size, char* data);
	void OnDisconnect(uint32_t botIndex);

	// 타이머 이벤트 핸들러
	void OnTimerEvent(uint32_t botIndex, TimerEventType eventType);

	// 채팅 간격 랜덤 생성
	uint32_t RandomChatInterval();

	// 봇 → 방 번호 매핑 (방당 최대 8명)
	int32_t AssignRoom(uint32_t botIndex);

	Config mConfig;
	std::vector<Bot> mBots;
	IOCPClient mIOCPClient;
	TimerScheduler mTimerScheduler;
	MetricsCollector mMetrics;

};
