#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "BotManager.h"
#include "Config.h"

#include <cstdio>
#include <string>
#include <iostream>

void PrintUsage()
{
	printf("=== ChatLoadTester ===\n");
	printf("Usage: ChatLoadTester.exe [options]\n");
	printf("  -ip <server_ip>      서버 IP (기본: 127.0.0.1)\n");
	printf("  -port <port>         서버 포트 (기본: 11021)\n");
	printf("  -bots <count>        봇 수 (기본: 2500)\n");
	printf("  -workers <count>     IOCP Worker 스레드 수 (기본: 2)\n");
	printf("  -id <instance_id>    인스턴스 ID 0~3 (기본: 0)\n");
	printf("  -csv <filepath>      CSV 출력 파일 (기본: metrics.csv)\n");
	printf("\n");
}

Config ParseArgs(int argc, char* argv[])
{
	Config config;

	for (int i = 1; i < argc; ++i)
	{
		std::string arg = argv[i];

		if (arg == "-ip" && i + 1 < argc)
			config.ServerIP = argv[++i];
		else if (arg == "-port" && i + 1 < argc)
			config.ServerPort = (uint16_t)atoi(argv[++i]);
		else if (arg == "-bots" && i + 1 < argc)
			config.BotCount = (uint32_t)atoi(argv[++i]);
		else if (arg == "-workers" && i + 1 < argc)
			config.WorkerThreadCount = (uint32_t)atoi(argv[++i]);
		else if (arg == "-id" && i + 1 < argc)
			config.InstanceId = (uint32_t)atoi(argv[++i]);
		else if (arg == "-csv" && i + 1 < argc)
			config.CsvFilePath = argv[++i];
	}

	return config;
}

int main(int argc, char* argv[])
{
	SetConsoleOutputCP(CP_UTF8);
	PrintUsage();

	Config config = ParseArgs(argc, argv);

	printf("설정:\n");
	printf("  서버: %s:%u\n", config.ServerIP.c_str(), config.ServerPort);
	printf("  인스턴스 ID: %u\n", config.InstanceId);
	printf("  봇 수: %u\n", config.BotCount);
	//printf("  Worker 스레드: %u\n", config.WorkerThreadCount);
	printf("  -workers <count>     IOCP Worker 스레드 수 (기본: 2)\n");
	printf("  채팅 간격: %u~%u ms\n", config.ChatIntervalMinMs, config.ChatIntervalMaxMs);
	printf("  방 수: %u (방당 %u명)\n", config.RoomCount, config.MaxUsersPerRoom);
	printf("  CSV: %s\n", config.CsvFilePath.c_str());
	printf("\n");

	BotManager botManager;

	if (!botManager.Init(config))
	{
		printf("[ERROR] BotManager 초기화 실패\n");
		return 1;
	}

	botManager.Start();

	printf("\n'quit' 입력 시 종료\n\n");

	while (true)
	{
		std::string input;
		std::getline(std::cin, input);

		if (input == "quit")
			break;
	}

	botManager.Stop();

	return 0;
}
