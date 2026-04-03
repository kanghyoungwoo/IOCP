#pragma once
#include "ObjectPool.h"
#include "PacketJob.h"
#include "Room.h"
#include "StrandCallback.h"
#include<vector>
#include <thread>
#include <intrin.h>

//#define USE_LOCKFREE_GLOBAL_QUEUE

#ifdef USE_LOCKFREE_GLOBAL_QUEUE
    #include "GlobalQueue_LockFree.h"
    using GlobalQueue = GlobalQueue_LockFree;
#else
    #include "GlobalQueue_MutexCV.h"
    using GlobalQueue = GlobalQueue_MutexCV;
#endif

class UserManager;

class StrandProcessor
{
public:
    StrandProcessor() = default;
    ~StrandProcessor()
    {
        //Stop();
    }

    void Init(uint32_t jobPoolSize, uint32_t callbackPoolSize, uint32_t maxRoomCount);
    
    void SetUserManager(UserManager* pUserManager)
    {
        mUserManager = pUserManager; 
    };

    void Start(int threadCount);
    void Stop();
    // 2의 거듭제곱으로 올려주는 유틸리티 함수
    inline uint32_t GetNextPowerOf2(uint32_t v)
    {
        v--;
        v |= v >> 1;
        v |= v >> 2;
        v |= v >> 4;
        v |= v >> 8;
        v |= v >> 16;
        v++;
        return v;
    }

    void EnqueueJob(Room* pRoom, uint32_t clientIndex, uint32_t targetGeneration, UINT32 sessionGeneration, uint16_t packetId, uint16_t dataSize, const char* data);

    // 로직 스레드가 완료한 콜백 작업 꺼내기
    StrandCallback* PopCallback()
    {
        return mCallbackQueue.Pop();
    }

    // 처리가 끝난 콜백 메모리 다시 풀에 반납
    void FreeCallback(StrandCallback* pCallback)
    {
        // 재사용을 위해 내부 상태를 초기화하고 반납
        pCallback->clientIndex = 0;
        pCallback->roomNumber = -1;
        pCallback->result = 0;
        pCallback->mpscNext.store(nullptr, std::memory_order_relaxed);

        mCallbackPool.Free(pCallback);
    }

    // 로드 테스트 용도
    uint64_t GetAllocFailCount() const { return mAllocFailCount.load(); }
    uint32_t GetJobPoolSize() const { return mJobPool.GetPoolSize(); }
    uint32_t GetCurrentFreeCount() const { return mJobPool.GetFreeCount(); }
    uint64_t GetAllocTotalCount() const { return mAllocTotalCount.load(std::memory_order_relaxed); }

private:
    UserManager* mUserManager = nullptr;
    void WorkerThreadMain();

    void ProcessRoom(Room* pRoom);

    PacketJob* PopWithBackoff(Room* pRoom);

    void DrainRoom(Room* pRoom);

    ObjectPool<PacketJob>    mJobPool;       // Job 할당/반납
   // GlobalQueue_MutexCV      mGlobalQueue;   // 방 배분 큐
    std::vector<std::thread> mLogicThreads;  // 처리 스레드 풀
    GlobalQueue mGlobalQueue;

    MPSCQueue<StrandCallback> mCallbackQueue;   // Logic Thread 용 결과물
    ObjectPool<StrandCallback> mCallbackPool;   // 콜백용 메모리풀

    // 모니터링 용도
    std::atomic<uint64_t> mAllocFailCount{ 0 };
    std::atomic<uint64_t> mAllocTotalCount{ 0 };

};