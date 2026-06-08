#include "LoginHandler.h"
#include "UserManager.h"
#include "RedisManager.h"
#include "MysqlManager.h"
#include "RedisTaskDefine.h"
#include "MySQLTaskDefine.h"
#include "ConfigManager.h"
#include <ctime>

LoginHandler::LoginHandler(UserManager* pUserMgr, RedisManager* pRedisMgr, MySQLManager* pMySQLMgr, SendFunc sendFunc)
	: mUserManager(pUserMgr)
	, mRedisManager(pRedisMgr)
	, mMySQLManager(pMySQLMgr)
	, mSendFunc(std::move(sendFunc))
{
}

void LoginHandler::ProcessLogin(UINT32 clientIndex, UINT32 generation, UINT16 packetSize, char* pPacket)
{
	if (LOGIN_REQUEST_PACKET_SIZE != packetSize)
		return;

	auto pLoginReqPacket = reinterpret_cast<LOGIN_REQUEST_PACKET*>(pPacket);
	auto pUserID = pLoginReqPacket->UserID;
	LOG_DEBUG("Requested user ID : %s\n", pUserID);

	// 로드 테스트용: test_user prefix면 Redis 생략
	const char* prefix = "test_user";
	if (strncmp(pUserID, prefix, strlen(prefix)) == 0)
	{
		LOGIN_RESPONSE_PACKET loginResPacket;
		loginResPacket.PacketId     = (UINT16)PACKET_ID::LOGIN_RESPONSE;
		loginResPacket.PacketLength = sizeof(loginResPacket);

		auto result = mUserManager->Adduser(pUserID, clientIndex);
		loginResPacket.Result = (UINT16)result;

		if (result == ERROR_CODE::NONE)
		{
			mUserManager->GetUserByConnIdx(clientIndex)->SetSessionGeneration(generation);
			mUserManager->IncreaseUserCnt();
			LOG_DEBUG("[Load Test] Dummy login Success: %s\n", pUserID);
		}

		mSendFunc(clientIndex, generation, sizeof(LOGIN_RESPONSE_PACKET), (char*)&loginResPacket);
		return;
	}

	LOGIN_RESPONSE_PACKET loginResPacket;
	loginResPacket.PacketId     = (UINT16)PACKET_ID::LOGIN_RESPONSE;
	loginResPacket.PacketLength = sizeof(LOGIN_RESPONSE_PACKET);

	// 중복 로그인 차단
	if (mUserManager->FindUserIndexByID(pUserID) != -1)
	{
		LOG_DEBUG("중복 로그인 차단! UserID = %s\n", pUserID);
		loginResPacket.Result = (UINT16)ERROR_CODE::LOGIN_USER_ALREADY;
		mSendFunc(clientIndex, generation, sizeof(LOGIN_RESPONSE_PACKET), (char*)&loginResPacket);
		return;
	}

	// 최대 접속자 초과
	if (mUserManager->GetCurrentUserCnt() >= mUserManager->GetMaxUserCnt())
	{
		loginResPacket.Result = (UINT16)ERROR_CODE::LOGIN_USER_USED_ALL_OBJ;
		mSendFunc(clientIndex, generation, sizeof(LOGIN_RESPONSE_PACKET), (char*)&loginResPacket);
		return;
	}

	// Redis 비동기 인증 요청
	LOG_DEBUG("새로운 사용자 - Redis로 전송\n");

	RedisLoginReq redisReq;
	CopyMemory(redisReq.UserID, pLoginReqPacket->UserID, (MAX_USER_ID_LENGTH + 1));
	CopyMemory(redisReq.UserPW, pLoginReqPacket->UserPW, (MAX_USER_PW_LENGTH + 1));

	RedisTask redistask;
	redistask.UserIndex  = clientIndex;
	redistask.Generation = generation;
	redistask.TaskID     = RedisTaskID::REQUEST_LOGIN;
	redistask.DataSize   = sizeof(RedisLoginReq);
	CopyMemory(redistask.body, (char*)&redisReq, redistask.DataSize);
	mRedisManager->PushTask(redistask);

	LOG_DEBUG("Login To Redis USER ID : %s\n", pUserID);
}

void LoginHandler::ProcessLoginDBResult(UINT32 clientIndex, UINT32 generation, UINT16 packetSize, char* pPacket)
{
	LOG_DEBUG("ProcessLoginDBResult. UserIndex : %d\n", clientIndex);

	auto pBody = (RedisLoginRes*)pPacket;

	if (pBody->Result == (UINT16)ERROR_CODE::NONE)
	{
		LOG_DEBUG("[DEBUG] Login successful for UserID: '%s'\n", pBody->UserID);

		auto result = mUserManager->Adduser(pBody->UserID, clientIndex);
		if (result != ERROR_CODE::NONE)
		{
			LOG_ERROR("Failed to add user to UserManager\n");
			pBody->Result = (UINT16)ERROR_CODE::LOGIN_USER_USED_ALL_OBJ;
		}
		else
		{
			auto pUser = mUserManager->GetUserByConnIdx(clientIndex);
			pUser->SetSessionGeneration(generation);
			LOG_DEBUG("User added successfully. UserID: '%s'\n", pUser->GetUserID().c_str());
			mUserManager->IncreaseUserCnt();

			MySQLLoginEventReq mysqlReq{};
			strcpy_s(mysqlReq.UserID, pUser->GetUserID().c_str());
			mysqlReq.TimestampSec = (UINT64)time(nullptr);

			MySQLTask mysqlTask;
			mysqlTask.UserIndex = clientIndex;
			mysqlTask.TaskID    = MySQLTaskID::INSERT_LOGIN_EVENT;
			mysqlTask.DataSize  = sizeof(MySQLLoginEventReq);
			CopyMemory(mysqlTask.body, &mysqlReq, mysqlTask.DataSize);

			if (!ConfigManager::GetInstance().Get().TestMode)
				mMySQLManager->PushTask(mysqlTask);
		}
	}

	LOGIN_RESPONSE_PACKET loginResPacket;
	loginResPacket.PacketId     = (UINT16)PACKET_ID::LOGIN_RESPONSE;
	loginResPacket.PacketLength = sizeof(LOGIN_RESPONSE_PACKET);
	loginResPacket.Result       = pBody->Result;
	mSendFunc(clientIndex, generation, sizeof(LOGIN_RESPONSE_PACKET), (char*)&loginResPacket);
}
