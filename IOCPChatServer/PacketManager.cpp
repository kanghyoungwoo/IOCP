#pragma once

//#include <utility>
//#include <cstring>

#include "PacketManager.h"
#include "UserManager.h"
//#include "RedisTaskDefine.h"
#include "RedisManager.h"

void PacketManager::Init(const UINT32 maxClient_)
{
	mRecvFunctionDictionary = std::unordered_map<int, PROCESS_RECV_PACKET_FUNCTION>();

	mRecvFunctionDictionary[(int)PACKET_ID::SYS_USER_CONNECT] = &PacketManager::ProcessUserConnect;
	mRecvFunctionDictionary[(int)PACKET_ID::SYS_USER_DISCONNECT] = &PacketManager::ProcessUserDisconnect;

	mRecvFunctionDictionary[(int)PACKET_ID::LOGIN_REQUEST] = &PacketManager::ProcessLogin;
	mRecvFunctionDictionary[(int)RedisTaskID::RESPONSE_LOGIN] = &PacketManager::ProcessLoginDBResult; // 서버가 자기 자신 호출

	mRecvFunctionDictionary[(int)PACKET_ID::ROOM_ENTER_REQUEST] = &PacketManager::ProcessEnterRoom;
	mRecvFunctionDictionary[(int)PACKET_ID::ROOM_LEAVE_REQUEST] = &PacketManager::ProcessLeaveRoom;
	mRecvFunctionDictionary[(int)PACKET_ID::ROOM_CHAT_REQUEST] = &PacketManager::ProcessRoomChatMessage;
	



	CreateComponent(maxClient_);

	mRedisManager = new RedisManager; // std::make_unique<RedisManager>();

}

void PacketManager::CreateComponent(const UINT32 maxClient_)
{
	mUserManager = new UserManager;
	mUserManager->Init(maxClient_);

	UINT32 startRoomNumber = 0;
	UINT32 maxRoomUserCount = 4;
	UINT32 maxRoomCount = 10;
	mRoomManager = new RoomManager;
	mRoomManager->SendPacketFunc = SendPacketFunc;
	mRoomManager->Init(startRoomNumber, maxRoomCount, maxRoomUserCount);
}


bool PacketManager::Run()
{
	if (mRedisManager->Run("127.0.0.1", 6379, 1) == false)
	{
		return false;
	}

	mIsRunProcessThread = true;
	mProcessThead = std::thread([this]() { ProcessPacket();});

	return true;
}

void PacketManager::End()
{
	mRedisManager->End();
	mIsRunProcessThread = false;
	if (mProcessThead.joinable())
	{
		mProcessThead.join();
	}

}

// usermanager의 GetUserByConnIdx를 이용하여 유저의 idx를 받은 후 user의 data를 SetPacketData를 통해 set함
void PacketManager::ReceivePacketData(const UINT32 clientIndex_, const UINT32 dataSize_, char* pData_)
{
	auto pUser = mUserManager->GetUserByConnIdx(clientIndex_);
	pUser->SetPacketData(dataSize_, pData_); // 링버퍼에 저장 후
	// queue에 알려줌 어떤 client의 요청이 왔는지
	EnqueuePacketData(clientIndex_);
}



void PacketManager::ProcessPacket()
{
	while (mIsRunProcessThread)
	{
		bool isIdle = true;

		if (auto packetData = DequePacketData(); packetData.PacketId > (UINT16)PACKET_ID::SYS_END)
		{
			isIdle = false;
			ProcessRecvPacket(packetData.ClientIndex, packetData.PacketId, packetData.DataSize, packetData.pDataPtr);
		}

		if (auto packetData = DequeSystemPacketData(); packetData.PacketId != 0)
		{
			isIdle = false;
			ProcessRecvPacket(packetData.ClientIndex, packetData.PacketId, packetData.DataSize, packetData.pDataPtr);
		}

		if (auto task = mRedisManager->TakeResponseTask(); task.TaskID != RedisTaskID::INVALID)
		{
			isIdle = false;
			ProcessRecvPacket(task.UserIndex, (UINT16)task.TaskID, task.DataSize, task.pData);
			task.release();
		}

		if (isIdle)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
	}
	// 이미 연결이 된 유저가 보낸 패킷이 있는지 알아보고 
	// 있으면 처리하고 
	// 시스템 패킷(네트워크가 보낸것, 연결, 연결종료 등..) 을 있으면 처리하고 
}

void PacketManager::EnqueuePacketData(const UINT32 clientIndex_)
{
	std::lock_guard<std::mutex> guard(mLock);
	mInComingPacketUserIndex.push_back(clientIndex_);
}

PacketInfo PacketManager::DequePacketData()
{
	UINT32 userIndex = 0;
	// 요청을 보낸 유저가 있는지 확인 후 
	// empty면 리턴
	{
		std::lock_guard<std::mutex>guard(mLock);
		if (mInComingPacketUserIndex.empty())
		{
			return PacketInfo();
		}
		// 있으면 데이터를 뽑아내고 
		// user index를 통해서 user 객체를 알아내고 링버퍼를 이용
		userIndex = mInComingPacketUserIndex.front();
		mInComingPacketUserIndex.pop_front();
	}
	auto pUser = mUserManager->GetUserByConnIdx(userIndex);
	auto packetData = pUser->GetPacket();
	packetData.ClientIndex = userIndex;

	return packetData;
}

void PacketManager::ProcessUserConnect(UINT32 clientIndex_, UINT16 packetSize_, char* pPacket)
{
	printf("[ProcessUserConnect] ClientIndex : %d\n", clientIndex_);
	auto pUser = mUserManager->GetUserByConnIdx(clientIndex_);
	pUser->Clear();
}

void PacketManager::ProcessUserDisconnect(UINT32 clientIndex_, UINT16 packetSize_, char* pPacket)
{
	printf("[ProcessUserDisconnect] ClientIndex : %d\n", clientIndex_);
	ClearConnectionInfo(clientIndex_);
	
}

void PacketManager::PushSystemPacket(PacketInfo packet_)
{
	std::lock_guard<std::mutex>guard(mLock);
	mSystemPacketQueue.push_back(packet_);
}


PacketInfo PacketManager::DequeSystemPacketData()
{
	std::lock_guard<std::mutex>guard(mLock);
	if (mSystemPacketQueue.empty())
	{
		return PacketInfo();
	}
	auto packetData = mSystemPacketQueue.front();
	mSystemPacketQueue.pop_front();
	return packetData;
}


void PacketManager::ProcessRecvPacket(const UINT32 clientIndex_, const UINT16 packetId_, const UINT16 packetSize_, char* pPacket_)
{
	// 패킷 id를 찾아서 
	// 관계된 객체를 할당해서 처리
	auto iter = mRecvFunctionDictionary.find(packetId_);
	if (iter != mRecvFunctionDictionary.end())
	{
		(this->*(iter->second))(clientIndex_, packetSize_, pPacket_);
	}
	// 잘못된 패킷 처리
	else
	{
		printf("알 수 없는 패킷 ID : %d (ClientIndex: %d)\n", packetId_, clientIndex_);
	}

}


void PacketManager::ProcessLogin(UINT32 clientIndex_, UINT16 packetSize_, char* pPacket_) // 최대 접속자 수, 중복 정도만 확인
{
	if (LOGIN_REQUEST_PACKET_SIZE != packetSize_)
	{
		return;
	}

	auto pLoginReqPacket = reinterpret_cast<LOGIN_REQUEST_PACKET*>(pPacket_);

	auto pUserID = pLoginReqPacket->UserID;
	printf("Requested user ID : %s\n", pUserID);

	LOGIN_RESPONSE_PACKET loginResPacket;
	loginResPacket.PacketId = (UINT16)PACKET_ID::LOGIN_RESPONSE;
	loginResPacket.PacketLength = sizeof(LOGIN_RESPONSE_PACKET);

	// 디버깅
	auto existingIndex = mUserManager->FindUserIndexByID(pUserID);
	printf("기존 사용자 검색 : UserID = %s -> Index = %d\n", pUserID, existingIndex);

	if (existingIndex == -1)
	{
		printf("새로운 사용자 - Redis로 전송\n");
		// Redis 요청
		printf("Login To Redis USER ID : %s\n", pUserID);
	}
	else
	{
		printf("중복 로그인 차단! UserID='%s', 기존Index=%d, 새요청Index=%d\n",
			pUserID, existingIndex, clientIndex_);
		loginResPacket.Result = (UINT16)ERROR_CODE::LOGIN_USER_ALREADY;
		SendPacketFunc(clientIndex_, sizeof(LOGIN_RESPONSE_PACKET), (char*)&loginResPacket);
		printf("중복 로그인 거부 응답 전송 완료\n");
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

	// 중복 체크
	// 이미 접속된 유저인지 확인하고 이미 접속된 유저라면 실패
	if (mUserManager->FindUserIndexByID(pUserID) == -1)
	{
		RedisLoginReq dbReq;

		CopyMemory(dbReq.UserID, pLoginReqPacket->UserID, (MAX_USER_ID_LENGTH + 1));
		CopyMemory(dbReq.UserPW, pLoginReqPacket->UserPW, (MAX_USER_PW_LENGTH + 1));

		RedisTask task;
		task.UserIndex = clientIndex_;
		task.TaskID = RedisTaskID::REQUEST_LOGIN;
		task.DataSize = sizeof(RedisLoginReq);
		task.pData = new char[task.DataSize];
		CopyMemory(task.pData, (char*)&dbReq, task.DataSize);
		mRedisManager->PushTask(task);

		printf("Login To Redis USER ID : %s\n", pUserID);
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
	printf("ProcessLoginDBResult. UserIndex : %d \n", clientIndex_);

	auto pBody = (RedisLoginRes*)pPacket_;

	if (pBody->Result == (UINT16)ERROR_CODE::NONE)
	{
		printf("[DEBUG] Login successful for UserID: '%s'\n", pBody->UserID);

		// UserManager에 사용자 추가
		auto result = mUserManager->Adduser(pBody->UserID, clientIndex_);
		if (result != ERROR_CODE::NONE) {
			printf("[ERROR] Failed to add user to UserManager\n");
			pBody->Result = (UINT16)ERROR_CODE::LOGIN_USER_USED_ALL_OBJ;
		}
		else {
			auto pUser = mUserManager->GetUserByConnIdx(clientIndex_);
			printf("[DEBUG] User added successfully. UserID: '%s'\n", pUser->GetUserID().c_str());
			mUserManager->IncreaseUserCnt();
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
	auto pReqUser = mUserManager->GetUserByConnIdx(clientIndex_);

	if (pReqUser->GetDomainState() == User::DOMAIN_STATE::ROOM)
	{
		auto roomNum = pReqUser->GetRoomIndex();
		mRoomManager->LeaveUser(roomNum, pReqUser);
	}

	if (pReqUser->GetDomainState() != User::DOMAIN_STATE::NONE)
	{
		mUserManager->DeleteUserInfo(pReqUser);
	}
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
		printf("유효하지 않은 유저 !. ClientIndex : %d\n", clientIndex_);
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
	printf("Enter Room Res Packet Send ! \n");

}

void PacketManager::ProcessLeaveRoom(UINT32 clientIndex_, UINT16 packetSize_, char* pPacket)
{

	UNREFERENCED_PARAMETER(packetSize_);
	UNREFERENCED_PARAMETER(pPacket);
	//  방 퇴장 요청 패킷을 받는다.
	auto pRoomLeaveReqPacket = reinterpret_cast<ROOM_LEAVE_REQUEST_PACKET*>(pPacket);
	//	유효한 유저인지 검사한다.
	auto pReqUser = mUserManager->GetUserByConnIdx(clientIndex_);
	if (!pReqUser || pReqUser == nullptr)
	{
		printf("유효하지 않은 유저 ! . ClientIndex : %d\n", clientIndex_);
		return;
	}
	// 방 퇴장 전 방 정보 미리 저장
	// 퇴장 후에는 정보가 사라지기 때문
	auto roomNumber = pReqUser->GetRoomIndex();
	auto pRoom = mRoomManager->GetRoomByNumber(roomNumber);

	//	응답 패킷을 생성하고
	ROOM_LEAVE_RESPONSE_PACKET roomLeaveResPacket;
	roomLeaveResPacket.PacketId = (UINT16)PACKET_ID::ROOM_LEAVE_RESPONSE;
	roomLeaveResPacket.PacketLength = sizeof(ROOM_LEAVE_RESPONSE_PACKET);

	//	RoomManager 객체의 leaveUser 함수를 호출한다.
	roomLeaveResPacket.Result = mRoomManager->LeaveUser(roomNumber, pReqUser);

	// 방 퇴장 성공 시 방 전체에 퇴장 알림
	if (roomLeaveResPacket.Result == (UINT16)ERROR_CODE::NONE)
	{	
		if (pRoom != nullptr)
		{
			// 임시 채팅 패킷 생성
			ROOM_CHAT_REQUEST_PACKET tempChatPacket;
			tempChatPacket.PacketId = (UINT16)PACKET_ID::ROOM_CHAT_REQUEST;
			tempChatPacket.PacketLength = sizeof(ROOM_CHAT_REQUEST_PACKET);

			sprintf_s(tempChatPacket.Message, "has left the room.");

			// 방 전체에 알림
			pRoom->NotifyChat(clientIndex_, pReqUser->GetUserID().c_str(), (char*)&tempChatPacket);
		}
	}

	//	해당 값의 결과를 응답 패킷의 데이터에 넣어서 전송한다.
	SendPacketFunc(clientIndex_, sizeof(ROOM_LEAVE_RESPONSE_PACKET), (char*)&roomLeaveResPacket);
	printf("Leave Room Res Packet Send ! \n");

}

void PacketManager::ProcessRoomChatMessage(UINT32 clientIndex_, UINT16 packetSize_, char* pPacket)
{
	UNREFERENCED_PARAMETER(packetSize_);
	//  채팅 패킷을 받는다.
	auto pRoomChatReqPacket = reinterpret_cast<ROOM_CHAT_REQUEST_PACKET*>(pPacket);
	//	해당 패킷에서 클라이언트 index, userId, message 정보를 추출한다.
	ROOM_CHAT_RESPONSE_PACKET roomChatResPacket;
	roomChatResPacket.PacketId = (UINT16)PACKET_ID::ROOM_CHAT_RESPONSE;
	roomChatResPacket.PacketLength = sizeof(ROOM_CHAT_RESPONSE_PACKET);
	roomChatResPacket.Result = (UINT16)ERROR_CODE::NONE;
	//	Room 객체로 해당 정보를 전달한다.
	auto pReqUser = mUserManager->GetUserByConnIdx(clientIndex_);
	auto roomNum = pReqUser->GetRoomIndex();

	auto pRoom = mRoomManager->GetRoomByNumber(roomNum);

	if (pRoom == nullptr || !pRoom)
	{
		roomChatResPacket.Result = (UINT16)ERROR_CODE::CHAT_ROOM_INVALID_ROOM_NUMBER;
		SendPacketFunc(clientIndex_, sizeof(ROOM_CHAT_RESPONSE_PACKET), (char*)&roomChatResPacket);
		return;
	}
	
	SendPacketFunc(clientIndex_, sizeof(ROOM_CHAT_RESPONSE_PACKET), (char*)&roomChatResPacket);
	
	//	Room 객체에서 브로드캐스트 전송을 수행한다.
	pRoom->NotifyChat(clientIndex_, pReqUser->GetUserID().c_str(), (char*)pRoomChatReqPacket);


}