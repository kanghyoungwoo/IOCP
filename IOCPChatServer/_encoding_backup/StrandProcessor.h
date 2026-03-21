#pragma once
#include "ObjectPool.h"
//#include "GlobalQueue_MutexCV.h"
#include "PacketJob.h"
#include "Room.h"
#include "StrandCallback.h"
#include<vector>
#include <thread>
#include <intrin.h>

#define USE_LOCKFREE_GLOBAL_QUEUE

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
        // Lock-Free ������ ���� 2�� �������� ����� ���缭 ť�� �ʱ�ȭ�մϴ�.
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
        // ť�� �� �ݰ� �ڴ� ������� �� �����
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

    // 2�� �������� �ø����ִ� ����� �Լ�
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
            // ���⼭ ī���� ����
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

    // ���� �����尡 �Ϸ��� �ݹ� �۾� ������
    StrandCallback* PopCallback()
    {
        return mCallbackQueue.Pop();
    }

    // ó���� ���� �ݹ� �޸� �ٽ� Ǯ�� �ݳ�
    void FreeCallback(StrandCallback* pCallback)
    {
        // ���� ����� ���� ���¸� �ʱ�ȭ�ϰ� �ݳ�
        pCallback->clientIndex = 0;
        pCallback->mpscNext.store(nullptr, std::memory_order_relaxed);

        mCallbackPool.Free(pCallback);
    }

    // �ε� �׽�Ʈ �뵵
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
            // ����ó��
            ProcessRoom(pRoom);

        }
    }

    void ProcessRoom(Room* pRoom)
    {
        do {
            // adaptive backoff�� Pop
            PacketJob* pJob = PopWithBackoff(pRoom);

            if (pJob == nullptr)
            {
                // preemption Hole Ÿ�Ӿƿ� -> ���弱��
                pRoom->SetBroken();
                DrainRoom(pRoom);
                return; // �� ó�� ����
            }

            // generation �ⱸ ����
            if (pJob->targetGeneration != pRoom->GetGeneration())
            {
                // �������ġ -> skip
                LOG_DEBUG("세대 패킷 무시\n");
            }
            else
            {
                // ���� ó��
                // ����Ͻ� ����
                User* pUser = pRoom->FindUserByClientIndex(pJob->clientIndex);

                if (pUser != nullptr)
                {
                    // ������ ���� (DISCONNECT)
                    if (pJob->packetId == (uint16_t)PACKET_ID::SYS_USER_DISCONNECT)
                    {
                        // 1. �� ���� ���� (���� ����, ���� �˸� ��ε�ĳ��Ʈ)
                        pRoom->LeaveUser(pUser);

                        // �ӽ� ä�� ��Ŷ ���� (ũ���� ����)
                        ROOM_CHAT_REQUEST_PACKET tempChatPacket;
                        tempChatPacket.PacketId = (UINT16)PACKET_ID::ROOM_CHAT_REQUEST;
                        tempChatPacket.PacketLength = sizeof(tempChatPacket);
                        memset(tempChatPacket.Message, 0, sizeof(tempChatPacket.Message));
                        strcpy_s(tempChatPacket.Message, sizeof(tempChatPacket.Message), "has left the room.");
                        
                        pRoom->NotifyChat(pJob->clientIndex, pUser->GetUserID().c_str(), (char*)&tempChatPacket);



                        // 2. �۷ι� ������ �ݹ����� ����
                        StrandCallback* cb = mCallbackPool.Alloc();
                        cb->type = StrandCallbackType::FREE_USER;
                        cb->clientIndex = pJob->clientIndex;
                        mCallbackQueue.Push(cb);
                    }
                    // ���� ä�� 
                    else if (pJob->packetId == (uint16_t)PACKET_ID::ROOM_CHAT_REQUEST)
                    {
                        constexpr uint16_t MIN_CHAT_PACKET_SIZE = sizeof(PACKET_HEADER);

                        if (pJob->dataSize <= MIN_CHAT_PACKET_SIZE)
                        {
                            // �޼����� �ƿ����� ������ ��Ŷ ���
                            mJobPool.Free(pJob);
                            continue;
                        }
                        // ���߿� printf�� strcpy�� �� �� �޸� �����÷ο� ����.
                            ROOM_CHAT_REQUEST_PACKET * pChatReq = (ROOM_CHAT_REQUEST_PACKET*)pJob->body;

                        // ������ ������ ����Ʈ�� Null�� ���ƹ��� (����)
                        uint16_t messageLen = pJob->dataSize - sizeof(PACKET_HEADER);
                        if (messageLen < 256) {
                            pChatReq->Message[messageLen] = '\0';
                        }
                        else {
                            pChatReq->Message[255] = '\0';
                        }

                        // ä�� ó��
                        // ��û�� �������� ���� ���� ��Ŷ ����
                        ROOM_CHAT_RESPONSE_PACKET resPacket;
                        resPacket.PacketId = (UINT16)PACKET_ID::ROOM_CHAT_RESPONSE;
                        resPacket.PacketLength = sizeof(ROOM_CHAT_RESPONSE_PACKET);
                        resPacket.Result = 0; // ERROR_CODE::NONE
                        pRoom->SendPacketFunc(pJob->clientIndex, sizeof(resPacket), (char*)&resPacket);

                        // �� ��ü�� ��ε�ĳ��Ʈ
                        pRoom->NotifyChat(pJob->clientIndex, pUser->GetUserID().c_str(), pJob->body);
                    }
                    // �������� ����
                    else if (pJob->packetId == (uint16_t)PACKET_ID::ROOM_LEAVE_REQUEST)
                    {
                        // ���� ó��

                        // �濡�� ���� ���� �˸�
                        pRoom->LeaveUser(pUser);

                        ROOM_CHAT_REQUEST_PACKET tempChatPacket;
                        tempChatPacket.PacketId = (UINT16)PACKET_ID::ROOM_CHAT_REQUEST;
                        tempChatPacket.PacketLength = sizeof(tempChatPacket);
                        memset(tempChatPacket.Message, 0, sizeof(tempChatPacket.Message));
                        strcpy_s(tempChatPacket.Message, sizeof(tempChatPacket.Message), "has left the room.");

                        pRoom->NotifyChat(pJob->clientIndex, pUser->GetUserID().c_str(), (char*)&tempChatPacket);

                        // Ŭ���̾�Ʈ�� ���� ���� ����
                        ROOM_LEAVE_RESPONSE_PACKET resPacket;
                        resPacket.PacketId = (UINT16)PACKET_ID::ROOM_LEAVE_RESPONSE;
                        resPacket.PacketLength = sizeof(ROOM_LEAVE_RESPONSE_PACKET);
                        resPacket.Result = 0; // ERROR_CODE::NONE
                        pRoom->SendPacketFunc(pJob->clientIndex, sizeof(resPacket), (char*)&resPacket);
                    
                        // ���� ����Ϳ��� ���� ���� ���� ��û �ݹ�
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
            return pJob; // ��κ��� ���⼭ �ٷ� ����

        // pop���� -> preemption Hole ����, �ܰ躰 ���
        uint32_t spinCount = 0;
        const uint32_t PHASE1_LIMIT = 64;
        const uint32_t PHASE2_LIMIT = 1024;
        const uint32_t TIMEOUT_LIMIT = 100000;

        while ((pJob = pRoom->GetLocalQueue().Pop()) == nullptr)
        {
            if (spinCount < PHASE1_LIMIT)
            {
                _mm_pause();    //  phase1: cpu�� ��� ���� �˸�
            }
            else if (spinCount < PHASE2_LIMIT)
            {
                Sleep(0);       // phase2 : ���� �켱���� �����忡 �纸
            }
            else if (spinCount >= TIMEOUT_LIMIT)
            {
                return nullptr; // phase3 : Ÿ�Ӿƿ�, ����
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
            // nullptr�̾ msgcount�� ���ҽ��Ѿ���
        } while (pRoom->GetMsgCount().fetch_sub(1, std::memory_order_acq_rel) > 1);

    }

    ObjectPool<PacketJob>    mJobPool;       // Job �Ҵ�/����
   // GlobalQueue_MutexCV      mGlobalQueue;   // �� �й� ť
    std::vector<std::thread> mLogicThreads;  // ó�� ������ Ǯ
    GlobalQueue mGlobalQueue;

    MPSCQueue<StrandCallback> mCallbackQueue;   // Logic Thread �� �����
    ObjectPool<StrandCallback> mCallbackPool;   // ������ �޸�Ǯ

    // ����� �뵵
    std::atomic<uint64_t> mAllocFailCount{ 0 };
    std::atomic<uint64_t> mAllocTotalCount{ 0 };

};