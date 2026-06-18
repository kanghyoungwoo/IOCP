// ringbuffer_test.cpp
//
// RingBuffer (바이트 링버퍼, Single-Producer/Single-Consumer) Relacy 검증
//
//  컴파일 플래그
//    (없음)            → 정상 버전                → race 0건 (통과 예상)
//    /DWEAKEN_PUB      → head.store relaxed       → 발행 누락 → buffer DATA RACE
//    /DWEAKEN_RECLAIM  → tail.store relaxed       → 반납 누락 → buffer DATA RACE
//
//  시나리오 (2-a / 2-b / 2-c)
//    BufferSize=2 → 3바이트 쓰기 시 슬롯 0이 재사용됨
//    T0(Producer): 'A','B','C' WriteByte
//    T1(Consumer): 3회 ReadByte → 'A','B','C' 순서 검증
//
//  한계 메모
//    실제 RingBuffer::Write/Read 는 memcpy 블록 전송이라 Relacy의 var 단위
//    추적이 불가능. 동일 happens-before 구조를 바이트 단위(WriteByte/ReadByte)로
//    환원해 검증한다. 메모리 오더 정확성 검증 목적은 동일하다.
//
#include "relacy_lib/relacy/relacy.hpp"
#include <cstddef>

static constexpr size_t BUFSZ = 2;           // 2의 거듭제곱
static constexpr size_t MASK  = BUFSZ - 1;
static constexpr int    N     = 3;           // 3바이트 → 슬롯 0 재사용 강제

struct RingBufferTest : rl::test_suite<RingBufferTest, 2>
{
    rl::var<char>        buffer[BUFSZ];       // payload — non-atomic (race 추적 대상)
    rl::atomic<size_t>   head;                // Producer만 변경
    rl::atomic<size_t>   tail;                // Consumer만 변경

    char                 received[N];

    void before()
    {
        head($).store(0, rl::mo_relaxed);
        tail($).store(0, rl::mo_relaxed);
        for (size_t i = 0; i < BUFSZ; ++i)
            buffer[i]($) = 0;
        for (int i = 0; i < N; ++i)
            received[i] = 0;
    }

    // ── Producer 전용 ──────────────────────────────────────────────────────
    bool WriteByte(char byte)
    {
        size_t h = head($).load(rl::mo_relaxed);
        size_t t = tail($).load(rl::mo_acquire);
        if (h - t == BUFSZ) return false;       // full

        buffer[h & MASK]($) = byte;             // payload write
#ifdef WEAKEN_PUB
        head($).store(h + 1, rl::mo_relaxed);   // ★ BUG: 발행(release) 누락
#else
        head($).store(h + 1, rl::mo_release);
#endif
        return true;
    }

    // ── Consumer 전용 ──────────────────────────────────────────────────────
    bool ReadByte(char& byte)
    {
        size_t t = tail($).load(rl::mo_relaxed);
        size_t h = head($).load(rl::mo_acquire);
        if (h == t) return false;               // empty

        byte = buffer[t & MASK]($);             // payload read
#ifdef WEAKEN_RECLAIM
        tail($).store(t + 1, rl::mo_relaxed);   // ★ BUG: 반납(release) 누락
#else
        tail($).store(t + 1, rl::mo_release);
#endif
        return true;
    }

    void thread(unsigned idx)
    {
        if (idx == 0)
        {
            const char* msg = "ABC";
            for (int i = 0; i < N; ++i)
                while (!WriteByte(msg[i])) {}    // 가득 차면 재시도
        }
        else
        {
            for (int i = 0; i < N; ++i)
            {
                char b = 0;
                while (!ReadByte(b)) {}          // 비어있으면 재시도
                received[i] = b;
            }
        }
    }

    void after()
    {
        // 바이트 FIFO 무손실 검증: 'A','B','C' 순서 그대로 수신
        RL_ASSERT(received[0] == 'A');
        RL_ASSERT(received[1] == 'B');
        RL_ASSERT(received[2] == 'C');
    }
};

int main()
{
    rl::test_params p;
    p.iteration_count = 1000000;
    rl::simulate<RingBufferTest>(p);
    return 0;
}
