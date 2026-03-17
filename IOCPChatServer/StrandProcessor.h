#pragma once
#include "ObjectPool.h"
#include "GlobalQueue_MutexCV.h"
#include "PacketJob.h"
#include "Room.h"
#include<vector>
#include <thread>
#include <intrin.h>

class StrandProcessor
{
public:
    StrandProcessor() = default;
    ~StrandProcessor()
    {
        //Stop();
    }

    void Init(uint32_t jobPoolSize)
    {
        mJobPool.Init(jobPoolSize);
    }

    void Start(int threadCount)
    {
        for (int i = 0;i < threadCount;i++)
        {
            mLogicThreads.emplace_back([this]() {WorkerThreadMain();});
        }
    }

    void Stop()
    {
        // 큐의 문 닫고 자는 스레드들 다 깨우기
        mGlobalQueue.Shutdown();

        for (auto& t : mLogicThreads)
        {
            if (t.joinable())
            {
                t.join();
            }
        }
        mLogicThreads.clear();
        printf("StrandProcessor: 모든 Logic Thread 안전하게 Stop 완료\n");
    }

    void EnqueueJob(Room* pRoom, uint32_t clientIndex, uint32_t targetGeneration, uint16_t packetId, uint16_t dataSize, const char* data)
    {
        PacketJob* pJob = mJobPool.Alloc();
        if (pJob == nullptr)
        {
            printf("Job Pool 비어있음. packet drop.\n"); // pool 고갈
            return;
        }
        pJob->clientIndex = clientIndex;
        pJob->roomIndex = pRoom->GetRoomNumber();
        pJob->targetGeneration = targetGeneration;
        pJob->packetId = packetId;
        pJob->dataSize = dataSize;

        if (dataSize > 0 && data != nullptr)
        {
            memcpy(pJob->body, data, dataSize);
        }
        auto result = pRoom->EnqueueJob(pJob);
        switch (result)
        {
        case Room::EnqueueResult::SUCCESS_FIRST:
            mGlobalQueue.Push(pRoom);
            break;
        case Room::EnqueueResult::SUCCESS_APPENDED:
            break;
        case Room::EnqueueResult::FAILED_DROPPED:
            mJobPool.Free(pJob);
            break;
        default:
            assert(false && "unknown EnqueueResult");
            mJobPool.Free(pJob);
            break;
        }
    }

private:
    void WorkerThreadMain()
    {
        while (true)
        {
            Room* pRoom = mGlobalQueue.Pop();
            if (pRoom == nullptr)
                break;  // shutdown

            if (pRoom->IsBroken())
            {
                DrainRoom(pRoom);
                continue;
            }
            // 정상처리
            ProcessRoom(pRoom);

        }
    }

    void ProcessRoom(Room* pRoom)
    {
        do {
            // adaptive backoff로 Pop
            PacketJob* pJob = PopWithBackoff(pRoom);

            if (pJob == nullptr)
            {
                // preemption Hole 타임아웃 -> 고장선언
                pRoom->SetBroken();
                DrainRoom(pRoom);
                return; // 방 처리 종료
            }

            // generation 출구 검증
            if (pJob->targetGeneration != pRoom->GetGeneration())
            {
                // 세대불일치 -> skip
                printf("유령 패킷 무시\n");
            }
            else
            {
                // 실제 처리
                // 비즈니스 로직
            }
            mJobPool.Free(pJob);
        } while (pRoom->GetMsgCount().fetch_sub(1, std::memory_order_acq_rel) > 1);
    }

    PacketJob* PopWithBackoff(Room* pRoom)
    {
        PacketJob* pJob = pRoom->GetLocalQueue().Pop();
        if (pJob != nullptr)
            return pJob; // 대부분은 여기서 바로 성공

        // pop실패 -> preemption Hole 구간, 단계별 대기
        uint32_t spinCount = 0;
        const uint32_t PHASE1_LIMIT = 64;
        const uint32_t PHASE2_LIMIT = 1024;
        const uint32_t TIMEOUT_LIMIT = 100000;

        while ((pJob = pRoom->GetLocalQueue().Pop()) == nullptr)
        {
            if (spinCount < PHASE1_LIMIT)
            {
                _mm_pause();    //  phase1: cpu에 잠시 쉼을 알림
            }
            else if (spinCount < PHASE2_LIMIT)
            {
                Sleep(0);       // phase2 : 같은 우선순위 쓰레드에 양보
            }
            else if (spinCount >= TIMEOUT_LIMIT)
            {
                return nullptr; // phase3 : 타임아웃, 고장
            }
            ++spinCount;
        }
        return pJob;
    }

    void DrainRoom(Room* pRoom)
    {
        do {
            PacketJob* pJob = pRoom->GetLocalQueue().Pop();
            if (pJob != nullptr)
                mJobPool.Free(pJob);
            // nullptr이어도 msgcount는 감소시켜야함
        } while (pRoom->GetMsgCount().fetch_sub(1, std::memory_order_acq_rel) > 1);

    }

    ObjectPool<PacketJob>    mJobPool;       // Job 할당/해제
    GlobalQueue_MutexCV      mGlobalQueue;   // 방 분배 큐
    std::vector<std::thread> mLogicThreads;  // 처리 스레드 풀
};