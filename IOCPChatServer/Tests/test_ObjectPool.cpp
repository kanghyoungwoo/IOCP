#include <gtest/gtest.h>
#include "../ObjectPool.h"
#include <thread>
#include <vector>
#include <set>
#include <atomic>

struct PoolItem
{
    uint32_t poolNext = NULL_INDEX;
    int data = 0;
};

class ObjectPoolTest : public ::testing::Test
{
protected:
    ObjectPool<PoolItem> pool;

    void SetUp() override
    {
        pool.Init(100);
    }
};

TEST(ObjectPoolBasic, AllocBeforeInitReturnsNull)
{
    ObjectPool<PoolItem> uninit;
    EXPECT_EQ(uninit.Alloc(), nullptr);
}

TEST_F(ObjectPoolTest, InitSetsSize)
{
    EXPECT_EQ(pool.GetPoolSize(), 100u);
    EXPECT_EQ(pool.GetFreeCount(), 100u);
}

TEST_F(ObjectPoolTest, AllocReturnsNonNull)
{
    PoolItem* p = pool.Alloc();
    EXPECT_NE(p, nullptr);
}

TEST_F(ObjectPoolTest, AllocAllUnique)
{
    std::set<PoolItem*> allocated;
    for (uint32_t i = 0; i < 100; ++i)
    {
        PoolItem* p = pool.Alloc();
        EXPECT_NE(p, nullptr);
        if (p) allocated.insert(p);
    }
    EXPECT_EQ(allocated.size(), 100u);
    EXPECT_EQ(pool.GetFreeCount(), 0u);
}

TEST_F(ObjectPoolTest, AllocExhaustedReturnsNull)
{
    for (uint32_t i = 0; i < 100; ++i)
        pool.Alloc();
    EXPECT_EQ(pool.Alloc(), nullptr);
}

TEST_F(ObjectPoolTest, FreeAndRealloc)
{
    PoolItem* p = pool.Alloc();
    EXPECT_NE(p, nullptr);
    EXPECT_EQ(pool.GetFreeCount(), 99u);

    pool.Free(p);
    EXPECT_EQ(pool.GetFreeCount(), 100u);

    PoolItem* p2 = pool.Alloc();
    EXPECT_NE(p2, nullptr);
}

TEST_F(ObjectPoolTest, FreeNullptrSafe)
{
    uint32_t before = pool.GetFreeCount();
    pool.Free(nullptr);
    EXPECT_EQ(pool.GetFreeCount(), before);
}

TEST_F(ObjectPoolTest, FreeCountTracking)
{
    EXPECT_EQ(pool.GetFreeCount(), 100u);

    std::vector<PoolItem*> items;
    for (int i = 0; i < 30; ++i)
        items.push_back(pool.Alloc());
    EXPECT_EQ(pool.GetFreeCount(), 70u);

    for (int i = 0; i < 10; ++i)
        pool.Free(items[i]);
    EXPECT_EQ(pool.GetFreeCount(), 80u);

    for (int i = 10; i < 30; ++i)
        pool.Free(items[i]);
    EXPECT_EQ(pool.GetFreeCount(), 100u);
}

TEST_F(ObjectPoolTest, AllocFailCount)
{
    EXPECT_EQ(pool.GetAllocFailCount(), 0u);
    pool.IncrementAllocFail();
    pool.IncrementAllocFail();
    EXPECT_EQ(pool.GetAllocFailCount(), 2u);
}

TEST_F(ObjectPoolTest, IsLockFree)
{
    EXPECT_TRUE(pool.IsLockFree());
}

TEST(ObjectPoolConcurrent, ConcurrentAllocFree)
{
    ObjectPool<PoolItem> concPool;
    concPool.Init(2000);

    constexpr int THREADS = 4;
    constexpr int OPS = 500;
    std::vector<std::thread> threads;
    std::atomic<uint32_t> totalAlloced{ 0 };

    for (int t = 0; t < THREADS; ++t)
    {
        threads.emplace_back([&concPool, &totalAlloced]()
        {
            std::vector<PoolItem*> local;
            local.reserve(OPS);
            for (int i = 0; i < OPS; ++i)
            {
                PoolItem* p = concPool.Alloc();
                if (p)
                {
                    local.push_back(p);
                    totalAlloced.fetch_add(1, std::memory_order_relaxed);
                }
            }
            for (auto* p : local)
                concPool.Free(p);
        });
    }

    for (auto& th : threads) th.join();

    EXPECT_EQ(concPool.GetFreeCount(), 2000u);
    EXPECT_LE(totalAlloced.load(), 2000u);
}

// ==================== AllocBatch tests (Phase 4) ====================

class ObjectPoolBatchTest : public ::testing::Test
{
protected:
    ObjectPool<PoolItem> pool;

    void SetUp() override
    {
        pool.Init(100);
    }
};

TEST_F(ObjectPoolBatchTest, AllocBatchBasic)
{
    PoolItem* out[16] = {};
    uint32_t got = pool.AllocBatch(16, out);
    EXPECT_EQ(got, 16u);
    EXPECT_EQ(pool.GetFreeCount(), 84u);

    for (uint32_t i = 0; i < got; ++i)
        EXPECT_NE(out[i], nullptr);
}

TEST_F(ObjectPoolBatchTest, AllocBatchReturnsUniquePointers)
{
    PoolItem* out[32] = {};
    uint32_t got = pool.AllocBatch(32, out);
    ASSERT_EQ(got, 32u);

    std::set<PoolItem*> seen;
    for (uint32_t i = 0; i < got; ++i)
    {
        ASSERT_NE(out[i], nullptr);
        EXPECT_EQ(seen.count(out[i]), 0u) << "Duplicate in AllocBatch output";
        seen.insert(out[i]);
    }
}

TEST_F(ObjectPoolBatchTest, AllocBatchPartialWhenNearExhausted)
{
    // Drain all but 5
    for (uint32_t i = 0; i < 95; ++i)
        pool.Alloc();

    EXPECT_EQ(pool.GetFreeCount(), 5u);

    PoolItem* out[20] = {};
    uint32_t got = pool.AllocBatch(20, out);
    EXPECT_EQ(got, 5u);
    EXPECT_EQ(pool.GetFreeCount(), 0u);
}

TEST_F(ObjectPoolBatchTest, AllocBatchFromEmptyReturnsZero)
{
    // Exhaust the pool
    for (uint32_t i = 0; i < 100; ++i)
        pool.Alloc();

    PoolItem* out[8] = {};
    uint32_t got = pool.AllocBatch(8, out);
    EXPECT_EQ(got, 0u);
}

TEST_F(ObjectPoolBatchTest, AllocBatchFreeCountTracking)
{
    PoolItem* out[10] = {};
    pool.AllocBatch(10, out);
    EXPECT_EQ(pool.GetFreeCount(), 90u);

    for (uint32_t i = 0; i < 10; ++i)
        pool.Free(out[i]);
    EXPECT_EQ(pool.GetFreeCount(), 100u);
}

TEST_F(ObjectPoolBatchTest, AllocBatchAndSingleAllocDoNotOverlap)
{
    PoolItem* batch[50] = {};
    uint32_t got = pool.AllocBatch(50, batch);
    ASSERT_EQ(got, 50u);

    std::set<PoolItem*> batchSet(batch, batch + got);

    // Remaining 50 via Alloc()
    std::vector<PoolItem*> singles;
    for (uint32_t i = 0; i < 50; ++i)
    {
        PoolItem* p = pool.Alloc();
        ASSERT_NE(p, nullptr);
        EXPECT_EQ(batchSet.count(p), 0u) << "Single Alloc returned item already in batch";
        singles.push_back(p);
    }

    EXPECT_EQ(pool.Alloc(), nullptr);  // pool exhausted

    for (auto* p : singles) pool.Free(p);
    for (uint32_t i = 0; i < got; ++i) pool.Free(batch[i]);
    EXPECT_EQ(pool.GetFreeCount(), 100u);
}

TEST(ObjectPoolBatchConcurrent, ConcurrentAllocBatch8Threads)
{
    // All threads race to allocate in batches until exhausted.
    // Freeing happens AFTER all threads join to prevent re-allocation
    // by other threads (which would make totalAlloced > POOL_SZ).
    ObjectPool<PoolItem> concPool;
    constexpr uint32_t POOL_SZ = 2000;
    concPool.Init(POOL_SZ);

    constexpr int THREADS = 8;
    constexpr uint32_t BATCH = 16;

    std::vector<std::vector<PoolItem*>> perThread(THREADS);
    std::atomic<bool> go{ false };
    std::vector<std::thread> threads;

    for (int t = 0; t < THREADS; ++t)
    {
        threads.emplace_back([&, t]()
        {
            while (!go.load(std::memory_order_acquire)) {}
            PoolItem* out[BATCH] = {};
            while (true)
            {
                uint32_t got = concPool.AllocBatch(BATCH, out);
                if (got == 0) break;
                for (uint32_t i = 0; i < got; ++i)
                    perThread[t].push_back(out[i]);
            }
            // Do NOT free here: freeing while other threads still run
            // allows re-allocation and inflates the total count.
        });
    }

    go.store(true, std::memory_order_release);
    for (auto& th : threads) th.join();

    uint32_t total = 0;
    for (int t = 0; t < THREADS; ++t)
        total += static_cast<uint32_t>(perThread[t].size());

    EXPECT_EQ(total, POOL_SZ);
    EXPECT_EQ(concPool.GetFreeCount(), 0u);

    for (int t = 0; t < THREADS; ++t)
        for (auto* p : perThread[t])
            concPool.Free(p);
    EXPECT_EQ(concPool.GetFreeCount(), POOL_SZ);
}

TEST(ObjectPoolBatchConcurrent, ConcurrentAllocBatchNoDuplicates)
{
    ObjectPool<PoolItem> concPool;
    constexpr uint32_t POOL_SZ = 1000;
    concPool.Init(POOL_SZ);

    constexpr int THREADS = 4;
    constexpr uint32_t BATCH = 16;

    std::vector<std::vector<PoolItem*>> results(THREADS);
    std::vector<std::thread> threads;

    for (int t = 0; t < THREADS; ++t)
    {
        threads.emplace_back([&, t]()
        {
            PoolItem* out[BATCH] = {};
            while (true)
            {
                uint32_t got = concPool.AllocBatch(BATCH, out);
                if (got == 0) break;
                results[t].insert(results[t].end(), out, out + got);
            }
        });
    }

    for (auto& th : threads) th.join();

    std::set<PoolItem*> seen;
    uint32_t total = 0;
    for (int t = 0; t < THREADS; ++t)
    {
        for (auto* p : results[t])
        {
            EXPECT_EQ(seen.count(p), 0u) << "Duplicate PoolItem* across threads";
            seen.insert(p);
            ++total;
        }
    }
    EXPECT_EQ(total, POOL_SZ);

    // Return all items
    for (int t = 0; t < THREADS; ++t)
        for (auto* p : results[t])
            concPool.Free(p);
    EXPECT_EQ(concPool.GetFreeCount(), POOL_SZ);
}
