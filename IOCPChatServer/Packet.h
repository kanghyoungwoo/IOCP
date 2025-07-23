#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
// 패킷의 ID로 클라이언트가 어떤 요청을 했는지 알아냄
typedef struct _RawPacketData
{
	UINT32 ClientSessionIndex = 0;
	UINT32 DataSize = 0;
	char* pPacketData = nullptr;

	void Set(_RawPacketData& value)
	{
		ClientSessionIndex = value.ClientSessionIndex;
		DataSize = value.DataSize;
		pPacketData = new char[value.DataSize];
		CopyMemory(pPacketData, value.pPacketData, value.DataSize);
		//memcpy(pPacketData, value.pPacketData, value.DataSize);
	}
	
	void Set(UINT32 ClientSessionIndex_, UINT32 DataSize_, char *pData_)
	{
		ClientSessionIndex = ClientSessionIndex_;
		DataSize = DataSize_;

		if (pPacketData != nullptr)
		{
			delete[] pPacketData;
			pPacketData = nullptr;
		}
		pPacketData = new char[DataSize_];
		CopyMemory(pPacketData, pData_, DataSize_);

		//memcpy(pPacketData, pData_, DataSize_);
	}

	void Release()
	{
		delete[] pPacketData;
		pPacketData = nullptr;

	}

	//~_stPacketData()
	//{
	//	if (pPacketData != nullptr)
	//	{
	//		delete[] pPacketData;
	//		pPacketData = nullptr;
	//	}
	//}

}RawPacketData;


//typedef struct _PacketInfo
//{
//	UINT32 ClientIndex = 0;
//	UINT32 PacketId = 0;
//	UINT32 DataSize = 0;
//	char* pDataPtr = nullptr;
//}PacketInfo;

struct PacketInfo
{
	UINT32 ClientIndex = 0;
	UINT32 PacketId = 0;
	UINT32 DataSize = 0;
	char* pDataPtr = nullptr;
};

enum class PACKET_ID : UINT16
{
	// SYSTEM
	SYS_USER_CONNECT = 11,
	SYS_USER_DISCONNECT = 12,
	SYS_END = 30,

	// DB
	DB_END = 199,


	// Client
	LOGIN_REQUEST = 201,
	LOGIN_RESPONSE = 202,
};

#pragma pack(push,1)	// 바이트 정렬
struct PACKET_HEADER
{
	UINT32 PacketLength;
	UINT32 PacketId;			// 패킷 ID 정보로 클라이언트가 어떤 요청을 했는지 알아내고 그에 따른 처리
	UINT32 PacketType;			// 압축 여부
};

const UINT32 PACKET_HEADER_LENGTH = sizeof(PACKET_HEADER);

// 로그인
const int MAX_USER_ID_LENGTH = 32;
const int MAX_USER_PW_LENGTH = 32;

struct LOGIN_REQUEST_PACKET : public PACKET_HEADER
{
	char UserID[MAX_USER_ID_LENGTH + 1];
	char UserPW[MAX_USER_PW_LENGTH + 1];
};

const size_t LOGIN_REQUEST_PACKET_SIZE = sizeof(LOGIN_REQUEST_PACKET);

struct LOGIN_RESPONSE_PACKET : public PACKET_HEADER
{
	UINT16 Result;
};

#pragma pack(pop)