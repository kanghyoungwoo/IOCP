#include "CallbackDispatcher.h"
#include "UserManager.h"
#include "RoomManager.h"
#include "MysqlManager.h"
#include "StrandProcessor.h"
#include "StrandCallback.h"
#include "MySQLTaskDefine.h"
#include "ConfigManager.h"
#include "ErrorCode.h"
#include <ctime>

CallbackDispatcher::CallbackDispatcher(UserManager* pUserMgr, RoomManager* pRoomMgr, MySQLManager* pMySQLMgr, StrandProcessor* pStrand)
	: mUserManager(pUserMgr)
	, mRoomManager(pRoomMgr)
	, mMySQLManager(pMySQLMgr)
	, mStrand(pStrand)
{
}

void CallbackDispatcher::Dispatch(PacketJob* pNotify)
{
	auto pUser = mUserManager->GetUserByConnIdx(pNotify->clientIndex);

	if (!pUser || pUser->GetSessionGeneration() != pNotify->sessionGeneration)
	{
		mStrand->FreeCallback(pNotify);
		return;
	}

	switch (pNotify->cb.type)
	{
	case StrandCallbackType::FREE_USER:
	{
		mUserManager->DeleteUserInfo(pUser);
		break;
	}
	case StrandCallbackType::USER_LEFT_ROOM:
	{
		pUser->SetDomainState(User::DOMAIN_STATE::LOGIN);
		pUser->ResetRoom();
		break;
	}
	case StrandCallbackType::USER_ENTERED_ROOM:
	{
		if (pNotify->cb.result == (UINT16)ERROR_CODE::NONE)
		{
			pUser->EnterRoom(pNotify->cb.roomNumber);

			MySQLRoomEventReq req{};
			strcpy_s(req.UserID, pUser->GetUserID().c_str());
			req.RoomNumber = pNotify->cb.roomNumber;
			req.EventType  = RoomEventType::ENTER;
			req.TimeStampSec = (UINT64)time(nullptr);

			MySQLTask task{};
			task.UserIndex = pNotify->clientIndex;
			task.TaskID    = MySQLTaskID::INSERT_ROOM_EVENT;
			task.DataSize  = sizeof(MySQLRoomEventReq);
			CopyMemory(task.body, &req, task.DataSize);

			if (!ConfigManager::GetInstance().Get().TestMode)
				mMySQLManager->PushTask(task);
		}
		break;
	}
	case StrandCallbackType::ROOM_BROKEN:
	{
		auto pRoom = mRoomManager->GetRoomByNumber(pNotify->cb.roomNumber);
		if (pRoom)
			pRoom->Reset(mRoomManager->FreeJobFunc);
		break;
	}
	}

	mStrand->FreeCallback(pNotify);
}
