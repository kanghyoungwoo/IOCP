#pragma once

#include "Define.h"
#include "UserManager.h"
#include "Packet.h"

#include <functional>


class Room
{
public:
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
			LOG_ERROR("Room is full. Cannot Enter the roomnum : %d\n", mRoomNumber);
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
		LOG_DEBUG("채팅 알림: UserID='%s', Message='%s'\n", roomChatNotifyPacket.UserID, roomChatNotifyPacket.Message);

		SendToAllUser(sizeof(roomChatNotifyPacket), (char*)&roomChatNotifyPacket, clientIndex_, false);
		
		/*CopyMemory(roomChatNotifyPacket.Message, msg_, sizeof(roomChatNotifyPacket.Message));
		CopyMemory(roomChatNotifyPacket.UserID, userID_, sizeof(roomChatNotifyPacket.UserID));

		SendToAllUser(sizeof(roomChatNotifyPacket), (char*)&roomChatNotifyPacket, clientIndex_, false);
		printf("채팅 알림: UserID='%s', Message='%s'\n", roomChatNotifyPacket.UserID, roomChatNotifyPacket.Message);
		printf("메세지가 전송되었습니다 !\n");*/
	}


	std::function<void(UINT32, UINT32, char*)>SendPacketFunc;

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

};