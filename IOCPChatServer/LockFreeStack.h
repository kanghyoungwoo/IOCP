#pragma once

#include <cstdint>
#include <atomic>

template<typename T>
class LockFreeStack
{
	struct Node {
		// Node<T> 구조체: 실제 데이터(T value)와 다음 노드를 가리킬 포인터가 필요
		T data;
		Node<T>* pNext;
	};

	// 16바이트 정렬 강제하여 하드웨어 128비트 CAS지원 확실하게 하도록
	struct alignas(16) TaggedPointer {
		// ABA 문제를 막기 위해 Node<T>* 포인터 하나와 uint32_t나 uintptr_t 같은 세대(Generation) 카운터를 하나로 묶은 녀석
		Node* ptr;
		uintptr_t generation;	// 부호 없는 정수로 세대 관리
	};

	void Push(const T& value)
	{
		//새로 넣을 데이터를 담은 Node를 동적 할당
		Node* node = new Node;
		node->data = value;
		//현재의 m_head 값을 읽어와서 oldHead라는 지역 변수에 저장
		// 처음 읽어올땐 다른 스레드와의 메모리 동기화 필요X, 가벼운 relaxed 사용
		TaggedPointer oldHead = m_head.load(std::memory_order_relaxed);
		
		while (true)
		{
			node->pNext = oldHead.ptr;
			
			TaggedPointer newHead;
			newHead.ptr = node;
			newHead.generation = oldHead.generation + 1;
			
			
			if (m_head.compare_exchange_weak(
				oldHead,
				newHead,
				std::memory_order_release,	// 성공시 내 데이터 남들에게 보여줌 
				std::memory_order_relaxed	// 실패시 주소만 필요하니 다시 읽기
			))
				break;
		}

	}

	bool Pop(T& outValue)
	{
		// 스택의 머리 가져옴
		TaggedPointer oldHead = m_head.load();
		while (true)
		{
			//  비어있는지 확인 
			if (oldHead.ptr == nullptr)
				return false;
			//  준비 (newHead 만들기, 세대 증가)
			TaggedPointer newHead;
			newHead.ptr = oldHead.ptr->pNext;
			newHead.generation = oldHead.generation + 1;
			//  CAS
			if (m_head.compare_exchange_weak(
				oldHead,
				newHead,
				std::memory_order_acquire,	//성공, oldHead.ptr의 데이터를 안전하게 읽기 위해 acquire
				std::memory_order_relaxed	//실패, 다음 루프를 위해 가볍게 상태만 갱신
			))
			{
				outValue = oldHead.ptr->data;
				return true;
			}
		}
	}

private:
	// 제어할 Head Node
	std::atomic<TaggedPointer> m_head;
};