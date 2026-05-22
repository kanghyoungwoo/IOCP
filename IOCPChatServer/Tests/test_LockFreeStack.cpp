#include <gtest/gtest.h>
#include "../LockFreeStack.h"
#include <thread>
#include <vector>
#include <set>

struct StackNode
{
    uint32_t poolNext = NULL_INDEX;
    int value = 0;
};

class LockFreeStackTest : public ::testing::Test
{
protected:
    static constexpr uint32_t POOL_SIZE = 100;
    StackNode pool[POOL_SIZE];
    LockFreeStack<StackNode> stack;

    void SetUp() override
    {
        stack.Init(pool);
    }
};

TEST_F(LockFreeStackTest, PopFromEmptyReturnsNull)
{
    EXPECT_EQ(stack.Pop(), nullptr);
}

TEST_F(LockFreeStackTest, PushPopSingle)
{
    pool[0].value = 42;
    stack.Push(&pool[0]);

    StackNode* p = stack.Pop();
    EXPECT_NE(p, nullptr);
    EXPECT_EQ(p, &pool[0]);
    EXPECT_EQ(p->value, 42);
}

TEST_F(LockFreeStackTest, PushPopMultipleLIFO)
{
    pool[0].value = 10;
    pool[1].value = 20;
    pool[2].value = 30;
    stack.Push(&pool[0]);
    stack.Push(&pool[1]);
    stack.Push(&pool[2]);

    StackNode* p1 = stack.Pop();
    StackNode* p2 = stack.Pop();
    StackNode* p3 = stack.Pop();
    EXPECT_NE(p1, nullptr);
    EXPECT_NE(p2, nullptr);
    EXPECT_NE(p3, nullptr);
    EXPECT_EQ(p1->value, 30);
    EXPECT_EQ(p2->value, 20);
    EXPECT_EQ(p3->value, 10);
    EXPECT_EQ(stack.Pop(), nullptr);
}

TEST_F(LockFreeStackTest, PushAllPopAll)
{
    for (uint32_t i = 0; i < POOL_SIZE; ++i)
    {
        pool[i].value = static_cast<int>(i);
        stack.Push(&pool[i]);
    }

    std::set<StackNode*> popped;
    for (uint32_t i = 0; i < POOL_SIZE; ++i)
    {
        StackNode* p = stack.Pop();
        EXPECT_NE(p, nullptr);
        if (p) popped.insert(p);
    }

    EXPECT_EQ(popped.size(), static_cast<size_t>(POOL_SIZE));
    EXPECT_EQ(stack.Pop(), nullptr);
}

TEST_F(LockFreeStackTest, IsLockFreeCheck)
{
    EXPECT_TRUE(stack.IsLockFree());
}

TEST(LockFreeStackConcurrent, ConcurrentPushPop)
{
    constexpr uint32_t N = 1000;
    std::vector<StackNode> pool(N);
    LockFreeStack<StackNode> s;
    s.Init(pool.data());

    for (uint32_t i = 0; i < N; ++i)
    {
        pool[i].value = static_cast<int>(i);
        s.Push(&pool[i]);
    }

    constexpr int THREADS = 4;
    std::vector<std::vector<StackNode*>> results(THREADS);
    std::vector<std::thread> threads;

    for (int t = 0; t < THREADS; ++t)
    {
        threads.emplace_back([&s, &results, t]()
        {
            while (true)
            {
                StackNode* node = s.Pop();
                if (!node) break;
                results[t].push_back(node);
            }
        });
    }

    for (auto& th : threads) th.join();

    size_t total = 0;
    std::set<StackNode*> unique;
    for (int t = 0; t < THREADS; ++t)
    {
        total += results[t].size();
        for (auto* p : results[t]) unique.insert(p);
    }

    EXPECT_EQ(total, static_cast<size_t>(N));
    EXPECT_EQ(unique.size(), static_cast<size_t>(N));
}

// ==================== PopBatch tests (Phase 4) ====================

class LockFreeStackBatchTest : public ::testing::Test
{
protected:
    static constexpr uint32_t POOL_SIZE = 200;
    StackNode pool[POOL_SIZE];
    LockFreeStack<StackNode> stack;

    void SetUp() override
    {
        stack.Init(pool, POOL_SIZE);
        for (uint32_t i = 0; i < POOL_SIZE; ++i)
        {
            pool[i].value = static_cast<int>(i);
            stack.Push(&pool[i]);
        }
    }
};

TEST_F(LockFreeStackBatchTest, PopBatchFromEmptyReturnsZero)
{
    // drain all first
    LockFreeStack<StackNode> empty;
    StackNode emptyPool[4];
    empty.Init(emptyPool, 4);

    StackNode* out[4] = {};
    uint32_t got = empty.PopBatch(4, out);
    EXPECT_EQ(got, 0u);
}

TEST_F(LockFreeStackBatchTest, PopBatchCorrectCount)
{
    StackNode* out[16] = {};
    uint32_t got = stack.PopBatch(16, out);
    EXPECT_EQ(got, 16u);
}

TEST_F(LockFreeStackBatchTest, PopBatchReturnsUniquePointers)
{
    StackNode* out[32] = {};
    uint32_t got = stack.PopBatch(32, out);
    ASSERT_GT(got, 0u);

    std::set<StackNode*> seen;
    for (uint32_t i = 0; i < got; ++i)
    {
        ASSERT_NE(out[i], nullptr);
        EXPECT_EQ(seen.count(out[i]), 0u) << "Duplicate pointer in batch";
        seen.insert(out[i]);
    }
}

TEST_F(LockFreeStackBatchTest, PopBatchPartialWhenFewerAvailable)
{
    // Drain all but 5
    for (uint32_t i = 0; i < POOL_SIZE - 5; ++i)
        stack.Pop();

    StackNode* out[20] = {};
    uint32_t got = stack.PopBatch(20, out);
    EXPECT_EQ(got, 5u);  // only 5 remain
    for (uint32_t i = 0; i < got; ++i)
        EXPECT_NE(out[i], nullptr);
}

TEST_F(LockFreeStackBatchTest, PopBatchTotalMatchesPushed)
{
    // Drain everything via batches of 16
    uint32_t totalPopped = 0;
    StackNode* out[16] = {};
    while (true)
    {
        uint32_t got = stack.PopBatch(16, out);
        if (got == 0) break;
        totalPopped += got;
    }
    EXPECT_EQ(totalPopped, POOL_SIZE);
    EXPECT_EQ(stack.Pop(), nullptr);
}

TEST_F(LockFreeStackBatchTest, PopBatchNoOverlapBetweenCalls)
{
    StackNode* first[32] = {};
    StackNode* second[32] = {};

    uint32_t n1 = stack.PopBatch(32, first);
    uint32_t n2 = stack.PopBatch(32, second);

    std::set<StackNode*> firstSet(first, first + n1);
    for (uint32_t i = 0; i < n2; ++i)
        EXPECT_EQ(firstSet.count(second[i]), 0u) << "Overlap between two PopBatch calls";
}

TEST_F(LockFreeStackBatchTest, PopBatchSingleItem)
{
    StackNode* out[1] = {};
    uint32_t got = stack.PopBatch(1, out);
    EXPECT_EQ(got, 1u);
    EXPECT_NE(out[0], nullptr);
}

TEST(LockFreeStackBatchConcurrent, ConcurrentPopBatchNoLoss)
{
    constexpr uint32_t N = 1000;
    std::vector<StackNode> pool(N);
    LockFreeStack<StackNode> s;
    s.Init(pool.data(), N);

    for (uint32_t i = 0; i < N; ++i)
    {
        pool[i].value = static_cast<int>(i);
        s.Push(&pool[i]);
    }

    constexpr int THREADS = 4;
    constexpr uint32_t BATCH = 16;
    std::atomic<uint32_t> totalPopped{ 0 };
    std::vector<std::thread> threads;

    for (int t = 0; t < THREADS; ++t)
    {
        threads.emplace_back([&]()
        {
            StackNode* out[BATCH] = {};
            while (true)
            {
                uint32_t got = s.PopBatch(BATCH, out);
                if (got == 0) break;
                totalPopped.fetch_add(got, std::memory_order_relaxed);
            }
        });
    }

    for (auto& th : threads) th.join();

    EXPECT_EQ(totalPopped.load(), N);
}

TEST(LockFreeStackBatchConcurrent, ConcurrentPopBatchNoDuplicates)
{
    constexpr uint32_t N = 1000;
    std::vector<StackNode> pool(N);
    LockFreeStack<StackNode> s;
    s.Init(pool.data(), N);

    for (uint32_t i = 0; i < N; ++i)
    {
        pool[i].value = static_cast<int>(i);
        s.Push(&pool[i]);
    }

    constexpr int THREADS = 4;
    constexpr uint32_t BATCH = 16;

    std::vector<std::vector<StackNode*>> results(THREADS);
    std::vector<std::thread> threads;

    for (int t = 0; t < THREADS; ++t)
    {
        threads.emplace_back([&, t]()
        {
            StackNode* out[BATCH] = {};
            while (true)
            {
                uint32_t got = s.PopBatch(BATCH, out);
                if (got == 0) break;
                results[t].insert(results[t].end(), out, out + got);
            }
        });
    }

    for (auto& th : threads) th.join();

    std::set<StackNode*> seen;
    uint32_t total = 0;
    for (int t = 0; t < THREADS; ++t)
    {
        for (auto* p : results[t])
        {
            EXPECT_EQ(seen.count(p), 0u) << "Duplicate pointer across threads";
            seen.insert(p);
            ++total;
        }
    }
    EXPECT_EQ(total, N);
}
