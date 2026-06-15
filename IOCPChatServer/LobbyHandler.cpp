#include "LobbyHandler.h"
#include "UserManager.h"
#include "RoomManager.h"
#include "StrandProcessor.h"
#include "Packet.h"
#include "ErrorCode.h"

LobbyHandler::LobbyHandler(UserManager* pUserMgr, RoomManager* pRoomMgr, StrandProcessor* pStrand, SendFunc sendFunc)
	: mUserManager(pUserMgr)
	, mRoomManager(pRoomMgr)
	, mStrand(pStrand)
	, mSendFunc(std::move(sendFunc))
{
}

void LobbyHandler::ProcessEnterRoom(UINT32 clientIndex, UINT32 generation, UINT16 packetSize, char* pPacket)
{
	UNREFERENCED_PARAMETER(packetSize);

	auto pRoomEnterReqPacket = reinterpret_cast<ROOM_ENTER_REQUEST_PACKET*>(pPacket);
	auto pReqUser = mUserManager->GetUserByConnIdx(clientIndex);
	if (pReqUser == nullptr)
	{
		LOG_ERROR("유효하지 않은 유저. ClientIndex : %d\n", clientIndex);
		return;
	}

	if (pReqUser->GetDomainState() != User::DOMAIN_STATE::LOGIN)
	{
		ROOM_ENTER_RESPONSE_PACKET errorPacket;
		errorPacket.PacketId     = (UINT16)PACKET_ID::ROOM_ENTER_RESPONSE;
		errorPacket.PacketLength = sizeof(ROOM_ENTER_RESPONSE_PACKET);
		errorPacket.Result       = (UINT16)ERROR_CODE::ENTER_ROOM_INVALID_USER_STATUS;
		mSendFunc(clientIndex, generation, sizeof(ROOM_ENTER_RESPONSE_PACKET), (char*)&errorPacket);
		return;
	}

	auto pRoom = mRoomManager->GetRoomByNumber(pRoomEnterReqPacket->RoomNumber);
	if (pRoom == nullptr)
	{
		ROOM_ENTER_RESPONSE_PACKET errorPacket;
		errorPacket.PacketId     = (UINT16)PACKET_ID::ROOM_ENTER_RESPONSE;
		errorPacket.PacketLength = sizeof(ROOM_ENTER_RESPONSE_PACKET);
		errorPacket.Result       = (UINT16)ERROR_CODE::ROOM_INVALID_INDEX;
		mSendFunc(clientIndex, generation, sizeof(ROOM_ENTER_RESPONSE_PACKET), (char*)&errorPacket);
		return;
	}

	mStrand->EnqueueJob(pRoom, clientIndex, pRoom->GetGeneration(), pReqUser->GetSessionGeneration(), (UINT16)PACKET_ID::ROOM_ENTER_REQUEST, packetSize, pPacket);
}
