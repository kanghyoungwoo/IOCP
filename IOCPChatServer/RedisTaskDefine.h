#pragma once

#include <windows.h>
#include "ErrorCode.h"
//#include "Packet.h"

enum class RedisTaskID : UINT16
{
	INVALID = 0,

	REQUEST_LOGIN = 1001,
	RESPONSE_LOGIN = 1002,
};


struct RedisTask
{
	UINT32 UserIndex = 0;
	RedisTaskID TaskID = RedisTaskID::INVALID;
	UINT16 DataSize = 0;
	char* pData = nullptr;

	void release()
	{
		if (pData != nullptr)
		{
			delete[] pData;
			pData = nullptr;
		}
	}
};

#pragma pack(push,1)

struct RedisLoginReq
{
	char UserID[MAX_USER_ID_LENGTH + 1]; // 유저 아이디
	char UserPW[MAX_USER_PW_LENGTH + 1];
};

struct RedisLoginRes
{
	UINT16 Result = (UINT16)ERROR_CODE::NONE; // 성공 여부
};


#pragma pack(pop)	// 바이트 정렬 해제