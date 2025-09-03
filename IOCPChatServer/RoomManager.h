#pragma once
#include "Room.h"

class RoomManager
{
public:
	RoomManager() = default;
	~RoomManager() = default;

	void Init(const INT32 beginRoomNumber_, const INT32 maxRoomCount_, const INT32 maxRoomUserCount_)
	{
		mBeginRoomNumber = beginRoomNumber_;
		mMaxRoomCount = maxRoomCount_;
		mEndRoomNumber = beginRoomNumber_ + maxRoomCount_;

		mRoomList = std::vector <Room*> (mMaxRoomCount);

		for (int i = 0;i < maxRoomCount_; i++)
		{
			mRoomList[i] = new Room;
			mRoomList[i]->SendPacketFunc = SendPacketFunc;
			mRoomList[i]->Init(i + beginRoomNumber_, maxRoomUserCount_);
		}
	}

	INT32 GetMaxRoomCount()
	{
		return mMaxRoomCount;
	}

	INT16 EnterUser(INT32 roomNumber_, User* user_)
	{
		auto room = GetRoomByNumber(roomNumber_);
		if (room == nullptr)
		{
			return (INT16)ERROR_CODE::ROOM_INVALID_INDEX;
		}

		return room->EnterUser(user_);
	}

	INT16 LeaveUser(INT32 roomNumber_, User* user_)
	{
		auto room = GetRoomByNumber(roomNumber_);
		if (room == nullptr)
		{
			return (INT16)ERROR_CODE::ROOM_INVALID_INDEX;
		}
		// 다시 한 번 확인
		user_->SetDomainState(User::DOMAIN_STATE::LOGIN);
		room->LeaveUser(user_);

		return (INT16)ERROR_CODE::NONE;
	}

	Room* GetRoomByNumber(INT32 number_)
	{
		if (number_ < mBeginRoomNumber || number_ >= mEndRoomNumber)
		{
			printf("유효하지 않은 방 번호 !\n");
			return nullptr;
		}
		INT32 index = number_ - mBeginRoomNumber;
		return mRoomList[index];
	}


	std::function<void(UINT32, UINT32, char*)>SendPacketFunc;
private:
	std::vector<Room*> mRoomList;
	INT32 mBeginRoomNumber = 0;
	INT32 mMaxRoomCount = 0;
	INT32 mEndRoomNumber = 0;
};