#pragma once

#include "PacketManager.h"
#include "UserManager.h"
#include "RoomManager.h"
#include "ConfigManager.h"
#include "RedisManager.h"
#include "MysqlManager.h"
#include "Define.h"
#include <chrono>

PacketManager::PacketManager() = default;
PacketManager::~PacketManager() = default;

void PacketManager::Init(const UINT32 maxClient_)
{
	RegisterHandlers();
	CreateComponent(maxClient_);

	mRedisManager = std::make_unique<RedisManager>();
	mMySQLManager = std::make_unique<MySQLManager>();

	mRedisManager->OnResponsePushed = [this]()
	{
		NotifyPacketEvent();
	};

	// Handler 초기화 — 모든 컴포넌트 생성 완료 후
	mLoginHandler       = std::make_unique<LoginHandler>(mUserManager.get(), mRedisManager.get(), mMySQLManager.get(), SendPacketFunc);
	mSessionHandler     = std::make_unique<SessionHandler>(mUserManager.get(), mRoomManager.get(), &m_strandProcessor);
	mLobbyHandler       = std::make_unique<LobbyHandler>(mUserManager.get(), mRoomManager.get(), &m_strandProcessor, SendPacketFunc);
	mCallbackDispatcher = std::make_unique<CallbackDispatcher>(mUserManager.get(), mRoomManager.get(), mMySQLManager.get(), &m_strandProcessor);
}

void PacketManager::RegisterHandlers()
{
	mPacketHandlers[(UINT16)PACKET_ID::SYS_USER_CONNECT] = [this](UINT32 ci, UINT32 gen, UINT16 sz, char* p)
		{ mSessionHandler->ProcessUserConnect(ci, gen, sz, p); };

	mPacketHandlers[(UINT16)PACKET_ID::SYS_USER_DISCONNECT] = [this](UINT32 ci, UINT32 gen, UINT16 sz, char* p)
		{ mSessionHandler->ProcessUserDisconnect(ci, gen, sz, p); };

	mPacketHandlers[(UINT16)PACKET_ID::LOGIN_REQUEST] = [this](UINT32 ci, UINT32 gen, UINT16 sz, char* p)
		{ mLoginHandler->ProcessLogin(ci, gen, sz, p); };

	mPacketHandlers[(UINT16)RedisTaskID::RESPONSE_LOGIN] = [this](UINT32 ci, UINT32 gen, UINT16 sz, char* p)
		{ mLoginHandler->ProcessLoginDBResult(ci, gen, sz, p); };

	mPacketHandlers[(UINT16)PACKET_ID::ROOM_ENTER_REQUEST] = [this](UINT32 ci, UINT32 gen, UINT16 sz, char* p)
		{ mLobbyHandler->ProcessEnterRoom(ci, gen, sz, p); };

	mPacketHandlers[(UINT16)PACKET_ID::SYS_PONG] = [this](UINT32 ci, UINT32 gen, UINT16 sz, char* p)
		{ LOG_DEBUG("[PacketManager] Client Index(%d)로부터 PONG 수신 완료\n", ci); };
}

void PacketManager::CreateComponent(const UINT32 maxClient_)
{
	const auto& config = ConfigManager::GetInstance().Get();
	//mUserManager = new UserManager;
	mUserManager = std::make_unique <UserManager>();
	mUserManager->Init(maxClient_);

	UINT32 startRoomNumber = config.StartRoomNumber;
	UINT32 maxRoomUserCount = config.MaxRoomUserCount;
	UINT32 maxRoomCount = config.MaxRoomCount;

	//mRoomManager = new RoomManager;
	mRoomManager = std::make_unique <RoomManager>();
	mRoomManager->SendPacketFunc  = SendPacketFunc;
	mRoomManager->EnqueueOnlyFunc = EnqueueOnlyFunc;
	mRoomManager->FreeJobFunc = [this](PacketJob* p) { m_strandProcessor.FreeJob(p); };
	mRoomManager->Init(startRoomNumber, maxRoomCount, maxRoomUserCount);

	m_strandProcessor.Init(config.JobPoolSize, config.MaxRoomCount);
	m_strandProcessor.SetUserManager(mUserManager.get());
}


bool PacketManager::Run()
{
	const auto& config = ConfigManager::GetInstance().Get();

	if (!config.TestMode)
	{


		if (mRedisManager->Run(config.RedisHost, config.RedisPort, 1) == false)
		{
			return false;
		}

		mMySQLManager->configure(
			config.MySQLHost.c_str(),
			config.MySQLUser.c_str(),
			config.MySQLPassword.c_str(),
			config.MySQLDatabase.c_str(),
			config.MySQLPort
		);

		if (mMySQLManager->Run(1) == false)
		{
			return false;
		}
	}
	else
	{
		LOG_DEBUG("[TestMode] DB 초기화 생략 (Redis/MySQL 비활성)\n");
	}
	mIsRunProcessThread = true;
	mProcessThead = std::thread([this]() { ProcessPacket();});
	m_strandProcessor.Start(config.MaxLogicThread);

	return true;
}

void PacketManager::End()
{
	// 패킷 처리 스레드 먼저 종료
	{
		std::lock_guard<std::mutex> lock(mLock);
		mIsRunProcessThread = false;
	}

	mPacketEventCV.notify_all();
	if (mProcessThead.joinable())
		mProcessThead.join();

	// 라우터 멈춤이 보장된 후 Strand 종료 (방에 남은 패킷 마저 처리)
	m_strandProcessor.Stop();
	// Stop 과정에서 마지막으로 밀어넣어진 콜백 찌꺼기 청소
	while (auto* pNotify = m_strandProcessor.PopCallback())
	{
		m_strandProcessor.FreeCallback(pNotify);
	}

	// DB 쓰레드 종료(큐 소진후)
	mRedisManager->End();
	mMySQLManager->End();

	FILE* fp = nullptr;
	fopen_s(&fp, "benchmark_result.txt", "a");
	if (fp) {
		fprintf(fp, "=== Benchmark Result ===\n");
		fprintf(fp, "Job Pool Size: %u\n", m_strandProcessor.GetJobPoolSize());
		fprintf(fp, "Alloc Fail Count: %llu\n", m_strandProcessor.GetAllocFailCount()); // 유지결정?
		fprintf(fp, "========================\n\n");

		fprintf(fp, "=== Benchmark Result ===\n");
		fprintf(fp, "IOCP Workers: %u\n", ConfigManager::GetInstance().Get().MaxIOWorkerThread);
		fprintf(fp, "Logic Threads: %u\n", ConfigManager::GetInstance().Get().MaxLogicThread);
		fprintf(fp, "Job Pool Size: %u\n", m_strandProcessor.GetJobPoolSize());
		fprintf(fp, "Alloc Fail Count: %llu\n", m_strandProcessor.GetAllocFailCount());
		fprintf(fp, "Current Pool Free Count: %u\n", m_strandProcessor.GetCurrentFreeCount());
		fprintf(fp, "Alloc Total Attempts: %llu\n", m_strandProcessor.GetAllocTotalCount());

		fprintf(fp, "========================\n\n");


		fclose(fp);
	}

}

// usermanager의 GetUserByConnIdx를 이용하여 유저의 idx를 받은 후 user의 data를 SetPacketData를 통해 set함
bool PacketManager::ReceivePacketData(const UINT32 clientIndex_, const UINT32 generation_, const UINT32 dataSize_, char* pData_)
{
	auto pUser = mUserManager->GetUserByConnIdx(clientIndex_);
	if (!pUser->SetPacketData(dataSize_, pData_))
	{
		LOG_ERROR("[ReceivePacketData] Client(%d) 버퍼 오버플로우\n", clientIndex_);
		return false;
	}

	if (pUser->GetDomainState() == User::DOMAIN_STATE::ROOM)
	{
		if (!pUser->TryAcquireRouting())
		{
			// ProcessThread가 drain 중 → Slow Path
			EnqueuePacketData(clientIndex_, generation_);
			return true;
		}

		char packetBuf[MAX_SINGLE_PACKET_SIZE];
		while (true)
		{
			auto packetData = pUser->GetPacket(packetBuf, sizeof(packetBuf));
			if (packetData.PacketId == 0)
				break;

			if (pUser->GetSessionGeneration() != generation_)
				break;

			if (packetData.DataSize == 0 || packetData.DataSize > MAX_SINGLE_PACKET_SIZE)
			{
				pUser->ReleaseRouting();
				return false;
			}

			INT32 roomIdx = pUser->GetRoomIndex();
			auto pRoom = mRoomManager->GetRoomByNumber(roomIdx);
			if (pRoom == nullptr)
			{
				LOG_ERROR("client %d의 방이 유효하지 않음. 강제 LOGIN 전환 및 현재 패킷 드랍\n", clientIndex_);

				// 1. 유저의 상태를 ROOM에서 LOGIN으로 원자적 강등 (기존 SlowPath 정책과 동일)
				pUser->ResetRoom();

				// 2. 라우팅 독점권을 반환
				pUser->ReleaseRouting();

				// 3. 현재 뽑아낸 packetData는 드랍하고
				// 링버퍼에 아직 남아있을지 모르는 다음 패킷들을 ProcessThread가 처리하도록 Slow Path로 넘김
				EnqueuePacketData(clientIndex_, generation_);
				return true;
			}

			m_strandProcessor.EnqueueJob(
				pRoom, clientIndex_,
				pRoom->GetGeneration(),
				pUser->GetSessionGeneration(),
				packetData.PacketId, packetData.DataSize, packetData.pDataPtr
			);
		}
		pUser->ReleaseRouting();
		return true;
	}

	EnqueuePacketData(clientIndex_, generation_);
	return true;
}


// ──────────────────────────────────────────────────────────────────────────
//  ProcessPacket : 오케스트레이터
//  "무엇을 하는가"만 기술하고 "어떻게 하는가"는 각 단계 함수에 위임한다.
// ──────────────────────────────────────────────────────────────────────────
void PacketManager::ProcessPacket()
{
	while (mIsRunProcessThread)
	{
		WaitAndSwapBuffers();           // 1. 동기화 및 버퍼 스왑
		if (!mIsRunProcessThread) break;

		ProcessSystemPackets();         // 2. connect / disconnect 시스템 패킷
		ProcessUserPackets();           // 3. 일반 유저 패킷 라우팅
		ProcessRedisTasks();            // 4. Redis 비동기 응답
		ProcessStrandCallbacks();       // 5. Strand 콜백 디스패치
	}
}

// ── 1단계: CV wait + 더블버퍼 스왑 ──────────────────────────────────────
void PacketManager::WaitAndSwapBuffers()
{
	std::unique_lock<std::mutex> lock(mLock);

	mPacketEventCV.wait(lock, [this]()
	{
		if (!mIsRunProcessThread)          return true;
		if (!mSystemWriteBuffer.empty())   return true;
		if (!mWriteBuffer.empty())         return true;
		if (mRedisManager && mRedisManager->HasResponseTask()) return true;
		return false;
	});

	if (!mIsRunProcessThread) return;

	// lock 보유 상태에서 swap → 이후 처리는 전부 lock-free
	std::swap(mSystemWriteBuffer, mSystemReadBuffer);
	std::swap(mWriteBuffer, mReadBuffer);
}

// ── 2단계: 시스템 패킷 처리 (connect / disconnect) ────────────────────────
void PacketManager::ProcessSystemPackets()
{
	for (auto& sysPacket : mSystemReadBuffer)
	{
		ProcessRecvPacket(sysPacket.ClientIndex, sysPacket.Generation,
			sysPacket.PacketId, sysPacket.DataSize, sysPacket.pDataPtr);
	}
	mSystemReadBuffer.clear();
}

// ── 3단계: 일반 유저 패킷 라우팅 ─────────────────────────────────────────
void PacketManager::ProcessUserPackets()
{
	// Queue Depth 모니터링 (1초 간격)
	{
		thread_local auto lastQueueLog = std::chrono::steady_clock::now();
		auto now     = std::chrono::steady_clock::now();
		auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastQueueLog).count();
		if (elapsed >= 1)
		{
			LOG_DEBUG("[QueueDepth] ThreadID=%lu, batch_size=%zu  sys_batch=%zu",
				GetCurrentThreadId(), mReadBuffer.size(), mSystemReadBuffer.size());
			lastQueueLog = now;
		}
	}

	char packetBuf[MAX_SINGLE_PACKET_SIZE];
	for (auto& task : mReadBuffer)
	{
		RouteSingleUserTask(task, packetBuf);
	}
	mReadBuffer.clear();
}

// ── 3-1: 유저 1명의 패킷 라우팅 ──────────────────────────────────────────
void PacketManager::RouteSingleUserTask(const PacketTask& task, char* packetBuf)
{
	auto pUser = mUserManager->GetUserByConnIdx(task.clientIndex);
	if (!pUser) return;

	// 접속 종료 중인 유저의 잔여 패킷은 링버퍼째 폐기
	if (pUser->IsDisconnecting())
	{
		pUser->ClearPacketBuffer();
		return;
	}

	// 동시 라우팅 방지 (IOCP Worker Fast Path와 경합 차단)
	if (!pUser->TryAcquireRouting()) return;

	auto packetData = pUser->GetPacket(packetBuf, MAX_SINGLE_PACKET_SIZE);
	if (packetData.PacketId == 0)
	{
		pUser->ReleaseRouting();
		return;
	}

	// 비정상 크기 패킷 → 연결 강제 종료
	if (packetData.DataSize == 0 || packetData.DataSize > MAX_SINGLE_PACKET_SIZE)
	{
		pUser->ReleaseRouting();
		mSessionHandler->ClearConnectionInfo(task.clientIndex);
		return;
	}

	// 유효 패킷 수신 → 활동 시간 갱신
	if (UpdateActivityFunc)
		UpdateActivityFunc(task.clientIndex);

	packetData.ClientIndex = task.clientIndex;
	packetData.Generation  = task.generation;

	// 로그인 전/시스템 패킷 이외는 세대 검사 (지각 패킷 폐기)
	const auto packetId = packetData.PacketId;
	if (packetId != (UINT16)PACKET_ID::SYS_USER_CONNECT &&
		packetId != (UINT16)PACKET_ID::LOGIN_REQUEST)
	{
		if (pUser->GetSessionGeneration() != task.generation)
		{
			LOG_DEBUG("Stale Packet 버림 (Gen: %d vs %d)", pUser->GetSessionGeneration(), task.generation);
			pUser->ReleaseRouting();
			return;
		}
	}

	// 방 안 패킷 → Strand, 로비 패킷 → ProcessRecvPacket
	auto routeOnePacket = [&](PacketInfo& pkt)
	{
		if (pUser->GetDomainState() == User::DOMAIN_STATE::ROOM)
		{
			auto pRoom = mRoomManager->GetRoomByNumber(pUser->GetRoomIndex());
			if (pRoom == nullptr)
			{
				LOG_ERROR("client %d의 방이 유효하지 않음.", task.clientIndex);
				pUser->SetDomainState(User::DOMAIN_STATE::LOGIN);
				pUser->ResetRoom();
				return;
			}
			m_strandProcessor.EnqueueJob(pRoom, task.clientIndex,
				pRoom->GetGeneration(), pUser->GetSessionGeneration(),
				pkt.PacketId, pkt.DataSize, pkt.pDataPtr);
		}
		else
		{
			ProcessRecvPacket(pkt.ClientIndex, pkt.Generation,
				pkt.PacketId, pkt.DataSize, pkt.pDataPtr);
		}
	};

	// 첫 번째 패킷 처리 후 링버퍼에 남은 패킷도 연속 처리
	routeOnePacket(packetData);
	while (true)
	{
		auto nextPacket = pUser->GetPacket(packetBuf, MAX_SINGLE_PACKET_SIZE);
		if (nextPacket.PacketId == 0) break;
		nextPacket.ClientIndex = task.clientIndex;
		routeOnePacket(nextPacket);
	}

	pUser->ReleaseRouting();
}

// ── 4단계: Redis 비동기 응답 처리 ────────────────────────────────────────
void PacketManager::ProcessRedisTasks()
{
	while (true)
	{
		auto task = mRedisManager->TakeResponseTask();
		if (task.TaskID == RedisTaskID::INVALID) break;

		ProcessRecvPacket(task.UserIndex, task.Generation,
			(UINT16)task.TaskID, task.DataSize, task.body);
	}
}

// ── 5단계: Strand 콜백 디스패치 ──────────────────────────────────────────
void PacketManager::ProcessStrandCallbacks()
{
	while (auto* pNotify = m_strandProcessor.PopCallback())
		mCallbackDispatcher->Dispatch(pNotify);
}

void PacketManager::EnqueuePacketData(const UINT32 clientIndex_, const UINT32 generation_)
{
	{
		std::lock_guard<std::mutex> guard(mLock);
		PacketTask task;
		task.clientIndex = clientIndex_;
		task.generation = generation_;  // ClientSession의 세대를 사용
		mWriteBuffer.push_back(task);
	}
	// 큐에 새로운 패킷이 들어왔다고 처리 쓰레드 깨움
	NotifyPacketEvent();
	//mInComingPacketUserIndex.push_back(clientIndex_);
}



void PacketManager::PushSystemPacket(PacketInfo packet_)
{
	{
		std::lock_guard<std::mutex>guard(mLock);
		//mSystemPacketQueue.push_back(packet_);
		mSystemWriteBuffer.push_back(packet_);
	}
	// 시스템 패킷 들어왔으니 처리 쓰레드 깨움
	NotifyPacketEvent();
}



void PacketManager::ProcessRecvPacket(const UINT32 clientIndex_, const UINT32 generation_, const UINT16 packetId_, const UINT16 packetSize_, char* pPacket_)
{
	// 패킷key값(packetid)을 찾으면 
	auto it = mPacketHandlers.find(packetId_);
	if (it != mPacketHandlers.end())
		it->second(clientIndex_, generation_, packetSize_, pPacket_);
	else
		LOG_ERROR("알 수 없는 패킷 ID : %d (ClientIndex: %d)\n", packetId_, clientIndex_);

}





void PacketManager::NotifyPacketEvent()
{
	mPacketEventCV.notify_one();
}
