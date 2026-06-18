// globalqueue_mpmc_test.cpp
//
// GlobalQueue_LockFree (Vyukov bounded MPMC ring) Relacy 검증
//   ※ 이 알고리즘도 Vyukov MPMC 정전 예제다.
//
//  컴파일 플래그
//    (없음)         → 정상: 2 Producer + 2 Consumer, Cap=4 → 무손실·무중복 (통과)
//    /DWEAKEN_PUB   → cell.sequence.store relaxed (producer)  → cell.data DATA RACE
//    /DWRAP         → 1P + 1C, Cap=2, push 3회 (wrap)         → 시퀀스 재활용 FIFO 검증
//
//  검증 항목
//    4-a 무손실·무중복 : 2P가 각 1아이템 push, 2C가 각 1개씩 정확히 수신 (합산 무손실/무중복)
//    4-b 발행 release  : producer cell.data write → cell.sequence.store(release).
//                        consumer cell.sequence.load(acquire) → cell.data read.
//                        누락 시 consumer의 data read 가 producer write 와 race
//    4-c 시퀀스 정확성 : Cap=2 에 3아이템 push → 슬롯 재활용(pos+mask+1) 정확성, FIFO 보존
//
//  모델 범위 (정직한 한계)
//    실제 코드의 std::counting_semaphore (블로킹/기상)는 모델에서 제외한다.
//    세마포어는 lock-free 정확성의 일부가 아니라 대기 메커니즘이므로,
//    Pop()을 "비면 즉시 -1 반환하는 try-pop"으로 환원해 순수 MPMC 링만 검증한다.
//    False Sharing(alignas)·Overflow Fail-Fast 도 Relacy 검증 대상이 아니다.
//
#include "relacy_lib/relacy/relacy.hpp"
#include <cstdint>

#if defined(WRAP)
static constexpr int      THREADS = 2;   // 1P + 1C
static constexpr uint32_t CAP     = 2;   // 슬롯 2개 → push 3회 시 wrap
static constexpr int      N       = 3;
#else
static constexpr int      THREADS = 4;   // 2P + 2C
static constexpr uint32_t CAP     = 4;
#endif
static constexpr uint32_t MASK = CAP - 1;

struct Cell
{
    rl::atomic<int32_t> sequence;
    rl::var<int>        data;            // payload — non-atomic (race 추적 대상)
};

struct MPMCTest : rl::test_suite<MPMCTest, THREADS>
{
    Cell                 buffer[CAP];
    rl::atomic<uint32_t> enqueuePos;
    rl::atomic<uint32_t> dequeuePos;

#if defined(WRAP)
    int received[N];
#else
    int got2;   // consumer(thread 2)
    int got3;   // consumer(thread 3)
#endif

    void before()
    {
        for (uint32_t i = 0; i < CAP; ++i)
        {
            buffer[i].sequence($).store((int32_t)i, rl::mo_relaxed);
            buffer[i].data($) = 0;
        }
        enqueuePos($).store(0, rl::mo_relaxed);
        dequeuePos($).store(0, rl::mo_relaxed);
#if defined(WRAP)
        for (int i = 0; i < N; ++i) received[i] = 0;
#else
        got2 = got3 = 0;
#endif
    }

    // ── Producer (MPMC) ────────────────────────────────────────────────────
    void Push(int value)
    {
        uint32_t pos = enqueuePos($).load(rl::mo_relaxed);
        Cell* cell = nullptr;
        while (true)
        {
            cell = &buffer[pos & MASK];
            int32_t seq  = cell->sequence($).load(rl::mo_acquire);
            int32_t diff = seq - (int32_t)pos;

            if (diff == 0)
            {
                if (enqueuePos($).compare_exchange_weak(
                        pos, pos + 1, rl::mo_relaxed, rl::mo_relaxed))
                    break;
            }
            else if (diff < 0)
            {
#if defined(WRAP)
                // 슬롯이 아직 미소비 상태 → consumer가 비울 때까지 대기.
                // (프로덕션은 MaxRoomCount<BufferSize 불변식으로 이 분기 자체가 불가능,
                //  여기서는 wrap 산술 검증을 위해 producer가 자리를 기다리도록 모델링)
                pos = enqueuePos($).load(rl::mo_relaxed);
#else
                RL_ASSERT(false);   // overflow — 발생 불가 (프로덕션 불변식)
#endif
            }
            else
            {
                pos = enqueuePos($).load(rl::mo_relaxed);
            }
        }
        cell->data($) = value;                          // payload write
#ifdef WEAKEN_PUB
        cell->sequence($).store((int32_t)(pos + 1), rl::mo_relaxed);  // ★ BUG: 발행 누락
#else
        cell->sequence($).store((int32_t)(pos + 1), rl::mo_release);
#endif
    }

    // ── Consumer (MPMC) — try-pop, 비면 -1 ─────────────────────────────────
    int Pop()
    {
        uint32_t pos = dequeuePos($).load(rl::mo_relaxed);
        Cell* cell = nullptr;
        while (true)
        {
            cell = &buffer[pos & MASK];
            int32_t seq  = cell->sequence($).load(rl::mo_acquire);
            int32_t diff = seq - (int32_t)(pos + 1);

            if (diff == 0)
            {
                if (dequeuePos($).compare_exchange_weak(
                        pos, pos + 1, rl::mo_relaxed, rl::mo_relaxed))
                    break;
            }
            else if (diff < 0)
            {
                return -1;          // 비어있음 (세마포어 모델 제외 → 즉시 반환)
            }
            else
            {
                pos = dequeuePos($).load(rl::mo_relaxed);
            }
        }
        int data = cell->data($);                        // payload read
        cell->sequence($).store((int32_t)(pos + MASK + 1), rl::mo_release);  // 슬롯 재활용
        return data;
    }

    int PopBlocking()
    {
        int v;
        do { v = Pop(); } while (v == -1);
        return v;
    }

    void thread(unsigned idx)
    {
#if defined(WRAP)
        // 1P + 1C, Cap=2, push 3 (wrap) → 시퀀스 재활용 FIFO 검증
        if (idx == 0)
        {
            for (int v = 1; v <= N; ++v)
                Push(v);
        }
        else
        {
            for (int i = 0; i < N; ++i)
                received[i] = PopBlocking();
        }
#else
        // 2P + 2C 무손실·무중복 검증
        if (idx == 0)       Push(100);
        else if (idx == 1)  Push(200);
        else if (idx == 2)  got2 = PopBlocking();
        else                got3 = PopBlocking();
#endif
    }

    void after()
    {
#if defined(WRAP)
        for (int i = 0; i < N; ++i)
            RL_ASSERT(received[i] == i + 1);            // FIFO 1,2,3
#else
        // 무손실(둘 다 존재) + 무중복(서로 다름)
        RL_ASSERT((got2 == 100 && got3 == 200) ||
                  (got2 == 200 && got3 == 100));
#endif
    }
};

int main()
{
    rl::test_params p;
    p.iteration_count = 3000000;
    rl::simulate<MPMCTest>(p);
    return 0;
}
