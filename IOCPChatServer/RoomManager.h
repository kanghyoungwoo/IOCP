#pragma once
#include "Room.h"
#include <vector>
#include <functional>
class RoomManager
{
public:
	RoomManager() = default;
	~RoomManager()
	{
		if (mRooms != nullptr)
		{
			delete[] mRooms;
			mRooms = nullptr;
		}
	}

	void Init(const INT32 beginRoomNumber_, const INT32 maxRoomCount_, const INT32 maxRoomUserCount_);

	INT32 GetMaxRoomCount()
	{
		return mMaxRoomCount;
	}

	INT16 EnterUser(INT32 roomNumber_, User* user_);

	INT16 LeaveUser(INT32 roomNumber_, User* user_);

	Room* GetRoomByNumber(INT32 number_);

	std::function<void(UINT32, UINT32, UINT32, char*)> SendPacketFunc;

private:
	//std::vector<Room*> mRoomList;
	Room* mRooms = nullptr;
	INT32 mBeginRoomNumber = 0;
	INT32 mMaxRoomCount = 0;
	INT32 mEndRoomNumber = 0;
};
