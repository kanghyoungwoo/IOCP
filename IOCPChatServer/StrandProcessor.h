#pragma once
#include "ObjectPool.h"
#include "GlobalQueue_MutexCV.h"
#include "PacketJob.h"
#include "Room.h"
#include "StrandCallback.h"
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

    void Init(uint32_t jobPoolSize, uint32_t callbackPoolSize)
    {
        mJobPool.Init(jobPoolSize);
        mCallbackPool.Init(callbackPoolSize);
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

    // 로직 스레드가 완료한 콜백 작업 꺼내옴
    StrandCallback* PopCallback()
    {
        return mCallbackQueue.Pop();
    }

    // 처리가 끝낸 콜백 메모리 다시 풀에 반납
    void FreeCallback(StrandCallback* pCallback)
    {
        // 다음 사용을 위해 상태를 초기화하고 반납
        pCallback->clientIndex = 0;
        pCallback->mpscNext.store(nullptr, std::memory_order_relaxed);

        mCallbackPool.Free(pCallback);
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
                User* pUser = pRoom->FindUserByClientIndex(pJob->clientIndex);

                if (pUser != nullptr)
                {
                    // 비정상 종료 (DISCONNECT)
                    if (pJob->packetId == (uint16_t)PACKET_ID::SYS_USER_DISCONNECT)
                    {
                        // 1. 방 내부 정리 (유저 제거, 퇴장 알림 브로드캐스트)
                        pRoom->LeaveUser(pUser);

                        // 임시 채팅 패킷 생성 (크래시 방지)
                        ROOM_CHAT_REQUEST_PACKET tempChatPacket;
                        tempChatPacket.PacketId = (UINT16)PACKET_ID::ROOM_CHAT_REQUEST;
                        tempChatPacket.PacketLength = sizeof(tempChatPacket);
                        memset(tempChatPacket.Message, 0, sizeof(tempChatPacket.Message));
                        strcpy_s(tempChatPacket.Message, sizeof(tempChatPacket.Message), "has left the room.");
                        
                        pRoom->NotifyChat(pJob->clientIndex, pUser->GetUserID().c_str(), (char*)&tempChatPacket);



                        // 2. 글로벌 정리는 콜백으로 위임
                        StrandCallback* cb = mCallbackPool.Alloc();
                        cb->type = StrandCallbackType::FREE_USER;
                        cb->clientIndex = pJob->clientIndex;
                        mCallbackQueue.Push(cb);
                    }
                    // 정상 채팅 
                    else if (pJob->packetId == (uint16_t)PACKET_ID::ROOM_CHAT_REQUEST)
                    {
                        if (pJob->dataSize < sizeof(ROOM_CHAT_REQUEST_PACKET))
                        {
                            // 패킷 크기가 구조체보다 작으면 비정상 패킷이므로 드랍(무시)
                            mJobPool.Free(pJob);
                            continue;
                        }
                        // 채팅 처리
                        // 요청한 유저에게 성공 응답 패킷 전송
                        ROOM_CHAT_RESPONSE_PACKET resPacket;
                        resPacket.PacketId = (UINT16)PACKET_ID::ROOM_CHAT_RESPONSE;
                        resPacket.PacketLength = sizeof(ROOM_CHAT_RESPONSE_PACKET);
                        resPacket.Result = 0; // ERROR_CODE::NONE
                        pRoom->SendPacketFunc(pJob->clientIndex, sizeof(resPacket), (char*)&resPacket);

                        // 방 전체에 브로드캐스트
                        pRoom->NotifyChat(pJob->clientIndex, pUser->GetUserID().c_str(), pJob->body);
                    }
                    // 정상적인 퇴장
                    else if (pJob->packetId == (uint16_t)PACKET_ID::ROOM_LEAVE_REQUEST)
                    {
                        // 퇴장 처리

                        // 방에서 유저 제거 알림
                        pRoom->LeaveUser(pUser);

                        ROOM_CHAT_REQUEST_PACKET tempChatPacket;
                        tempChatPacket.PacketId = (UINT16)PACKET_ID::ROOM_CHAT_REQUEST;
                        tempChatPacket.PacketLength = sizeof(tempChatPacket);
                        memset(tempChatPacket.Message, 0, sizeof(tempChatPacket.Message));
                        strcpy_s(tempChatPacket.Message, sizeof(tempChatPacket.Message), "has left the room.");

                        pRoom->NotifyChat(pJob->clientIndex, pUser->GetUserID().c_str(), (char*)&tempChatPacket);

                        // 클라이언트에 퇴장 성공 응답
                        ROOM_LEAVE_RESPONSE_PACKET resPacket;
                        resPacket.PacketId = (UINT16)PACKET_ID::ROOM_LEAVE_RESPONSE;
                        resPacket.PacketLength = sizeof(ROOM_LEAVE_RESPONSE_PACKET);
                        resPacket.Result = 0; // ERROR_CODE::NONE
                        pRoom->SendPacketFunc(pJob->clientIndex, sizeof(resPacket), (char*)&resPacket);
                    
                        // 메인 라우터에게 유저 상태 변경 요청 콜백
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

    MPSCQueue<StrandCallback> mCallbackQueue;   // Logic Thread → 라우터
    ObjectPool<StrandCallback> mCallbackPool;   // 락프리 메모리풀

};