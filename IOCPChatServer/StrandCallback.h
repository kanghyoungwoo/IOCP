#pragma once
#include<atomic>

enum class StrandCallbackType
{
	FREE_USER,			//	DISCONNECT -> usermanager에서 삭제
	USER_LEFT_ROOM,		//	ROOM_LEAVE -> DomainState를 Login으로 보귀
	// 추후 확장
};

struct StrandCallback
{
	StrandCallbackType type;
	uint32_t clientIndex;

	// MPSC Queue 
	std::atomic<StrandCallback*> mpscNext{ nullptr };

	//ObjectPool 
	uint32_t poolNext = UINT32_MAX;
};