// spsc_queue_test.cpp
//
// SPSCQueue (포인터 링버퍼, Single-Producer/Single-Consumer) Relacy 검증
//
//  컴파일 플래그
//    (없음)            → 정상 버전              → race 0건 (통과 예상)
//    /DWEAKEN_PUB      → writeIndex.store relaxed → 발행 누락 → buffer DATA RACE
//    /DWEAKEN_RECLAIM  → readIndex.store relaxed  → 반납 누락 → buffer DATA RACE
//
//  시나리오 (1-a / 1-b / 1-c)
//    Capacity=2 (사용 가능 슬롯 1개) → push 3회 시 슬롯 0이 재사용됨
//    T0(Producer): 1,2,3 push
//    T1(Consumer): 3회 pop → FIFO(1,2,3) 검증
//
//  검증되는 메모리 오더
//    1-b: writeIndex.store(release) ↔ writeIndex.load(acquire)
//         → "producer가 buffer에 쓴 값"의 가시성. 누락 시 consumer read와 race
//    1-c: readIndex.store(release)  ↔ readIndex.load(acquire)
//         → "consumer가 슬롯을 다 읽었다"는 사실. 누락 시 producer의 슬롯 재사용 write와 race
//
#include "relacy_lib/relacy/relacy.hpp"
#include <cstdint>

static constexpr uint32_t CAP  = 2;          // 2의 거듭제곱, 사용 가능 슬롯 1개
static constexpr uint32_t MASK = CAP - 1;
static constexpr int      N    = 3;          // push 3회 → 슬롯 0 재사용 강제

struct SPSCTest : rl::test_suite<SPSCTest, 2>
{
    rl::var<int>          buffer[CAP];        // payload — non-atomic (race 추적 대상)
    rl::atomic<uint32_t>  writeIndex;
    rl::atomic<uint32_t>  readIndex;

    int                   popped[N];          // consumer가 수신한 값 (FIFO 검증용)

    void before()
    {
        writeIndex($).store(0, rl::mo_relaxed);
        readIndex($).store(0, rl::mo_relaxed);
        for (uint32_t i = 0; i < CAP; ++i)
            buffer[i]($) = 0;
        for (int i = 0; i < N; ++i)
            popped[i] = 0;
    }

    // ── Producer 전용 ──────────────────────────────────────────────────────
    bool Push(int v)
    {
        uint32_t w    = writeIndex($).load(rl::mo_relaxed);
        uint32_t next = (w + 1) & MASK;

        if (next == readIndex($).load(rl::mo_acquire))
            return false;                       // full

        buffer[w]($) = v;                       // payload write
#ifdef WEAKEN_PUB
        writeIndex($).store(next, rl::mo_relaxed);   // ★ BUG: 발행(release) 누락
#else
        writeIndex($).store(next, rl::mo_release);
#endif
        return true;
    }

    // ── Consumer 전용 ──────────────────────────────────────────────────────
    bool Pop(int& out)
    {
        uint32_t r = readIndex($).load(rl::mo_relaxed);

        if (r == writeIndex($).load(rl::mo_acquire))
            return false;                       // empty

        out = buffer[r]($);                     // payload read
#ifdef WEAKEN_RECLAIM
        readIndex($).store((r + 1) & MASK, rl::mo_relaxed);  // ★ BUG: 반납(release) 누락
#else
        readIndex($).store((r + 1) & MASK, rl::mo_release);
#endif
        return true;
    }

    void thread(unsigned idx)
    {
        if (idx == 0)
        {
            for (int v = 1; v <= N; ++v)
                while (!Push(v)) {}             // 가득 차면 재시도
        }
        else
        {
            for (int i = 0; i < N; ++i)
            {
                int v = 0;
                while (!Pop(v)) {}              // 비어있으면 재시도
                popped[i] = v;
            }
        }
    }

    void after()
    {
        // FIFO 무손실 검증: 1,2,3 순서 그대로 수신되어야 함
        for (int i = 0; i < N; ++i)
            RL_ASSERT(popped[i] == i + 1);
    }
};

int main()
{
    rl::test_params p;
    p.iteration_count = 1000000;
    rl::simulate<SPSCTest>(p);
    return 0;
}
