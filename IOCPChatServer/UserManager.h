#pragma once

#include "User.h"
#include "ErrorCode.h"
#include <mutex>
#include <unordered_map>
#include <vector>
#include <atomic>
#include <memory>
#include <cassert>
//#include <string>


class UserManager
{
public:
	UserManager() = default;
	~UserManager() = default;

	void Init(const UINT32 maxUserCount_);

	INT32 GetCurrentUserCnt()
	{
		return mCurrentUserCnt;
	}

	INT32 GetMaxUserCnt()
	{
		return mMaxUserCnt;
	}

	void IncreaseUserCnt()
	{
		mCurrentUserCnt++;

	}

	void DecreaseUserCnt()
	{
		if (mCurrentUserCnt > 0)
		{
			//std::lock_guard<std::mutex>lock(mLock);
			--mCurrentUserCnt;
		}
	}
	ERROR_CODE Adduser(char* userID_, UINT32 clientIndex_);
	User* GetUserByConnIdx(INT32 clientIndex_)
	{
		assert(clientIndex_ >= 0 && (size_t)clientIndex_ < mUserObjPool.size());
		return mUserObjPool[clientIndex_].get();
	}
	void DeleteUserInfo(User* user_);
	INT32 FindUserIndexByID(char* userID_);

private:
	//INT32 mCurrentUserCnt = 0;
	std::atomic<int> mCurrentUserCnt = { 0 };
	INT32 mMaxUserCnt = -1;
	std::vector<std::unique_ptr<User>>mUserObjPool;
	std::unordered_map<std::string, int>mUserIDDictionary;
	//std::mutex mUserDictMutex;
};
