#pragma once

#include "PacketManager.h"
#include "UserManager.h"

void PacketManager::Init(const UINT32 maxClient_)
{
	mRecvFunctionDictionary = std::unordered_map<int, PROCESS_RECV_PACKET_FUNCTION>();

	mRecvFunctionDictionary[(int)PACKET_ID::SYS_USER_CONNECT] = &PacketManager::ProcessUserConnect;
	mRecvFunctionDictionary[(int)PACKET_ID::SYS_USER_DISCONNECT] = &PacketManager::ProcessUserDisconnect;

	mRecvFunctionDictionary[(int)PACKET_ID::LOGIN_REQUEST] = &PacketManager::ProcessLogin;

	CreateComponent(maxClient_);
}

void PacketManager::CreateComponent(const UINT32 maxClient_)
{
	mUserManager = new UserManager;
	mUserManager->Init(maxClient_);
}


bool PacketManager::Run()
{
	mIsRunProcessThread = true;
	mProcessThead = std::thread([this]() {ProcessPacket();});

	return true;
}

void PacketManager::End()
{
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

	// 접속자 수가 최대수인지 확인
	if (mUserManager->GetCurrentUserCnt() >= mUserManager->GetMaxUserCnt())
	{
		// 접속자 수가 최대라면 접속 불가
		loginResPacket.Result = (UINT16)ERROR_CODE::LOGIN_USER_USED_ALL_OBJ;
		SendPacketFunc(clientIndex_, sizeof(LOGIN_RESPONSE_PACKET), (char*)&loginResPacket);
		return;
	}

	// 중복 체크
	// 이미 접속된 유저인지 확인하고
	if (mUserManager->FindUserIndexByID(pUserID) == -1)
	{
		loginResPacket.Result = (UINT16)ERROR_CODE::NONE;
		SendPacketFunc(clientIndex_, sizeof(LOGIN_RESPONSE_PACKET), (char*)&loginResPacket);
	}
	else
	{
		// 접속중인 유저라면 실패
		loginResPacket.Result = (UINT16)ERROR_CODE::LOGIN_USER_ALREADY;
		SendPacketFunc(clientIndex_, sizeof(LOGIN_RESPONSE_PACKET), (char*)&loginResPacket);
		return;
	}
}


void PacketManager::ClearConnectionInfo(INT32 clientIndex_)
{
	auto pReqUser = mUserManager->GetUserByConnIdx(clientIndex_);
	if (pReqUser->GetDomainState() != User::DOMAIN_STATE::NONE)
	{
		mUserManager->DeleteUserInfo(pReqUser);
	}
}