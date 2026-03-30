#pragma once
#include "ObjectPool.h"
//#include "GlobalQueue_MutexCV.h"
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

class StrandProcessor
{
public:
    StrandProcessor() = default;
    ~StrandProcessor()
    {
        //Stop();
    }

    void Init(uint32_t jobPoolSize, uint32_t callbackPoolSize, uint32_t maxRoomCount)
    {
        mJobPool.Init(jobPoolSize);
        mCallbackPool.Init(callbackPoolSize);
#ifdef USE_LOCKFREE_GLOBAL_QUEUE
        // Lock-Free 방식은 2의 거듭제곱 크기의 바운디드 큐를 초기화합니다.
        uint32_t globalQueueSize = GetNextPowerOf2(maxRoomCount);
        mGlobalQueue.Init(globalQueueSize);
#endif
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
        // 큐를 먼저 닫고 자는 스레드들을 깨 깨운다
        mGlobalQueue.Shutdown();

        for (auto& t : mLogicThreads)
        {
            if (t.joinable())
            {
                t.join();
            }
        }
        mLogicThreads.clear();
        LOG_DEBUG("StrandProcessor: 모든 Logic Thread 종료하고 Stop 완료\n");
    }

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

    void EnqueueJob(Room* pRoom, uint32_t clientIndex, uint32_t targetGeneration, uint16_t packetId, uint16_t dataSize, const char* data)
    {
        mAllocTotalCount.fetch_add(1, std::memory_order_relaxed);
        PacketJob* pJob = mJobPool.Alloc();
        if (pJob == nullptr)
        {
            // 여기서 카운터 증가
            mAllocFailCount.fetch_add(1, std::memory_order_relaxed);
            LOG_ERROR_ONCE("Job Pool 소진. packet drop.\n");
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
        pCallback->mpscNext.store(nullptr, std::memory_order_relaxed);

        mCallbackPool.Free(pCallback);
    }

    // 로드 테스트 용도
    uint64_t GetAllocFailCount() const { return mAllocFailCount.load(); }
    uint32_t GetJobPoolSize() const { return mJobPool.GetPoolSize(); }
    uint32_t GetCurrentFreeCount() const { return mJobPool.GetFreeCount(); }
    uint64_t GetAllocTotalCount() const { return mAllocTotalCount.load(std::memory_order_relaxed); }

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
            // 일괄처리
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
                // preemption Hole 타임아웃 -> 방 강제종료
                pRoom->SetBroken();
                DrainRoom(pRoom);
                return; // 방 처리 종료
            }

            // generation 세대 검사
            if (pJob->targetGeneration != pRoom->GetGeneration())
            {
                // 세대불일치 -> skip
                LOG_DEBUG("세대 패킷 무시\n");
            }
            else
            {
                // 실제 처리
                // 비즈니스 로직
                User* pUser = pRoom->FindUserByClientIndex(pJob->clientIndex);

                if (pUser != nullptr)
                {
                    // 강제접속 종료 (DISCONNECT)
                    if (pJob->packetId == (uint16_t)PACKET_ID::SYS_USER_DISCONNECT)
                    {
                        // 1. 방 내부 정리 (유저 삭제, 퇴장 알림 브로드캐스트)
                        pRoom->LeaveUser(pUser);

                        // 임시 채팅 패킷 생성 (크기와 동일)
                        ROOM_CHAT_REQUEST_PACKET tempChatPacket;
                        tempChatPacket.PacketId = (UINT16)PACKET_ID::ROOM_CHAT_REQUEST;
                        tempChatPacket.PacketLength = sizeof(tempChatPacket);
                        memset(tempChatPacket.Message, 0, sizeof(tempChatPacket.Message));
                        strcpy_s(tempChatPacket.Message, sizeof(tempChatPacket.Message), "has left the room.");

                        pRoom->NotifyChat(pJob->clientIndex, pUser->GetUserID().c_str(), (char*)&tempChatPacket);



                        // 2. 글로벌 스레드에 콜백으로 전달
                        StrandCallback* cb = mCallbackPool.Alloc();
                        cb->type = StrandCallbackType::FREE_USER;
                        cb->clientIndex = pJob->clientIndex;
                        mCallbackQueue.Push(cb);
                    }
                    // 방 채팅
                    else if (pJob->packetId == (uint16_t)PACKET_ID::ROOM_CHAT_REQUEST)
                    {
                        constexpr uint16_t MIN_CHAT_PACKET_SIZE = sizeof(PACKET_HEADER);

                        if (pJob->dataSize <= MIN_CHAT_PACKET_SIZE)
                        {
                            // 메시지가 아예없는 빈 패킷 무시
                            mJobPool.Free(pJob);
                            continue;
                        }
                        // 나중에 printf와 strcpy를 쓸 때 메모리 오버플로우 방지.
                            ROOM_CHAT_REQUEST_PACKET * pChatReq = (ROOM_CHAT_REQUEST_PACKET*)pJob->body;

                        // 수신받은 바이트의 Null로 덮어쓰기 (안전)
                        uint16_t messageLen = pJob->dataSize - sizeof(PACKET_HEADER);
                        if (messageLen < 256) {
                            pChatReq->Message[messageLen] = '\0';
                        }
                        else {
                            pChatReq->Message[255] = '\0';
                        }

                        // 채팅 처리
                        // 요청한 클라이언트에 응답 결과 패킷 전송
                        ROOM_CHAT_RESPONSE_PACKET resPacket;
                        resPacket.PacketId = (UINT16)PACKET_ID::ROOM_CHAT_RESPONSE;
                        resPacket.PacketLength = sizeof(ROOM_CHAT_RESPONSE_PACKET);
                        resPacket.Result = 0; // ERROR_CODE::NONE
                        pRoom->SendPacketFunc(pJob->clientIndex, pUser->GetSessionGeneration(), sizeof(resPacket), (char*)&resPacket);

                        // 방 전체에 브로드캐스트
                        pRoom->NotifyChat(pJob->clientIndex, pUser->GetUserID().c_str(), pJob->body);
                    }
                    // 방에서의 퇴장
                    else if (pJob->packetId == (uint16_t)PACKET_ID::ROOM_LEAVE_REQUEST)
                    {
                        // 퇴장 처리

                        // 방에서 유저 삭제 알림
                        pRoom->LeaveUser(pUser);

                        ROOM_CHAT_REQUEST_PACKET tempChatPacket;
                        tempChatPacket.PacketId = (UINT16)PACKET_ID::ROOM_CHAT_REQUEST;
                        tempChatPacket.PacketLength = sizeof(tempChatPacket);
                        memset(tempChatPacket.Message, 0, sizeof(tempChatPacket.Message));
                        strcpy_s(tempChatPacket.Message, sizeof(tempChatPacket.Message), "has left the room.");

                        pRoom->NotifyChat(pJob->clientIndex, pUser->GetUserID().c_str(), (char*)&tempChatPacket);

                        // 클라이언트에 퇴장 응답 전송
                        ROOM_LEAVE_RESPONSE_PACKET resPacket;
                        resPacket.PacketId = (UINT16)PACKET_ID::ROOM_LEAVE_RESPONSE;
                        resPacket.PacketLength = sizeof(ROOM_LEAVE_RESPONSE_PACKET);
                        resPacket.Result = 0; // ERROR_CODE::NONE
                        pRoom->SendPacketFunc(pJob->clientIndex, pUser->GetSessionGeneration(), sizeof(resPacket), (char*)&resPacket);

                        // 패킷 매니저에게 상태 변경 요청 콜백
                        StrandCallback* cb = mCallbackPool.Alloc();
                        cb->type = StrandCallbackType::USER_LEFT_ROOM;
                        cb->clientIndex = pJob->clientIndex;
                        mCallbackQueue.Push(cb);

                    }
                }

            }
            mJobPool.Free(pJob);
        } while (pRoom->GetMsgCount().fetch_sub(1, std::memory_order_acq_rel) > 1);
    }

    PacketJob* PopWithBackoff(Room* pRoom)
    {
        PacketJob* pJob = pRoom->GetLocalQueue().Pop();
        if (pJob != nullptr)
            return pJob; // 대부분은 여기서 바로 성공

        // pop실패 -> preemption Hole 의심, 단계별 대기
        uint32_t spinCount = 0;
        const uint32_t PHASE1_LIMIT = 64;
        const uint32_t PHASE2_LIMIT = 1024;
        const uint32_t TIMEOUT_LIMIT = 100000;

        while ((pJob = pRoom->GetLocalQueue().Pop()) == nullptr)
        {
            if (spinCount < PHASE1_LIMIT)
            {
                _mm_pause();    //  phase1: cpu에게 대기 중임 알림
            }
            else if (spinCount < PHASE2_LIMIT)
            {
                Sleep(0);       // phase2 : 같은 우선순위 스레드에 양보
            }
            else if (spinCount >= TIMEOUT_LIMIT)
            {
                return nullptr; // phase3 : 타임아웃, 포기
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