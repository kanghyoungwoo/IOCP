#pragma once

//#include <utility>
//#include <cstring>

#include "PacketManager.h"
#include "UserManager.h"
//#include "RedisTaskDefine.h"
#include "ConfigManager.h"
#include "RedisManager.h"
#include "MysqlManager.h"
#include "Define.h"
#include <chrono>
#include <ctime>



void PacketManager::Init(const UINT32 maxClient_)
{
	//mRecvFunctionDictionary = std::unordered_map<int, PROCESS_RECV_PACKET_FUNCTION>();

	//mRecvFunctionDictionary[(int)PACKET_ID::SYS_USER_CONNECT] = &PacketManager::ProcessUserConnect;
	//mRecvFunctionDictionary[(int)PACKET_ID::SYS_USER_DISCONNECT] = &PacketManager::ProcessUserDisconnect;

	//mRecvFunctionDictionary[(int)PACKET_ID::LOGIN_REQUEST] = &PacketManager::ProcessLogin;
	//mRecvFunctionDictionary[(int)RedisTaskID::RESPONSE_LOGIN] = &PacketManager::ProcessLoginDBResult;

	//mRecvFunctionDictionary[(int)PACKET_ID::ROOM_ENTER_REQUEST] = &PacketManager::ProcessEnterRoom;
	//mRecvFunctionDictionary[(int)PACKET_ID::ROOM_LEAVE_REQUEST] = &PacketManager::ProcessLeaveRoom;
	//mRecvFunctionDictionary[(int)PACKET_ID::ROOM_CHAT_REQUEST] = &PacketManager::ProcessRoomChatMessage;
	RegisterHandlers();
	CreateComponent(maxClient_);

	mRedisManager = new RedisManager;
	mMySQLManager = new MySQLManager;

	mRedisManager->OnResponsePushed = [this]()
	{
		NotifyPacketEvent();
	};
}

void PacketManager::RegisterHandlers()
{
	// 시스템 패킷 핸들러
	mPacketHandlers[(UINT16)PACKET_ID::SYS_USER_CONNECT] = [this](UINT32 clientIndex, UINT16 packetSize, char* pPacket)
		{
			ProcessUserConnect(clientIndex, packetSize, pPacket);
		};
	mPacketHandlers[(UINT16)PACKET_ID::SYS_USER_DISCONNECT] = [this](UINT32 clientIndex, UINT16 packetSize, char* pPacket)
		{
			ProcessUserDisconnect(clientIndex, packetSize, pPacket);
		};
	// 로그인 핸들러
	mPacketHandlers[(UINT16)PACKET_ID::LOGIN_REQUEST] = [this](UINT32 clientIndex, UINT16 packetSize, char* pPacket)
		{
			ProcessLogin(clientIndex, packetSize, pPacket);
		};
	mPacketHandlers[(UINT16)RedisTaskID::RESPONSE_LOGIN] = [this](UINT32 clientIndex, UINT16 packetSize, char* pPacket)
		{
			ProcessLoginDBResult(clientIndex, packetSize, pPacket);
		};
	// 방 관련 핸들러
	mPacketHandlers[(UINT16)PACKET_ID::ROOM_ENTER_REQUEST] = [this](UINT32 clientIndex, UINT16 packetSize, char* pPacket)
		{
			ProcessEnterRoom(clientIndex, packetSize, pPacket);
		};
	//mPacketHandlers[(UINT16)PACKET_ID::ROOM_LEAVE_REQUEST] = [this](UINT32 clientIndex, UINT16 packetSize, char* pPacket)
	//	{
	//		ProcessLeaveRoom(clientIndex, packetSize, pPacket);
	//	};
	//mPacketHandlers[(UINT16)PACKET_ID::ROOM_CHAT_REQUEST] = [this](UINT32 clientIndex, UINT16 packetSize, char* pPacket)
	//	{
	//		ProcessRoomChatMessage(clientIndex, packetSize, pPacket);
	//	};

	// 좀비세션 관련 핸들러
	mPacketHandlers[(UINT16)PACKET_ID::SYS_PONG] = [this](UINT32 clientIndex, UINT16 packetSize, char* pPacket)
		{
			// RECV에서 이미 UpdateActivity() 완료. 추가 처리 없음.
			LOG_DEBUG("[PacketManager] Client Index(%d)로부터 PONG 수신 완료 (생존 연장)\n", clientIndex);
		};
}

void PacketManager::CreateComponent(const UINT32 maxClient_)
{
	const auto& config = ConfigManager::GetInstance().Get();
	mUserManager = new UserManager;
	mUserManager->Init(maxClient_);

	UINT32 startRoomNumber = config.StartRoomNumber;
	UINT32 maxRoomUserCount = config.MaxRoomUserCount;
	UINT32 maxRoomCount = config.MaxRoomCount;

	mRoomManager = new RoomManager;
	mRoomManager->SendPacketFunc = SendPacketFunc;
	mRoomManager->Init(startRoomNumber, maxRoomCount, maxRoomUserCount);

	m_strandProcessor.Init(config.JobPoolSize, config.CallbackPoolSize, config.MaxRoomCount);
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
	// 기존 종료 방식
	//mRedisManager->End();
	//mMySQLManager->End();

	//{
	//	std::lock_guard<std::mutex> lock(mLock);
	//	mIsRunProcessThread = false;
	//}
	//// wait중인 processPacket 쓰레드 깨움
	//mPacketEventCV.notify_all();
	////mIsRunProcessThread = false;
	//if (mProcessThead.joinable())
	//{
	//	mProcessThead.join();
	//}

}

// usermanager의 GetUserByConnIdx를 이용하여 유저의 idx를 받은 후 user의 data를 SetPacketData를 통해 set함
bool PacketManager::ReceivePacketData(const UINT32 clientIndex_, const UINT32 dataSize_, char* pData_)
{
	auto pUser = mUserManager->GetUserByConnIdx(clientIndex_);
	if (!pUser->SetPacketData(dataSize_, pData_))
	{
		LOG_ERROR("[ReceivePacketData] Client(%d) 버퍼 오버플로우 → 연결 해제 요청\n", clientIndex_);
		return false;
	}
	//pUser->SetPacketData(dataSize_, pData_); // 링버퍼에 저장 후
	// queue에 알려줌 어떤 client의 요청이 왔는지
	EnqueuePacketData(clientIndex_);
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
			//if (mSystemPacketQueue.empty() && mInComingPacketUserIndex.empty())
			//{
			//	mPacketEventCV.wait_for(lock,
			//		std::chrono::milliseconds(1),
			//		[this]()
			//		{
			//			return !mIsRunProcessThread || !mSystemPacketQueue.empty() || !mInComingPacketUserIndex.empty();
			//		});
			//}
			if (!mIsRunProcessThread)
				break;

			// 더블 버퍼링, swap and release lock
			std::swap(mSystemWriteBuffer, mSystemReadBuffer);
			std::swap(mWriteBuffer, mReadBuffer);
		}

		// lock 해제, 아래는 전부 lock-free
		
		int generationMismatchCount = 0;	// 배치 내 generation 불일치 카운트
		
		// Queue Depth logging (5sec interval)
		{
			static auto lastQueueLog = std::chrono::steady_clock::now();
			auto now = std::chrono::steady_clock::now();
			auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastQueueLog).count();
			if (elapsed >= 5)
			{
				LOG_DEBUG("[QueueDepth] batch_size=%zu  sys_batch=%zu\n", mReadBuffer.size(), mSystemReadBuffer.size());lastQueueLog = now;
			}
		 }

		// 시스템 패킷 처리
		for (auto& sysPacket : mSystemReadBuffer)
		{
			ProcessRecvPacket(sysPacket.ClientIndex, sysPacket.PacketId, sysPacket.DataSize, sysPacket.pDataPtr);
		}
		mSystemReadBuffer.clear();

		////bool isIdle = true;
		//// 시스템 패킷 처리
		//if (auto packetData = DequeSystemPacketData(); packetData.PacketId != 0)
		//{
		//	//isIdle = false;
		//	ProcessRecvPacket(packetData.ClientIndex, packetData.PacketId, packetData.DataSize, packetData.pDataPtr);
		//}

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
				while (pUser->GetPacket().PacketId != 0) {}
				continue;
			}

			if (pUser->GetGeneration() != task.generation)
			{
				//printf("Enqueue Generation: %d와 현재 Generation: %d가 서로 맞지 않습니다.\n", task.generation, pUser->GetGeneration());
				continue;
			}

			auto packetData = pUser->GetPacket();
			if (packetData.PacketId == 0)
				continue;

			// 완전한 패킷이 조립되었으므로 활동 시간 갱신
			if (UpdateActivityFunc)
				UpdateActivityFunc(task.clientIndex);

			packetData.ClientIndex = task.clientIndex;

			//if (pUser->GetDomainState() == User::DOMAIN_STATE::ROOM)
			//{
			//	auto pRoom = mRoomManager->GetRoomByNumber(pUser->GetRoomIndex());
			//	m_strandProcessor.EnqueueJob(pRoom, task.clientIndex,
			//		pRoom->GetGeneration(), packetData.PacketId,
			//		packetData.DataSize, packetData.pDataPtr);
			//}
			//else
			//{
			//	ProcessRecvPacket(packetData.ClientIndex, packetData.PacketId, packetData.DataSize, packetData.pDataPtr);
			//}
			// 
			// 첫 패킷 + 잔여 패킷 공통 처리
			auto processOnePacket = [&](PacketInfo& packetData)
				{
					if (pUser->GetDomainState() == User::DOMAIN_STATE::ROOM)
					{
						auto pRoom = mRoomManager->GetRoomByNumber(pUser->GetRoomIndex());
						m_strandProcessor.EnqueueJob(pRoom, task.clientIndex, pRoom->GetGeneration(), packetData.PacketId, packetData.DataSize, packetData.pDataPtr);
					}
					else
					{
						ProcessRecvPacket(packetData.ClientIndex, packetData.PacketId, packetData.DataSize, packetData.pDataPtr);
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

			ProcessRecvPacket(task.UserIndex, (UINT16)task.TaskID, task.DataSize, task.pData);
			task.release();
		}

		while (auto* cb = m_strandProcessor.PopCallback())
		{
			switch (cb->type)
			{
			case StrandCallbackType::FREE_USER:
				mUserManager->DeleteUserInfo(
					mUserManager->GetUserByConnIdx(cb->clientIndex));
				break;
			case StrandCallbackType::USER_LEFT_ROOM:
			{
				auto pUser = mUserManager->GetUserByConnIdx(cb->clientIndex);
				if (pUser)
					pUser->SetDomainState(User::DOMAIN_STATE::LOGIN);
				break;
			}
			}
			m_strandProcessor.FreeCallback(cb);
		}

	}
	// 이미 연결이 된 유저가 보낸 패킷이 있는지 알아보고 
	// 있으면 처리하고 
	// 시스템 패킷(네트워크가 보낸것, 연결, 연결종료 등..) 을 있으면 처리하고 
}

void PacketManager::EnqueuePacketData(const UINT32 clientIndex_)
{
	{
		std::lock_guard<std::mutex> guard(mLock);
		auto pUser = mUserManager->GetUserByConnIdx(clientIndex_);
		if (pUser)
		{
			PacketTask task;
			task.clientIndex = clientIndex_;
			task.generation = pUser->GetGeneration();	// 현재 generation
			//mInComingPacketUserIndex.push_back(task);
			// 더블 버퍼링 구현
			mWriteBuffer.push_back(task);
		}
	}
	// 큐에 새로운 패킷이 들어왔다고 처리 쓰레드 깨움
	NotifyPacketEvent();
	//mInComingPacketUserIndex.push_back(clientIndex_);
}

//PacketInfo PacketManager::DequePacketData()
//{
//	//UINT32 userIndex = 0;
//	PacketTask task;
//	// 요청을 보낸 유저가 있는지 확인 후 
//	// empty면 리턴
//	{
//		std::lock_guard<std::mutex>guard(mLock);
//		if (mInComingPacketUserIndex.empty())
//		{
//			return PacketInfo();
//		}
//		// 있으면 데이터를 뽑아내고 
//		// user index를 통해서 user 객체를 알아내고 링버퍼를 이용
//		//userIndex = mInComingPacketUserIndex.front();
//		task = mInComingPacketUserIndex.front();
//		mInComingPacketUserIndex.pop_front();
//	}
//	auto pUser = mUserManager->GetUserByConnIdx(task.clientIndex);
//	if (!pUser) // 유저가 이미 삭제됐으면
//		return PacketInfo();
//	if (pUser->GetGeneration() != task.generation)
//	{
//		// Generation 불일치로 패킷 처리 중단
//		printf("enqueue generation: %d 이 current generation: %d 맞지 않습니다\n", task.generation, pUser->GetGeneration());
//		return PacketInfo();
//	}
//
//	auto packetData = pUser->GetPacket();
//	packetData.ClientIndex = task.clientIndex;
//
//	return packetData;
//}

void PacketManager::ProcessUserConnect(UINT32 clientIndex_, UINT16 packetSize_, char* pPacket)
{
	LOG_DEBUG("[ProcessUserConnect] ClientIndex : %d\n", clientIndex_);
	auto pUser = mUserManager->GetUserByConnIdx(clientIndex_);
	//pUser->Clear();
}

void PacketManager::ProcessUserDisconnect(UINT32 clientIndex_, UINT16 packetSize_, char* pPacket)
{
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


//PacketInfo PacketManager::DequeSystemPacketData()
//{
//	std::lock_guard<std::mutex>guard(mLock);
//	if (mSystemPacketQueue.empty())
//	{
//		return PacketInfo();
//	}
//	auto packetData = mSystemPacketQueue.front();
//	mSystemPacketQueue.pop_front();
//	return packetData;
//}


void PacketManager::ProcessRecvPacket(const UINT32 clientIndex_, const UINT16 packetId_, const UINT16 packetSize_, char* pPacket_)
{
	// 패킷 id를 찾아서 
	// 관계된 객체를 할당해서 처리
	//auto iter = mRecvFunctionDictionary.find(packetId_);
	//if (iter != mRecvFunctionDictionary.end())
	//{
	//	(this->*(iter->second))(clientIndex_, packetSize_, pPacket_);
	//}
	//// 잘못된 패킷 처리
	//else
	//{
	//	printf("알 수 없는 패킷 ID : %d (ClientIndex: %d)\n", packetId_, clientIndex_);
	//}

	// 패킷key값(packetid)을 찾으면 
	auto it = mPacketHandlers.find(packetId_);
	if (it != mPacketHandlers.end())
		it->second(clientIndex_, packetSize_, pPacket_);
	else
		LOG_ERROR("알 수 없는 패킷 ID : %d (ClientIndex: %d)\n", packetId_, clientIndex_);

}


void PacketManager::ProcessLogin(UINT32 clientIndex_, UINT16 packetSize_, char* pPacket_) // 최대 접속자 수, 중복 정도만 확인
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
		pUser->SetLogin(pUserID);
		mUserManager->Adduser(pUserID, clientIndex_);

		LOGIN_RESPONSE_PACKET loginResPacket;
		loginResPacket.PacketId = (UINT16)PACKET_ID::LOGIN_RESPONSE;
		loginResPacket.PacketLength = sizeof(loginResPacket);
		loginResPacket.Result = (UINT16)ERROR_CODE::NONE;
		SendPacketFunc(clientIndex_, sizeof(LOGIN_RESPONSE_PACKET), (char*)&loginResPacket);

		LOG_DEBUG("[Load Test] Dummy login Success: %s\n", pUserID);
		
		return;
	}

	//// 여기까지가 로드테스트용도를 위한 코드


	LOGIN_RESPONSE_PACKET loginResPacket;
	loginResPacket.PacketId = (UINT16)PACKET_ID::LOGIN_RESPONSE;
	loginResPacket.PacketLength = sizeof(LOGIN_RESPONSE_PACKET);

	// 디버깅
	auto existingIndex = mUserManager->FindUserIndexByID(pUserID);
	LOG_DEBUG("기존 사용자 검색 : UserID = %s -> Index = %d\n", pUserID, existingIndex);

	if (existingIndex == -1)
	{
		LOG_DEBUG("새로운 사용자 - Redis로 전송\n");
		// Redis 요청
		LOG_DEBUG("Login To Redis USER ID : %s\n", pUserID);
	}
	else
	{
		LOG_DEBUG("중복 로그인 차단! UserID='%s', 기존Index=%d, 새요청Index=%d\n",
			pUserID, existingIndex, clientIndex_);
		loginResPacket.Result = (UINT16)ERROR_CODE::LOGIN_USER_ALREADY;
		SendPacketFunc(clientIndex_, sizeof(LOGIN_RESPONSE_PACKET), (char*)&loginResPacket);
		LOG_DEBUG("중복 로그인 거부 응답 전송 완료\n");
		return;
	}

	// 접속자 수가 최대수인지 확인
	if (mUserManager->GetCurrentUserCnt() >= mUserManager->GetMaxUserCnt())
	{
		// 접속자 수가 최대라면 접속 불가
		loginResPacket.Result = (UINT16)ERROR_CODE::LOGIN_USER_USED_ALL_OBJ;
		SendPacketFunc(clientIndex_, sizeof(LOGIN_RESPONSE_PACKET), (char*)&loginResPacket);
		return;
	}

	if (mUserManager->FindUserIndexByID(pUserID) == -1)
	{
		RedisLoginReq redisReq;

		CopyMemory(redisReq.UserID, pLoginReqPacket->UserID, (MAX_USER_ID_LENGTH + 1));
		CopyMemory(redisReq.UserPW, pLoginReqPacket->UserPW, (MAX_USER_PW_LENGTH + 1));

		RedisTask redistask;
		redistask.UserIndex = clientIndex_;
		redistask.TaskID = RedisTaskID::REQUEST_LOGIN;
		redistask.DataSize = sizeof(RedisLoginReq);

		//redistask.pData = new char[redistask.DataSize];
		//CopyMemory(redistask.pData, (char*)&redisReq, redistask.DataSize);
		//mRedisManager->PushTask(redistask);

		LOG_DEBUG("Login To Redis USER ID : %s\n", pUserID);
	}
	else
	{
		// 접속중인 유저라면 실패
		loginResPacket.Result = (UINT16)ERROR_CODE::LOGIN_USER_ALREADY;
		SendPacketFunc(clientIndex_, sizeof(LOGIN_RESPONSE_PACKET), (char*)&loginResPacket);
		return;
	}
}

void PacketManager::ProcessLoginDBResult(UINT32 clientIndex_, UINT16 packetSize_, char* pPacket_)
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
			//CopyMemory(mysqlTask.pData, &mysqlReq, mysqlTask.DataSize);
			//mMySQLManager->PushTask(mysqlTask);
		}
	}

	LOGIN_RESPONSE_PACKET loginResPacket;
	loginResPacket.PacketId = (UINT16)PACKET_ID::LOGIN_RESPONSE;
	loginResPacket.PacketLength = sizeof(LOGIN_RESPONSE_PACKET);
	loginResPacket.Result = pBody->Result;
	SendPacketFunc(clientIndex_, sizeof(LOGIN_RESPONSE_PACKET), (char*)&loginResPacket);
}

void PacketManager::ClearConnectionInfo(INT32 clientIndex_)
{
	{
		// queue에서 해당 유저의 대기중인 task 제거
		std::lock_guard<std::mutex>guard(mLock);
		//auto it = mInComingPacketUserIndex.begin();
		auto it = mWriteBuffer.begin();
		while (it != mWriteBuffer.end())
		{
			if (it->clientIndex == clientIndex_)
			{
				it = mWriteBuffer.erase(it);
				LOG_DEBUG("remove enqueue packetdata for disconnected used : %d\n", clientIndex_);
			}
			else
				++it;
		}
	}

	auto pReqUser = mUserManager->GetUserByConnIdx(clientIndex_);

	if (pReqUser->GetDomainState() == User::DOMAIN_STATE::ROOM)
	{
		//auto roomNum = pReqUser->GetRoomIndex();
		//mRoomManager->LeaveUser(roomNum, pReqUser);

		pReqUser->SetDisconnecting(); // flag만 세우고 DOMAINSTATE는 ROOM유지
		auto pRoom = mRoomManager->GetRoomByNumber(pReqUser->GetRoomIndex());
		// roomIndex를 정상적으로 읽을 수 있음
		m_strandProcessor.EnqueueJob(pRoom, clientIndex_, pRoom->GetGeneration(), (UINT16)PACKET_ID::SYS_USER_DISCONNECT, 0, nullptr);

	}
	else
	{
		// 방에 없는 유저 -> 즉시정리 Strand거칠 필요 X
		if (pReqUser->GetDomainState() != User::DOMAIN_STATE::NONE)
		{
			mUserManager->DeleteUserInfo(pReqUser);
		}
	}

	//if (pReqUser->GetDomainState() != User::DOMAIN_STATE::NONE)
	//{
	//	mUserManager->DeleteUserInfo(pReqUser);
	//}
}

void PacketManager::ProcessEnterRoom(UINT32 clientIndex_, UINT16 packetSize_, char* pPacket)
{
	UNREFERENCED_PARAMETER(packetSize_);

	//  방 입장 요청 패킷을 받는다.
	auto pRoomEnterReqPacket = reinterpret_cast<ROOM_ENTER_REQUEST_PACKET*>(pPacket);

	//	유효한 유저인지 검사한다.
	auto pReqUser = mUserManager->GetUserByConnIdx(clientIndex_);
	if (!pReqUser || pReqUser == nullptr)
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
		SendPacketFunc(clientIndex_, sizeof(ROOM_ENTER_RESPONSE_PACKET), (char*)&errorPacket);
		return;
		
	}

	//	응답 패킷을 생성하고
	ROOM_ENTER_RESPONSE_PACKET roomEnterResPacket;
	roomEnterResPacket.PacketId = (UINT16)PACKET_ID::ROOM_ENTER_RESPONSE;
	roomEnterResPacket.PacketLength = sizeof(ROOM_ENTER_RESPONSE_PACKET);
	//	RoomManager 객체의 EnterUser 함수를 호출한다.
	
	roomEnterResPacket.Result = mRoomManager->EnterUser(pRoomEnterReqPacket->RoomNumber, pReqUser);
	
	// 방 입장 성공 시 방 전체에 입장 알림
	if (roomEnterResPacket.Result == (UINT16)ERROR_CODE::NONE)
	{
		// MySQL : 방 입장 로그
		MySQLRoomEventReq req{};
		strcpy_s(req.UserID, pReqUser->GetUserID().c_str());
		req.RoomNumber = pRoomEnterReqPacket->RoomNumber;
		req.EventType = RoomEventType::ENTER;
		req.TimeStampSec = (UINT64)time(nullptr);

		MySQLTask task{};
		task.UserIndex = clientIndex_;
		task.TaskID = MySQLTaskID::INSERT_ROOM_EVENT;
		task.DataSize = sizeof(MySQLRoomEventReq);

		//task.pData = new char[task.DataSize];
		//CopyMemory(task.pData, &req, task.DataSize);
		//mMySQLManager->PushTask(task);

		auto pRoom = mRoomManager->GetRoomByNumber(pRoomEnterReqPacket->RoomNumber);
		if (pRoom != nullptr)
		{
			// 임시 채팅 패킷 생성
			ROOM_CHAT_REQUEST_PACKET tempChatPacket;
			tempChatPacket.PacketId = (UINT16)PACKET_ID::ROOM_CHAT_REQUEST;
			tempChatPacket.PacketLength = sizeof(ROOM_CHAT_REQUEST_PACKET);

			sprintf_s(tempChatPacket.Message, "entered the room.");

			// 방 전체에 알림
			pRoom->NotifyChat(clientIndex_, pReqUser->GetUserID().c_str(), (char*)&tempChatPacket);
		}
	}

	
	//	해당 값의 결과를 응답 패킷의 데이터에 넣어서 전송한다.
	SendPacketFunc(clientIndex_, sizeof(ROOM_ENTER_RESPONSE_PACKET), (char*)&roomEnterResPacket);
	LOG_DEBUG("Enter Room Res Packet Send ! \n");

}

//void PacketManager::ProcessLeaveRoom(UINT32 clientIndex_, UINT16 packetSize_, char* pPacket)
//{
//
//	UNREFERENCED_PARAMETER(packetSize_);
//	UNREFERENCED_PARAMETER(pPacket);
//	//  방 퇴장 요청 패킷을 받는다.
//	auto pRoomLeaveReqPacket = reinterpret_cast<ROOM_LEAVE_REQUEST_PACKET*>(pPacket);
//	//	유효한 유저인지 검사한다.
//	auto pReqUser = mUserManager->GetUserByConnIdx(clientIndex_);
//	if (!pReqUser || pReqUser == nullptr)
//	{
//		printf("유효하지 않은 유저 ! . ClientIndex : %d\n", clientIndex_);
//		return;
//	}
//	// 방 퇴장 전 방 정보 미리 저장
//	// 퇴장 후에는 정보가 사라지기 때문
//	auto roomNumber = pReqUser->GetRoomIndex();
//	auto pRoom = mRoomManager->GetRoomByNumber(roomNumber);
//
//	//	응답 패킷을 생성하고
//	ROOM_LEAVE_RESPONSE_PACKET roomLeaveResPacket;
//	roomLeaveResPacket.PacketId = (UINT16)PACKET_ID::ROOM_LEAVE_RESPONSE;
//	roomLeaveResPacket.PacketLength = sizeof(ROOM_LEAVE_RESPONSE_PACKET);
//
//	//	RoomManager 객체의 leaveUser 함수를 호출한다.
//	roomLeaveResPacket.Result = mRoomManager->LeaveUser(roomNumber, pReqUser);
//
//	// 방 퇴장 성공 시 방 전체에 퇴장 알림
//	if (roomLeaveResPacket.Result == (UINT16)ERROR_CODE::NONE)
//	{	
//		MySQLRoomEventReq req{};
//		strcpy_s(req.UserID, pReqUser->GetUserID().c_str());
//		req.RoomNumber = roomNumber;
//		req.EventType = RoomEventType::LEAVE;
//		req.TimeStampSec = (UINT64)time(nullptr);
//
//		MySQLTask task{};
//		task.UserIndex = clientIndex_;
//		task.TaskID = MySQLTaskID::INSERT_ROOM_EVENT;
//		task.DataSize = sizeof(MySQLRoomEventReq);
//		task.pData = new char[task.DataSize];
//		CopyMemory(task.pData, &req, task.DataSize);
//		mMySQLManager->PushTask(task);
//
//		if (pRoom != nullptr)
//		{
//			// 임시 채팅 패킷 생성
//			ROOM_CHAT_REQUEST_PACKET tempChatPacket;
//			tempChatPacket.PacketId = (UINT16)PACKET_ID::ROOM_CHAT_REQUEST;
//			tempChatPacket.PacketLength = sizeof(ROOM_CHAT_REQUEST_PACKET);
//
//			sprintf_s(tempChatPacket.Message, "has left the room.");
//
//			// 방 전체에 알림
//			pRoom->NotifyChat(clientIndex_, pReqUser->GetUserID().c_str(), (char*)&tempChatPacket);
//		}
//	}
//
//	//	해당 값의 결과를 응답 패킷의 데이터에 넣어서 전송한다.
//	SendPacketFunc(clientIndex_, sizeof(ROOM_LEAVE_RESPONSE_PACKET), (char*)&roomLeaveResPacket);
//	printf("Leave Room Res Packet Send ! \n");
//
//}

//void PacketManager::ProcessRoomChatMessage(UINT32 clientIndex_, UINT16 packetSize_, char* pPacket)
//{
//	UNREFERENCED_PARAMETER(packetSize_);
//	//  채팅 패킷을 받는다.
//	auto pRoomChatReqPacket = reinterpret_cast<ROOM_CHAT_REQUEST_PACKET*>(pPacket);
//	//	해당 패킷에서 클라이언트 index, userId, message 정보를 추출한다.
//	ROOM_CHAT_RESPONSE_PACKET roomChatResPacket;
//	roomChatResPacket.PacketId = (UINT16)PACKET_ID::ROOM_CHAT_RESPONSE;
//	roomChatResPacket.PacketLength = sizeof(ROOM_CHAT_RESPONSE_PACKET);
//	roomChatResPacket.Result = (UINT16)ERROR_CODE::NONE;
//	//	user 객체로 해당 정보를 전달한다.
//	auto pReqUser = mUserManager->GetUserByConnIdx(clientIndex_);
//
//	// 유저가 방에 있는지 확인
//	if (pReqUser->GetDomainState() != User::DOMAIN_STATE::ROOM)
//	{
//		ROOM_CHAT_RESPONSE_PACKET roomChatResPacket;
//		roomChatResPacket.PacketId = (UINT16)PACKET_ID::ROOM_CHAT_RESPONSE;
//		roomChatResPacket.PacketLength = sizeof(ROOM_CHAT_RESPONSE_PACKET);
//		roomChatResPacket.Result = (UINT16)ERROR_CODE::ENTER_ROOM_INVALID_USER_STATUS;
//		SendPacketFunc(clientIndex_, sizeof(ROOM_CHAT_RESPONSE_PACKET), (char*)&roomChatResPacket);
//		return;
//	}
//
//	auto roomNum = pReqUser->GetRoomIndex();
//
//	auto pRoom = mRoomManager->GetRoomByNumber(roomNum);
//
//	if (pRoom == nullptr || !pRoom)
//	{
//		roomChatResPacket.Result = (UINT16)ERROR_CODE::CHAT_ROOM_INVALID_ROOM_NUMBER;
//		SendPacketFunc(clientIndex_, sizeof(ROOM_CHAT_RESPONSE_PACKET), (char*)&roomChatResPacket);
//		return;
//	}
//	
//	SendPacketFunc(clientIndex_, sizeof(ROOM_CHAT_RESPONSE_PACKET), (char*)&roomChatResPacket);
//	
//	// MySQL 채팅 메세지 저장
//	MySQLChatMsgReq chatmsg{};
//	strcpy_s(chatmsg.UserID, pReqUser->GetUserID().c_str());
//	chatmsg.RoomNumber = roomNum;
//	strcpy_s(chatmsg.Message, pRoomChatReqPacket->Message);
//	chatmsg.TimeStampSec = (UINT64)time(nullptr);
//
//	MySQLTask task{};
//	task.UserIndex = clientIndex_;
//	task.TaskID = MySQLTaskID::INSERT_CHAT_MESSAGE;
//	task.DataSize = sizeof(MySQLChatMsgReq);
//	task.pData = new char[task.DataSize];
//	CopyMemory(task.pData, &chatmsg, task.DataSize);
//	mMySQLManager->PushTask(task);
//
//	//	Room 객체에서 브로드캐스트 전송을 수행한다.
//	pRoom->NotifyChat(clientIndex_, pReqUser->GetUserID().c_str(), (char*)pRoomChatReqPacket);
//
//
//}

void PacketManager::NotifyPacketEvent()
{
	mPacketEventCV.notify_one();
}
