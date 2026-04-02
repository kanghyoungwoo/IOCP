#include "StrandProcessor.h"

void StrandProcessor::Init(uint32_t jobPoolSize, uint32_t callbackPoolSize, uint32_t maxRoomCount)
{
    mJobPool.Init(jobPoolSize);
    mCallbackPool.Init(callbackPoolSize);
#ifdef USE_LOCKFREE_GLOBAL_QUEUE
    // Lock-Free 방식은 2의 거듭제곱 크기의 바운디드 큐를 초기화합니다.
    uint32_t globalQueueSize = GetNextPowerOf2(maxRoomCount);
    mGlobalQueue.Init(globalQueueSize);
#endif
}

void StrandProcessor::Start(int threadCount)
{
    for (int i = 0;i < threadCount;i++)
    {
        mLogicThreads.emplace_back([this]() {WorkerThreadMain();});
    }
}

void StrandProcessor::Stop()
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

void StrandProcessor::EnqueueJob(Room* pRoom, uint32_t clientIndex, uint32_t targetGeneration, uint16_t packetId, uint16_t dataSize, const char* data)
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
    User* pUser = mUserManager->GetUserByConnIdx(clientIndex);
    pJob->clientIndex = clientIndex;
    pJob->roomIndex = pRoom->GetRoomNumber();
    pJob->targetGeneration = targetGeneration;
    pJob->sessionGeneration = pUser->GetSessionGeneration();
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

void StrandProcessor::WorkerThreadMain()
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

void StrandProcessor::ProcessRoom(Room* pRoom)
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
                    if (cb == nullptr)// cb이 nullptr이면 crash 방지
                    {
                        LOG_ERROR_ONCE("Callback Pool 소진! callback drop.\n");
                        mJobPool.Free(pJob);
                        continue;
                    }
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
                    ROOM_CHAT_REQUEST_PACKET* pChatReq = (ROOM_CHAT_REQUEST_PACKET*)pJob->body;

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
                    if (cb == nullptr)// cb이 nullptr이면 crash 방지
                    {
                        LOG_ERROR_ONCE("Callback Pool 소진! callback drop.\n");
                        mJobPool.Free(pJob);
                        continue;
                    }
                    cb->type = StrandCallbackType::USER_LEFT_ROOM;
                    cb->clientIndex = pJob->clientIndex;
                    mCallbackQueue.Push(cb);

                }

            }
            // 입장 처리
            else if (pJob->packetId == (uint16_t)PACKET_ID::ROOM_ENTER_REQUEST)
            {
                // UserManager에서 유저 포인터 획득
                User* pEnterUser = mUserManager->GetUserByConnIdx(pJob->clientIndex);

                // ABA방지, 큐 대기중 접속이 끊겼거나 유저가 바뀐 경우 즉시 드랍
                if (pEnterUser == nullptr || pEnterUser->GetSessionGeneration() != pJob->sessionGeneration)
                {
                    mJobPool.Free(pJob);
                    continue;
                }

                ROOM_ENTER_RESPONSE_PACKET resPacket;
                resPacket.PacketId = (UINT16)PACKET_ID::ROOM_ENTER_RESPONSE;
                resPacket.PacketLength = sizeof(ROOM_ENTER_RESPONSE_PACKET);

                if (pRoom->FindUserByClientIndex(pJob->clientIndex) != nullptr)
                {
                    resPacket.Result = (UINT16)ERROR_CODE::ENTER_ROOM_ALREADY_ENTERED;
                }
                else
                {
                    // Strand 안에서 안전하게 입ㅈ아
                    resPacket.Result = pRoom->EnterUser(pEnterUser);
                }

                // 입장 성공시 알림
                if (resPacket.Result == (UINT16)ERROR_CODE::NONE)
                {
                    ROOM_CHAT_REQUEST_PACKET tempChatPacket;
                    tempChatPacket.PacketId = (UINT16)PACKET_ID::ROOM_CHAT_REQUEST;
                    tempChatPacket.PacketLength = sizeof(ROOM_CHAT_REQUEST_PACKET);
                    memset(tempChatPacket.Message, 0, sizeof(tempChatPacket.Message));

                    sprintf_s(tempChatPacket.Message, sizeof(tempChatPacket.Message), "[%s] entered the room.", pEnterUser->GetUserID().c_str());
                    pRoom->NotifyChat(pJob->clientIndex, pEnterUser->GetUserID().c_str(), (char*)&tempChatPacket);
                }

                // 응답 전송
                pRoom->SendPacketFunc(pJob->clientIndex, pEnterUser->GetSessionGeneration(), sizeof(ROOM_ENTER_RESPONSE_PACKET), (char*)&resPacket);

                // 콜백 : PacketManager에서 DomainState 변경
                StrandCallback* cb = mCallbackPool.Alloc();
                if (cb == nullptr)
                {
                    LOG_ERROR_ONCE("Callback Pool 소진! callback drop.\n");
                    mJobPool.Free(pJob);
                    continue;
                }

                cb->type = StrandCallbackType::USER_ENTERED_ROOM;
                cb->clientIndex = pJob->clientIndex;
                cb->roomNumber = pRoom->GetRoomNumber();
                cb->result = resPacket.Result;
                mCallbackQueue.Push(cb);
            }

        }
        mJobPool.Free(pJob);
    } while (pRoom->GetMsgCount().fetch_sub(1, std::memory_order_acq_rel) > 1);
}

PacketJob* StrandProcessor::PopWithBackoff(Room* pRoom)
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

void StrandProcessor::DrainRoom(Room* pRoom)
{
    do {
        PacketJob* pJob = pRoom->GetLocalQueue().Pop();
        if (pJob != nullptr)
            mJobPool.Free(pJob);
        // nullptr이어도 msgcount는 감소시켜야함
    } while (pRoom->GetMsgCount().fetch_sub(1, std::memory_order_acq_rel) > 1);

}