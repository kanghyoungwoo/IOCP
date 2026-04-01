#pragma once

class ObjectMemoryPool
{
private:
	// FreeList Node 역할을 할 구조체
	// 메모리 사용중이 아닐시 다음 빈 블록을 가리키는 포인터로 활용
	struct FreeNode {
		FreeNode* next;
	};

	FreeNode* freeListHead;	// 빈 메모리 블록 리스트의 머리
	char* memoryBlock;		// 전체 메모리 블록의 시작 주소
	size_t chunkSize;		// 각 블록의 크기
	size_t poolSize;		// 블록의 총 개수
	// 메모리, 문자열 사이즈에는 size_t를 사용

public:
	ObjectMemoryPool(size_t chunkSize, size_t poolSize)
		: chunkSize(chunkSize), poolSize(poolSize) {

		//// 유저 요청 크기를 넣고 // 생성자 인자로 변경
		//this->chunkSize = chunkSize;

		// 각 크기가 포인터 하나도 못 담을만큼 작다면
		if (this->chunkSize < sizeof(FreeNode*))
		{
			// 포인터 크기만큼 최소한 올려줌
			this->chunkSize = sizeof(FreeNode*);
		}

		// 큰 메모리 블록 한 번에 할당
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
		// 풀이 빈 경우
		if (freeListHead == nullptr)
		{
			//std::cout<<"MemoryPool is out of memory";
			return nullptr;
		}
		// 한 칸 꺼내기
		FreeNode* chunk = freeListHead;
		freeListHead = freeListHead->next;

		return chunk;
	}

	// 메모리 반환
	void deallocate(void* ptr)
	{
		if (ptr == nullptr)
			return;
		// 반납된 메모리를 FreeList의 맨 앞에 다시 끼워넣기
		FreeNode* chunk = static_cast<FreeNode*>(ptr);
		
		// 현재 Head를 새 노드의 next에 연결하여 리스트 유지
		chunk->next = freeListHead;

		// Head를 새로 반환된 노드로 업데이트
		freeListHead = chunk;
	}
};
