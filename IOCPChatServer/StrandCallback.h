#pragma once
#include<atomic>

enum class StrandCallbackType
{
	FREE_USER,			//	DISCONNECT -> UserManager에서 제거
	USER_LEFT_ROOM,		//	ROOM_LEAVE -> DomainState를 Login으로 복원
	 USER_ENTERED_ROOM,	//	ROOM_ENTER -> DomainState를 Room으로 변경
	// 추후 확장
};

struct StrandCallback
{
	StrandCallbackType type;
	uint32_t clientIndex;
	int32_t roomNumber = -1;	// 입장한 방 번호 전달용
	uint16_t result = 0;		// 방 입장 결과 코드 전달용

	// MPSC Queue 링크용
	std::atomic<StrandCallback*> mpscNext{ nullptr };

	// ObjectPool 링크용
	uint32_t poolNext = UINT32_MAX;
};
