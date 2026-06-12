#pragma once
#include <atomic>
#include <cstring>

// SPSC(Single Producer Single Consumer) Lock-Free Ring Buffer
// Producer : IOCP Worker  (SetPacketData) — head만 변경
// Consumer : IOCP Worker Fast Path 또는 ProcessThread (GetPacket) — tail만 변경
// mIsRouting 으로 Consumer가 항상 1개임이 보장됨
template<size_t BufferSize>
class RingBuffer
{
	static_assert(BufferSize > 0 && (BufferSize & (BufferSize - 1)) == 0,
		"RingBuffer: BufferSize must be a power of 2");
private:
	char buffer[BufferSize];

	// False Sharing 방지: head(Producer)와 tail(Consumer)을 별도 캐시라인에 배치
	alignas(64) std::atomic<size_t> head{ 0 };  // Producer만 변경
	alignas(64) std::atomic<size_t> tail{ 0 };  // Consumer만 변경

public:
	RingBuffer() = default;
	~RingBuffer() = default;

	// Consumer 기준 가용 데이터 크기
	size_t Size() const
	{
		size_t h = head.load(std::memory_order_acquire);
		size_t t = tail.load(std::memory_order_relaxed);
		return h - t;
	}

	bool IsEmpty() const { return Size() == 0; }

	bool IsFull() const
	{
		size_t h = head.load(std::memory_order_relaxed);
		size_t t = tail.load(std::memory_order_acquire);
		return (h - t) == BufferSize;
	}

	// Producer 전용
	bool WriteByte(char byte)
	{
		size_t h = head.load(std::memory_order_relaxed);
		size_t t = tail.load(std::memory_order_acquire);
		if (h - t == BufferSize) return false;

		buffer[h & (BufferSize - 1)] = byte;
		head.store(h + 1, std::memory_order_release);
		return true;
	}

	// Consumer 전용
	bool ReadByte(char& byte)
	{
		size_t t = tail.load(std::memory_order_relaxed);
		size_t h = head.load(std::memory_order_acquire);
		if (h == t) return false;

		byte = buffer[t & (BufferSize - 1)];
		tail.store(t + 1, std::memory_order_release);
		return true;
	}

	// Producer 전용: data를 버퍼에 씀
	size_t Write(const char* data, size_t length)
	{
		if (data == nullptr || length == 0) return 0;

		size_t h = head.load(std::memory_order_relaxed);
		size_t t = tail.load(std::memory_order_acquire);  // Consumer의 tail 동기화

		size_t available = BufferSize - (h - t);
		size_t toWrite   = (length < available) ? length : available;
		if (toWrite == 0) return 0;

		size_t head_index  = h & (BufferSize - 1);
		size_t firstChunk  = BufferSize - head_index;
		if (firstChunk >= toWrite)
		{
			memcpy(buffer + head_index, data, toWrite);
		}
		else
		{
			memcpy(buffer + head_index, data, firstChunk);
			memcpy(buffer, data + firstChunk, toWrite - firstChunk);
		}

		// 데이터 복사 완료 후 release → Consumer에게 데이터 가시성 보장
		head.store(h + toWrite, std::memory_order_release);
		return toWrite;
	}

	// Consumer 전용: 버퍼에서 데이터를 읽고 소비 (tail 전진)
	size_t Read(char* output, size_t max_length)
	{
		if (output == nullptr || max_length == 0) return 0;

		size_t t = tail.load(std::memory_order_relaxed);
		size_t h = head.load(std::memory_order_acquire);  // Producer의 head 동기화

		size_t available = h - t;
		size_t toRead    = (max_length < available) ? max_length : available;
		if (toRead == 0) return 0;

		size_t tail_index = t & (BufferSize - 1);
		size_t firstChunk = BufferSize - tail_index;
		if (firstChunk >= toRead)
		{
			memcpy(output, buffer + tail_index, toRead);
		}
		else
		{
			memcpy(output, buffer + tail_index, firstChunk);
			memcpy(output + firstChunk, buffer, toRead - firstChunk);
		}

		// 읽기 완료 후 release → Producer에게 공간 반환 가시성 보장
		tail.store(t + toRead, std::memory_order_release);
		return toRead;
	}

	// Consumer 전용: tail을 이동하지 않고 데이터만 들여다봄 (GetPacket 헤더 파싱용)
	bool PeekBlock(char* output, size_t length) const
	{
		if (output == nullptr || length == 0) return false;

		size_t t = tail.load(std::memory_order_relaxed);
		size_t h = head.load(std::memory_order_acquire);
		if (h - t < length) return false;

		size_t tail_index = t & (BufferSize - 1);
		size_t firstChunk = BufferSize - tail_index;
		if (firstChunk >= length)
		{
			memcpy(output, buffer + tail_index, length);
		}
		else
		{
			memcpy(output, buffer + tail_index, firstChunk);
			memcpy(output + firstChunk, buffer, length - firstChunk);
		}
		return true;
	}

	// disconnect/init 시에만 호출 — 동시 접근 없는 상태에서 사용
	void Clear()
	{
		head.store(0, std::memory_order_relaxed);
		tail.store(0, std::memory_order_relaxed);
	}
};
