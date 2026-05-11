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
