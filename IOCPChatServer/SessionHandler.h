#pragma once
#include "Define.h"

class UserManager;
class RoomManager;
class StrandProcessor;

class SessionHandler
{
public:
	SessionHandler(UserManager* pUserMgr, RoomManager* pRoomMgr, StrandProcessor* pStrand);

	void ProcessUserConnect(UINT32 clientIndex, UINT32 generation, UINT16 packetSize, char* pPacket);
	void ProcessUserDisconnect(UINT32 clientIndex, UINT32 generation, UINT16 packetSize, char* pPacket);
	void ClearConnectionInfo(INT32 clientIndex);

private:
	UserManager*     mUserManager;
	RoomManager*     mRoomManager;
	StrandProcessor* mStrand;
};
