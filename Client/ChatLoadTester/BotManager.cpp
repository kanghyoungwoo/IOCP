#include "BotManager.h"
#include "DebugLog.h"

bool BotManager::Init(const Config& config)
{
	mConfig = config;

	// IOCP 클라이언트 초기화
	if (!mIOCPClient.Init(config.BotCount, config.WorkerThreadCount))
	{
		DEBUG_LOG("[BotManager] IOCPClient 초기화 실패\n");
		return false;
	}

	// 콜백 설정
	mIOCPClient.SetOnConnectComplete([this](uint32_t idx, bool ok) { OnConnectComplete(idx, ok); });
	mIOCPClient.SetOnRecvComplete([this](uint32_t idx, uint32_t sz, char* d) { OnRecvComplete(idx, sz, d); });
	mIOCPClient.SetOnDisconnect([this](uint32_t idx) { OnDisconnect(idx); });

	// 봇 초기화
	mBots.resize(config.BotCount);
	for (uint32_t i = 0; i < config.BotCount; ++i)
	{
		char userID[64];
		snprintf(userID, sizeof(userID), "test_user_%u_%04u", config.InstanceId, i);

		int32_t roomNumber = AssignRoom(i);
		mBots[i].Init(i, userID, roomNumber);
		mBots[i].SetIOCPClient(&mIOCPClient);
		mBots[i].SetMetrics(&mMetrics);
	}

	// 타이머 이벤트 핸들러
	mTimerScheduler.SetEventHandler([this](uint32_t idx, TimerEventType type) { OnTimerEvent(idx, type); });

	DEBUG_LOG("[BotManager] 초기화 완료 (봇: %u, 방: %u)\n", config.BotCount, config.RoomCount);
	return true;
}

void BotManager::Start()
{
	// 지표 수집 시작
	mMetrics.Start(mConfig.MetricsIntervalMs, mConfig.CsvFilePath);

	// 타이머 시작
	mTimerScheduler.Start();

	// Ramp-up: 100ms마다 ConnectBatchSize개씩 접속 스케줄링
	uint32_t batchCount = 0;
	for (uint32_t i = 0; i < mConfig.BotCount; i += mConfig.ConnectBatchSize)
	{
		uint32_t delay = batchCount * mConfig.RampUpIntervalMs;

		uint32_t end = i + mConfig.ConnectBatchSize;
		if (end > mConfig.BotCount) end = mConfig.BotCount;

		for (uint32_t j = i; j < end; ++j)
		{
			mTimerScheduler.Schedule(j, TimerEventType::CONNECT, delay);
		}
		++batchCount;
	}

	uint32_t totalRampUpSec = (batchCount * mConfig.RampUpIntervalMs) / 1000;
	DEBUG_LOG("[BotManager] Ramp-up 스케줄 완료 (예상 소요: %u초)\n", totalRampUpSec);
}

void BotManager::Stop()
{
	mTimerScheduler.Stop();
	mIOCPClient.Shutdown();
	mMetrics.Stop();

	DEBUG_LOG("[BotManager] 종료 완료\n");
}

int32_t BotManager::AssignRoom(uint32_t botIndex)
{
	// 인스턴스별 글로벌 봇 인덱스로 방 분배 (인스턴스 간 방 겹침 방지)
	uint32_t globalIndex = mConfig.InstanceId * mConfig.BotCount + botIndex;
	return (int32_t)(globalIndex / mConfig.MaxUsersPerRoom) % mConfig.RoomCount;
}

uint32_t BotManager::RandomChatInterval()
{
	thread_local std::mt19937 tlsRng(std::random_device{}());
	std::uniform_int_distribution<uint32_t> dist(mConfig.ChatIntervalMinMs, mConfig.ChatIntervalMaxMs);
	return dist(tlsRng);
}

// --- IOCP 콜백 ---

void BotManager::OnConnectComplete(uint32_t botIndex, bool success)
{
	if (botIndex >= mBots.size()) return;

	mBots[botIndex].OnConnectComplete(success);

	if (success)
	{
		// 연결 성공 → 500ms 후 로그인
		mTimerScheduler.Schedule(botIndex, TimerEventType::SEND_LOGIN, 500);
	}
	else
	{
		// 실패 → 3초 후 재접속
		mTimerScheduler.Schedule(botIndex, TimerEventType::RECONNECT, 3000);
	}
}

void BotManager::OnRecvComplete(uint32_t botIndex, uint32_t size, char* data)
{
	if (botIndex >= mBots.size()) return;

	BotState prevState = mBots[botIndex].GetState();
	mBots[botIndex].OnRecvData(size, data);
	BotState newState = mBots[botIndex].GetState();

	// 상태 전이에 따라 다음 액션 스케줄링
	if (prevState != newState)
	{
		switch (newState)
		{
		case BotState::LOGGED_IN:
			// 로그인 완료 → 1초 후 방 입장
			mTimerScheduler.Schedule(botIndex, TimerEventType::ENTER_ROOM, 1000);
			break;

		case BotState::IN_ROOM:
			// 방 입장 완료 시에만 첫 채팅 타이머 스케줄
			// (채팅 응답에 의한 IN_ROOM 복귀는 OnTimerEvent에서 재스케줄)
			if (prevState == BotState::ENTER_ROOM_SENT)
			{
				mTimerScheduler.Schedule(botIndex, TimerEventType::SEND_CHAT, RandomChatInterval());
			}
			break;

		case BotState::ERROR_STATE:
			// 소켓만 닫아 OnDisconnect 콜백을 유도 (타이머 중복 방지)
			mIOCPClient.CloseSocket(botIndex);
			break;

		default:
			break;
		}
	}
}

void BotManager::OnDisconnect(uint32_t botIndex)
{
	if (botIndex >= mBots.size()) return;

	mBots[botIndex].OnDisconnect();

	// 5초 후 재접속
	mTimerScheduler.Schedule(botIndex, TimerEventType::RECONNECT, 5000);
}

// --- 타이머 이벤트 ---

void BotManager::OnTimerEvent(uint32_t botIndex, TimerEventType eventType)
{
	if (botIndex >= mBots.size()) return;

	// 봇의 소켓이 끊어졌거나 종료 상태라면 타이머 갱신을 멈추고 return
	// (RECONNECT는 DISCONNECTED 상태에서 발생하므로 예외)
	if (mBots[botIndex].GetState() == BotState::DISCONNECTED &&
		eventType != TimerEventType::RECONNECT)
		return;

	switch (eventType)
	{
	case TimerEventType::CONNECT:
	{
		mBots[botIndex].SetState(BotState::CONNECTING);
		mIOCPClient.AsyncConnect(botIndex, mConfig.ServerIP.c_str(), mConfig.ServerPort);
		break;
	}

	case TimerEventType::SEND_LOGIN:
		mBots[botIndex].DoSendLogin();
		break;

	case TimerEventType::ENTER_ROOM:
		mBots[botIndex].DoEnterRoom();
		break;

	case TimerEventType::SEND_CHAT:
		mBots[botIndex].DoSendChat();
		// 채팅 전송 성공/실패 무관하게 다음 채팅 타이머 예약
		if (mBots[botIndex].GetState() == BotState::IN_ROOM ||
			mBots[botIndex].GetState() == BotState::CHATTING)
		{
			mTimerScheduler.Schedule(botIndex, TimerEventType::SEND_CHAT, RandomChatInterval());
		}
		break;

	case TimerEventType::LEAVE_ROOM:
		mBots[botIndex].DoLeaveRoom();
		break;

	case TimerEventType::RECONNECT:
	{
		// 소켓 정리 후 재접속
		mIOCPClient.CloseSocket(botIndex);

		char userID[64];
		snprintf(userID, sizeof(userID), "test_user_%u_%04u", mConfig.InstanceId, botIndex);
		int32_t roomNumber = AssignRoom(botIndex);
		mBots[botIndex].Init(botIndex, userID, roomNumber);
		mBots[botIndex].SetIOCPClient(&mIOCPClient);
		mBots[botIndex].SetMetrics(&mMetrics);

		mBots[botIndex].SetState(BotState::CONNECTING);
		mIOCPClient.AsyncConnect(botIndex, mConfig.ServerIP.c_str(), mConfig.ServerPort);
		break;
	}
	}
}
