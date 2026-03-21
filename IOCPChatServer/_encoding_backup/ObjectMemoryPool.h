#pragma once

class ObjectMemoryPool
{
private:
	// FreeList Node 역할을 할 구조체
	// 메모리 사용중 아닐시 다음 빈 블록 가리키는 포인터로 쓰임
	struct FreeNode {
		FreeNode* next;
	};

	FreeNode* freeListHead;	// 빈 메모리 블록 리스트의 머리
	char* memoryBlock;		// 전체 메모리 블록의 시작 주소
	size_t chunkSize;		// 각 블록의 크기
	size_t poolSize;		// 블록의 총 개수
	// 메모리, 문자열 구조에는 size_t를 사용

public:
	ObjectMemoryPool(size_t chunkSize)
		: poolSize(poolSize) {

		// 유저 요청 크기대로 넣고
		this->chunkSize = chunkSize;

		// 그 크기가 포인터 하나 담을 공간보다 작다면
		if (this->chunkSize < sizeof(FreeNode*))
		{
			// 포인터 크기만큼 강제로 늘려줌
			this->chunkSize = sizeof(FreeNode*);
		}

		// 큰 메모리 덩어리 한 번에 할당
		memoryBlock = new char[this->chunkSize * poolSize];
		freeListHead = reinterpret_cast<FreeNode*>(memoryBlock);


		// 초기 메모리 블록들을 쪼개서 FreeList로 연결
		FreeNode* current = freeListHead;
		for (size_t i = 1; i < poolSize;++i)
		{
			FreeNode* nextnode = reinterpret_cast<FreeNode*>(memoryBlock + (i * this->chunkSize));
			current->next = nextnode;
			current = nextnode;
		}
		current->next = nullptr;
	}

	~ObjectMemoryPool() { delete[] memoryBlock; }

	// 메모리 할당
	void* allocate()
	{
		// 풀이 꽉 차면
		if (freeListHead == nullptr)
		{
			//std::cout<<"MemoryPool is out of memory";
			return nullptr;
		}
		// 맨 앞 빈 블록
		FreeNode* chunk = freeListHead;
		freeListHead = freeListHead->next;

		return chunk;
	}

	//메모리 해제
	void deallocate(void* ptr)
	{
		if (ptr == nullptr)
			return;
		// 반납된 메모리를 FreeList의 맨 앞에 다시 끼워넣음
		FreeNode* chunk = static_cast<FreeNode*>(ptr);
		freeListHead = chunk;
	}
};