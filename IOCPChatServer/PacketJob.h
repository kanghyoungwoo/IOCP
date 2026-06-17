#pragma once
#include <atomic>
#include <cstdint>
#include <cassert>
#include "StrandCallback.h"

static constexpr uint16_t MAX_PACKET_BODY_SIZE = 512;

struct PacketJob
{
	std::atomic<PacketJob*> mpscNext{ nullptr };  // 8 bytes, offset 0
	// 두 phase 공통 필드 (라우팅/ generation)
	uint32_t clientIndex = 0;               // 4 bytes, offset 8
	uint32_t sessionGeneration = 0;               // 4 bytes, offset 12
	std::atomic<uint32_t> poolNext{UINT32_MAX};

	enum class Phase : uint8_t { JOB, STRAND_CALLBACK };
	Phase phase = Phase::JOB;      // 1 byte,  offset 20
	uint8_t  _pad[3] = {};              // 명시적 패딩, offset 21

	//enum class Phase : uint8_t{ JOB, CALLBACK };

	union
	{
		struct {   // Phase::JOB
			uint32_t targetGeneration;   // room generation
			uint16_t packetId;
			uint16_t dataSize;
			char     body[MAX_PACKET_BODY_SIZE];
		} job;

		struct {   // Phase::CALLBACK
			StrandCallbackType type;
			int32_t  roomNumber;
			uint16_t result;
		} cb;
	};


	uint16_t GetPacketId() const
	{
		assert(phase == Phase::JOB && "JOB phase에서만 접근 가능");
		return job.packetId;
	}

	StrandCallbackType GetCallbackType() const
	{
		assert(phase == Phase::STRAND_CALLBACK && "CALLBACK phase에서만 접근 가능");
		return cb.type;
	}

	void TransitionToCallback(StrandCallbackType type, int32_t roomNum = -1, uint16_t result = 0)
	{
		assert(phase == Phase::JOB && "이미 CALLBACK 상태");
		phase = Phase::STRAND_CALLBACK;
		cb.type = type;
		cb.roomNumber = roomNum;
		cb.result = result;
		// mpscNext 초기화는 5번에서 결론 내림
	}
	//uint32_t clientIndex = 0;
	////uint32_t roomIndex = 0;
	//uint32_t targetGeneration = 0;	// 목표 세대값(room generation)
	//uint32_t sessionGeneration = 0;	// ABA방지

	//// 패킷 데이터
	//uint16_t packetId = 0;
	//uint16_t dataSize = 0;
	//char body[MAX_PACKET_BODY_SIZE] = {};

	//// MPSC Queue 링크용
	//std::atomic<PacketJob*> mpscNext{ nullptr };

	//// Object Pool 링크용
	//uint32_t poolNext = UINT32_MAX;
};
