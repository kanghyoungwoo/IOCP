#pragma once


template<size_t BufferSize>
class RingBuffer
{
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
		// 버퍼가 가득 찼는지 확인
		return full;
	}

	size_t Size() const
	{
		// 현재 저장된 데이터 크기 반환
		if (full)
			return BufferSize;
		if (head >= tail)
			return head - tail;
		if (tail > head)
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
		// 실제로 쓴 바이트 수 반환
		if (data == nullptr || length == 0)
			return 0;
		size_t written_count = 0;
		for (size_t i = 0;i < length;i++)
		{
			if (full)
				break;
			buffer[head] = data[i];
			head = (head + 1) % BufferSize;
			written_count++;

			if (head == tail)
				full = true;
		}

		return written_count;
	}

	size_t Read(char* output, size_t max_length)
	{
		// 실제로 읽은 바이트 수 반환
		if (output == nullptr || max_length == 0)
			return 0;
		size_t read_count = 0;
		while (read_count < max_length && !IsEmpty())
		{
			output[read_count] = buffer[tail];
			tail = (tail + 1) % BufferSize;
			read_count++;
			full = false;
		}
		return read_count;
	}

	bool Peek(char& byte, size_t offset = 0) const
	{
		// offset: tail로 부터 몇 번째 데이터를 볼지 (기본값 0 = 첫 번째)
		// 성공 true, 실패 false
		if (IsEmpty() || offset >= Size())
			return false;
		size_t peek_pos = (tail + offset) % BufferSize;
		byte = buffer[peek_pos];

		return true;
	}

	void Clear()
	{
		head = 0;
		tail = 0;
		full = false;
	}
};
