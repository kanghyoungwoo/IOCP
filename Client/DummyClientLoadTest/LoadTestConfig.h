#pragma once

#include <vector>

struct TestPhase
{
	int clientCount;		// total client count at end of this phase (cumulative)
	int delayBetweenMs;		// delay between client creation (ms)
	int holdTimeSec;		// stabilization hold time (seconds)
};

struct LoadTestConfig
{
	//static constexpr const char* SERVER_IP = "127.0.0.1";
	static constexpr const char* SERVER_IP = "3.39.246.141";
	static constexpr int SERVER_PORT = 11021;

	static constexpr int MAX_ROOMS = 250;
	static constexpr int MAX_USERS_PER_ROOM = 8;

	//static constexpr int CHAT_INTERVAL_SEC = 100;
	static constexpr int CHAT_INTERVAL_MS = 100;

	static constexpr int STATS_PRINT_INTERVAL_SEC = 10;

	static std::vector<TestPhase> GetDefaultPhases()
	{
		return {
			{  500, 20, 30 },	// Phase 1: Warm-up       (500 clients, 30s hold)
			{ 1000, 10, 60 },	// Phase 2: Medium load  (1000 clients, 60s hold)
			{ 1500, 10, 60 },	// Phase 3: High load    (1500 clients, 60s hold)
			{ 2000, 10, 60 },	// Phase 4: Peak load    (2000 clients, 60s hold)
		};
	}
};
