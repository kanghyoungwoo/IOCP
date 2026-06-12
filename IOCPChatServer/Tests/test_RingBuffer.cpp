#include <gtest/gtest.h>
#include "../RingBuffer.h"
#include <cstring>
#include <thread>
#include <atomic>

constexpr size_t TEST_BUF = 64;

class RingBufferTest : public ::testing::Test
{
protected:
    RingBuffer<TEST_BUF> buf;
};

TEST_F(RingBufferTest, EmptyDefaults)
{
    EXPECT_EQ(buf.Size(), 0u);
    EXPECT_TRUE(buf.IsEmpty());
    EXPECT_FALSE(buf.IsFull());
}

TEST_F(RingBufferTest, WriteSingleByte)
{
    EXPECT_TRUE(buf.WriteByte('A'));
    EXPECT_EQ(buf.Size(), 1u);
    EXPECT_FALSE(buf.IsEmpty());
}

TEST_F(RingBufferTest, ReadSingleByte)
{
    buf.WriteByte('Z');
    char b = 0;
    EXPECT_TRUE(buf.ReadByte(b));
    EXPECT_EQ(b, 'Z');
    EXPECT_TRUE(buf.IsEmpty());
}

TEST_F(RingBufferTest, ReadFromEmptyFails)
{
    char b = 0;
    EXPECT_FALSE(buf.ReadByte(b));
}

TEST_F(RingBufferTest, WriteReadBlock)
{
    const char data[] = "Hello, IOCP!";
    size_t len = strlen(data);
    EXPECT_EQ(buf.Write(data, len), len);
    char out[64] = {};
    EXPECT_EQ(buf.Read(out, len), len);
    EXPECT_EQ(memcmp(out, data, len), 0);
    EXPECT_TRUE(buf.IsEmpty());
}

TEST_F(RingBufferTest, PartialWriteWhenNearFull)
{
    char fill[32];
    memset(fill, 'X', 32);
    buf.Write(fill, 32);
    char big[48];
    memset(big, 'Y', 48);
    size_t written = buf.Write(big, 48);
    EXPECT_EQ(written, 32u);
    EXPECT_TRUE(buf.IsFull());
}

TEST_F(RingBufferTest, WrapAround)
{
    char fill[48];
    memset(fill, 'A', 48);
    buf.Write(fill, 48);
    char discard[48];
    buf.Read(discard, 48);

    char wrapData[32];
    for (int i = 0; i < 32; ++i)
        wrapData[i] = static_cast<char>(i);

    EXPECT_EQ(buf.Write(wrapData, 32), 32u);
    char out[32] = {};
    EXPECT_EQ(buf.Read(out, 32), 32u);
    EXPECT_EQ(memcmp(out, wrapData, 32), 0);
}

TEST_F(RingBufferTest, FullBufferRejectsWrite)
{
    char fill[TEST_BUF];
    memset(fill, 'Z', TEST_BUF);
    buf.Write(fill, TEST_BUF);
    EXPECT_TRUE(buf.IsFull());
    EXPECT_FALSE(buf.WriteByte('X'));
    EXPECT_EQ(buf.Write("abc", 3), 0u);
}

TEST_F(RingBufferTest, PeekBlockCrossWrap)
{
    char fill[60];
    memset(fill, 'A', 60);
    buf.Write(fill, 60);
    char discard[60];
    buf.Read(discard, 60);

    char wrapData[8] = { 'H','E','L','L','O','!','!','!' };
    buf.Write(wrapData, 8);

    char out[8] = {};
    EXPECT_TRUE(buf.PeekBlock(out, 8));
    EXPECT_EQ(memcmp(out, wrapData, 8), 0);
    EXPECT_EQ(buf.Size(), 8u);
}

TEST_F(RingBufferTest, PeekBlockTooLarge)
{
    buf.WriteByte('X');
    char out[4] = {};
    EXPECT_FALSE(buf.PeekBlock(out, 4));
}

TEST_F(RingBufferTest, ClearResetsState)
{
    buf.Write("Hello", 5);
    buf.Clear();
    EXPECT_EQ(buf.Size(), 0u);
    EXPECT_TRUE(buf.IsEmpty());
    EXPECT_TRUE(buf.WriteByte('A'));
}

TEST_F(RingBufferTest, NullptrHandling)
{
    EXPECT_EQ(buf.Write(nullptr, 10), 0u);
    EXPECT_EQ(buf.Read(nullptr, 10), 0u);
    EXPECT_FALSE(buf.PeekBlock(nullptr, 1));
}

TEST_F(RingBufferTest, RepeatedWriteReadCycles)
{
    for (int n = 0; n < 100; ++n)
    {
        SCOPED_TRACE(n);
        char writeData[16];
        for (int i = 0; i < 16; ++i)
            writeData[i] = static_cast<char>((n * 16 + i) & 0xFF);

        EXPECT_EQ(buf.Write(writeData, 16), 16u);

        char readData[16] = {};
        EXPECT_EQ(buf.Read(readData, 16), 16u);
        EXPECT_EQ(memcmp(readData, writeData, 16), 0);
    }
}

// head/tail이 마스킹 없이 단조증가하는지 검증 (kfifo 방식 핵심)
// 구 버전은 wrap 시 head/tail이 0으로 돌아왔으나 새 버전은 계속 증가해야 함
TEST_F(RingBufferTest, MonotonicHeadTail)
{
    constexpr size_t CYCLES = 10;
    char data[TEST_BUF] = {};
    char out[TEST_BUF]  = {};

    for (size_t i = 0; i < CYCLES; ++i)
    {
        buf.Write(data, TEST_BUF);
        buf.Read(out, TEST_BUF);
    }

    // CYCLES 번 가득 채우고 비웠으므로 head == tail == TEST_BUF * CYCLES
    EXPECT_EQ(buf.Size(), 0u);
    EXPECT_TRUE(buf.IsEmpty());
    // 마스킹 없이 증가했다면 head > TEST_BUF (구 버전은 0으로 wrap됐음)
    EXPECT_TRUE(buf.Write(data, 1) == 1u);
}

// SPSC 동시성 검증: Producer 스레드가 쓰는 동안 Consumer 스레드가 읽어도 데이터가 정확한지 확인
TEST(RingBufferSPSC, ConcurrentProducerConsumer)
{
    constexpr size_t BUF_SIZE  = 256;
    constexpr int    TOTAL_MSGS = 10000;
    constexpr int    MSG_SIZE   = 16;

    RingBuffer<BUF_SIZE> spscBuf;
    std::atomic<int> consumed{ 0 };
    std::atomic<bool> producerDone{ false };

    // Consumer 스레드
    std::thread consumer([&]()
    {
        char out[MSG_SIZE];
        int count = 0;
        while (count < TOTAL_MSGS)
        {
            if (spscBuf.Read(out, MSG_SIZE) == MSG_SIZE)
            {
                // 첫 바이트가 메시지 번호(하위 1바이트)와 일치하는지 검증
                EXPECT_EQ(static_cast<unsigned char>(out[0]),
                          static_cast<unsigned char>(count & 0xFF));
                ++count;
            }
        }
        consumed.store(count);
    });

    // Producer (메인 스레드)
    for (int n = 0; n < TOTAL_MSGS; ++n)
    {
        char msg[MSG_SIZE];
        memset(msg, static_cast<char>(n & 0xFF), MSG_SIZE);
        while (spscBuf.Write(msg, MSG_SIZE) == 0)
            ;  // 버퍼 가득 찼으면 Consumer가 비울 때까지 대기
    }

    consumer.join();
    EXPECT_EQ(consumed.load(), TOTAL_MSGS);
}
