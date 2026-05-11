#include <gtest/gtest.h>
#include "../MPSCQueue.h"
#include <thread>
#include <vector>
#include <set>
#include <atomic>

struct QueueNode
{
    std::atomic<QueueNode*> mpscNext{ nullptr };
    int value = 0;
};

class MPSCQueueTest : public ::testing::Test
{
protected:
    MPSCQueue<QueueNode> queue;
};

TEST_F(MPSCQueueTest, PopFromEmptyReturnsNull)
{
    EXPECT_EQ(queue.Pop(), nullptr);
}

TEST_F(MPSCQueueTest, SinglePushPop)
{
    QueueNode node;
    node.value = 42;
    queue.Push(&node);

    QueueNode* p = queue.Pop();
    EXPECT_NE(p, nullptr);
    EXPECT_EQ(p->value, 42);
    EXPECT_EQ(queue.Pop(), nullptr);
}

TEST_F(MPSCQueueTest, FIFOOrdering)
{
    QueueNode nodes[5];
    for (int i = 0; i < 5; ++i)
    {
        nodes[i].value = i * 10;
        queue.Push(&nodes[i]);
    }

    for (int i = 0; i < 5; ++i)
    {
        SCOPED_TRACE(i);
        QueueNode* p = queue.Pop();
        EXPECT_NE(p, nullptr);
        if (p) EXPECT_EQ(p->value, i * 10);
    }

    EXPECT_EQ(queue.Pop(), nullptr);
}

TEST_F(MPSCQueueTest, PushAfterDrain)
{
    QueueNode n1, n2;
    n1.value = 1; n2.value = 2;
    queue.Push(&n1);
    queue.Pop();
    EXPECT_EQ(queue.Pop(), nullptr);

    queue.Push(&n2);
    QueueNode* p = queue.Pop();
    EXPECT_NE(p, nullptr);
    if (p) EXPECT_EQ(p->value, 2);
}

TEST(MPSCQueueConcurrent, MultiProducerSingleConsumer)
{
    constexpr int PRODUCERS = 4;
    constexpr int PER_PRODUCER = 1000;
    constexpr int TOTAL = PRODUCERS * PER_PRODUCER;

    std::vector<QueueNode> pool(TOTAL);
    for (int i = 0; i < TOTAL; ++i)
        pool[i].value = i;

    MPSCQueue<QueueNode> q;
    std::atomic<int> done{ 0 };

    std::vector<std::thread> producers;
    for (int t = 0; t < PRODUCERS; ++t)
    {
        producers.emplace_back([&q, &pool, &done, t]()
        {
            int start = t * PER_PRODUCER;
            for (int i = 0; i < PER_PRODUCER; ++i)
                q.Push(&pool[start + i]);
            done.fetch_add(1, std::memory_order_release);
        });
    }

    std::vector<QueueNode*> consumed;
    consumed.reserve(TOTAL);

    while (consumed.size() < static_cast<size_t>(TOTAL))
    {
        QueueNode* node = q.Pop();
        if (node)
            consumed.push_back(node);
        else if (done.load(std::memory_order_acquire) >= PRODUCERS)
            std::this_thread::yield();
    }

    for (auto& t : producers) t.join();

    EXPECT_EQ(consumed.size(), static_cast<size_t>(TOTAL));

    std::set<QueueNode*> unique(consumed.begin(), consumed.end());
    EXPECT_EQ(unique.size(), static_cast<size_t>(TOTAL));
}

TEST_F(MPSCQueueTest, RapidPushPopCycles)
{
    QueueNode nodes[10];
    for (int n = 0; n < 50; ++n)
    {
        for (int i = 0; i < 10; ++i)
        {
            nodes[i].value = n * 10 + i;
            queue.Push(&nodes[i]);
        }
        for (int i = 0; i < 10; ++i)
        {
            SCOPED_TRACE(i);
            QueueNode* p = queue.Pop();
            EXPECT_NE(p, nullptr);
            if (p) EXPECT_EQ(p->value, n * 10 + i);
        }
        EXPECT_EQ(queue.Pop(), nullptr);
    }
}
