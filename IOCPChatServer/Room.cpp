#include "Room.h"
#include "Packet.h"

UINT16 Room::EnterUser(User* user_)
{
	//std::lock_guard<std::mutex> lock(mUserListMutex);

	// 현재 인원이 max보다 크면 입장 불가
	if (mCurrentUserCount >= mMaxUserCount)
	{
		LOG_DEBUG("Room is full. Cannot Enter the roomnum : %d\n", mRoomNumber);
		return (UINT16)ERROR_CODE::ENTER_ROOM_FULL_USER;
	}
	// 유저 리스트에 추가하고 count 증가
	mUserList.push_back(user_);
	++mCurrentUserCount;


	return (UINT16)ERROR_CODE::NONE;
}

void Room::LeaveUser(User* leaveUser_)
{
	//std::lock_guard<std::mutex> lock(mUserListMutex);
	auto leaveUserID = leaveUser_->GetUserID();

	mUserList.remove(leaveUser_);

	//mUserList.remove_if([leaveUser_](std::shared_ptr<User> pUser) {
	//	return leaveUser_.get() == pUser.get();  // 포인터 주소 비교
	//	});

	--mCurrentUserCount;
}

void Room::NotifyChat(INT32 clientIndex_, const char* userID_, const char* msg_)
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

	// msg_는 ROOM_CHAT_REQUEST_PACKET 포맷이므로 Message 필드를 추출해야 함
	auto pChatReqPacket = reinterpret_cast<const ROOM_CHAT_REQUEST_PACKET*>(msg_);
	strcpy_s(roomChatNotifyPacket.Message, sizeof(roomChatNotifyPacket.Message), pChatReqPacket->Message);

	// 디버그 로그
	LOG_DEBUG("Chat Notify: UserID='%s', Message='%s'\n", roomChatNotifyPacket.UserID, roomChatNotifyPacket.Message);

	SendToAllUser(sizeof(roomChatNotifyPacket), (char*)&roomChatNotifyPacket, clientIndex_, false);

	/*CopyMemory(roomChatNotifyPacket.Message, msg_, sizeof(roomChatNotifyPacket.Message));
	CopyMemory(roomChatNotifyPacket.UserID, userID_, sizeof(roomChatNotifyPacket.UserID));

	SendToAllUser(sizeof(roomChatNotifyPacket), (char*)&roomChatNotifyPacket, clientIndex_, false);
	printf("채팅 알림: UserID='%s', Message='%s'\n", roomChatNotifyPacket.UserID, roomChatNotifyPacket.Message);
	printf("메세지가 전송되었습니다 !\n");*/
}


void Room::Reset(INT32 roomNumber_, INT32 maxUserCount_)
{
	// 방 정보 초기화
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

Room::EnqueueResult Room::EnqueueJob(PacketJob* pJob)
{
	// 고장 시 즉시 거절
	if (mIsBroken.load(std::memory_order_acquire))
		return EnqueueResult::FAILED_DROPPED;

	if (pJob->job.targetGeneration != mGeneration.load(std::memory_order_acquire))
		return EnqueueResult::FAILED_DROPPED;

	// 큐에 삽입
	// 런타임 검증
	assert(pJob->phase == PacketJob::Phase::JOB);
	mLocalQueue.Push(pJob);

	// 카운터로 첫번째 여부 판별
	if (mMsgCount.fetch_add(1, std::memory_order_acq_rel) == 0)
	{
		return EnqueueResult::SUCCESS_FIRST;
	}

	return EnqueueResult::SUCCESS_APPENDED;
}

User* Room::FindUserByClientIndex(uint32_t clientIndex)
{
	for (auto pUser : mUserList)
	{
		if (pUser != nullptr && pUser->GetNetConnIndex() == clientIndex)
			return pUser;
	}
	return nullptr;
}

void Room::SendToAllUser(const UINT16 dataSize_, char* data_, const INT32 skipUserIndex_, bool skip_)
{
	// 1. Room 객체의 모든 유저의 세션 리스트를 가져
	// 2. 해당 리스트를 반복을 통해 Send 요청을 진행할 것
	// 3. 여기서 특이한 점은 패킷 매니저의 함수를 함수 포인터로 받아 방의 객체에 저장한 것
	// 4. 이 함수 포인터를 사용하여 패킷 매니저의 함수를 호출하여 데이터 전송

	//std::lock_guard<std::mutex> lock(mUserListMutex);

	for (auto pUser : mUserList)
	{
		if (pUser == nullptr)
			continue;
		if (skip_ == true && pUser->GetNetConnIndex() == skipUserIndex_)
			continue;
		SendPacketFunc((UINT32)pUser->GetNetConnIndex(), pUser->GetSessionGeneration(), (UINT32)dataSize_, data_);
	}

}