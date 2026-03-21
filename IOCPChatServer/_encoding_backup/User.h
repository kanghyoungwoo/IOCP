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
	static constexpr size_t MAX_PACKET_DATA_BUFFER_SIZE = 8096;
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
		//mGeneration++;	// ���� ���Ḷ�� ����

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

	// ������ ó�� Ȱ��
	// TODO: ������ ������ ���·� �ٲٱ�
	void SetPacketData(const UINT32 dataSize_, char* pData_)
	{
		if (pData_ == nullptr || dataSize_ == 0)
			return;

		std::lock_guard<std::mutex>lock(mPacketRingBuffMutex);

		size_t written = mPacketDataBuffer.Write(pData_, dataSize_);

		// ������ �� �� �� ��� ���
		if (written < dataSize_)
		{
			LOG_ERROR("%zu bytes out of %u bytes to packet buffer\n", written, dataSize_);
			return;
		}
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

		// ��ü ��Ŷ ũ�⸸ŭ �����Ͱ� �ִ��� Ȯ��
		if (pHeader->PacketLength > mPacketDataBuffer.Size())
		{
			LOG_DEBUG("Packet data insufficient - need(%d) : have(%zu)\n", pHeader->PacketLength, mPacketDataBuffer.Size());
			return PacketInfo();
		}
		
		// ��Ŷ ������ �ӽ� ���ۿ� �о����
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
		mCurDomainState = DOMAIN_STATE::ROOM;  // ���⼭ ���� ����
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

	UINT32 mPacketDataBufferWritePos = 0; // ������
	UINT32 mPacketDataBufferReadPos = 0; //

	//char* mPacketDataBuffer = nullptr;
	
	// ringbuffer�� ��Ŷ ������ ��ü
	RingBuffer<MAX_PACKET_DATA_BUFFER_SIZE> mPacketDataBuffer;
	
	DOMAIN_STATE mCurDomainState = DOMAIN_STATE::NONE;

	std::mutex mPacketRingBuffMutex;
	
	// �� �������� ���� ��Ŷ ���� ����
	char m_tempPacketBuffer[MAX_PACKET_DATA_BUFFER_SIZE];

	std::atomic<bool> mIsDisconnecting{ false };
	
};