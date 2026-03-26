#pragma once

#include "Packet.h"
#include "RingBuffer.h"
#include "Define.h"
#include <string>
#include <mutex>
#include <atomic>

class User
{
	//const UINT32 PACKET_DATA_BUFFER_SIZE = 8096;
	static constexpr size_t MAX_PACKET_DATA_BUFFER_SIZE = 8192;
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
		mPacketDataBuffer.Clear();
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
		//mGeneration++;	// 세대 카운터 증가

		mIsDisconnecting.store(false);
	}

	std::string GetUserID() const
	{
		return mUserID;
	}

	int SetLogin(char* userID_)
	{
		mCurDomainState = DOMAIN_STATE::LOGIN;
		mUserID = userID_;
		LOG_DEBUG("[SetLogin] UserID set to: '%s' for index: %d\n", userID_, mIndex);

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

	UINT32 GetGeneration() const
	{
		return mGeneration;
	}

	DOMAIN_STATE GetDomainState()
	{
		return mCurDomainState;
	}

	// 링버퍼 처리 활용
	// TODO: 링버퍼 구조체로 바꾸기
	bool SetPacketData(const UINT32 dataSize_, char* pData_)
	{
		if (pData_ == nullptr || dataSize_ == 0)
			return true;

		std::lock_guard<std::mutex>lock(mPacketRingBuffMutex);
		size_t written = mPacketDataBuffer.Write(pData_, dataSize_);

		// 링버퍼에 쓸 수 없는 경우 에러
		if (written < dataSize_)
		{
			LOG_ERROR("%zu bytes out of %u bytes to packet buffer\n", written, dataSize_);
			return false; // overflow 발생
		}
		return true;
	}

	PacketInfo GetPacket()
	{
		const int PACKET_SIZE_LENGTH = 2;
		const int PACKET_TYPE_LENGTH = 2;

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

		// 패킷 크기 유효성 검증
		if (pHeader->PacketLength < PACKET_HEADER_LENGTH || pHeader->PacketLength > MAX_PACKET_DATA_BUFFER_SIZE)
		{
			LOG_ERROR("Invalid PacketLength: %d (valid: %u~%zd) → 버퍼 초기화\n",
				pHeader->PacketLength, PACKET_HEADER_LENGTH, MAX_PACKET_DATA_BUFFER_SIZE);
			mPacketDataBuffer.Clear();  // 오염된 버퍼 폐기
			return PacketInfo();
		}

		// 전체 패킷 크기만큼 데이터가 있는지 확인
		if (pHeader->PacketLength > mPacketDataBuffer.Size())
		{
			LOG_DEBUG("Packet data insufficient - need(%d) : have(%zu)\n", pHeader->PacketLength, mPacketDataBuffer.Size());
			return PacketInfo();
		}

		// 패킷 데이터를 임시 버퍼에 읽어오기
		//static char tempPacketBuffer[MAX_PACKET_DATA_BUFFER_SIZE];


		size_t readBytes = mPacketDataBuffer.Read(m_tempPacketBuffer, pHeader->PacketLength);

		if (readBytes != pHeader->PacketLength)
		{
			LOG_ERROR("Packet read fail - expected(%d) vs actual(%zu)\n", pHeader->PacketLength, readBytes);
			return PacketInfo();
		}

		PacketInfo packetInfo;
		packetInfo.PacketId = pHeader->PacketId;
		packetInfo.DataSize = pHeader->PacketLength;
		//packetInfo.pDataPtr = tempPacketBuffer;
		packetInfo.pDataPtr = m_tempPacketBuffer;

		return packetInfo;
	}

	void EnterRoom(INT32 roomIndex_)
	{
		roomIndex = roomIndex_;
		mCurDomainState = DOMAIN_STATE::ROOM;  // 여기서 상태 변경
		LOG_DEBUG("Entered room [%d] !\n", roomIndex);

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

	void IncrementGeneration()
	{
		mGeneration++;
	}

	bool IsDisconnecting() const { return mIsDisconnecting.load(); }
	void SetDisconnecting() { mIsDisconnecting.store(true); }

private:
	INT32 mIndex = -1;
	std::string mUserID = "";
	bool mIsconfirm = false;
	INT32 roomIndex = -1;
	std::atomic<UINT32> mGeneration{ 0 };
	std::string mAuthToken = "";

	UINT32 mPacketDataBufferWritePos = 0; // 쓰기 위치
	UINT32 mPacketDataBufferReadPos = 0; // 읽기 위치

	//char* mPacketDataBuffer = nullptr;

	// ringbuffer로 패킷 데이터 교체
	RingBuffer<MAX_PACKET_DATA_BUFFER_SIZE> mPacketDataBuffer;

	DOMAIN_STATE mCurDomainState = DOMAIN_STATE::NONE;

	std::mutex mPacketRingBuffMutex;

	// 한 스레드만 사용할 패킷 임시 버퍼
	char m_tempPacketBuffer[MAX_PACKET_DATA_BUFFER_SIZE];

	std::atomic<bool> mIsDisconnecting{ false };

};