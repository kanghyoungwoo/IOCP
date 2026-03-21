#pragma once

#include "User.h"
#include "ErrorCode.h"
#include <mutex>
#include <unordered_map>
#include <vector>
#include <atomic>
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
		//mUserObjPool = std::vector<std::shared_ptr<User>>(maxUserCount_);

		for (auto i = 0;i < mMaxUserCnt;i++)
		{
			//mUserObjPool[i] = std::make_shared<User>();
			//mUserObjPool[i]->Init(i);
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
			//std::lock_guard<std::mutex>lock(mLock);
			--mCurrentUserCnt;
		}
	}

	ERROR_CODE Adduser(char* userID_, UINT32 clientIndex_)
	{
		std::string userIDStr = userID_;
		//std::lock_guard<std::mutex> lock(mUserDictMutex);

		// �ߺ� �˻� �� ����
		auto result = mUserIDDictionary.insert(std::pair<std::string, int>(userIDStr, clientIndex_));

		if (!result.second) // �̹� �����ϸ� ���� ����
		{
			LOG_DEBUG("Duplicate login attempt! :%s\n",userID_);
			return ERROR_CODE::LOGIN_USER_ALREADY;
		}

		// ���� ���� �� user ��ü ����
		mUserObjPool[clientIndex_]->SetLogin(userID_);
		LOG_DEBUG("User registered : %s (ClientIndex : %d)\n", userID_, clientIndex_);

		//UINT32 user_index = clientIndex_;
		//// ���ο� ���� �߻��Ҷ� usermanager�� �����ִ� user��ü �Ҵ�����
		//auto userIndex = clientIndex_;
		//mUserObjPool[userIndex]->SetLogin(userID_);

		//// string���� ��ȯ�ؼ� ����
		//std::string userIDStr = userID_;
		//mUserIDDictionary.insert(std::pair<std::string, int>(userIDStr, clientIndex_));


		////mUserIDDictionary.insert(std::pair<char*, int>(userID_, clientIndex_));
		
		return ERROR_CODE::NONE;
	}


	//std::shared_ptr<User> GetUserByConnIdx(INT32 clientIndex_)
	//{
	//	if (clientIndex_ < 0 || clientIndex_ >= mMaxUserCnt)
	//		return nullptr;
	//	return mUserObjPool[clientIndex_];
	//}
	User* GetUserByConnIdx(INT32 clientIndex_)
	{
		return mUserObjPool[clientIndex_];
	}

	void DeleteUserInfo(User* user_)
	{
		LOG_DEBUG("User info deleted : %s\n", user_->GetUserID().c_str());
		mUserIDDictionary.erase(user_->GetUserID());
		user_->Clear();
		user_->IncrementGeneration();
		DecreaseUserCnt();
	}


	INT32 FindUserIndexByID(char* userID_)
	{
		std::string userIDStr = userID_; // char*�� string���� ��ȯ

		//std::lock_guard<std::mutex>lock(mUserDictMutex);

		auto res = mUserIDDictionary.find(userIDStr);

		//auto res = mUserIDDictionary.find(userID_);
		if (res != mUserIDDictionary.end())
		{
			return (*res).second;
		}
		return -1;
	}

private:
	//INT32 mCurrentUserCnt = 0;
	std::atomic<int> mCurrentUserCnt = { 0 };
	INT32 mMaxUserCnt = -1;

	std::vector<User*> mUserObjPool;
	//std::vector<std::shared_ptr<User>> mUserObjPool;
	std::unordered_map<std::string, int>mUserIDDictionary;
	std::mutex mLock;
	//std::mutex mUserDictMutex;
};