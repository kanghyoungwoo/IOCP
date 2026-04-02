#pragma once
#include <atomic>
#include <cstdint>

static constexpr uint16_t MAX_PACKET_BODY_SIZE = 512;

struct PacketJob
{
	// 라우팅 정보
	uint32_t clientIndex = 0;
	//uint32_t roomIndex = 0;
	uint32_t targetGeneration = 0;	// 목표 세대값(room generation)
	uint32_t sessionGeneration = 0;	// ABA방지

	// 패킷 데이터
	uint16_t packetId = 0;
	uint16_t dataSize = 0;
	char body[MAX_PACKET_BODY_SIZE] = {};

	// MPSC Queue 링크용
	std::atomic<PacketJob*> mpscNext{ nullptr };

	// Object Pool 링크용
	uint32_t poolNext = UINT32_MAX;
};
