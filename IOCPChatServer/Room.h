#pragma once
#include "UserManager.h"
#include "Packet.h"
#include "PacketJob.h"
#include "MPSCQueue.h"
#include <functional>
#include <atomic>

template<typename T>
class LockFreeStack;

class Room
{
	friend class LockFreeStack<Room>;
public:
	enum class EnqueueResult
	{
		SUCCESS_FIRST,
		SUCCESS_APPENDED,
		FAILED_DROPPED
	};

	Room() = default;
	~Room() = default;


	void Init(const INT32 roomNumber_, const INT32 maxUsercount_)
	{
		mMaxUserCount = maxUsercount_;
		mRoomNumber = roomNumber_;
	}

	INT32 GetMaxUserCount()
	{
		return mMaxUserCount;
	}

	INT32 GetCurrentUserCount()
	{
		return mCurrentUserCount;
	}

	INT32 GetRoomNumber()
	{
		return mRoomNumber;
	}	

	UINT16 EnterUser(User* user_)
	{
		//std::lock_guard<std::mutex> lock(mUserListMutex);
		
		// 현재 유저가 max보다 많으면 
		if (mCurrentUserCount >= mMaxUserCount)
		{
			printf("Room is full. Cannot Enter the roomnum : %d\n", mRoomNumber);
			return (UINT16)ERROR_CODE::ENTER_ROOM_FULL_USER;
		}
		// 유저 리스트 목록에 추가 하고 count 증가
		mUserList.push_back(user_);
		++mCurrentUserCount;

		// 유저 객체에 입장한 방 설정
		user_->EnterRoom(mRoomNumber);

		return (UINT16)ERROR_CODE::NONE;
	}

	void LeaveUser(User* leaveUser_)
	{
		//std::lock_guard<std::mutex> lock(mUserListMutex);
		auto leaveUserID = leaveUser_->GetUserID();

		mUserList.remove(leaveUser_);

		//mUserList.remove_if([leaveUser_](std::shared_ptr<User> pUser) {
		//	return leaveUser_.get() == pUser.get();  // 포인터 주소 비교
		//	});

		--mCurrentUserCount;
	}

	void NotifyChat(INT32 clientIndex_, const char* userID_, const char* msg_)
	{
		ROOM_CHAT_NOTIFY_PACKET roomChatNotifyPacket;
		roomChatNotifyPacket.PacketId = (UINT16)PACKET_ID::ROOM_CHAT_NOTIFY;
		roomChatNotifyPacket.PacketLength = sizeof(roomChatNotifyPacket);

		// 메모리를 먼저 초기화
		memset(roomChatNotifyPacket.Message, 0, sizeof(roomChatNotifyPacket.Message));
		memset(roomChatNotifyPacket.UserID, 0, sizeof(roomChatNotifyPacket.UserID));

		//strncpy_s(roomChatNotifyPacket.UserID, sizeof(roomChatNotifyPacket.UserID), userID_, _TRUNCATE);
		//strncpy_s(roomChatNotifyPacket.Message, sizeof(roomChatNotifyPacket.Message), msg_, _TRUNCATE);

		

		// 문자열 안전 복사 (방법 1 - strcpy_s 사용)
		strcpy_s(roomChatNotifyPacket.UserID, sizeof(roomChatNotifyPacket.UserID), userID_);

		// msg_는 ROOM_CHAT_REQUEST_PACKET 포인터이므로 Message 멤버를 가져와야 함
		auto pChatReqPacket = reinterpret_cast<const ROOM_CHAT_REQUEST_PACKET*>(msg_);
		strcpy_s(roomChatNotifyPacket.Message, sizeof(roomChatNotifyPacket.Message), pChatReqPacket->Message);

		// 디버깅용 로그
		printf("채팅 알림: UserID='%s', Message='%s'\n", roomChatNotifyPacket.UserID, roomChatNotifyPacket.Message);

		SendToAllUser(sizeof(roomChatNotifyPacket), (char*)&roomChatNotifyPacket, clientIndex_, false);
		
		/*CopyMemory(roomChatNotifyPacket.Message, msg_, sizeof(roomChatNotifyPacket.Message));
		CopyMemory(roomChatNotifyPacket.UserID, userID_, sizeof(roomChatNotifyPacket.UserID));

		SendToAllUser(sizeof(roomChatNotifyPacket), (char*)&roomChatNotifyPacket, clientIndex_, false);
		printf("채팅 알림: UserID='%s', Message='%s'\n", roomChatNotifyPacket.UserID, roomChatNotifyPacket.Message);
		printf("메세지가 전송되었습니다 !\n");*/
	}


	std::function<void(UINT32, UINT32, char*)>SendPacketFunc;

	void Reset(INT32 roomNumber_, INT32 maxUserCount_)
	{
		// 기존 상태 초기화
		mRoomNumber = roomNumber_;
		mMaxUserCount = maxUserCount_;
		mCurrentUserCount = 0;
		mUserList.clear();

		// Strand 상태 초기화
		mMsgCount.store(0, std::memory_order_relaxed);
		mGeneration.fetch_add(1, std::memory_order_release);	// generation 증가 
		mIsBroken.store(false, std::memory_order_relaxed);

		// localQueue에 남은 잔여 Job drain
		// 나중에 ObjectPool<PacketJob>이 준비되면 여기서 Free 
		while (mLocalQueue.Pop() != nullptr)
		{

		}
	}

	EnqueueResult EnqueueJob(PacketJob* pJob)
	{
		// 실패시 즉시 거절
		if (mIsBroken.load(std::memory_order_acquire))
			return EnqueueResult::FAILED_DROPPED;

		if (pJob->targetGeneration != mGeneration.load(std::memory_order_acquire))
			return EnqueueResult::FAILED_DROPPED;

		// 큐에 삽입
		mLocalQueue.Push(pJob);

		// 카운터로 등록 여부 결정
		if (mMsgCount.fetch_add(1, std::memory_order_acq_rel) == 0)
		{
			return EnqueueResult::SUCCESS_FIRST;
		}

		return EnqueueResult::SUCCESS_APPENDED;
	}

	// Strand 접근자
	MPSCQueue<PacketJob>&	GetLocalQueue()	{ return mLocalQueue; }
	std::atomic<int>&		GetMsgCount()	{ return mMsgCount; }
	uint32_t				GetGeneration() { return mGeneration.load(std::memory_order_acquire); }
	bool                    IsBroken()		{ return mIsBroken.load(std::memory_order_acquire); }
	void                    SetBroken()		{ mIsBroken.store(true, std::memory_order_release); }

private:

	void SendToAllUser(const UINT16 dataSize_, char* data_, const INT32 skipUserIndex_, bool skip_)
	{
		// 1. Room 객체는 모든 유저의 정보를 리스트로 관리하고 있다.
		// 2. 해당 리스트를 반복을 돌며 Send 요청을 보내면 된다.
		// 3. 여기서 특이한 점은 패킷 매니저의 함수를 함수 포인터로 받아 유저 객체에 저장한 뒤
		// 4. 그 함수 포인터를 사용하여 패킷 매니저의 함수를 호출하여 전송을 수행한다.

		//std::lock_guard<std::mutex> lock(mUserListMutex);

		for (auto pUser : mUserList)
		{
			if (pUser == nullptr)
				continue;
			if (skip_ == true && pUser->GetNetConnIndex() == skipUserIndex_)
				continue;
			SendPacketFunc((UINT32)pUser->GetNetConnIndex(), (UINT32)dataSize_, data_);
		}

	}

	std::list<User*> mUserList;	// 유저 리스트 -> 멀티스레드 접근 위험
	//std::list<std::shared_ptr<User>>mUserList;
	//std::mutex mUserListMutex;

	INT32 mMaxUserCount = 0;
	INT32 mCurrentUserCount = 0;
	INT32 mRoomNumber = -1;

	// Strand 패턴
	MPSCQueue<PacketJob>	mLocalQueue;		// 이 방 전용 MPSC 큐
	std::atomic<int>		mMsgCount{ 0 };		// Push/Pop 동기화 카운터
	std::atomic<uint32_t>	mGeneration{ 0 };	// ABA 방어용 세대 카운터
	std::atomic<bool>		mIsBroken{ false };	// Poison Pill 격리 flag

	// Pool 링크  (Object Pool용, intrusive)
	uint32_t poolNext = UINT32_MAX;	// LockFreeStack이 사용

};