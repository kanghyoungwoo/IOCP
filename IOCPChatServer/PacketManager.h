#pragma once

#include "Packet.h"
//#include "RedisManager.h"

#include <unordered_map>
#include <deque>
#include <functional>
#include <thread>
#include <mutex>

class UserManager;

class RedisManager;

class PacketManager {
public:
	PacketManager() = default;
	~PacketManager() = default;

	void Init(const UINT32 maxClient_);
	bool Run();
	void End();
	void ReceivePacketData(const UINT32 clientIndex_, const UINT32 dataSize_, char* pData_);
	void PushSystemPacket(PacketInfo packet_);

	std::function<void(UINT32, UINT32, char*)>SendPacketFunc;


private:
	void CreateComponent(const UINT32 maxClient_);
	void ClearConnectionInfo(INT32 clientIndex_);
	void EnqueuePacketData(const UINT32 clientIndex_);
	PacketInfo DequePacketData();
	PacketInfo DequeSystemPacketData();
	void ProcessPacket();
	void ProcessRecvPacket(const UINT32 clientIndex_, const UINT16 packetId_, const UINT16 packetSize_, char* pPacket_);
	void ProcessUserConnect(UINT32 clientIndex_, UINT16 packetSize_, char* pPacket);
	void ProcessUserDisconnect(UINT32 clientIndex_m, UINT16 packetSize_, char* pPacket);
	void ProcessLogin(UINT32 clientIndex_, UINT16 packetSize_, char* pPacket_); // 최대 접속자 수, 중복 정도만 확인
	void ProcessLoginDBResult(UINT32 clientIndex_, UINT16 packetSize_, char* pPacket_);


	typedef void(PacketManager::* PROCESS_RECV_PACKET_FUNCTION)(UINT32, UINT16, char*);
	std::unordered_map<int, PROCESS_RECV_PACKET_FUNCTION>mRecvFunctionDictionary;

	UserManager* mUserManager;
	RedisManager* mRedisManager;

	std::function<void(int, char*)>mSendMQDataFunc;

	bool mIsRunProcessThread = false;
	std::thread mProcessThead;
	std::mutex mLock;

	// queue를 2개 가지고 있음 
	// IOCP에서 패킷을 처리하면 공용객체에 락을 걸어야 하기 떄문에 락을 안걸기 위해
	// packet처리 쓰레드는 여기서만 사용하고 iocp는 네트워크 처리
	std::deque<INT32> mInComingPacketUserIndex;		// 실제 데이터가 왔을 때 사용하는 queue
	std::deque<PacketInfo> mSystemPacketQueue;		// 네트워크 연결 처리하는 queue.. 이 두 가지 queue를 합치는게 제일 좋긴 함
};
