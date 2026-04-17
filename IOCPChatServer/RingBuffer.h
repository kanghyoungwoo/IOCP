#pragma once


template<size_t BufferSize>
class RingBuffer
{
	static_assert(BufferSize > 0 && (BufferSize & (BufferSize - 1)) == 0, "RingBuffer: BufferSize must be a power of 2");
private:
	char buffer[BufferSize];
	size_t head = 0;
	size_t tail = 0;

	bool full = false;
public:
	RingBuffer() = default;
	~RingBuffer() = default;

	enum class BufferResult
	{
		SUCCESS,
		BUFFER_FULL,
		INVALID_DATA
	};

	bool IsEmpty() const
	{
		// 버퍼가 비어있는지 확인
		if ((head == tail) && !full)
			return true;
		else
			return false;
	}

	bool IsFull() const
	{
		// 버퍼가 완전히 찼는지 확인
		return full;
	}

	size_t Size() const
	{
		// 현재 저장된 데이터 크기 반환
		if (full)
			return BufferSize;
		if (head >= tail)
			return head - tail;
		//if (tail > head)
		return BufferSize - tail + head;
	}

	bool WriteByte(char byte)
	{
		// 성공 true, 실패 false
		if (full)
			return false;
		buffer[head] = byte;
		head = (head + 1) % BufferSize;
		if (head == tail)
			full = true;

		return true;
	}

	bool ReadByte(char& byte)
	{
		// 성공 true, 실패 false
		if (IsEmpty())
			return false;
		byte = buffer[tail];
		tail = (tail + 1) % BufferSize;
		full = false;

		return true;
	}

	size_t Write(const char* data, size_t length)
	{
		// 쓰여진 총 바이트 수 반환
		if (data == nullptr || length == 0)
			return 0;

		size_t available = BufferSize - Size();
		size_t toWrite = (length < available) ? length : available;
		if (toWrite == 0)
			return 0;

		size_t firstChunk = BufferSize - head;	// head -> 버퍼 끝까지 남은공간
		if (firstChunk >= toWrite)
		{
			memcpy(buffer + head, data, toWrite);
		}
		else
		{
			memcpy(buffer + head, data, firstChunk);	// 경계 전
			memcpy(buffer, data + firstChunk, toWrite - firstChunk);	// wrap 후 
		}

		head = (head + toWrite) & (BufferSize - 1);
		full = (head == tail);

		return toWrite;
	}

	size_t Read(char* output, size_t max_length)
	{
		// 읽어온 총 바이트 수 반환
		if (output == nullptr || max_length == 0)
			return 0;

		size_t available = Size();
		size_t toRead = (max_length < available) ? max_length : available;
		if (toRead == 0)
			return 0;
		
		size_t firstChunk = BufferSize - tail;	// tail -> 버퍼 끝까지 읽을 수 있는 양
		if (firstChunk >= toRead)
		{
			memcpy(output, buffer + tail, toRead);
		}
		else
		{
			memcpy(output, buffer + tail, firstChunk);
			memcpy(output + firstChunk, buffer, toRead - firstChunk);
		}
		tail = (tail + toRead) & (BufferSize - 1);
		full = false;

		return toRead;
	}

	bool Peek(char& byte, size_t offset = 0) const
	{
		// offset: tail로부터 몇 번째 데이터를 볼지 (기본값 0 = 첫 번째)
		// 성공 true, 실패 false
		if (IsEmpty() || offset >= Size())
			return false;
		size_t peek_pos = (tail + offset) % BufferSize;
		byte = buffer[peek_pos];

		return true;
	}

	bool PeekBlock(char* output, size_t length) const
	{
		if (output == nullptr || length == 0 || Size() < length)
			return false;

		size_t firstChunk = BufferSize - tail;
		if (firstChunk >= length)
		{
			memcpy(output, buffer + tail, length);
		}
		else
		{
			memcpy(output, buffer + tail, firstChunk);
			memcpy(output + firstChunk, buffer, length - firstChunk);
		}

		return true;
	}

	void Clear()
	{
		head = 0;
		tail = 0;
		full = false;
	}
};
