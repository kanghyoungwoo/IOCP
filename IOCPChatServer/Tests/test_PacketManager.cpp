// test_PacketManager.cpp
//
// RouteSingleUserTask의 while 루프 알고리즘 회귀 테스트.
//
// [왜 PacketManager.cpp 를 직접 테스트하지 않는가]
//   PacketManager 는 MySQL · Redis · StrandProcessor 등 외부 의존성을 가진다.
//   CI 환경(GitHub Actions)에서 이 의존성들을 모두 빌드하면 테스트가 복잡해지므로,
//   핵심 알고리즘(while 루프)을 DrainUserPackets_Fixed 헬퍼로 추출하여
//   User 링버퍼 계층에서 직접 검증한다.
//
// [이 파일이 방어하는 버그]
//   Bug 1: while 루프에서 nextPacket.Generation 미설정
//          → 2번째 이후 패킷이 Generation=0 을 핸들러에 전달
//          → 세대 검증에 실패하거나 잘못된 연결에 패킷을 처리할 수 있음
//
//   Bug 2: while 루프에서 nextPacket.DataSize 미검사
//          → 비정상 크기 패킷을 첫 번째 패킷과 달리 검증 없이 통과
//          (GetPacket 내부 검증이 1차 방어선이지만 while 루프의 명시적 검사가 2차 방어)

#include <gtest/gtest.h>
#include "User.h"
#include "Packet.h"
#include <vector>
#include <cstring>

// ─── 헬퍼: 테스트용 최소 패킷 빌드 ──────────────────────────────────────────
static void BuildPacket(char* out, UINT16 packetId, UINT16 totalLen = PACKET_HEADER_LENGTH)
{
    memset(out, 0, totalLen);
    auto* hdr        = reinterpret_cast<PACKET_HEADER*>(out);
    hdr->PacketLength = totalLen;
    hdr->PacketId     = packetId;
    hdr->PacketType   = 0;
}

// ─── RouteSingleUserTask while 루프 재현 (수정 후 버전) ─────────────────────
// PacketManager 없이 알고리즘만 테스트하기 위한 헬퍼.
// PacketManager::RouteSingleUserTask 의 while 루프와 동일한 로직을 적용한다.
struct SimPacketTask { UINT32 clientIndex; UINT32 generation; };

static std::vector<PacketInfo> DrainUserPackets_Fixed(User& user, const SimPacketTask& task)
{
    std::vector<PacketInfo> out;
    char buf[MAX_SINGLE_PACKET_SIZE];

    // ── 첫 번째 패킷 ─────────────────────────────────────────────────────────
    auto first = user.GetPacket(buf, sizeof(buf));
    if (first.PacketId == 0)
        return out;

    first.ClientIndex = task.clientIndex;
    first.Generation  = task.generation;   // Fix 1 적용
    out.push_back(first);

    // ── 후속 패킷 while 루프 ─────────────────────────────────────────────────
    // 수정 전 버그:
    //   next.Generation 이 0 인 채로 핸들러에 전달됨 (Bug 1)
    //   DataSize 검사 없음 (Bug 2)
    while (true)
    {
        auto next = user.GetPacket(buf, sizeof(buf));
        if (next.PacketId == 0)
            break;

        next.ClientIndex = task.clientIndex;
        next.Generation  = task.generation;  // Fix 1: task 의 Generation 을 명시적으로 설정

        if (next.DataSize == 0 || next.DataSize > MAX_SINGLE_PACKET_SIZE)  // Fix 2: DataSize 검사
            break;

        out.push_back(next);
    }
    return out;
}

// ─── Fixture ─────────────────────────────────────────────────────────────────
class RouteSingleUserTaskTest : public ::testing::Test
{
protected:
    static constexpr UINT32 kClientIndex = 7;
    static constexpr UINT32 kGeneration  = 42;

    void SetUp() override
    {
        mUser.Init(kClientIndex);
        mUser.SetSessionGeneration(kGeneration);
    }

    void PushPacket(UINT16 packetId, UINT16 totalLen = PACKET_HEADER_LENGTH)
    {
        char raw[MAX_SINGLE_PACKET_SIZE] = {};
        BuildPacket(raw, packetId, totalLen);
        ASSERT_TRUE(mUser.SetPacketData(totalLen, raw));
    }

    User mUser;
};

// ─── Bug 1 문서화 ─────────────────────────────────────────────────────────────
// GetPacket() 은 PacketId / DataSize / pDataPtr 만 채운다.
// Generation 은 항상 0 — 호출자(RouteSingleUserTask)가 반드시 직접 설정해야 한다.
// while 루프에서 next.Generation = task.generation 을 빠뜨리면
// 2번째 이후 패킷은 Generation=0 을 핸들러에 전달한다.
TEST_F(RouteSingleUserTaskTest, GetPacket_NeverSetsGeneration_CallerMustOverride)
{
    PushPacket((UINT16)PACKET_ID::SYS_PONG);
    PushPacket((UINT16)PACKET_ID::SYS_PONG);

    char buf[MAX_SINGLE_PACKET_SIZE];
    auto first  = mUser.GetPacket(buf, sizeof(buf));
    auto second = mUser.GetPacket(buf, sizeof(buf));

    EXPECT_EQ(first.Generation,  0u)
        << "GetPacket 은 Generation 을 채우지 않는다 (항상 0)";
    EXPECT_EQ(second.Generation, 0u)
        << "while 루프에서 Generation 을 설정하지 않으면 두 번째 패킷도 0 이 됨 — Bug 1";
}

// ─── Bug 1 회귀 테스트 ───────────────────────────────────────────────────────
// 수정 후: 링버퍼에 넣은 모든 패킷이 task.generation 을 받아야 한다.
TEST_F(RouteSingleUserTaskTest, MultiPacket_AllPacketsReceiveTaskGeneration)
{
    constexpr int PACKET_COUNT = 3;
    for (int i = 0; i < PACKET_COUNT; ++i)
        PushPacket((UINT16)PACKET_ID::SYS_PONG);

    SimPacketTask task{kClientIndex, kGeneration};
    auto dispatched = DrainUserPackets_Fixed(mUser, task);

    ASSERT_EQ(dispatched.size(), (size_t)PACKET_COUNT)
        << "링버퍼에 넣은 패킷 " << PACKET_COUNT << "개 모두 처리됐어야 함";

    for (size_t i = 0; i < dispatched.size(); ++i)
    {
        EXPECT_EQ(dispatched[i].Generation, kGeneration)
            << "패킷[" << i << "] Generation 이 task.generation(" << kGeneration
            << ")이어야 함 — 0 이면 Bug 1 재현";
        EXPECT_EQ(dispatched[i].ClientIndex, kClientIndex);
    }
}

TEST_F(RouteSingleUserTaskTest, SinglePacket_GenerationAndClientIndexSet)
{
    PushPacket((UINT16)PACKET_ID::LOGIN_REQUEST,
               static_cast<UINT16>(sizeof(LOGIN_REQUEST_PACKET)));

    SimPacketTask task{kClientIndex, kGeneration};
    auto dispatched = DrainUserPackets_Fixed(mUser, task);

    ASSERT_EQ(dispatched.size(), 1u);
    EXPECT_EQ(dispatched[0].PacketId,    (UINT16)PACKET_ID::LOGIN_REQUEST);
    EXPECT_EQ(dispatched[0].Generation,  kGeneration);
    EXPECT_EQ(dispatched[0].ClientIndex, kClientIndex);
}

// ─── 서로 다른 패킷 ID 가 순서대로 읽히는지 확인 ────────────────────────────
TEST_F(RouteSingleUserTaskTest, MultiPacket_PacketIdsPreservedInOrder)
{
    PushPacket((UINT16)PACKET_ID::LOGIN_REQUEST,
               static_cast<UINT16>(sizeof(LOGIN_REQUEST_PACKET)));
    PushPacket((UINT16)PACKET_ID::ROOM_ENTER_REQUEST,
               static_cast<UINT16>(sizeof(ROOM_ENTER_REQUEST_PACKET)));

    SimPacketTask task{kClientIndex, kGeneration};
    auto dispatched = DrainUserPackets_Fixed(mUser, task);

    ASSERT_EQ(dispatched.size(), 2u);
    EXPECT_EQ(dispatched[0].PacketId, (UINT16)PACKET_ID::LOGIN_REQUEST);
    EXPECT_EQ(dispatched[1].PacketId, (UINT16)PACKET_ID::ROOM_ENTER_REQUEST);

    // 두 패킷 모두 동일한 Generation 을 가져야 한다
    EXPECT_EQ(dispatched[0].Generation, kGeneration);
    EXPECT_EQ(dispatched[1].Generation, kGeneration);
}

// ─── Bug 2 문서화 ────────────────────────────────────────────────────────────
// GetPacket 은 PacketLength 가 유효 범위를 벗어나면 내부에서 버퍼를 초기화하고
// PacketId=0 인 빈 PacketInfo 를 반환한다.
// while 루프의 DataSize 검사는 GetPacket 이 변경될 경우를 대비한 2차 방어선이다.
TEST_F(RouteSingleUserTaskTest, GetPacket_TooShortPacketLength_ReturnsEmpty)
{
    // PacketLength=2 < PACKET_HEADER_LENGTH(5) → GetPacket 이 버퍼 초기화 후 PacketId=0 반환
    char raw[MAX_SINGLE_PACKET_SIZE] = {};
    auto* hdr         = reinterpret_cast<PACKET_HEADER*>(raw);
    hdr->PacketLength  = 2;
    hdr->PacketId      = (UINT16)PACKET_ID::SYS_PONG;
    hdr->PacketType    = 0;
    mUser.SetPacketData(2, raw);

    char buf[MAX_SINGLE_PACKET_SIZE];
    auto pkt = mUser.GetPacket(buf, sizeof(buf));

    EXPECT_EQ(pkt.PacketId, 0u)
        << "잘못된 PacketLength 는 GetPacket 내부에서 이미 차단된다 (PacketId=0)";
}

// ─── 엣지 케이스: 빈 큐 ───────────────────────────────────────────────────────
TEST_F(RouteSingleUserTaskTest, EmptyQueue_ReturnsNoPackets)
{
    SimPacketTask task{kClientIndex, kGeneration};
    auto dispatched = DrainUserPackets_Fixed(mUser, task);
    EXPECT_TRUE(dispatched.empty())
        << "링버퍼가 비어있으면 아무 패킷도 처리되지 않아야 함";
}
