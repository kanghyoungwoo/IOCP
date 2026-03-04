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

		// 중복 검사 후 삽입
		auto result = mUserIDDictionary.insert(std::pair<std::string, int>(userIDStr, clientIndex_));

		if (!result.second) // 이미 존재하면 삽입 실패
		{
			printf("중복 로그인 시도 ! :%s\n",userID_);
			return ERROR_CODE::LOGIN_USER_ALREADY;
		}

		// 삽입 성공 시 user 객체 설정
		mUserObjPool[clientIndex_]->SetLogin(userID_);
		printf("사용자 등록 성공 : %s (ClientIndex : %d)\n", userID_, clientIndex_);

		//UINT32 user_index = clientIndex_;
		//// 새로운 연결 발생할때 usermanager가 갖고있는 user객체 할당해줌
		//auto userIndex = clientIndex_;
		//mUserObjPool[userIndex]->SetLogin(userID_);

		//// string으로 변환해서 저장
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
		printf("사용자 정보 삭제 : %s\n", user_->GetUserID().c_str());
		mUserIDDictionary.erase(user_->GetUserID());
		user_->Clear();
		user_->IncrementGeneration();
		DecreaseUserCnt();
	}


	INT32 FindUserIndexByID(char* userID_)
	{
		std::string userIDStr = userID_; // char*를 string으로 변환

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