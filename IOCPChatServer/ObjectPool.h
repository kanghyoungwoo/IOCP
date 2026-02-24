#pragma once

#include <cstdint>
#include <vector>
#include <mutex>
#include <cassert>

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
		for (auto* block : mAllBlocks)
		{
			delete block;
		}
		mAllBlocks.clear();
		mFreeList.clear();
	}

	// 복사/이동 금지
	ObjectPool(const ObjectPool&) = delete;
	ObjectPool& operator=(const ObjectPool&) = delete;

	// poolSize개의 객체를 미리 할당
	void Init(const uint32_t poolSize)
	{
		mAllBlocks.reserve(poolSize);
		mFreeList.reserve(poolSize);

		for (uint32_t i = 0; i < poolSize; ++i)
		{
			T* obj = new T();
			mAllBlocks.push_back(obj);
			mFreeList.push_back(obj);
		}

		mPoolSize = poolSize;
	}

	// 풀에서 객체 하나를 꺼낸다.
	// 풀이 비었으면 nullptr 반환 (호출 측에서 처리)
	T* Alloc()
	{
		std::lock_guard<std::mutex> guard(mLock);
		if (mFreeList.empty())
		{
			return nullptr;
		}
		T* obj = mFreeList.back();
		mFreeList.pop_back();
		return obj;
	}

	// 사용이 끝난 객체를 풀에 반납한다.
	void Free(T* obj)
	{
		if (obj == nullptr)
		{
			return;
		}
		std::lock_guard<std::mutex> guard(mLock);
		mFreeList.push_back(obj);
	}

	// 현재 사용 가능한 객체 수
	uint32_t GetAvailableCount()
	{
		std::lock_guard<std::mutex> guard(mLock);
		return static_cast<uint32_t>(mFreeList.size());
	}

	uint32_t GetPoolSize() const { return mPoolSize; }

private:
	std::vector<T*> mAllBlocks;		// 메모리 해제용 (소유권)
	std::vector<T*> mFreeList;		// 사용 가능한 객체 스택
	std::mutex mLock;
	uint32_t mPoolSize = 0;
};
