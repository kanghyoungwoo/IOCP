#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <atomic>
#include <mutex>
#include <vector>
#include <algorithm>
#include <chrono>
#include <cstdio>

class LoadTestStats
{
public:
	static LoadTestStats& Instance()
	{
		static LoadTestStats instance;
		return instance;
	}

	// Connection counters
	std::atomic<int> connectAttempted{ 0 };
	std::atomic<int> connectSuccess{ 0 };
	std::atomic<int> connectFailed{ 0 };

	// Login counters
	std::atomic<int> loginSent{ 0 };
	std::atomic<int> loginSuccess{ 0 };
	std::atomic<int> loginFailed{ 0 };

	// Room enter counters
	std::atomic<int> roomEnterSent{ 0 };
	std::atomic<int> roomEnterSuccess{ 0 };
	std::atomic<int> roomEnterFailed{ 0 };

	// Chat counters
	std::atomic<int> chatSent{ 0 };
	std::atomic<int> chatSuccess{ 0 };
	std::atomic<int> chatFailed{ 0 };
	std::atomic<int> chatNotifyReceived{ 0 };

	// Disconnect counter
	std::atomic<int> disconnected{ 0 };

	// Response time recording (microseconds)
	//void RecordLoginResponseTime(long long microseconds)
	//{
	//	std::lock_guard<std::mutex> lock(m_loginTimeMutex);
	//	m_loginResponseTimes.push_back(microseconds);
	//}

	void RecordLoginResponseTime(long long us) {
		m_loginRttSum.fetch_add(us, std::memory_order_relaxed);
		m_loginRttCount.fetch_add(1, std::memory_order_relaxed);
		long long prev = m_loginRttMax.load(std::memory_order_relaxed);
		while (us > prev && !m_loginRttMax.compare_exchange_weak(prev, us));
		for (int i = 0; i < BUCKET_COUNT; i++) {
			if (us <= BUCKET_BOUNDS[i]) {
				m_loginBuckets[i].fetch_add(1, std::memory_order_relaxed);
				break;
			}
		}
	}
	//void RecordRoomEnterResponseTime(long long microseconds)
	//{
	//	std::lock_guard<std::mutex> lock(m_roomTimeMutex);
	//	m_roomEnterResponseTimes.push_back(microseconds);
	//}

	//void RecordChatResponseTime(long long microseconds)
	//{
	//	std::lock_guard<std::mutex> lock(m_chatTimeMutex);
	//	m_chatResponseTimes.push_back(microseconds);
	//}

	void RecordRoomEnterResponseTime(long long us) {
		m_roomRttSum.fetch_add(us, std::memory_order_relaxed);
		m_roomRttCount.fetch_add(1, std::memory_order_relaxed);
		long long prev = m_roomRttMax.load(std::memory_order_relaxed);
		while (us > prev && !m_roomRttMax.compare_exchange_weak(prev, us));
		for (int i = 0; i < BUCKET_COUNT; i++) {
			if (us <= BUCKET_BOUNDS[i]) {
				m_roomBuckets[i].fetch_add(1, std::memory_order_relaxed);
				break;
			}
		}
	}

	void RecordChatResponseTime(long long us) {
		m_chatRttSum.fetch_add(us, std::memory_order_relaxed);
		m_chatRttCount.fetch_add(1, std::memory_order_relaxed);
		long long prev = m_chatRttMax.load(std::memory_order_relaxed);
		while (us > prev && !m_chatRttMax.compare_exchange_weak(prev, us));
		for (int i = 0; i < BUCKET_COUNT; i++) {
			if (us <= BUCKET_BOUNDS[i]) {
				m_chatBuckets[i].fetch_add(1, std::memory_order_relaxed);
				break;
			}
		}
	}

	struct TimingStats
	{
		long long avg_us = 0;
		long long max_us = 0;
		//long long min_us = 0;
		//long long max_us = 0;
		//long long avg_us = 0;
		//long long p50_us = 0;
		//long long p95_us = 0;
		//long long p99_us = 0;
		int count = 0;
	};

	TimingStats GetLoginTimingStats()
	{
		TimingStats stats;
		stats.count = m_loginRttCount.load();
		if (stats.count > 0)
			stats.avg_us = m_loginRttSum.load() / stats.count;
		stats.max_us = m_loginRttMax.load();
		return stats;
		//std::lock_guard<std::mutex> lock(m_loginTimeMutex);
		//return CalcStats(m_loginResponseTimes);
	}

	TimingStats GetRoomEnterTimingStats() {
		TimingStats stats;
		stats.count = m_roomRttCount.load();
		if (stats.count > 0)
			stats.avg_us = m_roomRttSum.load() / stats.count;
		stats.max_us = m_roomRttMax.load();
		return stats;
	}

	TimingStats GetChatTimingStats() {
		TimingStats stats;
		stats.count = m_chatRttCount.load();
		if (stats.count > 0)
			stats.avg_us = m_chatRttSum.load() / stats.count;
		stats.max_us = m_chatRttMax.load();
		return stats;
	}
	//TimingStats GetRoomEnterTimingStats()
	//{
	//	std::lock_guard<std::mutex> lock(m_roomTimeMutex);
	//	return CalcStats(m_roomEnterResponseTimes);
	//}

	//TimingStats GetChatTimingStats()
	//{
	//	std::lock_guard<std::mutex> lock(m_chatTimeMutex);
	//	return CalcStats(m_chatResponseTimes);
	//}

	// Reset per-phase stats (call after ramp-up, before measurement period)
	void ResetPhaseStats()
	{
		m_loginRttSum = 0; m_loginRttMax = 0; m_loginRttCount = 0;
		m_roomRttSum = 0;  m_roomRttMax = 0;  m_roomRttCount = 0;
		m_chatRttSum = 0;  m_chatRttMax = 0;  m_chatRttCount = 0;
		//{
		//	std::lock_guard<std::mutex> lock(m_loginTimeMutex);
		//	m_loginResponseTimes.clear();
		//}
		//{
		//	std::lock_guard<std::mutex> lock(m_roomTimeMutex);
		//	m_roomEnterResponseTimes.clear();
		//}
		//{
		//	std::lock_guard<std::mutex> lock(m_chatTimeMutex);
		//	m_chatResponseTimes.clear();
		//}
		// snapshot current counters for throughput delta
		mThroughput.prevChatSuccess = chatSuccess.load();
		mThroughput.prevChatNotify = chatNotifyReceived.load();
		mThroughput.prevTime = std::chrono::steady_clock::now();
		mThroughput.initialized = true;
	}

	void PrintSummary()
	{
		printf("\n========== LOAD TEST STATISTICS ==========\n");
		printf("  Connections:  attempted=%d  success=%d  failed=%d\n",
			connectAttempted.load(), connectSuccess.load(), connectFailed.load());
		printf("  Login:        sent=%d  success=%d  failed=%d\n",
			loginSent.load(), loginSuccess.load(), loginFailed.load());
		printf("  Room Enter:   sent=%d  success=%d  failed=%d\n",
			roomEnterSent.load(), roomEnterSuccess.load(), roomEnterFailed.load());
		int chatNoResponse = chatSent.load() - chatSuccess.load() - chatFailed.load();
		printf("  Chat:         sent=%d  success=%d  failed=%d  noResponse=%d  notify=%d\n",
			chatSent.load(), chatSuccess.load(), chatFailed.load(), chatNoResponse, chatNotifyReceived.load());
		printf("  Disconnected: %d\n", disconnected.load());

		// Throughput (delta since last reset or last print)
		auto now = std::chrono::steady_clock::now();
		if (mThroughput.initialized)
		{
			auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
				now - mThroughput.prevTime).count();
			if (elapsedMs > 0)
			{
				int deltaChatResp = chatSuccess.load() - mThroughput.prevChatSuccess;
				int deltaNotify = chatNotifyReceived.load() - mThroughput.prevChatNotify;
				printf("  Throughput:    chat_resp=%.1f/sec  notify=%.1f/sec  (%.1fs interval)\n",
					deltaChatResp * 1000.0 / elapsedMs,
					deltaNotify * 1000.0 / elapsedMs,
					elapsedMs / 1000.0);
			}
		}
		mThroughput.prevChatSuccess = chatSuccess.load();
		mThroughput.prevChatNotify = chatNotifyReceived.load();
		mThroughput.prevTime = now;
		mThroughput.initialized = true;

		auto loginTiming = GetLoginTimingStats();
		if (loginTiming.count > 0)
		{
			printf("  Login RTT:     avg=%.1fms  max=%.1fms  p50=<%.1fms  p95=<%.1fms  p99=<%.1fms  (n=%d)\n",
				loginTiming.avg_us / 1000.0, loginTiming.max_us / 1000.0,
				GetPercentile(m_loginBuckets, loginTiming.count, 0.50) / 1000.0,
				GetPercentile(m_loginBuckets, loginTiming.count, 0.95) / 1000.0,
				GetPercentile(m_loginBuckets, loginTiming.count, 0.99) / 1000.0,
				loginTiming.count);
		}

		auto roomTiming = GetRoomEnterTimingStats();
		if (roomTiming.count > 0)
		{
			printf("  RoomEnter RTT: avg=%.1fms  max=%.1fms  p50=<%.1fms  p95=<%.1fms  p99=<%.1fms  (n=%d)\n",
				roomTiming.avg_us / 1000.0, roomTiming.max_us / 1000.0,
				GetPercentile(m_roomBuckets, roomTiming.count, 0.50) / 1000.0,
				GetPercentile(m_roomBuckets, roomTiming.count, 0.95) / 1000.0,
				GetPercentile(m_roomBuckets, roomTiming.count, 0.99) / 1000.0,
				roomTiming.count);
		}

		auto chatTiming = GetChatTimingStats();
		if (chatTiming.count > 0)
		{
			printf("  Chat RTT:      avg=%.1fms  max=%.1fms  p50=<%.1fms  p95=<%.1fms  p99=<%.1fms  (n=%d)\n",
				chatTiming.avg_us / 1000.0, chatTiming.max_us / 1000.0,
				GetPercentile(m_chatBuckets, chatTiming.count, 0.50) / 1000.0,
				GetPercentile(m_chatBuckets, chatTiming.count, 0.95) / 1000.0,
				GetPercentile(m_chatBuckets, chatTiming.count, 0.99) / 1000.0,
				chatTiming.count);
		}

		printf("==========================================\n\n");
	}

	long long GetPercentile(std::atomic<int> buckets[], int total, double pct) {
		int target = (int)(total * pct);
		int cumulative = 0;
		for (int i = 0; i < BUCKET_COUNT; i++) {
			cumulative += buckets[i].load(std::memory_order_relaxed);
			if (cumulative >= target)
				return BUCKET_BOUNDS[i];
		}
		return BUCKET_BOUNDS[BUCKET_COUNT - 1];
	}

	void SaveToFile(const char* filename)
	{
		FILE* fp = nullptr;
		fopen_s(&fp, filename, "w");
		if (!fp) return;

		fprintf(fp, "========== LOAD TEST STATISTICS ==========\n");
		fprintf(fp, "Connections:  attempted=%d  success=%d  failed=%d\n",
			connectAttempted.load(), connectSuccess.load(), connectFailed.load());
		fprintf(fp, "Login:        sent=%d  success=%d  failed=%d\n",
			loginSent.load(), loginSuccess.load(), loginFailed.load());
		fprintf(fp, "Room Enter:   sent=%d  success=%d  failed=%d\n",
			roomEnterSent.load(), roomEnterSuccess.load(), roomEnterFailed.load());
		int chatNoResponse = chatSent.load() - chatSuccess.load() - chatFailed.load();
		fprintf(fp, "Chat:         sent=%d  success=%d  failed=%d  noResponse=%d  notify=%d\n",
			chatSent.load(), chatSuccess.load(), chatFailed.load(), chatNoResponse, chatNotifyReceived.load());
		fprintf(fp, "Disconnected: %d\n", disconnected.load());

		auto loginTiming = GetLoginTimingStats();
		if (loginTiming.count > 0)
		{
			fprintf(fp, "Login RTT:     avg=%.1fms  max=%.1fms  p50=<%.1fms  p95=<%.1fms  p99=<%.1fms  (n=%d)\n",
				loginTiming.avg_us / 1000.0, loginTiming.max_us / 1000.0,
				GetPercentile(m_loginBuckets, loginTiming.count, 0.50) / 1000.0,
				GetPercentile(m_loginBuckets, loginTiming.count, 0.95) / 1000.0,
				GetPercentile(m_loginBuckets, loginTiming.count, 0.99) / 1000.0,
				loginTiming.count);
		}

		auto roomTiming = GetRoomEnterTimingStats();
		if (roomTiming.count > 0)
		{
			fprintf(fp, "RoomEnter RTT: avg=%.1fms  max=%.1fms  p50=<%.1fms  p95=<%.1fms  p99=<%.1fms  (n=%d)\n",
				roomTiming.avg_us / 1000.0, roomTiming.max_us / 1000.0,
				GetPercentile(m_roomBuckets, roomTiming.count, 0.50) / 1000.0,
				GetPercentile(m_roomBuckets, roomTiming.count, 0.95) / 1000.0,
				GetPercentile(m_roomBuckets, roomTiming.count, 0.99) / 1000.0,
				roomTiming.count);
		}

		auto chatTiming = GetChatTimingStats();
		if (chatTiming.count > 0)
		{
			fprintf(fp, "Chat RTT:      avg=%.1fms  max=%.1fms  p50=<%.1fms  p95=<%.1fms  p99=<%.1fms  (n=%d)\n",
				chatTiming.avg_us / 1000.0, chatTiming.max_us / 1000.0,
				GetPercentile(m_chatBuckets, chatTiming.count, 0.50) / 1000.0,
				GetPercentile(m_chatBuckets, chatTiming.count, 0.95) / 1000.0,
				GetPercentile(m_chatBuckets, chatTiming.count, 0.99) / 1000.0,
				chatTiming.count);
		}

		fprintf(fp, "==========================================\n");
		fclose(fp);
	}

private:
	LoadTestStats() = default;
	LoadTestStats(const LoadTestStats&) = delete;
	LoadTestStats& operator=(const LoadTestStats&) = delete;

	/*TimingStats CalcStats(std::vector<long long>& times)
	{
		TimingStats stats{};
		stats.count = (int)times.size();
		if (stats.count == 0)
			return stats;

		std::sort(times.begin(), times.end());

		stats.min_us = times.front();
		stats.max_us = times.back();

		long long sum = 0;
		for (auto t : times)
			sum += t;
		stats.avg_us = sum / stats.count;

		stats.p50_us = times[(int)(stats.count * 0.50)];
		stats.p95_us = times[(int)(stats.count * 0.95)];

		int p99idx = (int)(stats.count * 0.99);
		if (p99idx >= stats.count)
			p99idx = stats.count - 1;
		stats.p99_us = times[p99idx];

		return stats;
	}*/
	static constexpr int BUCKET_COUNT = 8;
	static constexpr long long BUCKET_BOUNDS[BUCKET_COUNT] = {
		1000, 5000, 10000, 50000, 100000, 500000, 1000000, LLONG_MAX
	};

	// Login RTT
	std::atomic<long long> m_loginRttSum{ 0 };
	std::atomic<long long> m_loginRttMax{ 0 };
	std::atomic<int> m_loginRttCount{ 0 };
	std::atomic<int> m_loginBuckets[BUCKET_COUNT]{};

	// Room RTT
	std::atomic<long long> m_roomRttSum{ 0 };
	std::atomic<long long> m_roomRttMax{ 0 };
	std::atomic<int> m_roomRttCount{ 0 };
	std::atomic<int> m_roomBuckets[BUCKET_COUNT]{};

	// Chat RTT
	std::atomic<long long> m_chatRttSum{ 0 };
	std::atomic<long long> m_chatRttMax{ 0 };
	std::atomic<int> m_chatRttCount{ 0 };
	std::atomic<int> m_chatBuckets[BUCKET_COUNT]{};
	// Throughput tracking (delta between prints)
	struct ThroughputTracker
	{
		int prevChatSuccess = 0;
		int prevChatNotify = 0;
		std::chrono::steady_clock::time_point prevTime;
		bool initialized = false;
	} mThroughput;
};
