#pragma once
#include "LockFreeStack.h"
#include <cstdint>
#include <cassert>
#include <vector>
// 고정 크기 객체 풀 (스레드 세이프)
// - 서버 시작 시 N개를 미리 할당하고, Alloc/Free로 재사용
// - 내부적으로 Free List(스택)를 사용하여 O(1) 할당/반납을 보장
// - IOCP 워커 스레드(SendComplete)와 로직 스레드(SendMsg)가
//   동시에 접근하므로 mutex로 보호
template<typename T>
class ObjectPool
{
public:
	ObjectPool() = default;
	~ObjectPool()
	{
		// 풀이 소유한 메모리 해제
		if (m_poolBlock != nullptr)
		{
			delete[] m_poolBlock;
			m_poolBlock = nullptr;
		}

	}

	// 복사/이동 금지
	ObjectPool(const ObjectPool&) = delete;
	ObjectPool& operator=(const ObjectPool&) = delete;

	// poolSize개의 객체를 미리 할당
	void Init(const uint32_t poolSize)
	{
		//mFreeList.reserve(poolSize);

		// 연속된 배열로 할당
		m_poolBlock = new T[poolSize];

		// 스택에 배열 시작주소 알려줌
		mFreeStack.Init(m_poolBlock);	// 시작 주소 전달
		for (uint32_t i = 0; i < poolSize; ++i)
		{
			mFreeStack.Push(&m_poolBlock[i]);
		}

		mPoolSize = poolSize;

		// 디버깅용
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
		if (obj == nullptr)
		{
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
	//std::vector<T*> mAllBlocks;		// 메모리 해제용 (소유권)
	T* m_poolBlock = nullptr;
	uint32_t mPoolSize = 0;
	LockFreeStack<T> mFreeStack;
	std::atomic<uint64_t> mAllocFailCount{ 0 };


	// 디버깅용
	std::atomic<uint32_t> mFreeCount{ 0 };
};
