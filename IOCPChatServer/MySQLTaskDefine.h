#pragma once

#pragma once

#include <windows.h>
#include "ErrorCode.h"
#include "Packet.h"


enum class MySQLTaskID : UINT16
{
	INVALID = 0,

	//REQUEST_LOGIN = 2001,
	//RESPONSE_LOGIN = 2002,

	//REQUEST_USER_REGISTER = 2003,
	//RESPONSE_USER_REGISTER = 2004,

	//REQUEST_USER_PROFILE = 2005,
	//RESPONSE_USER_PROFILE = 2006,

	//REQUEST_SAVE_CHAT_MESSAGE = 2007,
	//RESPONSE_SAVE_CHAT_MESSAGE = 2008,


	//REQUEST_LOGIN_LOG = 2009,
	//RESPONSE_LOGIN_LOG = 2010,

	INSERT_LOGIN_EVENT = 2001,	//
	INSERT_ROOM_EVENT = 2002,	// 
	INSERT_CHAT_MESSAGE = 2003,	//
};

static constexpr UINT16 MAX_MYSQL_BODY_SIZE = 512;

struct MySQLTask
{
	UINT32 UserIndex = 0;
	MySQLTaskID TaskID = MySQLTaskID::INVALID;
	UINT16 DataSize = 0;
	//char* pData = nullptr;
	char body[MAX_MYSQL_BODY_SIZE] = {};

	//void release()
	//{
	//	if (pData != nullptr)
	//	{
	//		delete[] pData;
	//		pData = nullptr;
	//	}
	//}
};

#pragma pack(push,1)

struct MySQLLoginEventReq
{
	char UserID[MAX_USER_ID_LENGTH + 1] = { 0, };
	UINT64 TimestampSec = 0;
};


enum class RoomEventType : UINT8 
{
	ENTER = 1, 
	LEAVE = 2
};

struct MySQLRoomEventReq
{
	char UserID[MAX_USER_ID_LENGTH + 1] = { 0, };
	INT32 RoomNumber = -1;
	RoomEventType EventType = RoomEventType::ENTER;
	UINT64 TimeStampSec = 0;
};

struct MySQLChatMsgReq
{
	char UserID[MAX_USER_ID_LENGTH + 1] = { 0, };
	INT32 RoomNumber = -1;
	char Message[MAX_CHAT_MSG + 1] = { 0, };
	UINT64 TimeStampSec = 0;
};
//struct MySQLUserLoginReq
//{
//	char UserID[MAX_USER_ID_LENGTH + 1]; //
//	char UserPW[MAX_USER_PW_LENGTH + 1];
//};
//
//struct MySQLUserLoginRes
//{
//	UINT16 Result = (UINT16)ERROR_CODE::NONE; 
//	char UserID[MAX_USER_ID_LENGTH + 1]; //
//	UINT32 UserDBID = 0;
//	char NickName[MAX_USER_ID_LENGTH + 1];
//};
//
//struct MySQLUserRegisterReq
//{
//	char UserID[MAX_USER_ID_LENGTH + 1];
//	char UserPW[MAX_USER_PW_LENGTH + 1];
//	char NickName[MAX_USER_ID_LENGTH + 1];
//	char Email[64];
//};
//
//struct MySQLUserRegisterRes
//{
//	UINT16 Result = (UINT16)ERROR_CODE::NONE;
//	char UserID[MAX_USER_ID_LENGTH + 1];
//};
//
//struct MySQLUserProfileReq
//{
//	char UserID[MAX_USER_ID_LENGTH + 1];
//};
//
//struct MySQLUserProfileRes
//{
//	UINT16 Result = (UINT16)ERROR_CODE::NONE;
//	char UserID[MAX_USER_ID_LENGTH + 1];
//	char NickName[MAX_USER_ID_LENGTH + 1];
//	UINT32 LoginCount = 0;
//	char LastLoginTime[32];
//};


#pragma pack(pop)	//