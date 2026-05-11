#include <gtest/gtest.h>
#include "../RingBuffer.h"
#include <cstring>

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

TEST_F(RingBufferTest, PeekDoesNotConsume)
{
    buf.WriteByte('A');
    buf.WriteByte('B');
    buf.WriteByte('C');
    char b = 0;
    EXPECT_TRUE(buf.Peek(b, 0)); EXPECT_EQ(b, 'A');
    EXPECT_TRUE(buf.Peek(b, 1)); EXPECT_EQ(b, 'B');
    EXPECT_TRUE(buf.Peek(b, 2)); EXPECT_EQ(b, 'C');
    EXPECT_EQ(buf.Size(), 3u);
}

TEST_F(RingBufferTest, PeekOutOfRange)
{
    buf.WriteByte('A');
    char b = 0;
    EXPECT_FALSE(buf.Peek(b, 1));
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
