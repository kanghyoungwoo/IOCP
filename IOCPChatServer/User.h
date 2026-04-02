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
	}

	void Clear();


	std::string GetUserID() const
	{
		return mUserID;
	}

	int SetLogin(char* userID_);


	void SetDomainState(DOMAIN_STATE value_)
	{
		mCurDomainState = value_;
	}

	INT32 GetNetConnIndex()  const
	{
		return mIndex;
	}


	DOMAIN_STATE GetDomainState()
	{
		return mCurDomainState;
	}

	// 링버퍼 처리 활용
	// TODO: 링버퍼 구조체로 바꾸기
	bool SetPacketData(const UINT32 dataSize_, char* pData_);

	PacketInfo GetPacket();

	void EnterRoom(INT32 roomIndex_)
	{
		roomIndex = roomIndex_;
		mCurDomainState = DOMAIN_STATE::ROOM;  // 여기서 상태 변경
		LOG_DEBUG("Entered room [%d] !\n", roomIndex);

	}

	INT32 GetRoomIndex()
	{
		return roomIndex;
	}

	void ResetRoom()
	{
		roomIndex = -1;
	}

	//size_t GetBufferSize() const
	//{
	//	return mPacketDataBuffer.Size();
	//}

	//bool IsBufferEmpty() const
	//{
	//	return mPacketDataBuffer.IsEmpty();
	//}

	//bool IsBufferFull() const
	//{
	//	return mPacketDataBuffer.IsFull();
	//}


	bool IsDisconnecting() const
	{
		return mIsDisconnecting.load(); 
	}

	void SetDisconnecting()
	{
		mIsDisconnecting.store(true);
	}

	void SetSessionGeneration(UINT32 gen)
	{
		mSessionGeneration.store(gen, std::memory_order_release);
	}

	UINT32 GetSessionGeneration() const
	{
		return mSessionGeneration.load(std::memory_order_acquire);
	}



private:
	INT32 mIndex = -1;
	std::string mUserID = "";
	INT32 roomIndex = -1;
	std::atomic<UINT32> mSessionGeneration{ 0 };

	//UINT32 mPacketDataBufferWritePos = 0; // 쓰기 위치
	//UINT32 mPacketDataBufferReadPos = 0; // 읽기 위치

	// ringbuffer로 패킷 데이터 교체
	RingBuffer<MAX_PACKET_DATA_BUFFER_SIZE> mPacketDataBuffer;

	DOMAIN_STATE mCurDomainState = DOMAIN_STATE::NONE;

	std::mutex mPacketRingBuffMutex;

	// 한 스레드만 사용할 패킷 임시 버퍼
	char m_tempPacketBuffer[MAX_PACKET_DATA_BUFFER_SIZE];

	std::atomic<bool> mIsDisconnecting{ false };

};