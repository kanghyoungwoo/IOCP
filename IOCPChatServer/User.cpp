#include "User.h"

void User::Clear()
{
		std::lock_guard<std::mutex>lock(mPacketRingBuffMutex);

		mUserID = "";
		mCurDomainState = DOMAIN_STATE::NONE;
		mPacketDataBuffer.Clear();
		//mGeneration++;	// 세대 카운터 증가
		roomIndex = -1;
		mIsDisconnecting.store(false);

		mSessionGeneration.store(0, std::memory_order_relaxed);
}

int User::SetLogin(char* userID_)
{
	mCurDomainState = DOMAIN_STATE::LOGIN;
	mUserID = userID_;
	LOG_DEBUG("[SetLogin] UserID set to: '%s' for index: %d\n", userID_, mIndex);

	return 0;
}

bool User::SetPacketData(const UINT32 dataSize_, char* pData_)
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

PacketInfo User::GetPacket(char* outBuf, size_t bufSize)
{
	std::lock_guard<std::mutex>lock(mPacketRingBuffMutex);

	if (mPacketDataBuffer.Size() < PACKET_HEADER_LENGTH)
		return PacketInfo();

	char headerBuffer[PACKET_HEADER_LENGTH];
	if (!mPacketDataBuffer.PeekBlock(headerBuffer, PACKET_HEADER_LENGTH))
	{
		return PacketInfo();
	}

	auto pHeader = (PACKET_HEADER*)headerBuffer;

	// 패킷 크기 유효성 검증
	if (pHeader->PacketLength < PACKET_HEADER_LENGTH || pHeader->PacketLength > MAX_PACKET_DATA_BUFFER_SIZE)
	{
		LOG_ERROR("Invalid PacketLength: %d (valid: %u~%u) → 버퍼 초기화\n",
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

	size_t readBytes = mPacketDataBuffer.Read(outBuf, pHeader->PacketLength);

	if (readBytes != pHeader->PacketLength)
	{
		LOG_ERROR("Packet read fail - expected(%d) vs actual(%zu)\n", pHeader->PacketLength, readBytes);
		return PacketInfo();
	}

	PacketInfo packetInfo;
	packetInfo.PacketId = pHeader->PacketId;
	packetInfo.DataSize = pHeader->PacketLength;

	packetInfo.pDataPtr = outBuf;

	return packetInfo;
}