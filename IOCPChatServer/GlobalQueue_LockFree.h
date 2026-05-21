#pragma once
#include "Room.h"
#include <atomic>
#include <emmintrin.h>
#include <Windows.h>
#include <cstdint>
#include <stdexcept>

// ---------------------------------------------------------------
//  Producer 모드 선택
//   #define USE_SPMC : Producer = 1 (현재: PacketManager 단일 스레드)
//                      CAS 루프 제거, relaxed load/store만 사용
//   주석 처리          : MPMC 모드 (ProcessThread 샤딩 등 다중 Producer 시)
//                      기존 CAS 방식 유지
// ---------------------------------------------------------------
#define USE_SPMC // 주석 처리시 MPMC모드

class GlobalQueue_LockFree
{
private:
	struct Cell
	{
		std::atomic<int32_t> sequence;
		Room* data;
	};

	Cell* m_buffer = nullptr;	// 링버퍼 배열
	uint32_t m_bufferMask = 0;	// 비트마스크로 모듈러 연산
	alignas(64) std::atomic<uint32_t> m_enqueuePos;	// Producer 위치
	alignas(64) std::atomic<uint32_t> m_dequeuePos;	// Consumer 위치
	std::atomic<bool> m_shutdown;

public:
	GlobalQueue_LockFree()
	{
		m_enqueuePos = 0;
		m_dequeuePos = 0;
		m_shutdown = false;

	}
	~GlobalQueue_LockFree()
	{
		if (m_buffer)
		{
			delete[] m_buffer;
		}
	}
	Room* Pop();
	void Init(uint32_t bufferSize);
	void Push(Room* pRoom);
	void Shutdown()
	{
		// 큐를 닫는 신호, 더이상 빈 큐 대기 X
		m_shutdown.store(true, std::memory_order_release);
	}
};

