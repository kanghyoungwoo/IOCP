#include "SessionHandler.h"
#include "UserManager.h"
#include "RoomManager.h"
#include "StrandProcessor.h"
#include "Packet.h"

SessionHandler::SessionHandler(UserManager* pUserMgr, RoomManager* pRoomMgr, StrandProcessor* pStrand)
	: mUserManager(pUserMgr)
	, mRoomManager(pRoomMgr)
	, mStrand(pStrand)
{
}

void SessionHandler::ProcessUserConnect(UINT32 clientIndex, UINT32 generation, UINT16 packetSize, char* pPacket)
{
	LOG_DEBUG("[ProcessUserConnect] ClientIndex : %d\n", clientIndex);
	auto pUser = mUserManager->GetUserByConnIdx(clientIndex);
	pUser->SetSessionGeneration(generation);
}

void SessionHandler::ProcessUserDisconnect(UINT32 clientIndex, UINT32 generation, UINT16 packetSize, char* pPacket)
{
	auto pUser = mUserManager->GetUserByConnIdx(clientIndex);
	if (pUser == nullptr) return;

	if (pUser->GetSessionGeneration() != generation)
	{
		LOG_DEBUG("Stale Disconnect 무시 (Gen: %d vs %d)\n", generation, pUser->GetSessionGeneration());
		return;
	}

	LOG_DEBUG("[ProcessUserDisconnect] ClientIndex : %d\n", clientIndex);
	ClearConnectionInfo(clientIndex);
}

void SessionHandler::ClearConnectionInfo(INT32 clientIndex)
{
	auto pReqUser = mUserManager->GetUserByConnIdx(clientIndex);

	if (pReqUser->GetDomainState() == User::DOMAIN_STATE::ROOM)
	{
		pReqUser->SetDisconnecting();

		auto pRoom = mRoomManager->GetRoomByNumber(pReqUser->GetRoomIndex());
		if (pRoom == nullptr)
		{
			LOG_ERROR("클라이언트 %d의 방이 유효하지 않음. 유저 상태 초기화.\n", clientIndex);
			mUserManager->DeleteUserInfo(pReqUser);
			return;
		}

		mStrand->EnqueueJob(
			pRoom, clientIndex,
			pRoom->GetGeneration(),
			pReqUser->GetSessionGeneration(),
			(UINT16)PACKET_ID::SYS_USER_DISCONNECT, 0, nullptr);
	}
	else
	{
		if (pReqUser->GetDomainState() != User::DOMAIN_STATE::NONE)
			mUserManager->DeleteUserInfo(pReqUser);
	}
}
