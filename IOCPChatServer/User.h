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

	void ClearPacketBuffer()
	{
		std::lock_guard<std::mutex> lock(mPacketRingBuffMutex);
		mPacketDataBuffer.Clear();
	}

	std::string GetUserID() const
	{
		return mUserID;
	}

	int SetLogin(char* userID_);


	void SetDomainState(DOMAIN_STATE value_)
	{
		mCurDomainState.store(value_, std::memory_order_release);
	}

	INT32 GetNetConnIndex()  const
	{
		return mIndex;
	}


	DOMAIN_STATE GetDomainState()
	{
		return mCurDomainState.load(std::memory_order_acquire);
	}

	// 링버퍼 처리 활용
	// TODO: 링버퍼 구조체로 바꾸기
	bool SetPacketData(const UINT32 dataSize_, char* pData_);

	PacketInfo GetPacket(char* outBuf, size_t bufSize);

	void EnterRoom(INT32 roomIndex_)
	{
		roomIndex.store(roomIndex_, std::memory_order_release);
		mCurDomainState.store(DOMAIN_STATE::ROOM, std::memory_order_release);
		LOG_DEBUG("Entered room [%d] !\n", roomIndex_);
	}

	INT32 GetRoomIndex()
	{
		return roomIndex.load(std::memory_order_acquire);
	}

	void ResetRoom()
	{
		mCurDomainState.store(DOMAIN_STATE::LOGIN, std::memory_order_release);
		roomIndex.store(-1, std::memory_order_release);
	}

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

	bool TryAcquireRouting()
	{
		return !mIsRouting.test_and_set(std::memory_order_acquire);
	}
	void ReleaseRouting()
	{
		mIsRouting.clear(std::memory_order_release);
	}

private:
	INT32 mIndex = -1;
	std::string mUserID = "";
	std::atomic<INT32> roomIndex{ -1 };
	std::atomic<UINT32> mSessionGeneration{ 0 };

	//UINT32 mPacketDataBufferWritePos = 0; // 쓰기 위치
	//UINT32 mPacketDataBufferReadPos = 0; // 읽기 위치

	// ringbuffer로 패킷 데이터 교체
	RingBuffer<RING_BUFFER_SIZE> mPacketDataBuffer;

	std::atomic<DOMAIN_STATE> mCurDomainState{ DOMAIN_STATE::NONE };


	std::mutex mPacketRingBuffMutex;

	// 한 스레드만 사용할 패킷 임시 버퍼
	//char m_tempPacketBuffer[MAX_PACKET_DATA_BUFFER_SIZE];

	std::atomic<bool> mIsDisconnecting{ false };
	std::atomic_flag mIsRouting = ATOMIC_FLAG_INIT;

};