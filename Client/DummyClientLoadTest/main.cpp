#include <iostream>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>

#pragma comment(lib, "ws2_32.lib")
#include <winsock2.h>
#include <ws2tcpip.h>

#include "DummyClient.h"
#include "LoadTestConfig.h"
#include "LoadTestStats.h"

// global stop signal (referenced by DummyClient via extern)
std::atomic<bool> g_running{ true };

int main()
{
	WSADATA wsaData;
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
	{
		printf("WSAStartup failed\n");
		return 1;
	}

	auto phases = LoadTestConfig::GetDefaultPhases();
	auto& stats = LoadTestStats::Instance();

	std::vector<std::thread> clientThreads;
	int currentClientCount = 0;

	printf("========================================\n");
	printf("  IOCP Chat Server - Load Test Tool\n");
	printf("  Server: %s:%d\n", LoadTestConfig::SERVER_IP, LoadTestConfig::SERVER_PORT);
	printf("  Phases: %d\n", (int)phases.size());
	printf("  Room distribution: %d rooms x %d users\n",
		LoadTestConfig::MAX_ROOMS, LoadTestConfig::MAX_USERS_PER_ROOM);
	printf("========================================\n\n");

	// stats printer thread (prints every N seconds)
	std::thread statsPrinter([&]()
	{
		while (g_running)
		{
			std::this_thread::sleep_for(
				std::chrono::seconds(LoadTestConfig::STATS_PRINT_INTERVAL_SEC));
			if (g_running)
				stats.PrintSummary();
		}
	});

	// phased client creation
	for (int p = 0; p < (int)phases.size(); p++)
	{
		auto& phase = phases[p];
		int targetCount = phase.clientCount;

		printf("\n>>> Phase %d: Ramping %d -> %d clients (delay=%dms)\n",
			p + 1, currentClientCount, targetCount, phase.delayBetweenMs);

		for (int i = currentClientCount; i < targetCount; i++)
		{
			clientThreads.emplace_back([](int clientId)
			{
				DummyClient client(clientId);
				client.Run();
			}, i);

			std::this_thread::sleep_for(
				std::chrono::milliseconds(phase.delayBetweenMs));
		}
		currentClientCount = targetCount;

		// Reset per-phase stats AFTER ramp-up (clean measurement window)
		printf(">>> Phase %d: Ramp complete. Resetting stats for measurement.\n", p + 1);
		stats.ResetPhaseStats();

		printf(">>> Phase %d: Measuring for %d seconds... (%d clients active)\n",
			p + 1, phase.holdTimeSec, targetCount);
		std::this_thread::sleep_for(std::chrono::seconds(phase.holdTimeSec));

		printf("\n>>> Phase %d RESULTS (%d clients):\n", p + 1, targetCount);
		stats.PrintSummary();
	}

	printf("\n>>> All phases complete. Press Enter to stop...\n");
	std::cin.get();

	g_running = false;

	printf(">>> Waiting for all clients to disconnect...\n");

	for (auto& th : clientThreads)
	{
		if (th.joinable())
			th.join();
	}

	if (statsPrinter.joinable())
		statsPrinter.join();

	printf("\n>>> FINAL RESULTS:\n");
	stats.PrintSummary();
	// 파일 저장
	auto now = std::chrono::system_clock::now();
	auto t = std::chrono::system_clock::to_time_t(now);
	struct tm lt;
	localtime_s(&lt, &t);
	char filename[128];
	sprintf_s(filename, "LoadTestResult_%04d_%02d%02d_%02d%02d%02d.txt",
		lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday,
		lt.tm_hour, lt.tm_min, lt.tm_sec);

	stats.SaveToFile(filename);
	printf(">>> Results saved to %s\n", filename);
	WSACleanup();
	return 0;
}
