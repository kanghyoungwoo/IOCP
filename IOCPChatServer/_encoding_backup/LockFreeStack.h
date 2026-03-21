#pragma once

#include <cstdint>
#include <atomic>

static constexpr uint32_t NULL_INDEX = UINT32_MAX;

template<typename T>
class LockFreeStack
{
	// 기존 (16바이트, Lock-Free가 안됨)
	//// 16바이트 정렬 강제하여 하드웨어 128비트 CAS지원 확실하게 하도록
	//struct alignas(16) TaggedPointer {
	//	// ABA 문제를 막기 위해 Node<T>* 포인터 하나와 uint32_t나 uintptr_t 같은 세대(Generation) 카운터를 하나로 묶은 녀석
	//	T* ptr;
	//	uintptr_t generation;	// 부호 없는 정수로 세대 관리
	//};
	struct TaggedIndex
	{
		uint32_t index;			// 4바이트, 풀인덱스 (UINT_MAX = 비어있음)
		uint32_t generation;	// 4바이트, ABA 방어 count
	};

	std::atomic<uint64_t> m_head; // 기존 TaggedPointer -> uint64_t로 pack/unpack
	T* m_pool = nullptr;	// 풀 배열 시작 주소 (인덱스 포인터 변환용)

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
	// 풀에 다 쓴 객체 반납
	LockFreeStack() : m_head(Pack(NULL_INDEX, 0)) {}

	void Init(T* poolBase) { m_pool = poolBase; }
	
	//void Push(T* obj)
	//{
	//	//현재의 m_head 값을 읽어와서 oldHead라는 지역 변수에 저장
	//	// 처음 읽어올땐 다른 스레드와의 메모리 동기화 필요X, 가벼운 relaxed 사용
	//	TaggedPointer oldHead = m_head.load(std::memory_order_relaxed);
	//	
	//	while (true)
	//	{
	//		obj->poolNext = oldHead.ptr;
	//		
	//		TaggedPointer newHead;
	//		newHead.ptr = obj;
	//		newHead.generation = oldHead.generation + 1;
	//		
	//		
	//		if (m_head.compare_exchange_weak(
	//			oldHead,
	//			newHead,
	//			std::memory_order_release,	// 성공시 내 데이터 남들에게 보여줌 
	//			std::memory_order_relaxed	// 실패시 주소만 필요하니 다시 읽기
	//		))
	//			break;
	//	}

	//}

	void Push(T* obj)
	{
		uint32_t objIndex = static_cast<uint32_t>(obj - m_pool); // 포인터 → 인덱스
		uint64_t oldHead = m_head.load(std::memory_order_relaxed);

		while (true)
		{
			TaggedIndex old = Unpack(oldHead);
			obj->poolNext = old.index;	// T*가 아닌 uint32_t 인덱스 저장

			uint64_t newHead = Pack(objIndex, old.generation + 1);

			if (m_head.compare_exchange_weak(oldHead, newHead,
				std::memory_order_release,
				std::memory_order_relaxed))
				break;
		}
	}

	// 풀에서 빈 객체를 꺼내옴, 없으면 nullptr
	//T* Pop()
	//{
	//	// 스택의 머리 가져옴
	//	TaggedPointer oldHead = m_head.load(std::memory_order_relaxed);
	//	while (true)
	//	{
	//		//  비어있는지 확인 
	//		if (oldHead.ptr == nullptr)
	//			return nullptr;
	//		//  준비 (newHead 만들기, 세대 증가)
	//		TaggedPointer newHead;
	//		newHead.ptr = oldHead.ptr->poolNext;
	//		newHead.generation = oldHead.generation + 1;
	//		//  CAS
	//		if (m_head.compare_exchange_weak(
	//			oldHead,
	//			newHead,
	//			std::memory_order_acquire,	//성공, oldHead.ptr의 데이터를 안전하게 읽기 위해 acquire
	//			std::memory_order_relaxed	//실패, 다음 루프를 위해 가볍게 상태만 갱신
	//		))
	//		{
	//			return oldHead.ptr;
	//		}
	//	}
	//}
	T* Pop()
	{
		uint64_t oldHead = m_head.load(std::memory_order_relaxed);

		while (true)
		{
			TaggedIndex old = Unpack(oldHead);

			if (old.index == NULL_INDEX)
				return nullptr;

			uint32_t nextIndex = m_pool[old.index].poolNext;	// 인덱스로 접근
			uint64_t newHead = Pack(nextIndex, old.generation + 1);

			if (m_head.compare_exchange_weak(
				oldHead, newHead,
				std::memory_order_acquire,
				std::memory_order_relaxed))
			{
				return &m_pool[old.index];	// 인덱스 -> 포인터 전환
			}
		}
	}

	// 실제 lock-free로 작동하는지 테스트 용도
	bool IsLockFree() const
	{
		return m_head.is_lock_free();
	}

};