#include <gtest/gtest.h>
#include "../SPSCQueue.h"
#include <thread>
#include <vector>
#include <atomic>

struct SPSCNode
{
    int value = 0;
};

// ============================================================
//  Single-threaded fixture  (CAPACITY = 8, max storable = 7)
// ============================================================
class SPSCQueueTest : public ::testing::Test
{
protected:
    static constexpr uint32_t CAP = 8;
    SPSCQueue<SPSCNode, CAP> queue;
    SPSCNode nodes[CAP];

    void SetUp() override
    {
        for (int i = 0; i < static_cast<int>(CAP); ++i)
            nodes[i].value = i * 10;
    }
};

// --- 1. 빈 큐에서 Pop -> nullptr --------------------------------
TEST_F(SPSCQueueTest, PopFromEmptyReturnsNull)
{
    EXPECT_EQ(queue.Pop(), nullptr);
    EXPECT_TRUE(queue.IsEmpty());
    EXPECT_EQ(queue.Size(), 0u);
}

// --- 2. Push 1개 Pop 1개 --------------------------------------
TEST_F(SPSCQueueTest, SinglePushPop)
{
    EXPECT_TRUE(queue.Push(&nodes[0]));

    SPSCNode* p = queue.Pop();
    EXPECT_NE(p, nullptr);
    EXPECT_EQ(p->value, 0);
    EXPECT_EQ(queue.Pop(), nullptr);
}

// --- 3. FIFO 순서 보장 ----------------------------------------
//  링버퍼 기반이므로 Push 순서 = Pop 순서 (MPSCQueue와 다름)
TEST_F(SPSCQueueTest, FIFOOrdering)
{
    for (int i = 0; i < 5; ++i)
        EXPECT_TRUE(queue.Push(&nodes[i]));

    for (int i = 0; i < 5; ++i)
    {
        SCOPED_TRACE(i);
        SPSCNode* p = queue.Pop();
        ASSERT_NE(p, nullptr);
        EXPECT_EQ(p->value, i * 10);
    }
    EXPECT_EQ(queue.Pop(), nullptr);
}

// --- 4. 꽉 찬 큐에서 Push -> false (슬롯 1개 예약) ------------
//  CAPACITY=8 이므로 최대 7개 저장 가능
TEST_F(SPSCQueueTest, FullQueueRejectsPush)
{
    constexpr int MAX_ITEMS = CAP - 1; // 7
    for (int i = 0; i < MAX_ITEMS; ++i)
        EXPECT_TRUE(queue.Push(&nodes[i]));

    EXPECT_EQ(queue.Size(), static_cast<uint32_t>(MAX_ITEMS));

    // 8번째 Push -> 꽉 참, false 반환
    SPSCNode extra;
    EXPECT_FALSE(queue.Push(&extra));

    // Pop 1개 후 다시 Push 가능
    SPSCNode* p = queue.Pop();
    ASSERT_NE(p, nullptr);
    EXPECT_TRUE(queue.Push(&extra));
}

// --- 5. IsEmpty / Size 정확성 ---------------------------------
TEST_F(SPSCQueueTest, IsEmptyAndSize)
{
    EXPECT_TRUE(queue.IsEmpty());
    EXPECT_EQ(queue.Size(), 0u);

    queue.Push(&nodes[0]);
    EXPECT_FALSE(queue.IsEmpty());
    EXPECT_EQ(queue.Size(), 1u);

    queue.Push(&nodes[1]);
    EXPECT_EQ(queue.Size(), 2u);

    queue.Pop();
    EXPECT_EQ(queue.Size(), 1u);

    queue.Pop();
    EXPECT_TRUE(queue.IsEmpty());
    EXPECT_EQ(queue.Size(), 0u);
}

// --- 6. drain 후 재사용 ----------------------------------------
TEST_F(SPSCQueueTest, PushAfterDrain)
{
    queue.Push(&nodes[0]);
    queue.Pop();
    EXPECT_EQ(queue.Pop(), nullptr);

    queue.Push(&nodes[1]);
    SPSCNode* p = queue.Pop();
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->value, 10);
}

// --- 7. 인덱스 wrap-around ------------------------------------
//  CAPACITY=8 경계를 넘어서 Push/Pop 반복 -> 인덱스 랩어라운드 검증
TEST_F(SPSCQueueTest, WrapAround)
{
    // CAPACITY(8)보다 많이 통과시켜 인덱스가 0으로 돌아오는지 확인
    constexpr int ROUNDS = 30;
    for (int i = 0; i < ROUNDS; ++i)
    {
        SCOPED_TRACE(i);
        SPSCNode n;
        n.value = i;
        EXPECT_TRUE(queue.Push(&n));

        SPSCNode* p = queue.Pop();
        ASSERT_NE(p, nullptr);
        EXPECT_EQ(p->value, i);
    }
    EXPECT_TRUE(queue.IsEmpty());
}

// --- 8. Push/Pop 교대 반복 사이클 -----------------------------
TEST_F(SPSCQueueTest, RapidPushPopCycles)
{
    for (int round = 0; round < 20; ++round)
    {
        for (int i = 0; i < 5; ++i)
        {
            nodes[i].value = round * 100 + i;
            EXPECT_TRUE(queue.Push(&nodes[i]));
        }
        for (int i = 0; i < 5; ++i)
        {
            SCOPED_TRACE(i);
            SPSCNode* p = queue.Pop();
            ASSERT_NE(p, nullptr);
            EXPECT_EQ(p->value, round * 100 + i);
        }
        EXPECT_EQ(queue.Pop(), nullptr);
    }
}

// ============================================================
//  Capacity 상수 확인
// ============================================================
TEST_F(SPSCQueueTest, CapacityConstant)
{
    EXPECT_EQ((SPSCQueue<SPSCNode, 8>::Capacity()), 8u);
    EXPECT_EQ((SPSCQueue<SPSCNode, 1024>::Capacity()), 1024u);
}

// ============================================================
//  동시성 테스트 : Single Producer / Single Consumer
// ============================================================

// --- 9. 기본 SPSC: 데이터 손실 없음, 순서 보장 ---------------
TEST(SPSCQueueConcurrent, SingleProducerSingleConsumer)
{
    constexpr int  TOTAL = 10000;
    constexpr uint32_t CAP = 1024;

    std::vector<SPSCNode> pool(TOTAL);
    for (int i = 0; i < TOTAL; ++i)
        pool[i].value = i;

    SPSCQueue<SPSCNode, CAP> queue;
    std::vector<int> consumed;
    consumed.reserve(TOTAL);

    std::atomic<bool> producerDone{ false };

    std::thread producer([&]()
    {
        for (int i = 0; i < TOTAL; ++i)
        {
            // 큐가 꽉 차면 빌 때까지 대기 (backpressure)
            while (!queue.Push(&pool[i]))
                std::this_thread::yield();
        }
        producerDone.store(true, std::memory_order_release);
    });

    // Consumer (메인 스레드)
    while (consumed.size() < static_cast<size_t>(TOTAL))
    {
        SPSCNode* p = queue.Pop();
        if (p)
            consumed.push_back(p->value);
        else if (producerDone.load(std::memory_order_acquire))
            std::this_thread::yield();
    }

    producer.join();

    // 총 개수 검증
    EXPECT_EQ(consumed.size(), static_cast<size_t>(TOTAL));

    // FIFO 순서 검증 (링버퍼 기반 SPSC는 순서를 보장)
    for (int i = 0; i < TOTAL; ++i)
    {
        SCOPED_TRACE(i);
        EXPECT_EQ(consumed[i], i);
    }
}

// --- 10. 고처리량: 중복/누락 없음 검증 -----------------------
TEST(SPSCQueueConcurrent, HighThroughputNoDuplicate)
{
    constexpr int  TOTAL = 100000;
    constexpr uint32_t CAP = 4096;

    std::vector<SPSCNode> pool(TOTAL);
    for (int i = 0; i < TOTAL; ++i)
        pool[i].value = i;

    SPSCQueue<SPSCNode, CAP> queue;
    std::atomic<int> consumedCount{ 0 };
    std::vector<bool> seen(TOTAL, false);
    std::atomic<bool> producerDone{ false };

    std::thread producer([&]()
    {
        for (int i = 0; i < TOTAL; ++i)
        {
            while (!queue.Push(&pool[i]))
                std::this_thread::yield();
        }
        producerDone.store(true, std::memory_order_release);
    });

    // Consumer
    while (consumedCount.load(std::memory_order_relaxed) < TOTAL)
    {
        SPSCNode* p = queue.Pop();
        if (p)
        {
            seen[p->value] = true;
            consumedCount.fetch_add(1, std::memory_order_relaxed);
        }
        else if (producerDone.load(std::memory_order_acquire))
            std::this_thread::yield();
    }

    producer.join();

    EXPECT_EQ(consumedCount.load(), TOTAL);

    // 중복/누락 없음 검증
    int missing = 0;
    for (int i = 0; i < TOTAL; ++i)
        if (!seen[i]) ++missing;

    EXPECT_EQ(missing, 0) << missing << " items were never consumed";
}

// --- 11. Full/Empty 경계: backpressure 정확성 ----------------
//  Producer가 큐를 가득 채우고, Consumer가 하나씩 꺼낼 때
//  Push 실패 후 재시도로 데이터가 반드시 전달되는지 검증
TEST(SPSCQueueConcurrent, BackpressureNeverDrops)
{
    constexpr int  TOTAL = 5000;
    constexpr uint32_t CAP = 16; // 작은 큐로 backpressure 극대화

    std::vector<SPSCNode> pool(TOTAL);
    for (int i = 0; i < TOTAL; ++i)
        pool[i].value = i;

    SPSCQueue<SPSCNode, CAP> queue;
    std::vector<int> consumed;
    consumed.reserve(TOTAL);
    std::atomic<bool> producerDone{ false };

    std::thread producer([&]()
    {
        for (int i = 0; i < TOTAL; ++i)
        {
            while (!queue.Push(&pool[i]))
                std::this_thread::yield();
        }
        producerDone.store(true, std::memory_order_release);
    });

    while (consumed.size() < static_cast<size_t>(TOTAL))
    {
        SPSCNode* p = queue.Pop();
        if (p)
            consumed.push_back(p->value);
        else if (producerDone.load(std::memory_order_acquire))
            std::this_thread::yield();
    }

    producer.join();

    EXPECT_EQ(consumed.size(), static_cast<size_t>(TOTAL));
    for (int i = 0; i < TOTAL; ++i)
    {
        SCOPED_TRACE(i);
        EXPECT_EQ(consumed[i], i);
    }
}
