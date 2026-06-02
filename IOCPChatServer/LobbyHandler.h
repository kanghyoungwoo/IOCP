#pragma once
#include "Define.h"
#include <functional>

class UserManager;
class RoomManager;
class StrandProcessor;

class LobbyHandler
{
public:
	using SendFunc = std::function<void(UINT32, UINT32, UINT32, char*)>;

	LobbyHandler(UserManager* pUserMgr, RoomManager* pRoomMgr, StrandProcessor* pStrand, SendFunc sendFunc);

	void ProcessEnterRoom(UINT32 clientIndex, UINT32 generation, UINT16 packetSize, char* pPacket);

private:
	UserManager*     mUserManager;
	RoomManager*     mRoomManager;
	StrandProcessor* mStrand;
	SendFunc         mSendFunc;
};
