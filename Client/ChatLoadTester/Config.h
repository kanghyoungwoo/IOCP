#pragma once
#include <string>
#include <cstdint>

struct Config
{
	//std::string ServerIP = "127.0.0.1";
	std::string ServerIP = "3.39.246.141";

	uint16_t ServerPort = 11021;
	uint32_t BotCount = 2500;

	// 인스턴스 ID (0~3): EC2별 고유 ID → UserID/방 배정 충돌 방지
	uint32_t InstanceId = 0;

	// Ramp-up: 100ms마다 ConnectBatchSize개씩 접속
	uint32_t RampUpIntervalMs = 100;
	uint32_t ConnectBatchSize = 4;

	// 채팅 시나리오
	uint32_t ChatIntervalMinMs = 3000;   // 최소 채팅 간격 (ms)
	uint32_t ChatIntervalMaxMs = 10000;  // 최대 채팅 간격 (ms)

	// 방 설정
	uint32_t RoomCount = 1250;
	uint32_t MaxUsersPerRoom = 8;

	// 지표 출력 주기
	uint32_t MetricsIntervalMs = 1000;

	// IOCP Worker 스레드 수
	uint32_t WorkerThreadCount = 2;

	// CSV 출력 파일
	std::string CsvFilePath = "metrics.csv";
};
