#include "StrandProcessor.h"
#include "ClientSession.h"     // ClientSession::FlushAll() (Deferred Send)

void StrandProcessor::Init(uint32_t jobPoolSize, uint32_t maxRoomCount)
{
    mJobPool.Init(jobPoolSize);
#ifdef USE_LOCKFREE_GLOBAL_QUEUE
    // Lock-Free 방식은 2의 거듭제곱 크기의 바운디드 큐를 초기화
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
    // 큐를 먼저 닫고 자는 스레드들을 깨운다
    mGlobalQueue.Shutdown();

    for (auto& t : mLogicThreads)
    {
        if (t.joinable())
        {
            t.join();
        }
    }
    mLogicThreads.clear();
    LOG_INFO("[StrandProcessor] PopWithBackoff Hole 타임아웃(→방 강제종료): %llu회",
             mPopTimeoutCount.load(std::memory_order_relaxed));
    LOG_DEBUG("StrandProcessor: 모든 Logic Thread 종료하고 Stop 완료");
}

void StrandProcessor::EnqueueJob(Room* pRoom, uint32_t clientIndex, uint32_t targetGeneration, UINT32 sessionGeneration, uint16_t packetId, uint16_t dataSize, const char* data)
{
    mAllocTotalCount.fetch_add(1, std::memory_order_relaxed);

    thread_local PacketJob* tl_cache[BATCH_SIZE] = {};
    thread_local int tl_cacheCount = 0;

    if (tl_cacheCount == 0)
    {
        tl_cacheCount = (int)mJobPool.AllocBatch(BATCH_SIZE, tl_cache);
        if (tl_cacheCount == 0)
        {
            mAllocFailCount.fetch_add(1, std::memory_order_relaxed);
            LOG_ERROR_ONCE("Job Pool 소진. packet drop.");
            return;
        }
    }
    PacketJob* pJob = tl_cache[--tl_cacheCount];
    if (pJob == nullptr)
    {
        mAllocFailCount.fetch_add(1, std::memory_order_relaxed);
        LOG_ERROR_ONCE("Job Pool 소진. packet drop.");
        return;
    }

    // 구조체 변수 직접 덮어쓰는 방식을 통한 초기화
    pJob->phase = PacketJob::Phase::JOB;
    pJob->clientIndex = clientIndex;
    pJob->job.targetGeneration = targetGeneration;
    pJob->sessionGeneration = sessionGeneration;
    pJob->job.packetId = packetId;
    pJob->job.dataSize = dataSize;

    // 비정상적으로 큰 패킷 방어 및 누수 방지
    if (dataSize > MAX_PACKET_BODY_SIZE)
    {
        LOG_ERROR("비정상적인 패킷 크기 수신 (해킹 시도)! Size: %d", dataSize);
        mJobPool.Free(pJob);
        return;
    }

    // 시스템 패킷(Disconnect 등)이 아닌 일반 패킷인데 size가 0인 경우 방어
    if (dataSize == 0 && packetId != (uint16_t)PACKET_ID::SYS_USER_DISCONNECT)
    {
        LOG_ERROR("빈 패킷 수신 (해킹 시도)! PacketId: %d", packetId);
        mJobPool.Free(pJob);
        return;
    }

    if (dataSize > 0 && data != nullptr)
    {
        memcpy(pJob->job.body, data, dataSize);
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

// ──────────────────────────────────────────────────────────────────────────
//  ProcessRoom : 오케스트레이터
//  "무엇을 하는가"만 기술 — switch 디스패치로 각 핸들러에 위임
// ──────────────────────────────────────────────────────────────────────────
void StrandProcessor::ProcessRoom(Room* pRoom)
{
    do {
        // adaptive backoff로 Pop
        PacketJob* pJob = PopWithBackoff(pRoom);    //  MPSC PoP -> pJpb 획득

        if (pJob == nullptr)
        {
            // preemption Hole 타임아웃 -> 방 강제종료
            pRoom->SetBroken();
            DrainRoom(pRoom);
            return;
        }

        // generation 세대 검사 - 세대 불일치 -> 드랍
        if (pJob->job.targetGeneration != pRoom->GetGeneration())
        {
            LOG_DEBUG("세대 패킷 무시");
            mJobPool.Free(pJob);
            continue;
        }

        User* pUser = pRoom->FindUserByClientIndex(pJob->clientIndex);

        switch (pJob->job.packetId)
        {
        case (uint16_t)PACKET_ID::SYS_USER_DISCONNECT:
            if (pUser) HandleUserDisconnect(pRoom, pUser, pJob);
            else       mJobPool.Free(pJob);
            break;

        case (uint16_t)PACKET_ID::ROOM_CHAT_REQUEST:
            if (pUser) HandleRoomChat(pRoom, pUser, pJob);
            else       mJobPool.Free(pJob);
            break;

        case (uint16_t)PACKET_ID::ROOM_LEAVE_REQUEST:
            if (pUser) HandleRoomLeave(pRoom, pUser, pJob);
            else       mJobPool.Free(pJob);
            break;

        case (uint16_t)PACKET_ID::ROOM_ENTER_REQUEST:
            HandleRoomEnter(pRoom, pJob);   // 입장 시엔 아직 방에 없으므로 pUser 없음
            break;

        default:
            mJobPool.Free(pJob);
            break;
        }

    } while (pRoom->GetMsgCount().fetch_sub(1, std::memory_order_acq_rel) > 1);

    // ── Deferred Send flush ──────────────────────────────────────────────
    // 배치 처리 중 EnqueueOnly로 적재된 패킷들을 세션당 1회 scatter-gather WSASend로 송신
    ClientSession::FlushAll();
    // ────────────────────────────────────────────────────────────────────
}

// ── 핸들러: 강제 접속 종료 ────────────────────────────────────────────────
// pJob -> 콜백 큐로 소유권 이전 (Free 금지)
void StrandProcessor::HandleUserDisconnect(Room* pRoom, User* pUser, PacketJob* pJob)
{
    // 1. 방 내부 정리 (유저 삭제, 퇴장 알림 브로드캐스트)
    std::string leaverID = pUser->GetUserID();
    pRoom->LeaveUser(pUser);

    // 임시 채팅 패킷 생성
    ROOM_CHAT_REQUEST_PACKET tempChatPacket;
    tempChatPacket.PacketId     = (UINT16)PACKET_ID::ROOM_CHAT_REQUEST;
    tempChatPacket.PacketLength = sizeof(tempChatPacket);
    memset(tempChatPacket.Message, 0, sizeof(tempChatPacket.Message));
    strcpy_s(tempChatPacket.Message, sizeof(tempChatPacket.Message), "has left the room.");
    pRoom->NotifyChat(pJob->clientIndex, leaverID.c_str(), &tempChatPacket);

    pJob->phase   = PacketJob::Phase::STRAND_CALLBACK;
    pJob->cb.type = StrandCallbackType::FREE_USER;
    mCallbackQueue.Push(pJob);
}

// ── 핸들러: 방 채팅 ───────────────────────────────────────────────────────
// 처리 완료 후 자체 Free
void StrandProcessor::HandleRoomChat(Room* pRoom, User* pUser, PacketJob* pJob)
{
    if (pJob->job.dataSize < sizeof(ROOM_CHAT_REQUEST_PACKET))
    {
        // 메시지가 아예 없는 빈 패킷 무시
        mJobPool.Free(pJob);
        return;
    }

    // 나중에 strcpy를 쓸 때 메모리 오버플로우 방지
    auto* pChatReq = reinterpret_cast<ROOM_CHAT_REQUEST_PACKET*>(pJob->job.body);

    // 수신받은 바이트 끝에 Null 덮어쓰기 (안전)
    uint16_t messageLen = pJob->job.dataSize - sizeof(PACKET_HEADER);
    pChatReq->Message[messageLen < 256 ? messageLen : 255] = '\0';

    // 요청한 클라이언트에 응답 결과 패킷 전송
    ROOM_CHAT_RESPONSE_PACKET resPacket;
    resPacket.PacketId     = (UINT16)PACKET_ID::ROOM_CHAT_RESPONSE;
    resPacket.PacketLength = sizeof(ROOM_CHAT_RESPONSE_PACKET);
    resPacket.Result       = 0; // ERROR_CODE::NONE
    pRoom->SendPacketFunc(pJob->clientIndex, pUser->GetSessionGeneration(),
        sizeof(resPacket), (char*)&resPacket);

    // 방 전체에 브로드캐스트
    pRoom->NotifyChat(pJob->clientIndex, pUser->GetUserID().c_str(),
        reinterpret_cast<const ROOM_CHAT_REQUEST_PACKET*>(pJob->job.body));

    mJobPool.Free(pJob);
}

// ── 핸들러: 방 퇴장 ───────────────────────────────────────────────────────
// pJob -> 콜백 큐로 소유권 이전 (Free 금지)
void StrandProcessor::HandleRoomLeave(Room* pRoom, User* pUser, PacketJob* pJob)
{
    // 방에서 유저 삭제 및 퇴장 알림
    std::string leaverID = pUser->GetUserID();
    pRoom->LeaveUser(pUser);

    ROOM_CHAT_REQUEST_PACKET tempChatPacket;
    tempChatPacket.PacketId     = (UINT16)PACKET_ID::ROOM_CHAT_REQUEST;
    tempChatPacket.PacketLength = sizeof(tempChatPacket);
    memset(tempChatPacket.Message, 0, sizeof(tempChatPacket.Message));
    strncpy_s(tempChatPacket.Message, sizeof(tempChatPacket.Message), "has left the room.", _TRUNCATE);
    pRoom->NotifyChat(pJob->clientIndex, leaverID.c_str(), &tempChatPacket);

    // 클라이언트에 퇴장 응답 전송
    ROOM_LEAVE_RESPONSE_PACKET resPacket;
    resPacket.PacketId     = (UINT16)PACKET_ID::ROOM_LEAVE_RESPONSE;
    resPacket.PacketLength = sizeof(ROOM_LEAVE_RESPONSE_PACKET);
    resPacket.Result       = 0; // ERROR_CODE::NONE
    pRoom->SendPacketFunc(pJob->clientIndex, pUser->GetSessionGeneration(),
        sizeof(resPacket), (char*)&resPacket);

    pJob->phase   = PacketJob::Phase::STRAND_CALLBACK;
    pJob->cb.type = StrandCallbackType::USER_LEFT_ROOM;
    mCallbackQueue.Push(pJob);
}

// ── 핸들러: 방 입장 ───────────────────────────────────────────────────────
// 입장 시 pUser는 아직 방에 없으므로 UserManager에서 직접 조회
// 유효성 실패 시 자체 Free, 성공 시 pJob -> 콜백 큐로 소유권 이전
void StrandProcessor::HandleRoomEnter(Room* pRoom, PacketJob* pJob)
{
    // UserManager에서 유저 포인터 획득
    User* pEnterUser = mUserManager->GetUserByConnIdx(pJob->clientIndex);

    // ABA 방지: 큐 대기 중 접속이 끊겼거나 유저가 바뀐 경우 즉시 드랍
    if (pEnterUser == nullptr || pEnterUser->GetSessionGeneration() != pJob->sessionGeneration)
    {
        mJobPool.Free(pJob);
        return;
    }

    ROOM_ENTER_RESPONSE_PACKET resPacket;
    resPacket.PacketId     = (UINT16)PACKET_ID::ROOM_ENTER_RESPONSE;
    resPacket.PacketLength = sizeof(ROOM_ENTER_RESPONSE_PACKET);
    resPacket.Result       = (pRoom->FindUserByClientIndex(pJob->clientIndex) != nullptr)
                           ? (UINT16)ERROR_CODE::ENTER_ROOM_ALREADY_ENTERED
                           : pRoom->EnterUser(pEnterUser);  // Strand 안에서 안전하게 입장

    // 입장 성공 시 방 전체에 알림
    if (resPacket.Result == (UINT16)ERROR_CODE::NONE)
    {
        ROOM_CHAT_REQUEST_PACKET tempChatPacket;
        tempChatPacket.PacketId     = (UINT16)PACKET_ID::ROOM_CHAT_REQUEST;
        tempChatPacket.PacketLength = sizeof(ROOM_CHAT_REQUEST_PACKET);
        memset(tempChatPacket.Message, 0, sizeof(tempChatPacket.Message));
        int written = sprintf_s(tempChatPacket.Message, sizeof(tempChatPacket.Message),
            "[%s] entered the room.", pEnterUser->GetUserID().c_str());
        if (written < 0)
            tempChatPacket.Message[sizeof(tempChatPacket.Message) - 1] = '\0';
        pRoom->NotifyChat(pJob->clientIndex, pEnterUser->GetUserID().c_str(), &tempChatPacket);
    }

    // 응답 전송
    pRoom->SendPacketFunc(pJob->clientIndex, pEnterUser->GetSessionGeneration(), sizeof(ROOM_ENTER_RESPONSE_PACKET), (char*)&resPacket);

    pJob->phase         = PacketJob::Phase::STRAND_CALLBACK;
    pJob->cb.type       = StrandCallbackType::USER_ENTERED_ROOM;
    pJob->cb.roomNumber = pRoom->GetRoomNumber();
    pJob->cb.result     = resPacket.Result;
    mCallbackQueue.Push(pJob);
}

PacketJob* StrandProcessor::PopWithBackoff(Room* pRoom)
{
    // v4(IOCP Worker 직접 라우팅)에서 LocalQueue가 SPSC→MPSC로 바뀌면서,
    // Push의 m_tail.exchange()와 mpscNext.store() 사이 선점(Hole)이 실제로 발생할 수 있다.
    // 따라서 아래 백오프 루프는 라이브 경로다. (SPSC 시절엔 Hole이 없어 미실행이었음)
    PacketJob* pJob = pRoom->GetLocalQueue().Pop();
    if (pJob != nullptr)
        return pJob; // 대부분은 여기서 바로 성공 (Hole 아님)

    // Pop 실패 → 생산자 선점 Hole 의심. 단조 증가(escalating) 백오프로 대기한다.
    // 타임아웃은 spin '횟수'가 아니라 '경과 시간'으로 판정한다:
    //   phase3의 양보 호출(SwitchToThread) 비용이 가변적이라, 횟수 기준은 실제 대기시간이
    //   들쭉날쭉해진다. 시간 기준은 백오프 방식과 무관하게 타임아웃 실시간을 일정하게 유지한다.
    static const LARGE_INTEGER s_freq = [] {
        LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f;
    }();
    LARGE_INTEGER start;
    QueryPerformanceCounter(&start);
    constexpr double TIMEOUT_MS = 2.0;  // Hole 해소 대기 상한

    const uint32_t PHASE1_LIMIT = 64;
    const uint32_t PHASE2_LIMIT = 1024;

    uint32_t spinCount = 0;
    while ((pJob = pRoom->GetLocalQueue().Pop()) == nullptr)
    {
        if (spinCount < PHASE1_LIMIT)
        {
            _mm_pause();        // phase1: HT sibling에 힌트, 초단기 busy-wait
            ++spinCount;
        }
        else if (spinCount < PHASE2_LIMIT)
        {
            Sleep(0);           // phase2: 동일·상위 우선순위 ready 스레드에 양보
            ++spinCount;
        }
        else
        {
            // phase3: 현재 코어의 ready 스레드(=선점된 생산자)에 양보.
            // SwitchToThread는 우선순위 무관·동일 코어 양보라 Hole을 유발한 생산자를
            // 깨우는 데 가장 적합하다. (구버그: 이 구간이 무양보 busy-spin이라 같은 코어의
            //  생산자 재스케줄을 방해 → 자기가 기다리는 Hole을 자기가 막는 self-livelock)
            SwitchToThread();
            LARGE_INTEGER now;
            QueryPerformanceCounter(&now);
            double elapsedMs = (now.QuadPart - start.QuadPart) * 1000.0 / s_freq.QuadPart;
            if (elapsedMs >= TIMEOUT_MS)
            {
                mPopTimeoutCount.fetch_add(1, std::memory_order_relaxed);
                return nullptr; // 타임아웃 → 호출부에서 SetBroken
            }
        }
    }
    return pJob;
}

void StrandProcessor::DrainRoom(Room* pRoom)
{
    PacketJob* pRecycled = nullptr;
    do {
        PacketJob* pJob = pRoom->GetLocalQueue().Pop();
        if (pJob != nullptr)
        {
            if (pRecycled == nullptr)
                pRecycled = pJob;       // 첫 번째 job 재활용
            else
                mJobPool.Free(pJob);    // 나머지 반납
        }
    } while (pRoom->GetMsgCount().fetch_sub(1, std::memory_order_acq_rel) > 1);
    if (pRecycled != nullptr)
    {
        pRecycled->phase         = PacketJob::Phase::STRAND_CALLBACK;
        pRecycled->cb.type       = StrandCallbackType::ROOM_BROKEN;
        pRecycled->cb.roomNumber = pRoom->GetRoomNumber();
        mCallbackQueue.Push(pRecycled);
    }
}
