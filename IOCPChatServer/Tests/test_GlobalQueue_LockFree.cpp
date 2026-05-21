#include <gtest/gtest.h>

#include <winsock2.h>

#include "../GlobalQueue_LockFree.h"
#include <thread>
#include <vector>
#include <atomic>

// ============================================================
//  Fake Room* helpers
//  The queue stores/retrieves Room* pointers.
//  These tests only verify pointer values are preserved
//  correctly — the pointers are never dereferenced.
//  Room is ~32 KB due to SPSCQueue<PacketJob,4096>; constructing
//  thousands of them would waste hundreds of MB in test memory.
// ============================================================
static Room* fakeRoom(int i)
{
    // Base 0x100000 keeps addresses above the null page and
    // makes them look like real heap addresses in a debugger.
    return reinterpret_cast<Room*>(static_cast<uintptr_t>(0x100000) + static_cast<uintptr_t>(i) * 16);
}

static int fakeIndex(Room* p)
{
    return static_cast<int>((reinterpret_cast<uintptr_t>(p) - 0x100000) / 16);
}

// ============================================================
//  Single-threaded fixture  (QUEUE_SIZE = 8, capacity = 8)
//  Vyukov MPMC design: capacity == bufferSize (no -1 slot waste)
// ============================================================
class GlobalQueueTest : public ::testing::Test
{
protected:
    static constexpr uint32_t QUEUE_SIZE = 8;
    GlobalQueue_LockFree queue;

    void SetUp()   override { queue.Init(QUEUE_SIZE); }
    void TearDown() override { queue.Shutdown(); } // idempotent if already called
};

// --- 1. Init: non-power-of-2 must throw ----------------------
TEST(GlobalQueueInit, ThrowsOnNonPowerOfTwo)
{
    { GlobalQueue_LockFree q; EXPECT_THROW(q.Init(0), std::invalid_argument); }
    { GlobalQueue_LockFree q; EXPECT_THROW(q.Init(1), std::invalid_argument); }
    { GlobalQueue_LockFree q; EXPECT_THROW(q.Init(3), std::invalid_argument); }
    { GlobalQueue_LockFree q; EXPECT_THROW(q.Init(5), std::invalid_argument); }
    { GlobalQueue_LockFree q; EXPECT_THROW(q.Init(7), std::invalid_argument); }
}

TEST(GlobalQueueInit, PowerOfTwoSucceeds)
{
    { GlobalQueue_LockFree q; EXPECT_NO_THROW(q.Init(2));    q.Shutdown(); }
    { GlobalQueue_LockFree q; EXPECT_NO_THROW(q.Init(8));    q.Shutdown(); }
    { GlobalQueue_LockFree q; EXPECT_NO_THROW(q.Init(1024)); q.Shutdown(); }
}

// --- 2. Shutdown + empty Pop -> nullptr ----------------------
TEST_F(GlobalQueueTest, ShutdownEmptyReturnsNull)
{
    queue.Shutdown();
    EXPECT_EQ(queue.Pop(), nullptr);
}

// --- 3. Single Push/Pop --------------------------------------
TEST_F(GlobalQueueTest, SinglePushPop)
{
    queue.Push(fakeRoom(0));

    Room* r = queue.Pop();
    EXPECT_EQ(r, fakeRoom(0));

    queue.Shutdown();
    EXPECT_EQ(queue.Pop(), nullptr);
}

// --- 4. FIFO order -------------------------------------------
//  Bounded ring buffer guarantees FIFO within one Producer.
TEST_F(GlobalQueueTest, FIFOOrdering)
{
    for (int i = 0; i < 5; ++i)
        queue.Push(fakeRoom(i));

    for (int i = 0; i < 5; ++i)
    {
        SCOPED_TRACE(i);
        EXPECT_EQ(queue.Pop(), fakeRoom(i));
    }
    queue.Shutdown();
    EXPECT_EQ(queue.Pop(), nullptr);
}

// --- 5. Full capacity ----------------------------------------
//  With Init(8): capacity = 8 (all slots usable, Vyukov design)
TEST_F(GlobalQueueTest, PushFullCapacity)
{
    for (uint32_t i = 0; i < QUEUE_SIZE; ++i)
        queue.Push(fakeRoom(static_cast<int>(i)));

    for (uint32_t i = 0; i < QUEUE_SIZE; ++i)
    {
        SCOPED_TRACE(i);
        EXPECT_EQ(queue.Pop(), fakeRoom(static_cast<int>(i)));
    }
    queue.Shutdown();
    EXPECT_EQ(queue.Pop(), nullptr);
}

// --- 6. Drain then re-push -----------------------------------
TEST_F(GlobalQueueTest, PushAfterDrain)
{
    queue.Push(fakeRoom(0));
    EXPECT_EQ(queue.Pop(), fakeRoom(0));

    queue.Push(fakeRoom(1));
    EXPECT_EQ(queue.Pop(), fakeRoom(1));

    queue.Shutdown();
    EXPECT_EQ(queue.Pop(), nullptr);
}

// --- 7. Wrap-around: index crosses bufferSize boundary -------
//  40 push/pop cycles with QUEUE_SIZE=8 forces the ring to wrap
//  at least 4 times, verifying sequence counters recycle correctly.
TEST_F(GlobalQueueTest, WrapAround)
{
    constexpr int ROUNDS = 40;
    for (int i = 0; i < ROUNDS; ++i)
    {
        SCOPED_TRACE(i);
        queue.Push(fakeRoom(i));
        EXPECT_EQ(queue.Pop(), fakeRoom(i));
    }
    queue.Shutdown();
    EXPECT_EQ(queue.Pop(), nullptr);
}

// --- 8. Rapid push/pop cycles --------------------------------
TEST_F(GlobalQueueTest, RapidPushPopCycles)
{
    for (int round = 0; round < 20; ++round)
    {
        for (int i = 0; i < 4; ++i)
            queue.Push(fakeRoom(round * 4 + i));
        for (int i = 0; i < 4; ++i)
        {
            SCOPED_TRACE(round * 4 + i);
            EXPECT_EQ(queue.Pop(), fakeRoom(round * 4 + i));
        }
    }
    queue.Shutdown();
    EXPECT_EQ(queue.Pop(), nullptr);
}

// --- 9. Shutdown after partial fill: all pushed items drained -
TEST_F(GlobalQueueTest, ShutdownAfterPartialFill)
{
    queue.Push(fakeRoom(0));
    queue.Push(fakeRoom(1));
    queue.Push(fakeRoom(2));

    EXPECT_EQ(queue.Pop(), fakeRoom(0));
    EXPECT_EQ(queue.Pop(), fakeRoom(1));
    EXPECT_EQ(queue.Pop(), fakeRoom(2));

    queue.Shutdown();
    EXPECT_EQ(queue.Pop(), nullptr);
}

// ============================================================
//  Concurrent tests: SPMC (1 Producer, 4 Consumers)
//  Producer calls Shutdown() after the last Push so consumers
//  exit cleanly when the queue drains.
// ============================================================

// --- 10. No data loss ----------------------------------------
TEST(GlobalQueueSPMC, NoDataLoss)
{
    constexpr int     TOTAL      = 5000;
    constexpr int     CONSUMERS  = 4;
    constexpr uint32_t QUEUE_SIZE = 256;

    GlobalQueue_LockFree queue;
    queue.Init(QUEUE_SIZE);

    std::atomic<int> consumedCount{ 0 };

    std::vector<std::thread> consumers;
    for (int t = 0; t < CONSUMERS; ++t)
    {
        consumers.emplace_back([&]()
        {
            while (queue.Pop() != nullptr)
                consumedCount.fetch_add(1, std::memory_order_relaxed);
        });
    }

    std::thread producer([&]()
    {
        for (int i = 0; i < TOTAL; ++i)
            queue.Push(fakeRoom(i));
        queue.Shutdown(); // unblocks consumers after the queue drains
    });

    producer.join();
    for (auto& c : consumers) c.join();

    EXPECT_EQ(consumedCount.load(), TOTAL);
}

// --- 11. No duplicate items ----------------------------------
//  Each fakeRoom(i) is a distinct pointer value.
//  Every item must appear exactly once across all consumers.
TEST(GlobalQueueSPMC, NoDuplicate)
{
    constexpr int     TOTAL      = 5000;
    constexpr int     CONSUMERS  = 4;
    constexpr uint32_t QUEUE_SIZE = 256;

    GlobalQueue_LockFree queue;
    queue.Init(QUEUE_SIZE);

    // Each consumer thread collects its own result list (no shared writes)
    std::vector<std::vector<int>> results(CONSUMERS);

    std::vector<std::thread> consumers;
    for (int t = 0; t < CONSUMERS; ++t)
    {
        consumers.emplace_back([&, t]()
        {
            Room* r;
            while ((r = queue.Pop()) != nullptr)
                results[t].push_back(fakeIndex(r));
        });
    }

    std::thread producer([&]()
    {
        for (int i = 0; i < TOTAL; ++i)
            queue.Push(fakeRoom(i));
        queue.Shutdown();
    });

    producer.join();
    for (auto& c : consumers) c.join();

    // Merge and verify: total count == TOTAL, no duplicates, no missing
    std::vector<bool> seen(TOTAL, false);
    int total = 0;
    for (int t = 0; t < CONSUMERS; ++t)
    {
        for (int idx : results[t])
        {
            ASSERT_GE(idx, 0);
            ASSERT_LT(idx, TOTAL);
            EXPECT_FALSE(seen[idx]) << "Duplicate: index " << idx;
            seen[idx] = true;
            ++total;
        }
    }
    EXPECT_EQ(total, TOTAL);

    int missing = 0;
    for (int i = 0; i < TOTAL; ++i)
        if (!seen[i]) ++missing;
    EXPECT_EQ(missing, 0) << missing << " items were never consumed";
}

// --- 12. High throughput: no loss at scale -------------------
TEST(GlobalQueueSPMC, HighThroughput)
{
    constexpr int     TOTAL      = 100000;
    constexpr int     CONSUMERS  = 4;
    constexpr uint32_t QUEUE_SIZE = 1024;

    GlobalQueue_LockFree queue;
    queue.Init(QUEUE_SIZE);

    std::atomic<int> consumedCount{ 0 };

    std::vector<std::thread> consumers;
    for (int t = 0; t < CONSUMERS; ++t)
    {
        consumers.emplace_back([&]()
        {
            while (queue.Pop() != nullptr)
                consumedCount.fetch_add(1, std::memory_order_relaxed);
        });
    }

    std::thread producer([&]()
    {
        for (int i = 0; i < TOTAL; ++i)
            queue.Push(fakeRoom(i));
        queue.Shutdown();
    });

    producer.join();
    for (auto& c : consumers) c.join();

    EXPECT_EQ(consumedCount.load(), TOTAL);
}
