#include "GlobalQueue_LockFree.h"
#include <emmintrin.h>
#include <Windows.h>
#include <stdexcept>
#include <cassert>
#include <cstdlib>

GlobalQueue_LockFree::~GlobalQueue_LockFree()
{
	delete[] m_buffer;
}

void GlobalQueue_LockFree::Init(uint32_t bufferSize)
{
	// 2의 거듭제곱인지 검사 (비트마스크 사용 위해 필수조건)
	if (bufferSize < 2 || (bufferSize & (bufferSize - 1)) != 0)
		throw std::invalid_argument("bufferSize must be power of 2");

	m_bufferMask = bufferSize - 1;
	m_buffer = new Cell[bufferSize];

	// 각 슬롯의 sequence를 index로 초기화
	for (uint32_t i = 0; i < bufferSize; ++i)
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
#ifdef USE_SPMC
	// ---------------------------------------------------------------
	//  SPMC
	// ---------------------------------------------------------------
	uint32_t pos = m_enqueuePos.load(std::memory_order_relaxed);
	Cell* cell = &m_buffer[pos & m_bufferMask];

	// cell이 free일때까지 기다림
	while (cell->sequence.load(std::memory_order_acquire) != pos)
		_mm_pause();

	cell->data = pRoom;
	cell->sequence.store(pos + 1, std::memory_order_release);
	m_enqueuePos.store(pos + 1, std::memory_order_relaxed);

	// 세마포어 토큰 1개 발급
	mSem.release();

#else
	// ---------------------------------------------------------------
	//  MPMC
	// ---------------------------------------------------------------
	Cell* cell = nullptr;
	uint32_t pos = m_enqueuePos.load(std::memory_order_relaxed);

	while (true)
	{
		cell = &m_buffer[pos & m_bufferMask];
		uint32_t seq = cell->sequence.load(std::memory_order_acquire);

		int32_t diff = static_cast<int32_t>(seq - pos);

		if (diff == 0)
		{
			// CAS 시도
			if (m_enqueuePos.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed))
				break;
		}
		else if (diff < 0)
		{
			// Strand 불변식: 큐 버퍼는 StrandProcessor::Init에서
			// GetNextPowerOf2(MaxRoomCount) 이상으로 동적 설정되고, 각 방은
			// mMsgCount==0 일 때만 1회 Push되므로 동시 적재 Room 수 <= 버퍼 크기다.
			// 여기 도달 = 설정 오류 또는 메모리 오염 → 조용히 진행하지 않고 즉시 덤프.
			LOG_ERROR("[FATAL] GlobalQueue Overflow! BufferMask=%u enqueuePos=%u", m_bufferMask, pos);
			assert(false && "CRITICAL: GlobalQueue Overflow — MaxRoomCount >= BufferSize");
			std::abort(); // Release 빌드에서도 CrashDump 트리거 (아래는 도달 불가)
		}
		else
		{
			// 다른 스레드가 먼저 CAS에 성공해서 pos가 올라감
			pos = m_enqueuePos.load(std::memory_order_relaxed);
		}
	}
	// 성공적으로 자리를 확보했으니 데이터 저장
	cell->data = pRoom;

	// Consumer에게 읽기 준비완료 신호 전달
	cell->sequence.store(pos + 1, std::memory_order_release);

	// 세마포어 토큰 1개 발급 — Pop()의 acquire()가 대기 중이라면 즉시 기상
	mSem.release();
#endif
}

Room* GlobalQueue_LockFree::Pop()
{
	// ── Gatekeeper ────────────────────────────────────────────────────────────
	// 토큰 1개 소비. 큐가 비어있으면 WaitOnAddress로 블로킹 (CPU 0%).
	// 통과 조건: 아이템 토큰(Push가 발급) 또는 shutdown 토큰(Shutdown이 발급)
	mSem.acquire();
	// ──────────────────────────────────────────────────────────────────────────

	Cell* cell = nullptr;
	uint32_t pos = m_dequeuePos.load(std::memory_order_relaxed);

	while (true)
	{
		cell = &m_buffer[pos & m_bufferMask];
		uint32_t seq = cell->sequence.load(std::memory_order_acquire);

		// seq == pos+1 이어야 데이터가 읽기 준비 완료된 것
		int32_t diff = static_cast<int32_t>(seq - (pos + 1));

		if (diff == 0)
		{
			// 내 슬롯 확정
			if (m_dequeuePos.compare_exchange_weak(pos, pos + 1,
				std::memory_order_relaxed))
				break;
		}
		else if (diff < 0)
		{
			// 큐가 비어있음 → shutdown 토큰인지 확인
			// ※ acquire()를 이미 아이템 토큰으로 통과했을 때도 diff<0이 될 수 있음:
			//   Producer가 슬롯을 예약(CAS)했지만 sequence 갱신 전인 경우 (선점 지연).
			//   이 경우 m_shutdown == false이므로 짧은 spin 후 재시도.
			if (m_shutdown.load(std::memory_order_acquire))
			{
				mSem.release(); // cascade: 다음 대기 스레드에게 shutdown 신호 전파
				return nullptr;
			}
			_mm_pause();
			pos = m_dequeuePos.load(std::memory_order_relaxed);
		}
		else
		{
			// 다른 Consumer가 이 슬롯을 먼저 가져감 (CAS 경쟁) → pos 갱신 후 재시도
			pos = m_dequeuePos.load(std::memory_order_relaxed);
		}
	}

	// 데이터 읽기
	Room* data = cell->data;

	// 슬롯 재활용 — Producer에게 빈자리 신호
	cell->sequence.store(pos + m_bufferMask + 1, std::memory_order_release);

	return data;
}
