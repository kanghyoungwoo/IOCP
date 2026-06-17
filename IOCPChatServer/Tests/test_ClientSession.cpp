#include <gtest/gtest.h>

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>

#include "../ClientSession.h"
#include "../Define.h"

#include <cstring>

// ============================================================================
//  ClientSession 단위 테스트
//
//  소켓·IOCP 의존 코드(OnConnect, BindRecv, SendIO, AcceptCompletion 등)는
//  실제 핸들 없이 호출할 수 없으므로 제외.
//  아래 테스트들은 소켓 없이 안전하게 호출 가능한 경로만 검증한다.
// ============================================================================

// ── §1  Clear() ─────────────────────────────────────────────────────────────
//  mSendPool=nullptr 이더라도 큐·pendingList 가 비어있으면 크래시 없음.

TEST(ClientSessionClear, EmptyState_NoCrash)
{
    ClientSession s;
    EXPECT_NO_FATAL_FAILURE(s.Clear());
}

TEST(ClientSessionClear, RepeatedClear_NoCrash)
{
    ClientSession s;
    s.Clear();
    s.Clear();
    EXPECT_NO_FATAL_FAILURE(s.Clear());
}

// ── §2  SendMsg() 조기 반환 ──────────────────────────────────────────────────
//  연결되지 않은 세션에 SendMsg → false (소켓 접근 없음)

TEST(ClientSessionSendMsg, Disconnected_ReturnsFalse)
{
    ClientSession s;
    char buf[32] = "hello";
    EXPECT_FALSE(s.SendMsg(5, buf));
}

TEST(ClientSessionSendMsg, ZeroSize_Disconnected_ReturnsFalse)
{
    ClientSession s;
    char buf[32] = {};
    EXPECT_FALSE(s.SendMsg(0, buf));
}

// ── §3  DisconnectAsync() ────────────────────────────────────────────────────

// 세대 불일치 → 즉시 반환 (소켓 미접촉)
TEST(ClientSessionDisconnect, GenerationMismatch_NoCrash)
{
    ClientSession s;
    // mGeneration = 0, 잘못된 세대를 전달
    EXPECT_NO_FATAL_FAILURE(s.DisconnectAsync(1u));
    EXPECT_NO_FATAL_FAILURE(s.DisconnectAsync(999u));
}

// 올바른 세대로 호출, 소켓은 INVALID_SOCKET → 소켓 블록 진입 안함
TEST(ClientSessionDisconnect, ValidGen_InvalidSocket_NoCrash)
{
    ClientSession s;
    // m_socketClient = INVALID_SOCKET (기본값)
    // → if (m_socketClient != INVALID_SOCKET) 블록 스킵
    EXPECT_NO_FATAL_FAILURE(s.DisconnectAsync(0u));
}

// 두 번째 호출은 CAS 실패로 조기 반환
TEST(ClientSessionDisconnect, DoubleCall_SecondCallIsNoop)
{
    ClientSession s;
    s.DisconnectAsync(0u);     // 첫 번째: CAS 획득 → mIsDisconnecting = true
    EXPECT_NO_FATAL_FAILURE(s.DisconnectAsync(0u));  // 두 번째: CAS 실패 → 즉시 반환
}

// ── §4  SendComplete() 스태일 세대 조기 반환 ─────────────────────────────────
//  mPendingSendCount = 0 이면 mSendPool->Free() 호출 없음 → nullptr pool 안전

TEST(ClientSessionSendComplete, StaleGeneration_NoCrash)
{
    ClientSession s;
    s.Init(0, INVALID_HANDLE_VALUE, nullptr);

    SendOverlappedEx ovl{};
    ovl.base.generation = 99u;  // mGeneration=0 와 불일치 → stale 경로
    ovl.base.op = CompletionOp::IO(IOOperation::SEND);

    // mPendingSendCount=0 이므로 풀 접근 없이 반환
    EXPECT_NO_FATAL_FAILURE(s.SendComplete(&ovl, 64u));
}

TEST(ClientSessionSendComplete, NotConnected_SameGen_NoCrash)
{
    ClientSession s;
    s.Init(0, INVALID_HANDLE_VALUE, nullptr);

    SendOverlappedEx ovl{};
    ovl.base.generation = 0u;  // generation 일치 but !IsConnected() → stale 경로
    ovl.base.op = CompletionOp::IO(IOOperation::SEND);

    EXPECT_NO_FATAL_FAILURE(s.SendComplete(&ovl, 64u));
}

// ── §5  Init / GetIndex ───────────────────────────────────────────────────────

TEST(ClientSessionInit, GetIndex_AfterInit)
{
    ClientSession s;
    s.Init(42u, INVALID_HANDLE_VALUE, nullptr);
    EXPECT_EQ(s.GetIndex(), 42u);
}

TEST(ClientSessionInit, GetSocket_DefaultIsInvalid)
{
    ClientSession s;
    EXPECT_EQ(s.GetSocket(), INVALID_SOCKET);
}

TEST(ClientSessionInit, IsConnected_DefaultFalse)
{
    ClientSession s;
    EXPECT_FALSE(s.IsConnected());
}

TEST(ClientSessionInit, GetGeneration_DefaultZero)
{
    ClientSession s;
    EXPECT_EQ(s.GetGeneration(), 0u);
}

// ── §6  AddRef / ReleaseRef ───────────────────────────────────────────────────

TEST(ClientSessionRefCount, InitialRefCountZero)
{
    ClientSession s;
    EXPECT_EQ(s.GetRefCount(), 0);
}

TEST(ClientSessionRefCount, AddRef_Increments)
{
    ClientSession s;
    s.AddRef();
    EXPECT_EQ(s.GetRefCount(), 1);
    s.ReleaseRef();  // cleanup
}

TEST(ClientSessionRefCount, ReleaseRef_LastRef_ReturnsTrue)
{
    ClientSession s;
    s.AddRef();                     // refCount = 1
    bool last = s.ReleaseRef();     // refCount = 0 → Clear() 호출 (빈 큐 → 안전)
    EXPECT_TRUE(last);
    EXPECT_EQ(s.GetRefCount(), 0);
}

TEST(ClientSessionRefCount, ReleaseRef_NotLastRef_ReturnsFalse)
{
    ClientSession s;
    s.AddRef();
    s.AddRef();                     // refCount = 2
    bool last = s.ReleaseRef();     // refCount = 1
    EXPECT_FALSE(last);
    EXPECT_EQ(s.GetRefCount(), 1);
    s.ReleaseRef();                 // cleanup
}

// ── §7  TryMarkDisconnected ───────────────────────────────────────────────────

TEST(ClientSessionMark, TryMarkDisconnected_AlreadyFalse_ReturnsFalse)
{
    ClientSession s;
    // 초기 mIsConnected = false → exchange(false) → 이전값 false 반환
    EXPECT_FALSE(s.TryMarkDisconnected());
}

// ── §8  CloseAcceptSocket ─────────────────────────────────────────────────────

TEST(ClientSessionSocket, CloseAcceptSocket_InvalidSocket_NoCrash)
{
    ClientSession s;  // m_socketClient = INVALID_SOCKET
    EXPECT_NO_FATAL_FAILURE(s.CloseAcceptSocket());
    EXPECT_EQ(s.GetSocket(), INVALID_SOCKET);
}

// ── §9  Activity / Ping 시간 ──────────────────────────────────────────────────

TEST(ClientSessionTime, GetLastActivityTime_Default_Zero)
{
    ClientSession s;
    EXPECT_EQ(s.GetLastActivityTime(), 0ULL);
}

TEST(ClientSessionTime, UpdateActivity_SetsNonzero)
{
    ClientSession s;
    s.UpdateActivity();
    EXPECT_GT(s.GetLastActivityTime(), 0ULL);
}

TEST(ClientSessionTime, SetGetLastPingTime)
{
    ClientSession s;
    EXPECT_EQ(s.GetLastPingTime(), 0ULL);
    s.SetLastPingTime(12345ULL);
    EXPECT_EQ(s.GetLastPingTime(), 12345ULL);
}

// ============================================================================
//  §10  실제 소켓/IOCP 기반 통합 테스트 (Live socket fixture)
//
//  loopback TCP 연결과 진짜 IOCP 핸들을 만들어, 소켓이 반드시 필요한 경로
//  (OnConnect / BindRecv / BindIOCompletionPort / SendIO / SendComplete /
//   Closed / AcceptCompletion / PostImmediateAccept / DisconnectAsync 소켓 분기)
//  를 검증한다.
//
//  핵심 안전장치:
//   - 세션 멤버(stOverlappedEx 등)에 걸린 pending I/O 가 세션 소멸 전에
//     모두 완료되도록 TearDown 에서 IOCP 를 완전히 drain 한다.
//   - EnqueueOnly 가 등록한 thread_local 플러시 리스트를 TearDown 에서
//     FlushAll() 로 초기화해 dangling 포인터를 남기지 않는다.
// ============================================================================

class ClientSessionLiveTest : public ::testing::Test
{
protected:
    HANDLE      iocp       = INVALID_HANDLE_VALUE;
    SOCKET      listenSock = INVALID_SOCKET;
    SOCKET      peerSock   = INVALID_SOCKET;   // 클라이언트 쪽 끝단
    ObjectPool<SendOverlappedEx> sendPool;
    ClientSession session;
    sockaddr_in addr{};

    static void SetUpTestSuite()
    {
        WSADATA w;
        WSAStartup(MAKEWORD(2, 2), &w);
    }
    static void TearDownTestSuite()
    {
        WSACleanup();
    }

    void SetUp() override
    {
        iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
        ASSERT_NE(iocp, (HANDLE)nullptr);

        sendPool.Init(64);

        listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        ASSERT_NE(listenSock, INVALID_SOCKET);

        addr.sin_family      = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port        = 0;   // 임의 포트 자동 할당
        ASSERT_EQ(bind(listenSock, (sockaddr*)&addr, sizeof(addr)), 0);

        int len = sizeof(addr);
        ASSERT_EQ(getsockname(listenSock, (sockaddr*)&addr, &len), 0);
        ASSERT_EQ(listen(listenSock, 4), 0);

        // AcceptEx 완료 패킷이 iocp 로 오도록 listen 소켓을 연결
        CreateIoCompletionPort((HANDLE)listenSock, iocp, (ULONG_PTR)0xFFFF, 0);

        session.Init(0, iocp, &sendPool);
    }

    // 실제 연결된 소켓을 만들어 OnConnect 로 세션에 주입
    bool ConnectSession()
    {
        peerSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (peerSock == INVALID_SOCKET) return false;
        if (connect(peerSock, (sockaddr*)&addr, sizeof(addr)) != 0) return false;

        SOCKET accepted = accept(listenSock, nullptr, nullptr);
        if (accepted == INVALID_SOCKET) return false;

        return session.OnConnect(iocp, accepted);
    }

    // IOCP 에서 SEND 완료 패킷 하나를 꺼내 반환 (없으면 nullptr).
    // recv 는 데이터가 없어 완료되지 않으므로 송신 완료만 도착한다.
    SendOverlappedEx* WaitSendCompletion(DWORD& bytesOut)
    {
        for (int i = 0; i < 10; ++i)
        {
            DWORD        bytes = 0;
            ULONG_PTR    key   = 0;
            LPOVERLAPPED ov    = nullptr;
            if (GetQueuedCompletionStatus(iocp, &bytes, &key, &ov, 1000) && ov)
            {
                auto* base = reinterpret_cast<OverlappedBase*>(ov);
                if (base->op.IsIO() && base->op.io == IOOperation::SEND)
                {
                    bytesOut = bytes;
                    return reinterpret_cast<SendOverlappedEx*>(base);
                }
            }
        }
        return nullptr;
    }

    // 큐에 남은 완료 패킷을 모두 비운다 (세션 소멸 전 커널 참조 제거)
    void DrainIocp()
    {
        for (;;)
        {
            DWORD       bytes = 0;
            ULONG_PTR   key   = 0;
            LPOVERLAPPED ov   = nullptr;
            BOOL ok = GetQueuedCompletionStatus(iocp, &bytes, &key, &ov, 100);
            if (!ok && ov == nullptr) break;   // 타임아웃 → 큐 비었음
            // ok 여부와 무관하게 패킷을 하나 뽑았으면 계속 drain
        }
    }

    void TearDown() override
    {
        // thread_local 플러시 리스트 초기화 (dangling 방지)
        ClientSession::FlushAll();

        // 세션 소켓을 닫아 pending recv/send 가 에러로 완료되게 함
        if (session.GetSocket() != INVALID_SOCKET)
            session.Closed(true);

        if (peerSock != INVALID_SOCKET)
            closesocket(peerSock);

        DrainIocp();

        if (listenSock != INVALID_SOCKET)
            closesocket(listenSock);
        if (iocp != INVALID_HANDLE_VALUE)
            CloseHandle(iocp);
    }
};

// ── §10-1 OnConnect (Clear + BindIOCompletionPort + BindRecv) ────────────────

TEST_F(ClientSessionLiveTest, OnConnect_RealSocket_Succeeds)
{
    EXPECT_TRUE(ConnectSession());
    EXPECT_TRUE(session.IsConnected());
    EXPECT_EQ(session.GetGeneration(), 1u);     // OnConnect 가 generation++ 함
    EXPECT_NE(session.GetSocket(), INVALID_SOCKET);
}

// ── §10-2 SendMsg 정상 전송 경로 (SendIO → WSASend) ──────────────────────────

TEST_F(ClientSessionLiveTest, SendMsg_RealSocket_Sends)
{
    ASSERT_TRUE(ConnectSession());

    char msg[] = "hello world";
    EXPECT_TRUE(session.SendMsg((UINT32)strlen(msg), msg));
}

// ── §10-3 EnqueueOnly + TryFlush (Deferred Send 실제 전송) ────────────────────

TEST_F(ClientSessionLiveTest, EnqueueOnly_ThenTryFlush_Sends)
{
    ASSERT_TRUE(ConnectSession());

    char msg[] = "deferred-data";
    EXPECT_TRUE(session.EnqueueOnly((UINT32)strlen(msg), msg));  // 큐 적재
    EXPECT_TRUE(session.TryFlush());                             // SendIO 발사
}

// ── §10-4 SendComplete 정상 완료 처리 ────────────────────────────────────────
//  실제 송신 완료 패킷을 IOCP 에서 꺼내 SendComplete 에 넘긴다.

TEST_F(ClientSessionLiveTest, SendComplete_NormalCompletion)
{
    ASSERT_TRUE(ConnectSession());

    char msg[] = "complete-me";
    const UINT32 sz = (UINT32)strlen(msg);
    ASSERT_TRUE(session.SendMsg(sz, msg));

    DWORD bytes = 0;
    SendOverlappedEx* sent = WaitSendCompletion(bytes);
    ASSERT_NE(sent, nullptr) << "송신 완료 패킷을 받아야 한다";

    session.SendComplete(sent, bytes);          // 정상 완료 경로 (반납 + 큐 비움)
    EXPECT_EQ(sendPool.GetFreeCount(), 64u);    // 사용 중이던 패킷 반납됨
}

// ── §10-4b SendComplete dwIoSize==0 → 연결 끊김 처리 ─────────────────────────

TEST_F(ClientSessionLiveTest, SendComplete_ZeroBytes_DropsAndDisconnects)
{
    ASSERT_TRUE(ConnectSession());

    char msg[] = "data";
    ASSERT_TRUE(session.SendMsg(4, msg));

    DWORD bytes = 0;
    SendOverlappedEx* sent = WaitSendCompletion(bytes);
    ASSERT_NE(sent, nullptr);

    // dwIoSize=0 강제 → 연결 끊김으로 간주, pending 반납 + DisconnectAsync
    session.SendComplete(sent, 0);
    EXPECT_EQ(sendPool.GetFreeCount(), 64u);
}

// ── §10-4c SendComplete 연결 종료 상태 → stale 경로로 반납만 ─────────────────

TEST_F(ClientSessionLiveTest, SendComplete_NotConnected_DropsPending)
{
    ASSERT_TRUE(ConnectSession());

    char msg[] = "data";
    ASSERT_TRUE(session.SendMsg(4, msg));

    DWORD bytes = 0;
    SendOverlappedEx* sent = WaitSendCompletion(bytes);
    ASSERT_NE(sent, nullptr);

    session.TryMarkDisconnected();              // mIsConnected = false
    session.SendComplete(sent, bytes);          // !IsConnected() → stale 경로 반납
    EXPECT_EQ(sendPool.GetFreeCount(), 64u);
}

// ── §10-4d SendComplete 후 큐에 더 있으면 이어서 전송 (hasMore) ──────────────

TEST_F(ClientSessionLiveTest, SendComplete_HasMoreQueued_ContinuesSending)
{
    ASSERT_TRUE(ConnectSession());

    char a[] = "first";
    ASSERT_TRUE(session.SendMsg(5, a));         // SendIO 발사, mIsSending=true, pending=1

    char b[] = "second";
    ASSERT_TRUE(session.EnqueueOnly(6, b));     // mIsSending=true 이므로 큐에만 적재

    DWORD bytes = 0;
    SendOverlappedEx* sent = WaitSendCompletion(bytes);   // 'a' 의 완료
    ASSERT_NE(sent, nullptr);

    // 정상 완료 → 큐에 'b' 가 남아있음 → hasMore=true → SendIO('b')
    session.SendComplete(sent, bytes);

    // 'b' 의 완료도 처리되어야 풀이 완전히 비워짐
    DWORD bytes2 = 0;
    SendOverlappedEx* sent2 = WaitSendCompletion(bytes2);
    ASSERT_NE(sent2, nullptr);
    session.SendComplete(sent2, bytes2);

    EXPECT_EQ(sendPool.GetFreeCount(), 64u);
}

// ── §10-4e 풀 고갈 시 SendMsg / EnqueueOnly 드랍 ─────────────────────────────

TEST_F(ClientSessionLiveTest, SendMsg_PoolExhausted_ReturnsFalse)
{
    ASSERT_TRUE(ConnectSession());

    // 풀(64개)을 전부 소진시켜 Alloc 실패를 유도
    SendOverlappedEx* drain[64];
    for (int i = 0; i < 64; ++i) drain[i] = sendPool.Alloc();

    char msg[] = "x";
    EXPECT_FALSE(session.SendMsg(1, msg));                 // Alloc null → 드랍
    EXPECT_GE(sendPool.GetAllocFailCount(), 1u);

    for (int i = 0; i < 64; ++i) sendPool.Free(drain[i]);  // 반납
}

TEST_F(ClientSessionLiveTest, EnqueueOnly_PoolExhausted_ReturnsFalse)
{
    ASSERT_TRUE(ConnectSession());

    SendOverlappedEx* drain[64];
    for (int i = 0; i < 64; ++i) drain[i] = sendPool.Alloc();

    char msg[] = "x";
    EXPECT_FALSE(session.EnqueueOnly(1, msg));             // Alloc null → 드랍

    for (int i = 0; i < 64; ++i) sendPool.Free(drain[i]);
}

// ── §10-5 Closed (shutdown + setsockopt + closesocket) ───────────────────────

TEST_F(ClientSessionLiveTest, Closed_RealSocket_InvalidatesSocket)
{
    ASSERT_TRUE(ConnectSession());

    session.Closed();
    EXPECT_EQ(session.GetSocket(), INVALID_SOCKET);
}

TEST_F(ClientSessionLiveTest, Closed_Forced_InvalidatesSocket)
{
    ASSERT_TRUE(ConnectSession());

    session.Closed(true);   // SO_LINGER l_onoff=1 경로
    EXPECT_EQ(session.GetSocket(), INVALID_SOCKET);
}

// ── §10-6 Clear 가 큐에 남은 송신 패킷을 풀에 반납 ───────────────────────────

TEST_F(ClientSessionLiveTest, Clear_FreesQueuedSends)
{
    ASSERT_TRUE(ConnectSession());

    char msg[] = "queued";
    ASSERT_TRUE(session.EnqueueOnly((UINT32)strlen(msg), msg));  // 큐에만 적재
    EXPECT_EQ(sendPool.GetFreeCount(), 63u);                     // 1개 사용 중

    session.Clear();                                            // 큐 비우며 반납
    EXPECT_EQ(sendPool.GetFreeCount(), 64u);                    // 전부 반납됨
}

// ── §10-7 SendMsg / EnqueueOnly 비정상 크기 → DisconnectAsync 소켓 분기 ──────

TEST_F(ClientSessionLiveTest, SendMsg_OversizedPacket_ReturnsFalse)
{
    ASSERT_TRUE(ConnectSession());

    char msg[8] = {};
    // dataSize > MAX_SOCKBUF → 악성 패킷으로 간주, DisconnectAsync 호출
    EXPECT_FALSE(session.SendMsg(MAX_SOCKBUF + 1, msg));
}

TEST_F(ClientSessionLiveTest, EnqueueOnly_OversizedPacket_ReturnsFalse)
{
    ASSERT_TRUE(ConnectSession());

    char msg[8] = {};
    EXPECT_FALSE(session.EnqueueOnly(MAX_SOCKBUF + 1, msg));
}

// ── §10-8 DisconnectAsync 연결된 소켓 분기 (shutdown + CancelIoEx) ───────────

TEST_F(ClientSessionLiveTest, DisconnectAsync_ConnectedSocket_CancelsPendingIo)
{
    ASSERT_TRUE(ConnectSession());

    // generation 일치 + 유효 소켓 → shutdown + CancelIoEx 경로 진입
    EXPECT_NO_FATAL_FAILURE(session.DisconnectAsync(session.GetGeneration()));
}

// ── §10-8b SendIO 실패 경로 (소켓 무효화 후 전송 시도) ──────────────────────
//  Closed() 는 소켓을 INVALID_SOCKET 으로 만들지만 IsConnected() 는 true 로
//  남는다. 이 상태로 전송을 시도하면 WSASend 가 실패해 SendIO 실패 분기를 탄다.

TEST_F(ClientSessionLiveTest, TryFlush_SendIOFails_ReturnsFalse)
{
    ASSERT_TRUE(ConnectSession());

    char msg[] = "x";
    ASSERT_TRUE(session.EnqueueOnly(1, msg));   // 큐 적재
    session.Closed();                           // 소켓 무효화 (IsConnected 는 true 유지)

    // IsConnected()==true → SendIO 진입 → WSASend(INVALID_SOCKET) 실패 → false
    EXPECT_FALSE(session.TryFlush());
    EXPECT_EQ(sendPool.GetFreeCount(), 64u);    // 실패 경로에서 패킷 반납됨
}

TEST_F(ClientSessionLiveTest, SendMsg_SendIOFails_StillReturnsTrue)
{
    ASSERT_TRUE(ConnectSession());
    session.Closed();                           // 소켓 무효화

    char msg[] = "x";
    // SendMsg 는 SendIO 실패 시 DisconnectAsync 만 호출하고 true 를 반환한다
    EXPECT_TRUE(session.SendMsg(1, msg));
    EXPECT_EQ(sendPool.GetFreeCount(), 64u);
}

// ── §10-8c OnConnect 중 BindRecv 실패 (무효 소켓) ────────────────────────────

TEST_F(ClientSessionLiveTest, OnConnect_InvalidSocket_BindRecvFails)
{
    // INVALID_SOCKET → WSARecv 실패 → BindRecv false → OnConnect false
    EXPECT_FALSE(session.OnConnect(iocp, INVALID_SOCKET));
}

// ── §10-9 PostImmediateAccept (WSASocket + AcceptEx) ─────────────────────────

TEST_F(ClientSessionLiveTest, PostImmediateAccept_PostsAcceptEx)
{
    EXPECT_TRUE(session.PostImmediateAccept(listenSock));
    EXPECT_NE(session.GetSocket(), INVALID_SOCKET);
    // pending AcceptEx 는 TearDown 의 Closed→drain 에서 정리됨
}

// ── §10-10 AcceptCompletion 전체 흐름 ────────────────────────────────────────
//  PostImmediateAccept → 클라이언트 connect → AcceptEx 완료 → AcceptCompletion

TEST_F(ClientSessionLiveTest, AcceptCompletion_FullFlow_Succeeds)
{
    ASSERT_TRUE(session.PostImmediateAccept(listenSock));

    // 클라이언트가 접속하여 AcceptEx 를 완료시킴
    peerSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ASSERT_NE(peerSock, INVALID_SOCKET);
    ASSERT_EQ(connect(peerSock, (sockaddr*)&addr, sizeof(addr)), 0);

    // AcceptEx 완료 패킷 수신 대기
    DWORD        bytes = 0;
    ULONG_PTR    key   = 0;
    LPOVERLAPPED ov    = nullptr;
    ASSERT_TRUE(GetQueuedCompletionStatus(iocp, &bytes, &key, &ov, 2000));
    ASSERT_NE(ov, (LPOVERLAPPED)nullptr);

    // 완료된 accept 컨텍스트로 AcceptCompletion 실행
    EXPECT_TRUE(session.AcceptCompletion(listenSock));
    EXPECT_TRUE(session.IsConnected());
}
