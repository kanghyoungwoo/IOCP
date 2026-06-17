#pragma once

#include <cstdint>
#include <atomic>

static constexpr uint32_t NULL_INDEX = UINT32_MAX;

template<typename T>
class LockFreeStack
{
	// 구조체 (16바이트, Lock-Free용 인덱스)
	//// 16바이트 정렬 선언하여 하드웨어 128비트 CAS에서 확실하게 하도록

	struct TaggedIndex
	{
		uint32_t index;			// 4바이트, 풀 인덱스 (UINT_MAX = 비어있음)
		uint32_t generation;	// 4바이트, ABA 방지 count
	};

	std::atomic<uint64_t> m_head; // 기존 TaggedPointer -> uint64_t로 pack/unpack
	T* m_pool = nullptr;	// 풀 배열 시작 주소 (인덱스 ↔ 포인터 변환용)
	uint32_t m_capacity = 0;
	static uint64_t Pack(uint32_t index, uint32_t gen)
	{
		return static_cast<uint64_t>(index) | (static_cast<uint64_t>(gen) << 32);
	}

	static TaggedIndex Unpack(uint64_t val)
	{
		return { static_cast<uint32_t>(val), static_cast<uint32_t>(val >> 32) };
	}

public:
	//LockFreeStack() : m_head(TaggedPointer{ nullptr, 0 }) {}
	// 풀이 빈 채 객체 반납
	LockFreeStack() : m_head(Pack(NULL_INDEX, 0)) {}

	void Init(T* poolBase, uint32_t capacity = UINT32_MAX)
	{
		m_pool = poolBase;
		m_capacity = capacity;
	}


	void Push(T* obj)
	{
		uint32_t objIndex = static_cast<uint32_t>(obj - m_pool); // 포인터 → 인덱스
		uint64_t oldHead = m_head.load(std::memory_order_relaxed);

		while (true)
		{
			TaggedIndex old = Unpack(oldHead);
			obj->poolNext.store(old.index, std::memory_order_relaxed);

			uint64_t newHead = Pack(objIndex, old.generation + 1);

			if (m_head.compare_exchange_weak(oldHead, newHead,
				std::memory_order_release,
				std::memory_order_relaxed))
				break;
			_mm_pause();
		}
	}

	T* Pop()
	{
		uint64_t oldHead = m_head.load(std::memory_order_relaxed);

		while (true)
		{
			TaggedIndex old = Unpack(oldHead);

			if (old.index == NULL_INDEX)
				return nullptr;

			uint32_t nextIndex = m_pool[old.index].poolNext.load(std::memory_order_relaxed);
			uint64_t newHead = Pack(nextIndex, old.generation + 1);

			if (m_head.compare_exchange_weak(
				oldHead, newHead,
				std::memory_order_acquire,
				std::memory_order_relaxed))
			{
				return &m_pool[old.index];	// 인덱스 -> 포인터 변환
			}
			_mm_pause();
		}
	}

	// 실제 lock-free로 동작하는지 테스트 용도
	bool IsLockFree() const
	{
		return m_head.is_lock_free();
	}

	uint32_t PopBatch(uint32_t count, T** outArray)
	{
		uint64_t oldHead = m_head.load(std::memory_order_relaxed);
		while (true)
		{
			TaggedIndex old = Unpack(oldHead);
			if (old.index == NULL_INDEX)
				return 0;

			uint32_t curIdx = old.index;
			uint32_t actualCount = 0;
			for (uint32_t i = 0; i < count; ++i)
			{
				if (curIdx == NULL_INDEX) break;
				if (curIdx >= m_capacity) break;  // OOB 방어
				outArray[actualCount++] = &m_pool[curIdx];
				curIdx = m_pool[curIdx].poolNext.load(std::memory_order_relaxed);
			}
			if (actualCount == 0) return 0;

			uint64_t newHead = Pack(curIdx, old.generation + 1);
			if (m_head.compare_exchange_weak(oldHead, newHead,
				std::memory_order_acquire, std::memory_order_relaxed))
			{
				return actualCount;
			}
			_mm_pause();
		}
	}
};
