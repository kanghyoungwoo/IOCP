#pragma once
#include <atomic>
#include <cstdint>

// ============================================================
//  SPSCQueue - Single Producer / Single Consumer 링버퍼 큐
//
//  [사용 전제]
//   - Producer : 반드시 1개 스레드만 Push() 호출 (PacketManager)
//   - Consumer : 반드시 1개 스레드만 Pop()  호출 (Strand Logic Worker)
//   - CAPACITY : 반드시 2의 거듭제곱 (비트마스크 연산 사용)
//
//  [MPSCQueue 대비 이점]
//   - Hole 상태 구조적으로 불가능 → PopWithBackoff 불필요
//   - exchange 없이 store 1회로 Push 완료
//   - 연속 배열 → 포인터 체인보다 캐시 친화적
// ============================================================

template<typename T, uint32_t CAPACITY>
class SPSCQueue
{
    static_assert((CAPACITY & (CAPACITY - 1)) == 0, "CAPACITY must be power of 2");
    static_assert(CAPACITY >= 2, "CAPACITY must be at least 2");

    static constexpr uint32_t MASK = CAPACITY - 1;

    // 링버퍼 본체 (포인터 배열)
    // CAPACITY - 1 개까지 저장 가능 (full/empty 구분을 위해 슬롯 1개 예약)
    T* m_buffer[CAPACITY];

    // ── Producer 전용 캐시라인 ───────────────────────────────────────────
    //    m_readIndex와 같은 캐시라인에 있으면, Producer가 m_writeIndex를
    //    업데이트할 때마다 Consumer 측 캐시라인이 무효화됨 (False Sharing)
    //    alignas(64)로 별도 캐시라인에 배치하여 방지
    alignas(64) std::atomic<uint32_t> m_writeIndex{ 0 };

    // ── Consumer 전용 캐시라인 ───────────────────────────────────────────
    //    위와 동일한 이유로 alignas(64) 적용
    alignas(64) std::atomic<uint32_t> m_readIndex{ 0 };

public:
    SPSCQueue()
    {
        for (uint32_t i = 0; i < CAPACITY; ++i)
            m_buffer[i] = nullptr;
    }

    // 복사/이동 금지
    SPSCQueue(const SPSCQueue&) = delete;
    SPSCQueue& operator=(const SPSCQueue&) = delete;

    // ----------------------------------------------------------------
    //  Push (Producer 전용)
    //  @return true: 성공 / false: 큐 가득 참
    //
    //  Memory Order:
    //   1. m_writeIndex.load(relaxed)
    //      Producer만 이 값을 수정하므로 동기화 불필요
    //
    //   2. m_readIndex.load(acquire)
    //      Consumer의 m_readIndex.store(release) 와 동기화
    //      → "소비자가 이미 비운 슬롯" 이 이 시점에서 보임
    //      → full 여부 판단에 필요한 최신 readIndex를 안전하게 읽음
    //
    //   3. m_buffer[write] = node  (일반 쓰기)
    //      인덱스 업데이트(release) 이전에 반드시 완료됨이 보장됨
    //
    //   4. m_writeIndex.store(release)
    //      이 store 이전의 모든 쓰기(m_buffer에 node 저장)가
    //      Consumer의 acquire 시점에 반드시 보이도록 보장
    // ----------------------------------------------------------------
    bool Push(T* node)
    {
        const uint32_t write = m_writeIndex.load(std::memory_order_relaxed);
        const uint32_t next  = (write + 1) & MASK;

        // full 체크: next가 readIndex에 따라붙으면 꽉 찬 상태
        if (next == m_readIndex.load(std::memory_order_acquire))
            return false;

        m_buffer[write] = node;

        // [발행 포인트] 데이터 쓰기 이후에 인덱스를 올림 (release)
        m_writeIndex.store(next, std::memory_order_release);
        return true;
    }

    // ----------------------------------------------------------------
    //  Pop (Consumer 전용)
    //  @return T*: 성공 / nullptr: 큐 비어있음
    //
    //  Memory Order:
    //   1. m_readIndex.load(relaxed)
    //      Consumer만 이 값을 수정하므로 동기화 불필요
    //
    //   2. m_writeIndex.load(acquire)
    //      Producer의 m_writeIndex.store(release) 와 동기화
    //      → "생산자가 buffer에 쓴 데이터" 가 이 시점에서 보임
    //      → m_buffer[read]를 읽기 전에 Producer의 쓰기가 완료됐음을 보장
    //
    //   3. m_buffer[read] 읽기 (일반 읽기)
    //      위 acquire 덕분에 안전하게 접근 가능
    //
    //   4. m_readIndex.store(release)
    //      "이 슬롯을 다 썼다"는 사실을 Producer에게 알림
    //      → Producer의 m_readIndex.load(acquire) 와 동기화
    //      → Producer가 이 슬롯을 재사용해도 된다고 판단할 수 있음
    // ----------------------------------------------------------------
    T* Pop()
    {
        const uint32_t read = m_readIndex.load(std::memory_order_relaxed);

        // empty 체크: readIndex가 writeIndex에 따라붙으면 빈 상태
        if (read == m_writeIndex.load(std::memory_order_acquire))
            return nullptr;

        T* node = m_buffer[read];

        // [소비 완료 발행] 슬롯 반납을 Producer에게 알림 (release)
        m_readIndex.store((read + 1) & MASK, std::memory_order_release);
        return node;
    }

    // ----------------------------------------------------------------
    //  유틸리티 (모니터링 용도, 멀티스레드 환경에서는 근사치)
    // ----------------------------------------------------------------
    bool IsEmpty() const
    {
        const uint32_t read  = m_readIndex.load(std::memory_order_relaxed);
        const uint32_t write = m_writeIndex.load(std::memory_order_acquire);
        return read == write;
    }

    // 현재 저장된 원소 수 (스냅샷, 정확한 값이 아닐 수 있음)
    uint32_t Size() const
    {
        const uint32_t write = m_writeIndex.load(std::memory_order_relaxed);
        const uint32_t read  = m_readIndex.load(std::memory_order_relaxed);
        return (write - read) & MASK;
    }

    static constexpr uint32_t Capacity() { return CAPACITY; }
};
