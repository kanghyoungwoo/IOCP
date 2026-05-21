#pragma once
#include "LockFreeStack.h"
#include <cstdint>
#include <cassert>
#include <vector>
// 고정 크기 객체 풀 (락프리 스택 기반)
// - 서버 시작 시 N개를 미리 할당하고, Alloc/Free로 운용
// - 내부적으로 Free List(스택)를 사용하여 O(1) 할당/반납이 가능
// - IOCP 워커 스레드(SendComplete)와 메인 스레드(SendMsg)가
//   동시에 접근하므로 락프리로 보호
template<typename T>
class ObjectPool
{
public:
	ObjectPool() = default;
	~ObjectPool()
	{
		// 소멸자가 할 일이 있는 타입인 경우만
		if constexpr (!std::is_trivially_destructible_v<T>)
		{
			for (uint32_t i = 0;i < mPoolSize;++i)
			{
				m_poolBlock[i].~T();
			}
		}
		// 풀에 할당된 메모리 해제
		std::free(m_poolBlock);
		m_poolBlock = nullptr;
		

	}

	// 복사/이동 금지
	ObjectPool(const ObjectPool&) = delete;
	ObjectPool& operator=(const ObjectPool&) = delete;

	// poolSize만큼 객체를 미리 할당
	void Init(const uint32_t poolSize)
	{
		mPoolSize = poolSize;
		// 디버깅용 로그 추가
		uint64_t totalBytes = (uint64_t)sizeof(T) * poolSize;
		//printf("Pointer Size: %zu (8이면 64bit, 4면 32bit)\\n", sizeof(void*));
		//printf("[ObjectPool Init] Type Size: %zu bytes, Count: %u, Requesting: %llu MB\n",sizeof(T), poolSize, totalBytes / (1024 * 1024));
		//mFreeList.reserve(poolSize);

		// 연속된 배열로 할당
		//m_poolBlock = new T[poolSize];
		m_poolBlock = static_cast<T*>(std::malloc(sizeof(T) * poolSize));

		if (m_poolBlock == nullptr)
		{
			//LOG_DEBUG("FATAL : Objectpool malloc failed !\n");
			return;
		}

		// 스택에 배열 시작주소 알려줌
		mFreeStack.Init(m_poolBlock);	// 베이스 주소 설정
		for (uint32_t i = 0; i < poolSize; ++i)
		{
			//mFreeStack.Push(&m_poolBlock[i]);
			new (&m_poolBlock[i]) T();
			mFreeStack.Push(&m_poolBlock[i]);
		}
		mFreeCount.store(poolSize, std::memory_order_relaxed);
	}

	// 풀에서 객체 하나를 꺼낸다.
	// 풀이 비었으면 nullptr 반환 (호출 측에서 처리)
	T* Alloc()
	{
		T* p = mFreeStack.Pop();
		if (p) mFreeCount.fetch_sub(1, std::memory_order_relaxed);
		return p;

		//return mFreeStack.Pop(); // nullptr이면 풀 소진
	}

	// 사용이 끝난 객체를 풀에 반납한다.
	void Free(T* obj)
	{
		// nullptr 체크를 가장 먼저
		if (obj == nullptr)
		{
			return;
		}
		// 풀 범위 밖 포인터 차단
		if (obj < m_poolBlock || obj >= m_poolBlock + mPoolSize)
		{
			assert(false && "ObjectPool::Free - pointer out of pool range");
			return;
		}

		// 정렬 오류 차단 (풀 내부이지만 객체 경계가 아닌 위치)
		if ((obj - m_poolBlock) * sizeof(T) % sizeof(T) != 0)
		{
			assert(false && "ObjectPool::Free - misaligned pointer");
			return;
		}

		mFreeStack.Push(obj);
		mFreeCount.fetch_add(1, std::memory_order_relaxed);
	}

	// 테스트 용도
	 bool IsLockFree() const
	{
		 return mFreeStack.IsLockFree();
	 }

	uint32_t GetPoolSize() const { return mPoolSize; }
	//uint32_t GetFreeCount() const { return mFreeStack.GetCount(); }
	uint32_t GetFreeCount() const { return mFreeCount.load(std::memory_order_relaxed); }
	void IncrementAllocFail() { mAllocFailCount.fetch_add(1, std::memory_order_relaxed); }
	uint64_t GetAllocFailCount() const { return mAllocFailCount.load(std::memory_order_relaxed); }


private:
	//std::vector<T*> mAllBlocks;		// 메모리 관리용 (소멸자)
	T* m_poolBlock = nullptr;
	uint32_t mPoolSize = 0;
	LockFreeStack<T> mFreeStack;
	std::atomic<uint64_t> mAllocFailCount{ 0 };


	// 디버깅
	std::atomic<uint32_t> mFreeCount{ 0 };
};
