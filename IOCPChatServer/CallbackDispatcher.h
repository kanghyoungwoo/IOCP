#pragma once
#include "PacketJob.h"

class UserManager;
class RoomManager;
class MySQLManager;
class StrandProcessor;

class CallbackDispatcher
{
public:
	CallbackDispatcher(UserManager* pUserMgr, RoomManager* pRoomMgr, MySQLManager* pMySQLMgr, StrandProcessor* pStrand);

	// pNotify의 처리 + FreeCallback까지 담당
	void Dispatch(PacketJob* pNotify);

private:
	UserManager*     mUserManager;
	RoomManager*     mRoomManager;
	MySQLManager*    mMySQLManager;
	StrandProcessor* mStrand;
};
