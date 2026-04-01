#include "GlobalQueue_LockFree.h"
#include <emmintrin.h>
#include <Windows.h>
#include <stdexcept>

void GlobalQueue_LockFree::Init(uint32_t bufferSize)
{
	// 2의 거듭제곱인지 검사 (비트마스크 사용 위해 필수조건)
	if (bufferSize < 2 || (bufferSize & (bufferSize - 1)) != 0)
		throw std::invalid_argument("bufferSize must be power of 2");

	m_bufferMask = bufferSize - 1;
	m_buffer = new Cell[bufferSize];
	// 각 슬롯의 sequence를 index로 초기화
	for (uint32_t i = 0;i < bufferSize;++i)
	{
		m_buffer[i].sequence.store(i, std::memory_order_relaxed);
		m_buffer[i].data = nullptr;
	}

	m_enqueuePos.store(0, std::memory_order_relaxed);
	m_dequeuePos.store(0, std::memory_order_relaxed);
	m_shutdown.store(false, std::memory_order_relaxed);
}

void GlobalQueue_LockFree::Push(Room* pRoom)
{
	Cell* cell = nullptr;
	uint32_t pos = m_enqueuePos.load(std::memory_order_relaxed);

	while (true)
	{
		cell = &m_buffer[pos & m_bufferMask];
		uint32_t seq = cell->sequence.load(std::memory_order_acquire);

		// 오버플로우 안전한 부호있는 정수비교
		int32_t diff = static_cast<int32_t>(seq - pos);

		if (diff == 0)
		{
			// CAS 시도
			if (m_enqueuePos.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed))
				break;
		}
		else if (diff < 0)
		{
			// 큐가 가득 참
			// 다른 스레드가 먼저 넣어서 밀린 경우이므로 pos 최신화
			pos = m_enqueuePos.load(std::memory_order_relaxed);
		}
		else
		{
			// 다른 스레드가 먼저 CAS에 성공해서 pos가 올라감
			pos = m_enqueuePos.load(std::memory_order_relaxed);
		}
	}

	// 성공적으로 자리를 확보했으니 데이터 저장
	cell->data = pRoom;

	// Consumer에게 읽기 준비완료 신호 전달 (release)
	cell->sequence.store(pos + 1, std::memory_order_release);
}

Room* GlobalQueue_LockFree::Pop()
{
	Cell* cell = nullptr;
	uint32_t pos = m_dequeuePos.load(std::memory_order_relaxed);
	uint32_t spinCount = 0;

	while (true)
	{
		cell = &m_buffer[pos & m_bufferMask];
		uint32_t seq = cell->sequence.load(std::memory_order_acquire);

		// Pop에서는 seq가 pos + 1 이어야 데이터가 있는 것
		int32_t diff = static_cast<int32_t>(seq - (pos + 1));

		if (diff == 0)
		{
			// CAS 시도
			if (m_dequeuePos.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed))
				break;
		}
		else if (diff < 0)
		{
			// 큐가 비어있음 -> shutdown 체크를 해야 잔여 패킷 처리가능
			if (m_shutdown.load(std::memory_order_acquire))
				return nullptr;

			// 자연스런 backoff
			if (spinCount < 1024)
			{
				_mm_pause(); // 짧은 spin
			}
			else
			{
				Sleep(0);	// OS 스케줄러에 양보
			}
			spinCount++;
			pos = m_dequeuePos.load(std::memory_order_relaxed);
		}
		else
		{
			// 다른 Consumer가 먼저 가져감
			pos = m_dequeuePos.load(std::memory_order_relaxed);
		}
	}

	// 데이터 읽기
	Room* data = cell->data;

	// 슬롯 재활용을 위해 Producer에게 빈자리 신호
	cell->sequence.store(pos + m_bufferMask + 1, std::memory_order_release);

	return data;
}

