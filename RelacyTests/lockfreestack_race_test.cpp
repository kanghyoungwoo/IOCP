// lockfreestack_race_test.cpp
//
// LockFreeStack의 poolNext Data Race 재현 및 검증 (Relacy Race Detector)
//
//  컴파일 플래그
//    /DRACE_BUG 있음 → rl::var<uint32_t> poolNext (non-atomic) → DATA RACE 검출 예상
//    /DRACE_BUG 없음 → rl::atomic<uint32_t> poolNext (atomic)  → 정상 종료 예상
//
//  시나리오 (1b / 2b)
//    T0: PopBatch(2) — 스택 체인을 순회하며 poolNext 투기적 읽기
//    T1: Pop()       — 헤드 노드 획득 (poolNext 읽기)
//        Push(got)   — 같은 노드를 즉시 돌려놓음 (poolNext 쓰기)
//
//  재현되는 레이스
//    T0 가 pool[X].poolNext 를 읽는 도중
//    T1 의 Push 가 pool[X].poolNext 를 씀
//    → 세대(generation) CAS 가 실패를 정확히 감지해도
//      투기적 읽기 자체가 C++ UB(Data Race)
//
#include "relacy_lib/relacy/relacy.hpp"
#include <cstdint>

// ── 상수 ─────────────────────────────────────────────────────────────────────
static constexpr uint32_t NULL_IDX = UINT32_MAX;
static constexpr uint32_t POOL_CAP = 4;   // 상태공간 제한 — 작게 유지

// ── Tagged pointer (index | generation<<32) ───────────────────────────────────
static inline uint64_t Pack(uint32_t idx, uint32_t gen) noexcept
{
    return static_cast<uint64_t>(idx) | (static_cast<uint64_t>(gen) << 32);
}
struct Tagged { uint32_t idx, gen; };
static inline Tagged Unpack(uint64_t v) noexcept
{
    return { static_cast<uint32_t>(v), static_cast<uint32_t>(v >> 32) };
}

// ── 노드 ─────────────────────────────────────────────────────────────────────
struct Node
{
#ifdef RACE_BUG
    rl::var<uint32_t>    poolNext;   // non-atomic → Relacy가 data race 탐지
#else
    rl::atomic<uint32_t> poolNext;   // atomic     → race 없음
#endif
};

// ── 테스트 수트 ───────────────────────────────────────────────────────────────
//  스레드 2개 (T0=PopBatch, T1=Pop→Push)
//  expected result: RACE_BUG=data_race / 수정=success (default)
struct PopBatchRaceTest : rl::test_suite<PopBatchRaceTest, 2>
{
    rl::atomic<uint64_t> m_head;
    Node                 pool[POOL_CAP];

    // ── 초기화: 0→1→2→3→NULL, head=(idx=0, gen=0) ─────────────────────────
    void before()
    {
        for (uint32_t i = 0; i < POOL_CAP - 1; ++i)
        {
#ifdef RACE_BUG
            pool[i].poolNext($) = i + 1;
#else
            pool[i].poolNext($).store(i + 1, rl::mo_relaxed);
#endif
        }
#ifdef RACE_BUG
        pool[POOL_CAP - 1].poolNext($) = NULL_IDX;
#else
        pool[POOL_CAP - 1].poolNext($).store(NULL_IDX, rl::mo_relaxed);
#endif
        m_head($).store(Pack(0, 0), rl::mo_relaxed);
    }

    // ── Push: poolNext 쓰기 → CAS ──────────────────────────────────────────
    void Push(uint32_t objIdx)
    {
        uint64_t oldHead = m_head($).load(rl::mo_relaxed);
        while (true)
        {
            Tagged old = Unpack(oldHead);

#ifdef RACE_BUG
            pool[objIdx].poolNext($) = old.idx;                      // ★ non-atomic 쓰기
#else
            pool[objIdx].poolNext($).store(old.idx, rl::mo_relaxed);
#endif
            uint64_t newHead = Pack(objIdx, old.gen + 1);
            if (m_head($).compare_exchange_weak(
                    oldHead, newHead, rl::mo_release, rl::mo_relaxed))
                return;
        }
    }

    // ── Pop: poolNext 투기적 읽기 → CAS ────────────────────────────────────
    uint32_t Pop()
    {
        uint64_t oldHead = m_head($).load(rl::mo_relaxed);
        while (true)
        {
            Tagged old = Unpack(oldHead);
            if (old.idx == NULL_IDX) return NULL_IDX;

#ifdef RACE_BUG
            uint32_t nextIdx = pool[old.idx].poolNext($);            // non-atomic 읽기
#else
            uint32_t nextIdx = pool[old.idx].poolNext($).load(rl::mo_relaxed);
#endif
            uint64_t newHead = Pack(nextIdx, old.gen + 1);
            if (m_head($).compare_exchange_weak(
                    oldHead, newHead, rl::mo_acquire, rl::mo_relaxed))
                return old.idx;
        }
    }

    // ── PopBatch: 체인 순회 → CAS ──────────────────────────────────────────
    // ★ BUG 포인트: pool[curIdx].poolNext 를 non-atomic 으로 읽으며 순회
    //    이 읽기가 진행 중일 때 다른 스레드가 같은 노드의 poolNext 를 쓰면
    //    C++ Data Race (UB) — 세대 CAS 가 올바르게 실패해도 UB 는 이미 발생
    uint32_t PopBatch(uint32_t count)
    {
        uint64_t oldHead = m_head($).load(rl::mo_relaxed);
        while (true)
        {
            Tagged old = Unpack(oldHead);
            if (old.idx == NULL_IDX) return 0;

            uint32_t curIdx = old.idx;
            uint32_t actual = 0;
            for (uint32_t i = 0; i < count; ++i)
            {
                if (curIdx == NULL_IDX || curIdx >= POOL_CAP) break;
                ++actual;
#ifdef RACE_BUG
                curIdx = pool[curIdx].poolNext($);     // ★ non-atomic 순회 ← THE BUG
#else
                curIdx = pool[curIdx].poolNext($).load(rl::mo_relaxed);
#endif
            }
            if (actual == 0) return 0;

            uint64_t newHead = Pack(curIdx, old.gen + 1);
            if (m_head($).compare_exchange_weak(
                    oldHead, newHead, rl::mo_acquire, rl::mo_relaxed))
                return actual;
        }
    }

    // ── 스레드 진입점 ─────────────────────────────────────────────────────
    void thread(unsigned idx)
    {
        if (idx == 0)
        {
            // T0: PopBatch — 2노드 순회, poolNext 투기적 읽기
            PopBatch(2);
        }
        else
        {
            // T1: Pop → Push — 노드를 꺼냈다 즉시 돌려놓음
            //     Push 내부에서 pool[got].poolNext 를 새로 씀 ← T0 와 race
            uint32_t got = Pop();
            if (got != NULL_IDX)
                Push(got);
        }
    }
};

// ── main ─────────────────────────────────────────────────────────────────────
int main()
{
    rl::test_params p;
    p.iteration_count = 200000;   // 충분한 스케줄 탐색
    rl::simulate<PopBatchRaceTest>(p);
    return 0;
}
