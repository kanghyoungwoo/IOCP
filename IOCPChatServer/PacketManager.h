#pragma once

#include "Packet.h"
//#include "RedisManager.h"
#include "RoomManager.h"
#include "StrandProcessor.h"

#include <unordered_map>
#include <deque>
#include <functional>
#include <thread>
#include <mutex>
#include <condition_variable>

class UserManager;
class RedisManager;
class MySQLManager;

struct PacketTask
{
	UINT32 clientIndex;
	UINT32 generation;	// enqueue 시점의 generation
};

class PacketManager {
public:
	PacketManager();// = default;
	~PacketManager();// = default;

	void Init(const UINT32 maxClient_);
	bool Run();
	void End();
	bool ReceivePacketData(const UINT32 clientIndex_, const UINT32 generation_, const UINT32 dataSize_, char* pData_);
	void PushSystemPacket(PacketInfo packet_);

	std::function<void(UINT32, UINT32, UINT32, char*)> SendPacketFunc;
	std::function<void(UINT32)> UpdateActivityFunc;


private:
	using PacketHandler = std::function<void(UINT32 clientIndex, UINT32 generation, UINT16 packetSize, char* pPacket)>;
	void RegisterHandlers(); // 핸들러 함수 등록
	// std::function 기반 디스패치
	std::unordered_map<UINT16, PacketHandler>mPacketHandlers;
	void CreateComponent(const UINT32 maxClient_);
	void ClearConnectionInfo(INT32 clientIndex_);

	void EnqueuePacketData(const UINT32 clientIndex_, const UINT32 generation_);
	//PacketInfo DequePacketData();
	//PacketInfo DequeSystemPacketData();
	void ProcessPacket();
	void ProcessRecvPacket(const UINT32 clientIndex_, const UINT32 generation_, const UINT16 packetId_, const UINT16 packetSize_, char* pPacket_);
	void ProcessUserConnect(UINT32 clientIndex_, UINT32 generation_, UINT16 packetSize_, char* pPacket);
	void ProcessUserDisconnect(UINT32 clientIndex_, UINT32 generation_, UINT16 packetSize_, char* pPacket);
	void ProcessLogin(UINT32 clientIndex_, UINT32 generation_, UINT16 packetSize_, char* pPacket_); // 최대 접속자 수, 중복 로그인 확인
	void ProcessLoginDBResult(UINT32 clientIndex_, UINT32 generation_, UINT16 packetSize_, char* pPacket_);
	void ProcessEnterRoom(UINT32 clientIndex_, UINT32 generation_, UINT16 packetSize_, char* pPacket_);
	void ProcessLeaveRoom(UINT32 clientIndex_, UINT32 generation_, UINT16 packetSize_, char* pPacket_);
	void ProcessRoomChatMessage(UINT32 clientIndex_, UINT32 generation_, UINT16 packetSize_, char* pPacket_);

	void NotifyPacketEvent();

	typedef void(PacketManager::* PROCESS_RECV_PACKET_FUNCTION)(UINT32, UINT32, UINT16, char*);
	std::unordered_map<int, PROCESS_RECV_PACKET_FUNCTION>mRecvFunctionDictionary;

	//UserManager* mUserManager;
	//RedisManager* mRedisManager;
	//RoomManager* mRoomManager;
	//MySQLManager* mMySQLManager;

	std::unique_ptr<UserManager> mUserManager;
	std::unique_ptr<RedisManager> mRedisManager;
	std::unique_ptr<RoomManager> mRoomManager;
	std::unique_ptr<MySQLManager> mMySQLManager;

	StrandProcessor m_strandProcessor;

	std::function<void(int, char*)>mSendMQDataFunc;

	bool mIsRunProcessThread = false;
	std::thread mProcessThead;
	std::mutex mLock;
	std::condition_variable mPacketEventCV;

	// queue를 2개 두어서 경합 감소
	// IOCP에서 패킷을 처리하면 수신객체에 락을 걸어야 하기 때문에 락을 안걸기 위해
	// packet처리 쓰레드에서는 여기서만 사용하고 iocp는 네트워크 처리
	//std::deque<INT32> mInComingPacketUserIndex;		// 수신 데이터가 있는 유저 인덱스 저장하는 queue
	//std::deque<PacketTask>mInComingPacketUserIndex;
	//std::deque<PacketInfo> mSystemPacketQueue;		// 네트워크 이벤트 처리하는 queue.. 왜 두 개의 queue로 나누는건 좋은 건지 정리

	// 더블 버퍼링
	std::deque<PacketInfo>mSystemWriteBuffer;
	std::deque<PacketInfo>mSystemReadBuffer;

	std::deque<PacketTask> mWriteBuffer;	// IOCP Worker Thread가 push
	std::deque<PacketTask> mReadBuffer;		// ProcessThread가 소비

};
