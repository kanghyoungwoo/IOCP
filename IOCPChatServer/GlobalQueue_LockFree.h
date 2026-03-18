#pragma once
#include "Room.h"
#include <atomic>
#include <emmintrin.h>
#include <Windows.h>
#include <cstdint>
#include <stdexcept>

class GlobalQueue_LockFree
{
private:
	struct Cell
	{
		std::atomic<int32_t> sequence;
		Room* data;
	};

	Cell* m_buffer = nullptr;	//	고정배열
	uint32_t m_bufferMask = 0;	//	비트마스크로 모듈러 연산
	alignas(64) std::atomic<uint32_t> m_enqueuePos;	// Producer 위치
	alignas(64) std::atomic<uint32_t> m_dequeuePos;	// Consumer 위치
	std::atomic<bool> m_shutdown;

public:
	GlobalQueue_LockFree()
	{
		m_enqueuePos = 0;
		m_dequeuePos = 0;
		m_shutdown = false;

	}
	~GlobalQueue_LockFree()
	{
		if (m_buffer)
		{
			delete[] m_buffer;
		}
	}

	void Init(uint32_t bufferSize)
	{
		// 2의 제곱수인지 검사(비트마스크 쓰기 위한 필수조건)
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

	void Push(Room* pRoom)
	{
		Cell* cell = nullptr;
		uint32_t pos = m_enqueuePos.load(std::memory_order_relaxed);

		while (true)
		{
			cell = &m_buffer[pos & m_bufferMask];
			uint32_t seq = cell->sequence.load(std::memory_order_acquire);

			// 오버플로우 방어하는 부호있는 정수연산
			int32_t diff = static_cast<int32_t>(seq - pos);

			if (diff == 0)
			{
				// CAS시도
				if (m_enqueuePos.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed))
					break;
			}
			else if (diff < 0)
			{
				// 큐가 꽉참
				//다른 스레드가 먼저 넣었을 수도 있으니 pos 최신화
				pos = m_enqueuePos.load(std::memory_order_relaxed);
			}
			else
			{
				// 다른 쓰레드가 먼저 CAS에 성공해서 pos가 올라감
				pos = m_enqueuePos.load(std::memory_order_relaxed);
			}
		}

		// 성공적으로 자리를 확보했으니 데이터 쓰기
		cell->data = pRoom;

		// Consumer에게 슬롯 준비완료 신호 보냄 release
		cell->sequence.store(pos + 1, std::memory_order_release);
	}

	Room* Pop()
	{
		Cell* cell = nullptr;
		uint32_t pos = m_dequeuePos.load(std::memory_order_relaxed);
		uint32_t spinCount = 0;

		while (true)
		{
			cell = &m_buffer[pos & m_bufferMask];
			uint32_t seq = cell->sequence.load(std::memory_order_acquire);

			// Pop에선 seq가 pos + 1 이어야 데이터가 있는것
			int32_t diff = static_cast<int32_t>(seq - (pos + 1));

			if (diff == 0)
			{
				// CAS 시도
				if (m_dequeuePos.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed))
					break;
			}
			else if (diff < 0)
			{
				// 큐가 비어있음 -> shutdown 체크를 해야 잔여 패킷 소진가능
				if (m_shutdown.load(std::memory_order_acquire))
					return nullptr;

				// 자원절약 backoff
				if (spinCount < 1024)
				{
					_mm_pause(); // 짧은 spin
				}
				else
				{
					Sleep(0);	// OS스케줄러에 양보
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


	void Shutdown()
	{
		// 큐의 문을 닫음, 더이상 빈 큐 대기 X
		m_shutdown.store(true, std::memory_order_release);
	}
};

