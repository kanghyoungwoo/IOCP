#pragma once

#include "User.h"
#include "ErrorCode.h"

#include <unordered_map>
//#include <vector>
//#include <string>


class UserManager 
{
public:
	UserManager() = default;
	~UserManager() = default;

	void Init(const UINT32 maxUserCount_)
	{
		mMaxUserCnt = maxUserCount_;
		mUserObjPool = std::vector<User*>(mMaxUserCnt);

		for (auto i = 0;i < mMaxUserCnt;i++)
		{
			mUserObjPool[i] = new User;
			mUserObjPool[i]->Init(i);
		}
	}

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
			mCurrentUserCnt--;
		}
	}

	ERROR_CODE Adduser(char* userID_, UINT32 clientIndex_)
	{
		UINT32 user_index = clientIndex_;
		// 새로운 연결 발생할때 usermanager가 갖고있는 user객체 할당해줌
		auto userIndex = clientIndex_;
		mUserObjPool[userIndex]->SetLogin(userID_);

		// string으로 변환해서 저장
		std::string userIDStr = userID_;
		mUserIDDictionary.insert(std::pair<std::string, int>(userIDStr, clientIndex_));


		//mUserIDDictionary.insert(std::pair<char*, int>(userID_, clientIndex_));
		
		return ERROR_CODE::NONE;
	}


	User* GetUserByConnIdx(INT32 clientIndex_)
	{
		return mUserObjPool[clientIndex_];
	}

	void DeleteUserInfo(User* user_)
	{
		mUserIDDictionary.erase(user_->GetUserID());
		user_->Clear();
	}


	INT32 FindUserIndexByID(char* userID_)
	{
		std::string userIDStr = userID_; // char*를 string으로 변환
		auto res = mUserIDDictionary.find(userIDStr);

		//auto res = mUserIDDictionary.find(userID_);
		if (res != mUserIDDictionary.end())
		{
			return (*res).second;
		}
		return -1;
	}

private:
	INT32 mCurrentUserCnt = 0;
	INT32 mMaxUserCnt = -1;


	std::vector<User*> mUserObjPool;
	std::unordered_map<std::string, int>mUserIDDictionary;
};