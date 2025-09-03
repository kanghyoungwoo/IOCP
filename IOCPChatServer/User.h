#pragma once

#include "Packet.h"
#include "RingBuffer.h"
#include <string>
#include <mutex>

class User
{
	//const UINT32 PACKET_DATA_BUFFER_SIZE = 8096;
	static constexpr size_t PACKET_DATA_BUFFER_SIZE = 8096;
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
		//mPacketDataBuffer = new char[PACKET_DATA_BUFFER_SIZE];
	}

	void Clear()
	{
		std::lock_guard<std::mutex>lock(mPacketRingBuffMutex);
		mUserID = "";
		mIsconfirm = false;
		mAuthToken = "";
		mCurDomainState = DOMAIN_STATE::NONE;
		mPacketDataBuffer.Clear();

		//mPacketDataBufferWritePos = 0;
		//mPacketDataBufferReadPos = 0;
	}

	std::string GetUserID() const
	{
		return mUserID;
	}

	int SetLogin(char* userID_)
	{
		mCurDomainState = DOMAIN_STATE::LOGIN;
		mUserID = userID_;
		printf("[SetLogin] UserID set to: '%s' for index: %d\n", userID_, mIndex);

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

	//std::string GetUserId() const
	//{
	//	return mUserID;
	//}

	DOMAIN_STATE GetDomainState()
	{
		return mCurDomainState;
	}

	// 링버퍼 처럼 활용
	// TODO: 완전한 링버퍼 형태로 바꾸기
	void SetPacketData(const UINT32 dataSize_, char* pData_)
	{
		if (pData_ == nullptr || dataSize_ == 0)
			return;

		std::lock_guard<std::mutex>lock(mPacketRingBuffMutex);

		size_t written = mPacketDataBuffer.Write(pData_, dataSize_);

		// 데이터 다 못 쓸 경우 경고
		if (written < dataSize_)
		{
			printf("%zu bytes out of %u bytes to packet buffer\n", written, dataSize_);
			return;
		}

		/////

		//if ((mPacketDataBufferWritePos + dataSize_) >= PACKET_DATA_BUFFER_SIZE) // 남은 공간이 부족하면 앞쪽으로 압축 이동
		//{
		//	auto remainDataSize = mPacketDataBufferWritePos - mPacketDataBufferReadPos;

		//	if (remainDataSize > 0)
		//	{
		//		CopyMemory(&mPacketDataBuffer[0], &mPacketDataBuffer[mPacketDataBufferReadPos], remainDataSize);
		//		mPacketDataBufferWritePos = remainDataSize;
		//	}
		//	else
		//	{
		//		mPacketDataBufferWritePos = 0;
		//	}
		//	mPacketDataBufferReadPos = 0;
		//}
		//CopyMemory(&mPacketDataBuffer[mPacketDataBufferWritePos], pData_, dataSize_);
		//mPacketDataBufferWritePos += dataSize_;
	}
	
	PacketInfo GetPacket()
	{
		const int PACKET_SIZE_LENGTH = 2;
		const int PACKET_TYPE_LENGTH = 2;
		//short packetSize = 0;

		//UINT32 remainByte = mPacketDataBufferWritePos - mPacketDataBufferReadPos;

		//if (remainByte < PACKET_HEADER_LENGTH)
		//{
		//	return PacketInfo();
		//}

		//auto pHeader = (PACKET_HEADER*)&mPacketDataBuffer[mPacketDataBufferReadPos];

		//if (pHeader->PacketLength > remainByte)
		//{
		//	printf("패킷 데이터 부족: 필요(%d) vs 현재(%d)\n", pHeader->PacketLength, remainByte);
		//	return PacketInfo();
		//}

		//PacketInfo packetInfo;
		//packetInfo.PacketId = pHeader->PacketId;
		//packetInfo.DataSize = pHeader->PacketLength;
		//packetInfo.pDataPtr = &mPacketDataBuffer[mPacketDataBufferReadPos];

		//mPacketDataBufferReadPos += pHeader->PacketLength;

		//return packetInfo;

		std::lock_guard<std::mutex>lock(mPacketRingBuffMutex);

		if (mPacketDataBuffer.Size() < PACKET_HEADER_LENGTH)
			return PacketInfo();

		char headerBuffer[PACKET_HEADER_LENGTH];
		bool peekSuccess = true;
		for (size_t i = 0; i < PACKET_HEADER_LENGTH; i++)
		{
			if (!mPacketDataBuffer.Peek(headerBuffer[i], i))
			{
				peekSuccess = false;
				break;
			}
		}

		if (!peekSuccess)
		{
			return PacketInfo();
		}

		auto pHeader = (PACKET_HEADER*)headerBuffer;

		// 전체 패킷 크기만큼 데이터가 있는지 확인
		if (pHeader->PacketLength > mPacketDataBuffer.Size())
		{
			printf("패킷 데이터 부족 - 필요(%d) : 현재(%zu\n)", pHeader->PacketLength, mPacketDataBuffer.Size());
			return PacketInfo();
		}
		
		// 패킷 데이터 임시 버퍼에 읽어오기
		static char tempPacketBuffer[PACKET_DATA_BUFFER_SIZE];
		size_t readBytes = mPacketDataBuffer.Read(tempPacketBuffer, pHeader->PacketLength);

		if (readBytes != pHeader->PacketLength)
		{
			printf("패킷 읽기 실패 - 예상(%d) vs 실제(%zu)\n", pHeader->PacketLength, readBytes);
			return PacketInfo();
		}

		PacketInfo packetInfo;
		packetInfo.PacketId = pHeader->PacketId;
		packetInfo.DataSize = pHeader->PacketLength;
		packetInfo.pDataPtr = tempPacketBuffer;

		return packetInfo;
	}

	void EnterRoom(INT32 roomIndex_)
	{
		roomIndex = roomIndex_;
		mCurDomainState = DOMAIN_STATE::ROOM;  // 여기서 상태 변경
		printf("[%d]번 방에 입장하였습니다. ! \n", roomIndex);
		
	}

	INT32 GetRoomIndex()
	{
		//mCurDomainState = DOMAIN_STATE::ROOM;
		return roomIndex;
	}

	
	size_t GetBufferSize() const
	{
		return mPacketDataBuffer.Size();
	}

	bool IsBufferEmpty() const
	{
		return mPacketDataBuffer.IsEmpty();
	}

	bool IsBufferFull() const
	{
		return mPacketDataBuffer.IsFull();
	}

private:
	INT32 mIndex = -1;
	std::string mUserID = "";
	bool mIsconfirm = false;
	INT32 roomIndex = -1;
	std::string mAuthToken = "";

	UINT32 mPacketDataBufferWritePos = 0; // 링버퍼
	UINT32 mPacketDataBufferReadPos = 0; //

	//char* mPacketDataBuffer = nullptr;
	
	// ringbuffer로 패킷 데이터 교체
	RingBuffer<PACKET_DATA_BUFFER_SIZE> mPacketDataBuffer;
	
	DOMAIN_STATE mCurDomainState = DOMAIN_STATE::NONE;

	std::mutex mPacketRingBuffMutex;
	
	
};