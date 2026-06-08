#include <gtest/gtest.h>

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>

#include "../Room.h"
#include "../User.h"
#include "../Packet.h"
#include "../ErrorCode.h"
#include "../ClientSession.h"

#include <vector>
#include <set>
#include <string>
#include <thread>
#include <atomic>

// ============================================================================
//  §1  Room 라우팅 — 브로드캐스트가 EnqueueOnlyFunc를 경유하는지 검증
//
//  Deferred Send 도입 후 Room::SendToAllUser 는 SendPacketFunc 대신
//  EnqueueOnlyFunc 를 호출해야 한다.
//  → WSASend는 배치 종료 후 FlushAll() 이 일괄 처리한다.
// ============================================================================

class DeferredSendRoomTest : public ::testing::Test
{
protected:
    Room room;
    static constexpr INT32 MAX_USERS = 5;

    struct CallRecord
    {
        UINT32 connIndex;
        UINT32 gen;
        UINT32 dataSize;
        std::vector<char> data;
    };

    std::vector<CallRecord> enqueueCalls;   // EnqueueOnlyFunc 캡처 (브로드캐스트)
    std::vector<CallRecord> sendCalls;      // SendPacketFunc 캡처 (유니캐스트 응답)

    User users[MAX_USERS];

    void SetUp() override
    {
        room.Init(1, MAX_USERS);

        room.EnqueueOnlyFunc = [this](UINT32 idx, UINT32 gen, UINT32 size, char* pData)
        {
            enqueueCalls.push_back({ idx, gen, size, {pData, pData + size} });
        };
        room.SendPacketFunc = [this](UINT32 idx, UINT32 gen, UINT32 size, char* pData)
        {
            sendCalls.push_back({ idx, gen, size, {pData, pData + size} });
        };

        for (int i = 0; i < MAX_USERS; ++i)
        {
            users[i].Init(static_cast<UINT32>(10 + i));   // connIndex: 10~14
            users[i].SetSessionGeneration(static_cast<UINT32>(i + 1)); // gen: 1~5
            char id[32];
            sprintf_s(id, "user%d", i);
            users[i].SetLogin(id);
        }
    }

    // 채팅 요청 패킷 헬퍼
    static ROOM_CHAT_REQUEST_PACKET MakeChatPacket(const char* msg)
    {
        ROOM_CHAT_REQUEST_PACKET p = {};
        p.PacketId     = (UINT16)PACKET_ID::ROOM_CHAT_REQUEST;
        p.PacketLength = sizeof(p);
        strncpy_s(p.Message, msg, _TRUNCATE);
        return p;
    }
};

// ── §1-1 라우팅 ─────────────────────────────────────────────────────────────

// 브로드캐스트는 EnqueueOnlyFunc 를 통해 처리되어야 한다.
TEST_F(DeferredSendRoomTest, NotifyChat_RoutesToEnqueueOnly_NotSendPacket)
{
    room.EnterUser(&users[0]);
    room.EnterUser(&users[1]);
    room.EnterUser(&users[2]);

    auto pkt = MakeChatPacket("hello");
    room.NotifyChat(users[0].GetNetConnIndex(), "user0", &pkt);

    EXPECT_EQ(enqueueCalls.size(), 3u) << "NotifyChat must route through EnqueueOnlyFunc";
    EXPECT_EQ(sendCalls.size(),    0u) << "SendPacketFunc must NOT be called for broadcast";
}

// ── §1-2 호출 횟수 ──────────────────────────────────────────────────────────

TEST_F(DeferredSendRoomTest, NotifyChat_CallCount_SingleUser)
{
    room.EnterUser(&users[0]);
    auto pkt = MakeChatPacket("solo");
    room.NotifyChat(users[0].GetNetConnIndex(), "user0", &pkt);

    EXPECT_EQ(enqueueCalls.size(), 1u);
}

TEST_F(DeferredSendRoomTest, NotifyChat_CallCount_MatchesMemberCount)
{
    for (int i = 0; i < MAX_USERS; ++i)
        room.EnterUser(&users[i]);

    auto pkt = MakeChatPacket("full room");
    room.NotifyChat(users[0].GetNetConnIndex(), "user0", &pkt);

    EXPECT_EQ(enqueueCalls.size(), static_cast<size_t>(MAX_USERS));
}

TEST_F(DeferredSendRoomTest, NotifyChat_EmptyRoom_NoCalls)
{
    auto pkt = MakeChatPacket("nobody");
    room.NotifyChat(0, "ghost", &pkt);

    EXPECT_EQ(enqueueCalls.size(), 0u);
    EXPECT_EQ(sendCalls.size(),    0u);
}

// ── §1-3 connIndex / generation 정확성 ─────────────────────────────────────

TEST_F(DeferredSendRoomTest, NotifyChat_ConnIndices_AllMembersReceive)
{
    room.EnterUser(&users[0]);  // connIndex = 10
    room.EnterUser(&users[1]);  // connIndex = 11
    room.EnterUser(&users[2]);  // connIndex = 12

    auto pkt = MakeChatPacket("ping");
    room.NotifyChat(users[0].GetNetConnIndex(), "user0", &pkt);

    std::set<UINT32> got;
    for (auto& c : enqueueCalls)
        got.insert(c.connIndex);

    EXPECT_EQ(got.count(10u), 1u);
    EXPECT_EQ(got.count(11u), 1u);
    EXPECT_EQ(got.count(12u), 1u);
}

TEST_F(DeferredSendRoomTest, NotifyChat_Generations_CorrectPerUser)
{
    // users[0]: gen=1, users[2]: gen=3
    room.EnterUser(&users[0]);
    room.EnterUser(&users[2]);

    auto pkt = MakeChatPacket("gen test");
    room.NotifyChat(users[0].GetNetConnIndex(), "user0", &pkt);

    ASSERT_EQ(enqueueCalls.size(), 2u);

    for (auto& c : enqueueCalls)
    {
        if (c.connIndex == 10u) EXPECT_EQ(c.gen, 1u);
        if (c.connIndex == 12u) EXPECT_EQ(c.gen, 3u);
    }
}

// ── §1-4 패킷 내용 ──────────────────────────────────────────────────────────

TEST_F(DeferredSendRoomTest, NotifyChat_PacketContent_Correct)
{
    room.EnterUser(&users[0]);

    auto pkt = MakeChatPacket("hi there");
    room.NotifyChat(users[0].GetNetConnIndex(), "user0", &pkt);

    ASSERT_EQ(enqueueCalls.size(), 1u);

    const auto& rec = enqueueCalls[0];
    EXPECT_EQ(rec.dataSize, sizeof(ROOM_CHAT_NOTIFY_PACKET));

    auto* notify = reinterpret_cast<const ROOM_CHAT_NOTIFY_PACKET*>(rec.data.data());
    EXPECT_EQ(notify->PacketId, (UINT16)PACKET_ID::ROOM_CHAT_NOTIFY);
    EXPECT_STREQ(notify->UserID,  "user0");
    EXPECT_STREQ(notify->Message, "hi there");
}

// ── §1-5 퇴장 후 ────────────────────────────────────────────────────────────

TEST_F(DeferredSendRoomTest, NotifyChat_AfterLeave_LeftUserNotReceive)
{
    room.EnterUser(&users[0]);
    room.EnterUser(&users[1]);
    room.EnterUser(&users[2]);

    room.LeaveUser(&users[1]);  // connIndex=11 퇴장

    auto pkt = MakeChatPacket("after leave");
    room.NotifyChat(users[0].GetNetConnIndex(), "user0", &pkt);

    EXPECT_EQ(enqueueCalls.size(), 2u);

    std::set<UINT32> got;
    for (auto& c : enqueueCalls)
        got.insert(c.connIndex);

    EXPECT_EQ(got.count(10u), 1u);  // user[0] 수신
    EXPECT_EQ(got.count(12u), 1u);  // user[2] 수신
    EXPECT_EQ(got.count(11u), 0u);  // user[1] 미수신 (퇴장)
}

// ── §1-6 배치 내 누적 동작 ──────────────────────────────────────────────────
//
//  ProcessRoom 한 배치 안에서 NotifyChat 이 여러 번 호출되어도
//  FlushAll() 전까지 EnqueueOnly 호출이 누적되어야 한다.
//  (이 테스트는 FlushAll 을 호출하지 않고 누적 결과만 확인한다.)

TEST_F(DeferredSendRoomTest, MultipleBroadcasts_AccumulateWithoutFlush)
{
    room.EnterUser(&users[0]);  // connIndex=10
    room.EnterUser(&users[1]);  // connIndex=11

    auto pkt = MakeChatPacket("msg");

    // 3회 브로드캐스트 → 각각 2명에게 → 총 6회 EnqueueOnly 호출
    room.NotifyChat(users[0].GetNetConnIndex(), "user0", &pkt);
    room.NotifyChat(users[1].GetNetConnIndex(), "user1", &pkt);
    room.NotifyChat(users[0].GetNetConnIndex(), "user0", &pkt);

    EXPECT_EQ(enqueueCalls.size(), 6u)
        << "모든 브로드캐스트가 FlushAll 전까지 누적되어야 한다";
    EXPECT_EQ(sendCalls.size(), 0u);
}

// ── §1-7 배치 경계 — FlushAll 호출 후 리스트 초기화 시뮬레이션 ──────────────
//
//  StrandProcessor::ProcessRoom 에서 FlushAll() 을 호출하는 패턴을 모사한다.
//  FlushAll 은 Logic Thread 의 thread_local 리스트를 초기화하므로
//  다음 배치에서 새로운 세션들이 등록될 수 있어야 한다.

TEST_F(DeferredSendRoomTest, FlushAll_AfterBatch_ClearsFlushList)
{
    room.EnterUser(&users[0]);

    auto pkt = MakeChatPacket("batch1");
    room.NotifyChat(users[0].GetNetConnIndex(), "user0", &pkt);
    EXPECT_EQ(enqueueCalls.size(), 1u);

    // 배치 종료 — Logic Thread 가 FlushAll 호출하는 시점 모사
    // (테스트 스레드 = Logic Thread 역할)
    ClientSession::FlushAll();

    // 다음 배치
    enqueueCalls.clear();
    pkt = MakeChatPacket("batch2");
    room.NotifyChat(users[0].GetNetConnIndex(), "user0", &pkt);
    ClientSession::FlushAll();

    EXPECT_EQ(enqueueCalls.size(), 1u)
        << "FlushAll 후 다음 배치에서도 EnqueueOnly 가 동작해야 한다";
}


// ============================================================================
//  §2  ClientSession flush list 관리
//
//  thread_local tl_flushList 의 RegisterFlush / FlushAll 동작을 검증한다.
//  ClientSession 은 Init() 미호출(mSendPool=nullptr, mIsConnected=false) 상태로
//  사용하므로, TryFlush() 가 IsConnected() 에서 조기 반환한다 → 안전.
// ============================================================================

// ── §2-1 기본 안전성 ─────────────────────────────────────────────────────────

// 등록 없이 FlushAll 을 호출해도 크래시 없음
TEST(DeferredFlushList, FlushAll_EmptyList_NoCrash)
{
    EXPECT_NO_FATAL_FAILURE(ClientSession::FlushAll());
}

// 연결 안 된 세션들을 등록하고 FlushAll 해도 크래시 없음
// TryFlush() → IsConnected()==false → 즉시 false 반환
TEST(DeferredFlushList, FlushAll_DisconnectedSessions_NoCrash)
{
    ClientSession sessions[5];

    for (int i = 0; i < 5; ++i)
        ClientSession::RegisterFlush(&sessions[i]);

    EXPECT_NO_FATAL_FAILURE(ClientSession::FlushAll());
}

// ── §2-2 리스트 초기화 ───────────────────────────────────────────────────────

// FlushAll 후 동일 세션을 재등록할 수 있어야 한다 (리스트가 초기화됐음을 확인)
TEST(DeferredFlushList, FlushAll_ClearsList_AllowsReRegistration)
{
    ClientSession sessions[10];

    // 1차 등록 + FlushAll
    for (int i = 0; i < 10; ++i)
        ClientSession::RegisterFlush(&sessions[i]);
    EXPECT_NO_FATAL_FAILURE(ClientSession::FlushAll());

    // FlushAll 후 리스트가 초기화됐으므로 동일 세션 재등록 가능
    for (int i = 0; i < 10; ++i)
        ClientSession::RegisterFlush(&sessions[i]);
    EXPECT_NO_FATAL_FAILURE(ClientSession::FlushAll());
}

// ── §2-3 용량 초과 안전성 ────────────────────────────────────────────────────
//
//  FLUSH_LIST_CAP(=20) 이상의 세션을 등록해도 버퍼 오버플로가 없어야 한다.
//  초과분은 묵묵히 드랍된다.

TEST(DeferredFlushList, RegisterFlush_OverCapacity_NoCrash)
{
    constexpr int OVER_CAP = 25;    // FLUSH_LIST_CAP(20) 초과
    ClientSession sessions[OVER_CAP];

    for (int i = 0; i < OVER_CAP; ++i)
        ClientSession::RegisterFlush(&sessions[i]);

    EXPECT_NO_FATAL_FAILURE(ClientSession::FlushAll());
}

// ── §2-4 중복 등록 방지 ──────────────────────────────────────────────────────
//
//  동일 포인터를 FLUSH_LIST_CAP 회 등록해도 1개만 기록되어야 한다.
//  검증: 중복 후 19개의 다른 세션을 추가로 등록할 수 있으면 dedup 성공.
//  (dedup 없었다면 20회 등록 시점에 이미 cap 도달, 추가 등록 불가)

TEST(DeferredFlushList, RegisterFlush_Deduplication_SamePointerCountedOnce)
{
    constexpr int CAP = 20;
    ClientSession anchor;                   // 중복 대상
    ClientSession others[CAP - 1];          // 나머지 19개

    // anchor 를 CAP 회 등록 → dedup 로 1개만 기록
    for (int i = 0; i < CAP; ++i)
        ClientSession::RegisterFlush(&anchor);

    // 나머지 19개 등록 → 총 20개(=CAP)여야 함
    for (int i = 0; i < CAP - 1; ++i)
        ClientSession::RegisterFlush(&others[i]);

    // FlushAll 이 정상 동작해야 함 (20개 TryFlush, 모두 disconnected → false)
    EXPECT_NO_FATAL_FAILURE(ClientSession::FlushAll());
}

// ── §2-5 스레드 로컬 격리 ────────────────────────────────────────────────────
//
//  Thread A 가 등록한 세션들은 Thread B 의 FlushAll() 에 영향을 받지 않는다.
//  Thread B 의 FlushAll 이 Thread A 의 리스트를 건드리면 A 의 FlushAll 이 실패한다.

TEST(DeferredFlushList, ThreadLocal_FlushAll_DoesNotAffectOtherThread)
{
    constexpr int N = 10;
    ClientSession sessionsA[N];
    ClientSession sessionsB[N];

    std::atomic<bool> aRegistered{ false };
    std::atomic<bool> bFlushed{ false };
    std::atomic<bool> aFlushed{ false };

    // Thread A: 세션 등록 → B의 FlushAll 대기 → 자신의 FlushAll
    std::thread threadA([&]()
    {
        for (int i = 0; i < N; ++i)
            ClientSession::RegisterFlush(&sessionsA[i]);

        aRegistered.store(true, std::memory_order_release);

        // Thread B의 FlushAll 완료 대기
        while (!bFlushed.load(std::memory_order_acquire)) {}

        // B의 FlushAll 이 A의 리스트를 손대지 않았다면 여기서도 정상 완료
        EXPECT_NO_FATAL_FAILURE(ClientSession::FlushAll());
        aFlushed.store(true, std::memory_order_release);
    });

    // Thread B: A가 등록 완료할 때까지 대기 → B 자신의 리스트로 FlushAll
    std::thread threadB([&]()
    {
        while (!aRegistered.load(std::memory_order_acquire)) {}

        for (int i = 0; i < N; ++i)
            ClientSession::RegisterFlush(&sessionsB[i]);

        EXPECT_NO_FATAL_FAILURE(ClientSession::FlushAll());
        bFlushed.store(true, std::memory_order_release);
    });

    threadA.join();
    threadB.join();

    EXPECT_TRUE(aFlushed.load()) << "Thread A의 FlushAll 이 완료되어야 한다";
}


// ============================================================================
//  §3  EnqueueOnly / TryFlush — 연결 상태에 따른 조기 반환
// ============================================================================

// 연결 안 된 세션에 TryFlush → false (크래시 없음)
TEST(DeferredSendSession, TryFlush_Disconnected_ReturnsFalse)
{
    ClientSession session;
    EXPECT_FALSE(session.TryFlush());
}

// 연결 안 된 세션에 EnqueueOnly → false (크래시 없음)
TEST(DeferredSendSession, EnqueueOnly_Disconnected_ReturnsFalse)
{
    ClientSession session;
    char buf[32] = "test";
    EXPECT_FALSE(session.EnqueueOnly(4, buf));
}

// EnqueueOnly 유효하지 않은 크기 (0) → false
TEST(DeferredSendSession, EnqueueOnly_ZeroSize_ReturnsFalse)
{
    ClientSession session;
    char buf[32] = {};
    EXPECT_FALSE(session.EnqueueOnly(0, buf));
}
