#pragma once
#include<atomic>
#include<thread>

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
		prev->mpscNext.store(node, std::memory_order_release);	// Hole 구간
	}

	T* Pop()
	{
#ifdef _DEBUG
		// 단일 Consumer 불변식 검증: 동일 스레드만 Pop() 호출해야 함
		thread_local std::thread::id tl_consumerThread;
		auto currentThread = std::this_thread::get_id();
		if (tl_consumerThread == std::thread::id{})
			tl_consumerThread = currentThread;
		assert(tl_consumerThread == currentThread && "MPSC 위반: Pop()은 단일 Consumer에서만 호출 가능");
#endif
		T* head = m_head;
		T* next = head->mpscNext.load(std::memory_order_acquire);

		// 상황1: head가 stub이고 next가 있음 -> stub 건너뛰기
		if (head == &m_stub)
		{
			if (next == nullptr)
				return nullptr;
			m_head = next;
			head = next;
			next = head->mpscNext.load(std::memory_order_acquire);
		}
		// 상황2: head가 실제로 노드이고 next가 있음 -> 정상 pop
		if (next != nullptr)
		{
			m_head = next;
			return head;
		}
		// 상황3: head가 실제로 노드이고 next가 nullptr -> hole이거나 진짜 마지막임
		// stub을 tail에 다시 넣어야 함
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
	T m_stub;					//  빈 큐 상태를 명확히 구분하기 위한 센티넬

	// alignas(64) 로 거짓 캐시 무효화 방지
	alignas(64) std::atomic<T*> m_tail;		// Producer들이 exchange

	// m_head는 의도적으로 non-atomic:
	// MPSC 설계상 Consumer는 반드시 1개여야 함
	// 이 큐는 StrandProcessor::ProcessRoom()에서만 Pop() 호출 (Strand = 방 별 전용 스레드)
	// 다중 Consumer에서 Pop()을 호출하면 데이터 레이스 발생
	alignas(64) T* m_head;					// Consumer만 접근(atomic 불필요)
};
