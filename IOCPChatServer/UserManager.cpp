#include "UserManager.h"

void UserManager::Init(const UINT32 maxUserCount_)
{
	mMaxUserCnt = maxUserCount_;
	mUserObjPool.clear();
	mUserObjPool.reserve(mMaxUserCnt);

	for (auto i = 0;i < mMaxUserCnt;i++)
	{
		auto newUser = std::make_unique<User>();
		newUser->Init(i);
		mUserObjPool.push_back(std::move(newUser));
	}
}

ERROR_CODE UserManager::Adduser(char* userID_, UINT32 clientIndex_)
{
	std::string userIDStr = userID_;
	//std::lock_guard<std::mutex> lock(mUserDictMutex);

	// 중복 검사 후 삽입
	auto result = mUserIDDictionary.insert(std::pair<std::string, int>(userIDStr, clientIndex_));

	if (!result.second) // 이미 존재하면 삽입 실패
	{
		LOG_DEBUG("Duplicate login attempt! :%s\n", userID_);
		return ERROR_CODE::LOGIN_USER_ALREADY;
	}

	// 삽입 성공 시 user 객체 설정
	mUserObjPool[clientIndex_]->SetLogin(userID_);
	LOG_DEBUG("User registered : %s (ClientIndex : %d)\n", userID_, clientIndex_);

	//UINT32 user_index = clientIndex_;
	//// 새로운 접속 발생할때 usermanager가 갖고있는 user객체 할당해줌
	//auto userIndex = clientIndex_;
	//mUserObjPool[userIndex]->SetLogin(userID_);

	//// string으로 변환해서 저장
	//std::string userIDStr = userID_;
	//mUserIDDictionary.insert(std::pair<std::string, int>(userIDStr, clientIndex_));


	////mUserIDDictionary.insert(std::pair<char*, int>(userID_, clientIndex_));

	return ERROR_CODE::NONE;
}

void UserManager::DeleteUserInfo(User* user_)
{
	LOG_DEBUG("User info deleted : %s\n", user_->GetUserID().c_str());
	mUserIDDictionary.erase(user_->GetUserID());
	user_->Clear();
	user_->IncrementGeneration();
	DecreaseUserCnt();
}


INT32 UserManager::FindUserIndexByID(char* userID_)
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