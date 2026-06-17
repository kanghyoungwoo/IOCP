#pragma once
#include "Room.h"
#include <atomic>
#include <semaphore>    // C++20 — MSVC STL: WaitOnAddress / WakeByAddressSingle 기반
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
//#define USE_SPMC // 주석 처리시 MPMC모드 // IOCP Worker 다중 producer -> MPMC 모드

class GlobalQueue_LockFree
{
private:
	struct alignas(64) Cell
	{
		std::atomic<int32_t> sequence;
		Room* data;
	};

	Cell* m_buffer = nullptr;	// 링버퍼 배열
	uint32_t m_bufferMask = 0;	// 비트마스크로 모듈러 연산
	alignas(64) std::atomic<uint32_t> m_enqueuePos;	// Producer 위치
	alignas(64) std::atomic<uint32_t> m_dequeuePos;	// Consumer 위치
	std::atomic<bool> m_shutdown{ false };

	// 카운팅 세마포어: 카운트 == 큐 안의 아이템 수
	//   Push()  → release() 1회  (토큰 발급)
	//   Pop()   → acquire() 1회  (토큰 소비, 블로킹 대기)
	// MSVC STL 내부: release → WakeByAddressSingle
	//                acquire → WaitOnAddress  (Win32 HANDLE 불필요)
	std::counting_semaphore<> mSem{ 0 };

public:
	GlobalQueue_LockFree()
	{
		m_enqueuePos = 0;
		m_dequeuePos = 0;
	}
	~GlobalQueue_LockFree();
	Room* Pop();
	void Init(uint32_t bufferSize);
	void Push(Room* pRoom);
	void Shutdown()
	{
		// CAS: 이중 호출 방지 (TearDown 등에서 중복 호출 안전)
		bool expected = false;
		if (!m_shutdown.compare_exchange_strong(expected, true,
			std::memory_order_release, std::memory_order_relaxed))
			return;

		// 대기 중인 첫 번째 Pop() 스레드를 깨운다.
		// Pop() 내부에서 m_shutdown 확인 후 재 release → cascade로 전체 전파
		mSem.release();
	}
};

