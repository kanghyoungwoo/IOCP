// mpsc_queue_test.cpp
//
// MPSCQueue (Vyukov intrusive MPSC) Relacy 검증
//   ※ 이 알고리즘은 Relacy 제작자(Dmitry Vyukov)의 정전(正典) 예제다.
//
//  컴파일 플래그
//    (없음)         → 정상: 2 Producer + 1 Consumer       → 무손실·무중복 (통과)
//    /DWEAKEN_LINK  → prev->mpscNext.store relaxed         → payload DATA RACE
//    /DVIOLATE_SC   → 1 Producer + 2 Consumer (불변식 위반) → m_head DATA RACE
//
//  검증 항목
//    3-a 무손실·무중복 : 2P가 각 1노드 push, 1C가 둘 다 정확히 1회 수신
//    3-b Hole 처리     : exchange와 link.store 사이 선점 인터리빙 → C가 nullptr 재시도
//                        (3-a 시나리오에 자연 포함; Relacy가 해당 인터리빙 탐색)
//    3-c 링크 release  : node payload write → Push, Pop 후 payload read.
//                        link.store(release) ↔ next.load(acquire) 가 payload 가시성 보장.
//                        누락 시 C의 payload read 가 P의 write 와 race
//    3-d 단일 C 불변식 : 코드 주석이 "다중 Consumer = data race"라 명시한 가정을
//                        실제로 재현. m_head 가 non-atomic이므로 두 C가 동시 접근 시 race
//
#include "relacy_lib/relacy/relacy.hpp"

struct Node
{
    rl::atomic<Node*> mpscNext;
    rl::var<int>      payload;
};

#ifdef VIOLATE_SC
static constexpr int THREADS = 3;   // 1P + 2C
#else
static constexpr int THREADS = 3;   // 2P + 1C
#endif

struct MPSCTest : rl::test_suite<MPSCTest, THREADS>
{
    Node              m_stub;
    Node              nodeA;
    Node              nodeB;

    rl::var<Node*>    m_head;        // Consumer 전용 — non-atomic (불변식: 단일 C)
    rl::atomic<Node*> m_tail;        // Producer들이 exchange

    bool              seenA;
    bool              seenB;

    void before()
    {
        m_stub.mpscNext($).store(nullptr, rl::mo_relaxed);
        m_stub.payload($) = 0;
        nodeA.mpscNext($).store(nullptr, rl::mo_relaxed);
        nodeA.payload($) = 0;
        nodeB.mpscNext($).store(nullptr, rl::mo_relaxed);
        nodeB.payload($) = 0;

        m_head($)  = &m_stub;
        m_tail($).store(&m_stub, rl::mo_relaxed);

        seenA = false;
        seenB = false;
    }

    // ── Producer (다중 호출 안전) ──────────────────────────────────────────
    void Push(Node* node)
    {
        node->mpscNext($).store(nullptr, rl::mo_relaxed);
        Node* prev = m_tail($).exchange(node, rl::mo_acq_rel);
        // ↑↓ 사이가 "Hole" 구간 — 여기서 선점되면 Consumer는 일시적으로 nullptr 수신
#ifdef WEAKEN_LINK
        prev->mpscNext($).store(node, rl::mo_relaxed);   // ★ BUG: link 발행(release) 누락
#else
        prev->mpscNext($).store(node, rl::mo_release);
#endif
    }

    // ── Consumer (단일 호출 전제) ──────────────────────────────────────────
    Node* Pop()
    {
        Node* head = m_head($);                              // ★ non-atomic load
        Node* next = head->mpscNext($).load(rl::mo_acquire);

        if (head == &m_stub)
        {
            if (next == nullptr) return nullptr;
            m_head($) = next;                               // ★ non-atomic store
            head = next;
            next = head->mpscNext($).load(rl::mo_acquire);
        }
        if (next != nullptr)
        {
            m_head($) = next;                               // ★ non-atomic store
            return head;
        }
        Node* tail = m_tail($).load(rl::mo_acquire);
        if (head != tail)
            return nullptr;                                 // Hole — 재시도 유도
        Push(&m_stub);                                      // stub 재삽입
        next = head->mpscNext($).load(rl::mo_acquire);
        if (next != nullptr)
        {
            m_head($) = next;
            return head;
        }
        return nullptr;
    }

    void Consume(Node* n)
    {
        int p = n->payload($);                              // payload read
        RL_ASSERT(p == 100 || p == 200);
        if (p == 100) { RL_ASSERT(!seenA); seenA = true; }
        else          { RL_ASSERT(!seenB); seenB = true; }
    }

    void thread(unsigned idx)
    {
#ifdef VIOLATE_SC
        // 1P + 2C : 단일 Consumer 불변식 위반 → m_head race 재현
        if (idx == 0)
        {
            nodeA.payload($) = 100; Push(&nodeA);
            nodeB.payload($) = 200; Push(&nodeB);
        }
        else
        {
            Pop();   // 두 Consumer가 m_head 를 동시 접근
            Pop();
        }
#else
        // 2P + 1C : 정상 동작 검증
        if (idx == 0)
        {
            nodeA.payload($) = 100;   // payload write (link.store가 발행)
            Push(&nodeA);
        }
        else if (idx == 1)
        {
            nodeB.payload($) = 200;
            Push(&nodeB);
        }
        else
        {
            int got = 0;
            while (got < 2)
            {
                Node* n = Pop();
                if (n != nullptr) { Consume(n); ++got; }   // Hole이면 nullptr → 재시도
            }
        }
#endif
    }

#ifndef VIOLATE_SC
    void after()
    {
        RL_ASSERT(seenA && seenB);   // 무손실·무중복
    }
#endif
};

int main()
{
    rl::test_params p;
    p.iteration_count = 2000000;
    rl::simulate<MPSCTest>(p);
    return 0;
}
