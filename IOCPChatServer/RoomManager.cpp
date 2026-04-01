#include "RoomManager.h"

void RoomManager::Init(const INT32 beginRoomNumber_, const INT32 maxRoomCount_, const INT32 maxRoomUserCount_)
{
	mBeginRoomNumber = beginRoomNumber_;
	mMaxRoomCount = maxRoomCount_;
	mEndRoomNumber = beginRoomNumber_ + maxRoomCount_;

	//mRoomList = std::vector <Room*> (mMaxRoomCount);

	//for (int i = 0;i < maxRoomCount_; i++)
	//{
	//	mRoomList[i] = new Room;
	//	mRoomList[i]->SendPacketFunc = SendPacketFunc;
	//	mRoomList[i]->Init(i + beginRoomNumber_, maxRoomUserCount_);
	//}
	mRooms = new Room[mMaxRoomCount];
	for (int i = 0;i < maxRoomCount_;i++)
	{
		mRooms[i].SendPacketFunc = SendPacketFunc;
		mRooms[i].Init(i + beginRoomNumber_, maxRoomUserCount_);
	}
}

INT16 RoomManager::EnterUser(INT32 roomNumber_, User* user_)
{
	auto room = GetRoomByNumber(roomNumber_);
	if (room == nullptr)
	{
		return (INT16)ERROR_CODE::ROOM_INVALID_INDEX;
	}

	return room->EnterUser(user_);
}

INT16 RoomManager::LeaveUser(INT32 roomNumber_, User* user_)
{
	auto room = GetRoomByNumber(roomNumber_);
	if (room == nullptr)
	{
		return (INT16)ERROR_CODE::ROOM_INVALID_INDEX;
	}
	// 다시 로비로 상태 변경
	user_->SetDomainState(User::DOMAIN_STATE::LOGIN);
	room->LeaveUser(user_);

	return (INT16)ERROR_CODE::NONE;
}

Room* RoomManager::GetRoomByNumber(INT32 number_)
{
	if (number_ < mBeginRoomNumber || number_ >= mEndRoomNumber)
	{
		LOG_ERROR("Invalid room number!\n");
		return nullptr;
	}
	INT32 index = number_ - mBeginRoomNumber;
	//return mRoomList[index];
	return &mRooms[index];
}

