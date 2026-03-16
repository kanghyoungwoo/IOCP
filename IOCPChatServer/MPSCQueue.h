#pragma once
#include<atomic>

template<typename T>
class MPSCQueue
{
public:
	MPSCQueue()
	{
		m_stub.mpscNext.store(nullptr, std::memory_order_relaxed);
		m_head = &m_stub;
		m_tail.store(&m_stub, std::memory_order_relaxed);
	}

	void Push(T* node)
	{
		node->mpscNext.store(nullptr, std::memory_order_relaxed);
		T* prev = m_tail.exchange(node, std::memory_order_acq_rel);
		prev->mpscNext.store(node, std::memory_order_release);	// Hole지점 
	}

	T* Pop()
	{
		T* head = m_head;
		T* next = head->mpscNext.load(std::memory_order_acquire);

		// 상황1 head가 stub이고 next가 있음 -> stub 건너뛰기
		if (head == &m_stub)
		{
			if (next == nullptr)
				return nullptr;
			m_head = next;
			head = next;
			next = head->mpscNext.load(std::memory_order_acquire);
		}
		// 상황2 head가 데이터 노드이고 next가 있음 -> 정상 pop
		if (next != nullptr)
		{
			m_head = next;
			return head;
		}
		// 상황3 head가 마지막 노드이고 next가 nullptr -> hole이거나 진짜 비어있음
		// stub을 tail에 재삽입 해야함
		T* tail = m_tail.load(std::memory_order_acquire);
		if (head != tail)
		{
			return nullptr;
		}
		Push(&m_stub);
		next = head->mpscNext.load(std::memory_order_acquire);
		if (next != nullptr)
		{
			m_head = next;
			return head;
		}
		return nullptr;
	}

private:
	T m_stub;					// 파수꾼 노드, 빈 큐 상태 관리를 위한 더미 노드
	
	// alignas(64) 을 통해 캐시 무효화 방지
	alignas(64) std::atomic<T*> m_tail;		// Producer들이 exchange
	alignas(64) T* m_head;					// Consumer만 접근(atomic 불필요)
};