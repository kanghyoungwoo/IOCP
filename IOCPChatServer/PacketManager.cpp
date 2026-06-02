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
	mRoomManager->SendPacketFunc = SendPacketFunc;
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


void PacketManager::ProcessPacket()
{
	char packetBuf[MAX_SINGLE_PACKET_SIZE];
	while (mIsRunProcessThread)
	{
		// 1. wait + swap (lock은 1회만)
		{
			std::unique_lock<std::mutex> lock(mLock);

			mPacketEventCV.wait(lock, [this]()
			{
				if (!mIsRunProcessThread)
					return true;

				if (!mSystemWriteBuffer.empty())
					return true;

				if (!mWriteBuffer.empty())
					return true;

				if (mRedisManager && mRedisManager->HasResponseTask())
					return true;

				return false;
			});

			if (!mIsRunProcessThread)
				break;

			// 더블 버퍼링, swap and release lock
			std::swap(mSystemWriteBuffer, mSystemReadBuffer);
			std::swap(mWriteBuffer, mReadBuffer);
		}

		// lock 해제, 아래는 전부 lock-free
		
		// Queue Depth logging (5sec interval)
		{
			// thread_local을 써서 스레드 충돌 방지
			thread_local auto lastQueueLog = std::chrono::steady_clock::now();
			auto now = std::chrono::steady_clock::now();
			auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastQueueLog).count();

			if (elapsed >= 1)
			{
				printf("[QueueDepth] ThreadID=%lu, batch_size=%zu  sys_batch=%zu\n",
					GetCurrentThreadId(), mReadBuffer.size(), mSystemReadBuffer.size());

				lastQueueLog = now;
			}
		 }

		// 시스템 패킷 처리
		for (auto& sysPacket : mSystemReadBuffer)
		{
			ProcessRecvPacket(sysPacket.ClientIndex, sysPacket.Generation, sysPacket.PacketId, sysPacket.DataSize, sysPacket.pDataPtr);
		}
		mSystemReadBuffer.clear();


		// 일반 패킷 처리
		for (auto& task : mReadBuffer)
		{
			auto pUser = mUserManager->GetUserByConnIdx(task.clientIndex);
			if (!pUser)
				continue;

			// 죽어가는 유저의 잔여 패킷은 버림 
			if (pUser->IsDisconnecting())
			{
				// 링버퍼에 남은 데이터 전부 소진(drain)
				pUser->ClearPacketBuffer();  // 링버퍼 한방에 초기화
				continue;
			}
			// 독점권 먼저 획득
			if (!pUser->TryAcquireRouting())
				continue;  // IOCP Worker Fast Path 처리 중 -> 안전하게 skip

			auto packetData = pUser->GetPacket(packetBuf, sizeof(packetBuf));
			if (packetData.PacketId == 0)
			{
				pUser->ReleaseRouting();
				continue;
			}

			if (packetData.DataSize == 0 || packetData.DataSize > MAX_SINGLE_PACKET_SIZE)
			{
				//LOG_ERROR("비정상적인 패킷 크기 수신 (해킹 시도)! Size: %d\n", packetData.DataSize);
				//pUser->SetDisconnecting();
				//// 악성 유저이므로 연결을 끊어버림 (Strand를 통해 안전하게 종료 처리)
				//m_strandProcessor.EnqueueJob(nullptr, task.clientIndex, 0, pUser->GetSessionGeneration(), (UINT16)PACKET_ID::SYS_USER_DISCONNECT, 0, nullptr);
				pUser->ReleaseRouting();
				mSessionHandler->ClearConnectionInfo(task.clientIndex);
				continue;
			}

			// 완전한 패킷이 조립되었으므로 활동 시간 갱신
			if (UpdateActivityFunc)
				UpdateActivityFunc(task.clientIndex);

			packetData.ClientIndex = task.clientIndex;
			packetData.Generation = task.generation;

			auto packetId = packetData.PacketId;
			// 시스템 패킷과 로그인 요청 패킷은 세대 검사에서 제외 (무사 통과)
			if (packetId != (UINT16)PACKET_ID::SYS_USER_CONNECT && packetId != (UINT16)PACKET_ID::LOGIN_REQUEST)
			{
				// 이미 로그인된 유저인데 세대가 다르다면 (이전 세대의 지각 패킷) 버림
				if (pUser->GetSessionGeneration() != task.generation)
				{
					LOG_DEBUG("Stale Packet 버림 (Gen: %d vs %d)\n", pUser->GetSessionGeneration(), task.generation);
					pUser->ReleaseRouting();
					continue;
				}
			}

			// 첫 패킷 + 잔여 패킷 공통 처리
			auto processOnePacket = [&](PacketInfo& packetData)
				{
					if (pUser->GetDomainState() == User::DOMAIN_STATE::ROOM)
					{
						auto pRoom = mRoomManager->GetRoomByNumber(pUser->GetRoomIndex());
						if (pRoom == nullptr)
						{
							LOG_ERROR("client %d의 방이 유효하지 않음.\n", task.clientIndex);
							pUser->SetDomainState(User::DOMAIN_STATE::LOGIN);
							pUser->ResetRoom();
							return;
						}
						m_strandProcessor.EnqueueJob(pRoom, task.clientIndex, pRoom->GetGeneration(),pUser->GetSessionGeneration(), packetData.PacketId, packetData.DataSize, packetData.pDataPtr);
					}
					else
					{
						ProcessRecvPacket(packetData.ClientIndex, packetData.Generation, packetData.PacketId, packetData.DataSize, packetData.pDataPtr);
					}
				};
			processOnePacket(packetData);

			// 같은 유저의 링버퍼에 남은 패킷 있으면 계속 처리
			while (true)
			{
				auto nextPacket = pUser->GetPacket(packetBuf, sizeof(packetBuf));
				if (nextPacket.PacketId == 0)
				{
					break;
				}
				nextPacket.ClientIndex = task.clientIndex;
				processOnePacket(nextPacket);
			}

			pUser->ReleaseRouting();  // 루프 끝에 반환
		}
		mReadBuffer.clear();

		
	
		while (true)
		{
			auto task = mRedisManager->TakeResponseTask();
			if (task.TaskID == RedisTaskID::INVALID)
				break;

			ProcessRecvPacket(task.UserIndex, task.Generation, (UINT16)task.TaskID, task.DataSize, task.body);
			//task.release();
		}

		while (auto* pNotify = m_strandProcessor.PopCallback())
			mCallbackDispatcher->Dispatch(pNotify);

	}
	// 이미 연결이 된 유저가 보낸 패킷이 있는지 알아보고 
	// 있으면 처리하고 
	// 시스템 패킷(네트워크가 보낸것, 연결, 연결종료 등..) 을 있으면 처리하고 
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
