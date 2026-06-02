#pragma once
#include "Packet.h"
#include "ErrorCode.h"
#include <functional>

class UserManager;
class RedisManager;
class MySQLManager;

class LoginHandler
{
public:
	using SendFunc = std::function<void(UINT32, UINT32, UINT32, char*)>;

	LoginHandler(UserManager* pUserMgr, RedisManager* pRedisMgr, MySQLManager* pMySQLMgr, SendFunc sendFunc);

	void ProcessLogin(UINT32 clientIndex, UINT32 generation, UINT16 packetSize, char* pPacket);
	void ProcessLoginDBResult(UINT32 clientIndex, UINT32 generation, UINT16 packetSize, char* pPacket);

private:
	UserManager*  mUserManager;
	RedisManager* mRedisManager;
	MySQLManager* mMySQLManager;
	SendFunc      mSendFunc;
};
