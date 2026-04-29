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
	auto result = mUserIDDictionary.insert(std::pair<std::string, int>(userIDStr, clientIndex_));	// 맵에 먼저 등록

	if (!result.second) // 이미 존재하면 삽입 실패
	{
		LOG_DEBUG("중복 로그인 시도 ! :%s\n", userID_);
		return ERROR_CODE::LOGIN_USER_ALREADY;
	}

	// 삽입 성공 시 user 객체 설정
	mUserObjPool[clientIndex_]->SetLogin(userID_);	// 그 후 user 상태 변경
	LOG_DEBUG("User registered : %s (ClientIndex : %d)\n", userID_, clientIndex_);

	return ERROR_CODE::NONE;
}

void UserManager::DeleteUserInfo(User* user_)
{
	LOG_DEBUG("User info deleted : %s\n", user_->GetUserID().c_str());
	mUserIDDictionary.erase(user_->GetUserID());	// id로 맵에서 제거
	user_->Clear();		// clear() -> mUserid = ""
	DecreaseUserCnt();	// 카운트 감소
}


INT32 UserManager::FindUserIndexByID(char* userID_)
{
	//std::lock_guard<std::mutex>lock(mUserDictMutex);

	auto res = mUserIDDictionary.find(userID_);

	//auto res = mUserIDDictionary.find(userID_);
	if (res != mUserIDDictionary.end())
	{
		return res->second;
	}
	return -1;
}