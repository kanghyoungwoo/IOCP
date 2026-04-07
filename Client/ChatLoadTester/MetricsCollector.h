#pragma once

#include <cstdint>
#include <atomic>
#include <thread>
#include <mutex>
#include <vector>
#include <string>
#include <fstream>
#include <chrono>

class MetricsCollector
{
public:
	MetricsCollector() = default;
	~MetricsCollector();

	void Start(uint32_t intervalMs, const std::string& csvPath);
	void Stop();

	// 이벤트 기록 (atomic - 여러 스레드에서 호출)
	void OnConnect()     { mConnectCount.fetch_add(1, std::memory_order_relaxed); }
	void OnConnectFail() { mConnectFailCount.fetch_add(1, std::memory_order_relaxed); }
	void OnDisconnect()  { mDisconnectCount.fetch_add(1, std::memory_order_relaxed); }
	void OnSend(uint32_t bytes)
	{
		mSendPacketCount.fetch_add(1, std::memory_order_relaxed);
		mSendBytes.fetch_add(bytes, std::memory_order_relaxed);
	}
	void OnRecv(uint32_t bytes)
	{
		mRecvPacketCount.fetch_add(1, std::memory_order_relaxed);
		mRecvBytes.fetch_add(bytes, std::memory_order_relaxed);
	}

	// #4 Latency 기록 - TLS 버퍼 방식 (Lock-Free)
	void RecordLatency(double latencyMs);

	uint64_t GetTotalConnections() const { return mTotalConnections; }

private:
	void MetricsThread();
	void PrintAndSave();
	void CollectTLSLatencySamples();

	// Atomic 카운터
	std::atomic<uint64_t> mConnectCount{ 0 };
	std::atomic<uint64_t> mConnectFailCount{ 0 };
	std::atomic<uint64_t> mDisconnectCount{ 0 };
	std::atomic<uint64_t> mSendPacketCount{ 0 };
	std::atomic<uint64_t> mRecvPacketCount{ 0 };
	std::atomic<uint64_t> mSendBytes{ 0 };
	std::atomic<uint64_t> mRecvBytes{ 0 };

	// #4 TLS Latency 수거용 구조
	struct TLSLatencyBuffer
	{
		std::vector<double> samples;
		std::mutex mutex;  // swap 시에만 잠깐 사용
	};

	// 등록된 TLS 버퍼 목록
	std::mutex mTLSRegistryMutex;
	std::vector<std::unique_ptr<TLSLatencyBuffer>> mTLSBuffers;

	// Metrics 스레드에서 모은 샘플
	std::vector<double> mCollectedSamples;

	// 누적 통계
	uint64_t mTotalConnections = 0;
	uint64_t mTotalDisconnections = 0;
	uint64_t mTotalSendPackets = 0;
	uint64_t mTotalRecvPackets = 0;

	// 스레드 관리
	std::thread mThread;
	std::atomic<bool> mIsRunning{ false };
	uint32_t mIntervalMs = 1000;

	// CSV 출력
	std::ofstream mCsvFile;
	std::chrono::steady_clock::time_point mStartTime;
};
