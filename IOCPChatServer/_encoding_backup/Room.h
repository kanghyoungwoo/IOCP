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
		
		// ���� ������ max���� ������ 
		if (mCurrentUserCount >= mMaxUserCount)
		{
			LOG_DEBUG("Room is full. Cannot Enter the roomnum : %d\n", mRoomNumber);
			return (UINT16)ERROR_CODE::ENTER_ROOM_FULL_USER;
		}
		// ���� ����Ʈ ��Ͽ� �߰� �ϰ� count ����
		mUserList.push_back(user_);
		++mCurrentUserCount;

		// ���� ��ü�� ������ �� ����
		user_->EnterRoom(mRoomNumber);

		return (UINT16)ERROR_CODE::NONE;
	}

	void LeaveUser(User* leaveUser_)
	{
		//std::lock_guard<std::mutex> lock(mUserListMutex);
		auto leaveUserID = leaveUser_->GetUserID();

		mUserList.remove(leaveUser_);

		//mUserList.remove_if([leaveUser_](std::shared_ptr<User> pUser) {
		//	return leaveUser_.get() == pUser.get();  // ������ �ּ� ��
		//	});

		--mCurrentUserCount;
	}

	void NotifyChat(INT32 clientIndex_, const char* userID_, const char* msg_)
	{
		ROOM_CHAT_NOTIFY_PACKET roomChatNotifyPacket;
		roomChatNotifyPacket.PacketId = (UINT16)PACKET_ID::ROOM_CHAT_NOTIFY;
		roomChatNotifyPacket.PacketLength = sizeof(roomChatNotifyPacket);

		// �޸𸮸� ���� �ʱ�ȭ
		memset(roomChatNotifyPacket.Message, 0, sizeof(roomChatNotifyPacket.Message));
		memset(roomChatNotifyPacket.UserID, 0, sizeof(roomChatNotifyPacket.UserID));

		//strncpy_s(roomChatNotifyPacket.UserID, sizeof(roomChatNotifyPacket.UserID), userID_, _TRUNCATE);
		//strncpy_s(roomChatNotifyPacket.Message, sizeof(roomChatNotifyPacket.Message), msg_, _TRUNCATE);

		

		// ���ڿ� ���� ���� (��� 1 - strcpy_s ���)
		strcpy_s(roomChatNotifyPacket.UserID, sizeof(roomChatNotifyPacket.UserID), userID_);

		// msg_�� ROOM_CHAT_REQUEST_PACKET �������̹Ƿ� Message ����� �����;� ��
		auto pChatReqPacket = reinterpret_cast<const ROOM_CHAT_REQUEST_PACKET*>(msg_);
		strcpy_s(roomChatNotifyPacket.Message, sizeof(roomChatNotifyPacket.Message), pChatReqPacket->Message);

		// ������ �α�
		LOG_DEBUG("Chat Notify: UserID='%s', Message='%s'\n", roomChatNotifyPacket.UserID, roomChatNotifyPacket.Message);

		SendToAllUser(sizeof(roomChatNotifyPacket), (char*)&roomChatNotifyPacket, clientIndex_, false);
		
		/*CopyMemory(roomChatNotifyPacket.Message, msg_, sizeof(roomChatNotifyPacket.Message));
		CopyMemory(roomChatNotifyPacket.UserID, userID_, sizeof(roomChatNotifyPacket.UserID));

		SendToAllUser(sizeof(roomChatNotifyPacket), (char*)&roomChatNotifyPacket, clientIndex_, false);
		printf("ä�� �˸�: UserID='%s', Message='%s'\n", roomChatNotifyPacket.UserID, roomChatNotifyPacket.Message);
		printf("�޼����� ���۵Ǿ����ϴ� !\n");*/
	}


	std::function<void(UINT32, UINT32, char*)>SendPacketFunc;

	void Reset(INT32 roomNumber_, INT32 maxUserCount_)
	{
		// ���� ���� �ʱ�ȭ
		mRoomNumber = roomNumber_;
		mMaxUserCount = maxUserCount_;
		mCurrentUserCount = 0;
		mUserList.clear();

		// Strand ���� �ʱ�ȭ
		mMsgCount.store(0, std::memory_order_relaxed);
		mGeneration.fetch_add(1, std::memory_order_release);	// generation ���� 
		mIsBroken.store(false, std::memory_order_relaxed);

		// localQueue�� ���� �ܿ� Job drain
		// ���߿� ObjectPool<PacketJob>�� �غ�Ǹ� ���⼭ Free 
		while (mLocalQueue.Pop() != nullptr)
		{

		}
	}

	EnqueueResult EnqueueJob(PacketJob* pJob)
	{
		// ���н� ��� ����
		if (mIsBroken.load(std::memory_order_acquire))
			return EnqueueResult::FAILED_DROPPED;

		if (pJob->targetGeneration != mGeneration.load(std::memory_order_acquire))
			return EnqueueResult::FAILED_DROPPED;

		// ť�� ����
		mLocalQueue.Push(pJob);

		// ī���ͷ� ��� ���� ����
		if (mMsgCount.fetch_add(1, std::memory_order_acq_rel) == 0)
		{
			return EnqueueResult::SUCCESS_FIRST;
		}

		return EnqueueResult::SUCCESS_APPENDED;
	}

	User* FindUserByClientIndex(uint32_t clientIndex)
	{
		for (auto pUser : mUserList)
		{
			if (pUser != nullptr && pUser->GetNetConnIndex() == clientIndex)
				return pUser;
		}
		return nullptr;
	}

	// Strand ������
	MPSCQueue<PacketJob>&	GetLocalQueue()	{ return mLocalQueue; }
	std::atomic<int>&		GetMsgCount()	{ return mMsgCount; }
	uint32_t				GetGeneration() { return mGeneration.load(std::memory_order_acquire); }
	bool                    IsBroken()		{ return mIsBroken.load(std::memory_order_acquire); }
	void                    SetBroken()		{ mIsBroken.store(true, std::memory_order_release); }

private:

	void SendToAllUser(const UINT16 dataSize_, char* data_, const INT32 skipUserIndex_, bool skip_)
	{
		// 1. Room ��ü�� ��� ������ ������ ����Ʈ�� ����
		// 2. �ش� ����Ʈ�� �ݺ��� ���� Send ��û�� ������ ��
		// 3. ���⼭ Ư���� ���� ��Ŷ �Ŵ����� �Լ��� �Լ� �����ͷ� �޾� ���� ��ü�� ������ ��
		// 4. �� �Լ� �����͸� ����Ͽ� ��Ŷ �Ŵ����� �Լ��� ȣ���Ͽ� ������ ����

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

	std::list<User*> mUserList;	// ���� ����Ʈ -> ��Ƽ������ ���� ����
	//std::list<std::shared_ptr<User>>mUserList;
	//std::mutex mUserListMutex;

	INT32 mMaxUserCount = 0;
	INT32 mCurrentUserCount = 0;
	INT32 mRoomNumber = -1;

	// Strand ����
	MPSCQueue<PacketJob>	mLocalQueue;		// �� �� ���� MPSC ť
	std::atomic<int>		mMsgCount{ 0 };		// Push/Pop ����ȭ ī����
	std::atomic<uint32_t>	mGeneration{ 0 };	// ABA ���� ���� ī����
	std::atomic<bool>		mIsBroken{ false };	// Poison Pill �ݸ� flag

	// Pool ��ũ  (Object Pool��, intrusive)
	uint32_t poolNext = UINT32_MAX;	// LockFreeStack�� ���

};