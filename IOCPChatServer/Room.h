#pragma once
#include "UserManager.h"
#include "Packet.h"
#include "Define.h"
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


	INT32 GetRoomNumber()
	{
		return mRoomNumber;
	}

	UINT16 EnterUser(User* user_);
	void LeaveUser(User* leaveUser_);

	void NotifyChat(INT32 clientIndex_, const char* userID_, const char* msg_);

	std::function<void(UINT32, UINT32, UINT32, char*)> SendPacketFunc;
	std::function<void(PacketJob*)> FreeJobFunc;
	void Reset(INT32 roomNumber_, INT32 maxUserCount_);

	EnqueueResult EnqueueJob(PacketJob* pJob);

	User* FindUserByClientIndex(uint32_t clientIndex);

	// Strand 접근자
	MPSCQueue<PacketJob>&	GetLocalQueue()	{ return mLocalQueue; }
	std::atomic<int>&		GetMsgCount()	{ return mMsgCount; }
	uint32_t				GetGeneration() { return mGeneration.load(std::memory_order_acquire); }
	bool                    IsBroken()		{ return mIsBroken.load(std::memory_order_acquire); }
	void                    SetBroken()		{ mIsBroken.store(true, std::memory_order_release); }

private:

	void SendToAllUser(const UINT16 dataSize_, char* data_, const INT32 skipUserIndex_, bool skip_);

	std::list<User*> mUserList;	// 유저 리스트 -> 멀티스레드 접근 없음
	//std::list<std::shared_ptr<User>>mUserList;
	//std::mutex mUserListMutex;

	INT32 mMaxUserCount = 0;
	INT32 mCurrentUserCount = 0;
	INT32 mRoomNumber = -1;

	// Strand 멤버
	MPSCQueue<PacketJob>	mLocalQueue;		// 방 별 전용 MPSC 큐
	std::atomic<int>		mMsgCount{ 0 };		// Push/Pop 동기화 카운터
	std::atomic<uint32_t>	mGeneration{ 0 };	// ABA 방지 세대 카운터
	std::atomic<bool>		mIsBroken{ false };	// Poison Pill 격리 flag

	// Pool 링크 (Object Pool용, intrusive)
	uint32_t poolNext = UINT32_MAX;	// LockFreeStack에서 사용

};
