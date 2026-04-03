#pragma once

#include "PacketManager.h"
#include "UserManager.h"
#include "RoomManager.h"
#include "ConfigManager.h"
#include "RedisManager.h"
#include "MysqlManager.h"
#include "Define.h"
#include <chrono>
#include <ctime>

PacketManager::PacketManager() = default;
PacketManager::~PacketManager() = default;

void PacketManager::Init(const UINT32 maxClient_)
{
	RegisterHandlers();
	CreateComponent(maxClient_);

	//mRedisManager = new RedisManager;
	//mMySQLManager = new MySQLManager;
	mRedisManager = std::make_unique<RedisManager>();
	mMySQLManager = std::make_unique<MySQLManager>();

	mRedisManager->OnResponsePushed = [this]()
	{
		NotifyPacketEvent();
	};
}

void PacketManager::RegisterHandlers()
{
	// 시스템 패킷 핸들러
	mPacketHandlers[(UINT16)PACKET_ID::SYS_USER_CONNECT] = [this](UINT32 clientIndex, UINT32 generation, UINT16 packetSize, char* pPacket)
		{
			ProcessUserConnect(clientIndex, generation, packetSize, pPacket);
		};
	mPacketHandlers[(UINT16)PACKET_ID::SYS_USER_DISCONNECT] = [this](UINT32 clientIndex, UINT32 generation, UINT16 packetSize, char* pPacket)
		{
			ProcessUserDisconnect(clientIndex, generation, packetSize, pPacket);
		};
	// 로그인 핸들러
    mPacketHandlers[(UINT16)PACKET_ID::LOGIN_REQUEST] = [this](UINT32 clientIndex, UINT32 generation, UINT16 packetSize, char* pPacket)
        {
            ProcessLogin(clientIndex, generation, packetSize, pPacket);
        };
    mPacketHandlers[(UINT16)RedisTaskID::RESPONSE_LOGIN] = [this](UINT32 clientIndex, UINT32 generation, UINT16 packetSize, char* pPacket)
        {
            ProcessLoginDBResult(clientIndex, generation, packetSize, pPacket);
        };
	// 방 관련 핸들러
	mPacketHandlers[(UINT16)PACKET_ID::ROOM_ENTER_REQUEST] = [this](UINT32 clientIndex, UINT32 generation, UINT16 packetSize, char* pPacket)
		{
			ProcessEnterRoom(clientIndex, generation, packetSize, pPacket);
		};

	// 좀비세션 관련 핸들러
	mPacketHandlers[(UINT16)PACKET_ID::SYS_PONG] = [this](UINT32 clientIndex, UINT32 generation, UINT16 packetSize, char* pPacket)
		{
			LOG_DEBUG("[PacketManager] Client Index(%d)로부터 PONG 수신 완료\n", clientIndex);
		};
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
	mRoomManager->Init(startRoomNumber, maxRoomCount, maxRoomUserCount);

	m_strandProcessor.Init(config.JobPoolSize, config.CallbackPoolSize, config.MaxRoomCount);
	m_strandProcessor.SetUserManager(mUserManager.get());
}


bool PacketManager::Run()
{
	const auto& config = ConfigManager::GetInstance().Get();

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
	while (auto* cb = m_strandProcessor.PopCallback())
	{
		m_strandProcessor.FreeCallback(cb);
	}

	// DB 쓰레드 종료(큐 소진후)
	mRedisManager->End();
	mMySQLManager->End();

	FILE* fp = nullptr;
	fopen_s(&fp, "benchmark_result.txt", "a");
	if (fp) {
		fprintf(fp, "=== Benchmark Result ===\n");
		fprintf(fp, "Job Pool Size: %u\n", m_strandProcessor.GetJobPoolSize());
		fprintf(fp, "Alloc Fail Count: %llu\n", m_strandProcessor.GetAllocFailCount());
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
		LOG_ERROR("[ReceivePacketData] Client(%d) 버퍼 오버플로우 → 연결 해제 요청\n", clientIndex_);
		return false;
	}
	//pUser->SetPacketData(dataSize_, pData_); // 링버퍼에 저장 후
	// queue에 알려줌 어떤 client의 요청이 왔는지
	EnqueuePacketData(clientIndex_, generation_);
	return true;
}


void PacketManager::ProcessPacket()
{
	while (mIsRunProcessThread)
	{
		// 1. wait + swap (lock은 1회만)
		{
			std::unique_lock<std::mutex> lock(mLock);

			mPacketEventCV.wait(lock, [this]()
			{
				if (!mIsRunProcessThread)
					return true;

				//if (!mSystemPacketQueue.empty())
				//	return true;

				//if (!mInComingPacketUserIndex.empty())
				//	return true;

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
			static auto lastQueueLog = std::chrono::steady_clock::now();
			auto now = std::chrono::steady_clock::now();
			auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastQueueLog).count();
			if (elapsed >= 5)
			{
				LOG_DEBUG("[QueueDepth] batch_size=%zu  sys_batch=%zu\n", mReadBuffer.size(), mSystemReadBuffer.size());
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
				if (pUser->IsDisconnecting())
				{
					pUser->ClearPacketBuffer();  // 링버퍼 한방에 초기화
					continue;
				}
				continue;
			}

			auto packetData = pUser->GetPacket();
			if (packetData.PacketId == 0)
				continue;

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
				auto nextPacket = pUser->GetPacket();
				if (nextPacket.PacketId == 0)
				{
					break;
				}
				nextPacket.ClientIndex = task.clientIndex;
				processOnePacket(nextPacket);
			}

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

		while (auto* cb = m_strandProcessor.PopCallback())
		{
			auto pUser = mUserManager->GetUserByConnIdx(cb->clientIndex);

			// 유저 없음 or 세대 불일치
			if (!pUser || pUser->GetSessionGeneration() != cb->sessionGeneration)
			{
				m_strandProcessor.FreeCallback(cb);
				continue;
			}
			switch (cb->type)
			{
			case StrandCallbackType::FREE_USER:
			{
				mUserManager->DeleteUserInfo(pUser);
				break;
			}
			case StrandCallbackType::USER_LEFT_ROOM:
			{
				pUser->SetDomainState(User::DOMAIN_STATE::LOGIN);
				pUser->ResetRoom();
				break;
			}
			case StrandCallbackType::USER_ENTERED_ROOM:
			{
				if (cb->result == (UINT16)ERROR_CODE::NONE)
				{
					pUser->EnterRoom(cb->roomNumber);

					// MySQL 로그
					MySQLRoomEventReq req{};
					strcpy_s(req.UserID, pUser->GetUserID().c_str());
					req.RoomNumber = cb->roomNumber;
					req.EventType = RoomEventType::ENTER;
					req.TimeStampSec = (UINT64)time(nullptr);

					MySQLTask task{};
					task.UserIndex = cb->clientIndex;
					task.TaskID = MySQLTaskID::INSERT_ROOM_EVENT;
					task.DataSize = sizeof(MySQLRoomEventReq);
					//task.pData = new char[task.DataSize];
					CopyMemory(task.body, &req, task.DataSize);
					mMySQLManager->PushTask(task);
				}
				
				break;
			}
			}// switch의 }
			m_strandProcessor.FreeCallback(cb);
		}

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
		task.generation = generation_;  // ★ ClientSession의 세대를 사용
		mWriteBuffer.push_back(task);
	}
	// 큐에 새로운 패킷이 들어왔다고 처리 쓰레드 깨움
	NotifyPacketEvent();
	//mInComingPacketUserIndex.push_back(clientIndex_);
}


void PacketManager::ProcessUserConnect(UINT32 clientIndex_, UINT32 generation_, UINT16 packetSize_, char* pPacket)
{
	LOG_DEBUG("[ProcessUserConnect] ClientIndex : %d\n", clientIndex_);
	auto pUser = mUserManager->GetUserByConnIdx(clientIndex_);
	pUser->SetSessionGeneration(generation_);  //접속 시점에 세대 설정
}

void PacketManager::ProcessUserDisconnect(UINT32 clientIndex_, UINT32 generation_, UINT16 packetSize_, char* pPacket)
{
	auto pUser = mUserManager->GetUserByConnIdx(clientIndex_);
	if (pUser == nullptr) return;

	// Stale Disconnect 방어
	if (pUser->GetSessionGeneration() != generation_)
	{
		LOG_DEBUG("Stale Disconnect 무시 (Gen: %d vs %d)\n",
			generation_, pUser->GetSessionGeneration());
		return;
	}
	LOG_DEBUG("[ProcessUserDisconnect] ClientIndex : %d\n", clientIndex_);
	ClearConnectionInfo(clientIndex_);
	
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


void PacketManager::ProcessLogin(UINT32 clientIndex_, UINT32 generation_,  UINT16 packetSize_, char* pPacket_) // 최대 접속자 수, 중복 정도만 확인
{
	if (LOGIN_REQUEST_PACKET_SIZE != packetSize_)
	{
		return;
	}

	auto pLoginReqPacket = reinterpret_cast<LOGIN_REQUEST_PACKET*>(pPacket_);
	
	auto pUserID = pLoginReqPacket->UserID;
	LOG_DEBUG("Requested user ID : %s\n", pUserID);

	//// 로드 테스트용 코드
	//// id가 test_user일시 인증 뛰어넘고 즉시 로그인 처리
	const char* prefix = "test_user";
	size_t prefixLen = strlen(prefix);
	if (strncmp(pUserID, prefix, prefixLen) == 0) // prefic로 시작하는 아이디면
	{
		// usermanager에 사용자 추가
		auto pUser = mUserManager->GetUserByConnIdx(clientIndex_);

		LOGIN_RESPONSE_PACKET loginResPacket;
		loginResPacket.PacketId = (UINT16)PACKET_ID::LOGIN_RESPONSE;
		loginResPacket.PacketLength = sizeof(loginResPacket);

		auto result = mUserManager->Adduser(pUserID, clientIndex_);
		loginResPacket.Result = (UINT16)result;  // 성공이면 NONE, 실패면 에러코드

		if (result == ERROR_CODE::NONE)
		{
			pUser->SetSessionGeneration(generation_);
			mUserManager->IncreaseUserCnt();
			LOG_DEBUG("[Load Test] Dummy login Success: %s\n", pUserID);
		}

		SendPacketFunc(clientIndex_, generation_, sizeof(LOGIN_RESPONSE_PACKET), (char*)&loginResPacket);
		
		return;
	}

	//// 여기까지가 로드테스트용도를 위한 코드


	LOGIN_RESPONSE_PACKET loginResPacket;
	loginResPacket.PacketId = (UINT16)PACKET_ID::LOGIN_RESPONSE;
	loginResPacket.PacketLength = sizeof(LOGIN_RESPONSE_PACKET);

	// 디버깅
	auto existingIndex = mUserManager->FindUserIndexByID(pUserID);
	LOG_DEBUG("기존 사용자 검색 : UserID = %s -> Index = %d\n", pUserID, existingIndex);

	// 기존 접속 유저 찾은 경우(중복 로그인)
	if (existingIndex != -1)
	{
		LOG_DEBUG("중복 로그인 차단! UserID = %s\n", pUserID);
		loginResPacket.Result = (UINT16)ERROR_CODE::LOGIN_USER_ALREADY;
		SendPacketFunc(clientIndex_, generation_, sizeof(LOGIN_RESPONSE_PACKET), (char*)&loginResPacket);
		return; // 중복이므로 차단
	}


	LOG_DEBUG("Login To Redis USER ID : %s\n", pUserID);
	


	// 접속자 수가 최대수인지 확인
	if (mUserManager->GetCurrentUserCnt() >= mUserManager->GetMaxUserCnt())
	{
		// 접속자 수가 최대라면 접속 불가
		loginResPacket.Result = (UINT16)ERROR_CODE::LOGIN_USER_USED_ALL_OBJ;
		SendPacketFunc(clientIndex_, generation_, sizeof(LOGIN_RESPONSE_PACKET), (char*)&loginResPacket);
		return;
	}


	// 신규로그인 redis 인증요청
	LOG_DEBUG("새로운 사용자 - Redis로 전송\n");

	RedisLoginReq redisReq;
	CopyMemory(redisReq.UserID, pLoginReqPacket->UserID, (MAX_USER_ID_LENGTH + 1));
	CopyMemory(redisReq.UserPW, pLoginReqPacket->UserPW, (MAX_USER_PW_LENGTH + 1));

	RedisTask redistask;
	redistask.UserIndex = clientIndex_;
	redistask.Generation = generation_;
	redistask.TaskID = RedisTaskID::REQUEST_LOGIN;
	redistask.DataSize = sizeof(RedisLoginReq);

	//redistask.pData = new char[redistask.DataSize];
	CopyMemory(redistask.body, (char*)&redisReq, redistask.DataSize);
	mRedisManager->PushTask(redistask);

	LOG_DEBUG("Login To Redis USER ID : %s\n", pUserID);
}

void PacketManager::ProcessLoginDBResult(UINT32 clientIndex_, UINT32 generation_, UINT16 packetSize_, char* pPacket_)
{
	LOG_DEBUG("ProcessLoginDBResult. UserIndex : %d \n", clientIndex_);

	auto pBody = (RedisLoginRes*)pPacket_;

	// redis 성공시
	if (pBody->Result == (UINT16)ERROR_CODE::NONE)
	{
		LOG_DEBUG("[DEBUG] Login successful for UserID: '%s'\n", pBody->UserID);

		//OnLoginSuccess(clientIndex_, pBody->UserID);
		//UserManager에 사용자 추가
		auto result = mUserManager->Adduser(pBody->UserID, clientIndex_);
		if (result != ERROR_CODE::NONE) {
			LOG_ERROR("Failed to add user to UserManager\n");
			pBody->Result = (UINT16)ERROR_CODE::LOGIN_USER_USED_ALL_OBJ;
		}
		else {
			auto pUser = mUserManager->GetUserByConnIdx(clientIndex_);
			pUser->SetSessionGeneration(generation_);
			LOG_DEBUG("User added successfully. UserID: '%s'\n", pUser->GetUserID().c_str());
			mUserManager->IncreaseUserCnt();

			// MySQL: 로그인 기록
			MySQLLoginEventReq mysqlReq{};
			strcpy_s(mysqlReq.UserID, pUser->GetUserID().c_str());
			mysqlReq.TimestampSec = (UINT64)time(nullptr);

			MySQLTask mysqlTask;
			mysqlTask.UserIndex = clientIndex_;
			mysqlTask.TaskID = MySQLTaskID::INSERT_LOGIN_EVENT;
			mysqlTask.DataSize = sizeof(MySQLLoginEventReq);

			//mysqlTask.pData = new char[mysqlTask.DataSize];
			CopyMemory(mysqlTask.body, &mysqlReq, mysqlTask.DataSize);
			mMySQLManager->PushTask(mysqlTask);
		}
	}

	LOGIN_RESPONSE_PACKET loginResPacket;
	loginResPacket.PacketId = (UINT16)PACKET_ID::LOGIN_RESPONSE;
	loginResPacket.PacketLength = sizeof(LOGIN_RESPONSE_PACKET);
	loginResPacket.Result = pBody->Result;
	SendPacketFunc(clientIndex_, generation_, sizeof(LOGIN_RESPONSE_PACKET), (char*)&loginResPacket);
}

void PacketManager::ClearConnectionInfo(INT32 clientIndex_)
{
	// 로직은 이미 processPacket에서 진행
	//{
	//	// queue에서 해당 유저의 대기중인 task 제거
	//	std::lock_guard<std::mutex>guard(mLock);
	//	//auto it = mInComingPacketUserIndex.begin();
	//	auto it = mWriteBuffer.begin();
	//	while (it != mWriteBuffer.end())
	//	{
	//		if (it->clientIndex == clientIndex_)
	//		{
	//			it = mWriteBuffer.erase(it);
	//			LOG_DEBUG("remove enqueue packetdata for disconnected used : %d\n", clientIndex_);
	//		}
	//		else
	//			++it;
	//	}
	//}

	auto pReqUser = mUserManager->GetUserByConnIdx(clientIndex_);

	if (pReqUser->GetDomainState() == User::DOMAIN_STATE::ROOM)
	{
		//auto roomNum = pReqUser->GetRoomIndex();
		//mRoomManager->LeaveUser(roomNum, pReqUser);

		pReqUser->SetDisconnecting(); // flag만 세우고 DOMAINSTATE는 ROOM유지
		auto pRoom = mRoomManager->GetRoomByNumber(pReqUser->GetRoomIndex());
		if (pRoom == nullptr)
		{
			LOG_ERROR("클라이언트 %d의 방이 유효하지 않음. 유저 상태 초기화.\n", clientIndex_);
			mUserManager->DeleteUserInfo(pReqUser);
			return;
		}
		// roomIndex를 정상적으로 읽을 수 있음
		m_strandProcessor.EnqueueJob(pRoom, clientIndex_, pRoom->GetGeneration(),pReqUser->GetSessionGeneration(), (UINT16)PACKET_ID::SYS_USER_DISCONNECT, 0, nullptr);

	}
	else
	{
		// 방에 없는 유저 -> 즉시정리 Strand거칠 필요 X
		if (pReqUser->GetDomainState() != User::DOMAIN_STATE::NONE)
		{
			mUserManager->DeleteUserInfo(pReqUser);
		}
	}

}

void PacketManager::ProcessEnterRoom(UINT32 clientIndex_, UINT32 generation_, UINT16 packetSize_, char* pPacket)
{
	UNREFERENCED_PARAMETER(packetSize_);

	//  방 입장 요청 패킷을 받는다.
	auto pRoomEnterReqPacket = reinterpret_cast<ROOM_ENTER_REQUEST_PACKET*>(pPacket);
	//	유효한 유저인지 검사한다.
	auto pReqUser = mUserManager->GetUserByConnIdx(clientIndex_);
	if (pReqUser == nullptr)
	{
		LOG_ERROR("유효하지 않은 유저 !. ClientIndex : %d\n", clientIndex_);
		return;
	}

	// 로그인 상태 검증 추가
	if (pReqUser->GetDomainState() != User::DOMAIN_STATE::LOGIN)
	{
		ROOM_ENTER_RESPONSE_PACKET errorPacket;
		errorPacket.PacketId = (UINT16)PACKET_ID::ROOM_ENTER_RESPONSE;
		errorPacket.PacketLength = sizeof(ROOM_ENTER_RESPONSE_PACKET);
		errorPacket.Result = (UINT16)ERROR_CODE::ENTER_ROOM_INVALID_USER_STATUS;
		SendPacketFunc(clientIndex_, generation_, sizeof(ROOM_ENTER_RESPONSE_PACKET), (char*)&errorPacket);
		return;
		
	}

	// 방 존재 여부 확인
	auto pRoom = mRoomManager->GetRoomByNumber(pRoomEnterReqPacket->RoomNumber);
	if (pRoom == nullptr)
	{
		ROOM_ENTER_RESPONSE_PACKET errorPacket;
		errorPacket.PacketId = (UINT16)PACKET_ID::ROOM_ENTER_RESPONSE;
		errorPacket.PacketLength = sizeof(ROOM_ENTER_RESPONSE_PACKET);
		errorPacket.Result = (UINT16)ERROR_CODE::ROOM_INVALID_INDEX;
		SendPacketFunc(clientIndex_, generation_, sizeof(ROOM_ENTER_RESPONSE_PACKET), (char*)&errorPacket);
		return;
	}

	// 실제 입장 Strand에 넘김
	m_strandProcessor.EnqueueJob(pRoom, clientIndex_, pRoom->GetGeneration(),pReqUser->GetSessionGeneration(), (UINT16)PACKET_ID::ROOM_ENTER_REQUEST, packetSize_, pPacket);

	////	응답 패킷을 생성하고
	//ROOM_ENTER_RESPONSE_PACKET roomEnterResPacket;
	//roomEnterResPacket.PacketId = (UINT16)PACKET_ID::ROOM_ENTER_RESPONSE;
	//roomEnterResPacket.PacketLength = sizeof(ROOM_ENTER_RESPONSE_PACKET);
	////	RoomManager 객체의 EnterUser 함수를 호출한다.
	//
	//roomEnterResPacket.Result = mRoomManager->EnterUser(pRoomEnterReqPacket->RoomNumber, pReqUser);
	//
	//// 방 입장 성공 시 방 전체에 입장 알림
	//if (roomEnterResPacket.Result == (UINT16)ERROR_CODE::NONE)
	//{
	//	// MySQL : 방 입장 로그
	//	MySQLRoomEventReq req{};
	//	strcpy_s(req.UserID, pReqUser->GetUserID().c_str());
	//	req.RoomNumber = pRoomEnterReqPacket->RoomNumber;
	//	req.EventType = RoomEventType::ENTER;
	//	req.TimeStampSec = (UINT64)time(nullptr);

	//	MySQLTask task{};
	//	task.UserIndex = clientIndex_;
	//	task.TaskID = MySQLTaskID::INSERT_ROOM_EVENT;
	//	task.DataSize = sizeof(MySQLRoomEventReq);

	//	task.pData = new char[task.DataSize];
	//	CopyMemory(task.pData, &req, task.DataSize);
	//	mMySQLManager->PushTask(task);

	//	auto pRoom = mRoomManager->GetRoomByNumber(pRoomEnterReqPacket->RoomNumber);
	//	if (pRoom != nullptr)
	//	{
	//		// 임시 채팅 패킷 생성
	//		ROOM_CHAT_REQUEST_PACKET tempChatPacket;
	//		tempChatPacket.PacketId = (UINT16)PACKET_ID::ROOM_CHAT_REQUEST;
	//		tempChatPacket.PacketLength = sizeof(ROOM_CHAT_REQUEST_PACKET);

	//		sprintf_s(tempChatPacket.Message, "entered the room.");

	//		// 방 전체에 알림
	//		pRoom->NotifyChat(clientIndex_, pReqUser->GetUserID().c_str(), (char*)&tempChatPacket);
	//	}
	//}

	//
	////	해당 값의 결과를 응답 패킷의 데이터에 넣어서 전송한다.
	//SendPacketFunc(clientIndex_, generation_, sizeof(ROOM_ENTER_RESPONSE_PACKET), (char*)&roomEnterResPacket);
	//LOG_DEBUG("Enter Room Res Packet Send ! \n");

}



void PacketManager::NotifyPacketEvent()
{
	mPacketEventCV.notify_one();
}
