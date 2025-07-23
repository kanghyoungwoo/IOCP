#pragma once

#include "Packet.h"

#include <string>

class User
{
	const UINT32 PACKET_DATA_BUFFER_SIZE = 8096;
public:
	enum class DOMAIN_STATE
	{
		NONE = 0,
		LOGIN = 1,
		ROOM = 2
	};

	User() = default;
	~User() = default;

	void Init(const UINT32 index_)
	{
		mIndex = index_;
		mPacketDataBuffer = new char[PACKET_DATA_BUFFER_SIZE];
	}

	void Clear()
	{
		//mRoomIndex = -1;
		//mIndex = -1;
		mUserID = "";
		mIsconfirm = false;
		mAuthToken = "";
		mCurDomainState = DOMAIN_STATE::NONE;

		mPacketDataBufferWritePos = 0;
		mPacketDataBufferReadPos = 0;
	}

	std::string GetUserID() const
	{
		return mUserID;
	}

	int SetLogin(char* userID_)
	{
		mCurDomainState = DOMAIN_STATE::LOGIN;
		mUserID = userID_;

		return 0;
	}

	void SetDomainState(DOMAIN_STATE value_)
	{
		mCurDomainState = value_;
	}

	INT32 GetNetConnIndex()  const
	{
		return mIndex;
	}

	DOMAIN_STATE GetUSerID() const
	{
		return mCurDomainState;
	}

	// 링버퍼 처럼 활용
	// TODO: 완전한 링버퍼 형태로 바꾸기
	void SetPacketData(const UINT32 dataSize_, char* pData_)
	{
		if ((mPacketDataBufferWritePos + dataSize_) >= PACKET_DATA_BUFFER_SIZE) // 남은 공간이 부족하면 앞쪽으로 압축 이동
		{
			auto remainDataSize = mPacketDataBufferWritePos - mPacketDataBufferReadPos;

			if (remainDataSize > 0)
			{
				CopyMemory(&mPacketDataBuffer[0], &mPacketDataBuffer[mPacketDataBufferReadPos], remainDataSize);
				mPacketDataBufferWritePos = remainDataSize;
			}
			else
			{
				mPacketDataBufferWritePos = 0;
			}
			mPacketDataBufferReadPos = 0;
		}
		CopyMemory(&mPacketDataBuffer[mPacketDataBufferWritePos], pData_, dataSize_);
		mPacketDataBufferWritePos += dataSize_;
	}
	
	PacketInfo GetPacket()
	{
		const int PACKET_SIZE_LENGTH = 2;
		const int PACKET_TYPE_LENGTH = 2;
		short packetSize = 0;

		UINT32 remainByte = mPacketDataBufferWritePos - mPacketDataBufferReadPos;

		if (remainByte < PACKET_HEADER_LENGTH)
		{
			return PacketInfo();
		}

		auto pHeader = (PACKET_HEADER*)&mPacketDataBuffer[mPacketDataBufferReadPos];

		if (pHeader->PacketLength > remainByte)
		{
			return PacketInfo();
		}

		PacketInfo packetInfo;
		packetInfo.PacketId = pHeader->PacketId;
		packetInfo.DataSize = pHeader->PacketLength;
		packetInfo.pDataPtr = &mPacketDataBuffer[mPacketDataBufferReadPos];

		mPacketDataBufferReadPos += pHeader->PacketLength;

		return packetInfo;
	}

	DOMAIN_STATE GetDomainState()
	{
		return mCurDomainState;
	}

	

private:
	INT32 mIndex = -1;
	std::string mUserID = "";
	bool mIsconfirm = false;
	std::string mAuthToken = "";

	UINT32 mPacketDataBufferWritePos = 0; // 링버퍼
	UINT32 mPacketDataBufferReadPos = 0; //

	char* mPacketDataBuffer = nullptr;
	
	DOMAIN_STATE mCurDomainState = DOMAIN_STATE::NONE;
	
};