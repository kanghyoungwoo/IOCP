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
