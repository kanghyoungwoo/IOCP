#include "MetricsCollector.h"
#include <cstdio>
#include <algorithm>
#include <numeric>

MetricsCollector::~MetricsCollector()
{
	Stop();
}

void MetricsCollector::Start(uint32_t intervalMs, const std::string& csvPath)
{
	mIntervalMs = intervalMs;
	mStartTime = std::chrono::steady_clock::now();

	mCsvFile.open(csvPath, std::ios::out | std::ios::trunc);
	if (mCsvFile.is_open())
	{
		mCsvFile << "elapsed_sec,conn/s,conn_fail/s,disconn/s,"
			"send_pkt/s,recv_pkt/s,send_KB/s,recv_KB/s,"
			"total_conn,total_disconn,"
			"lat_min_ms,lat_avg_ms,lat_max_ms,lat_p99_ms,lat_samples\n";
	}

	mIsRunning = true;
	mThread = std::thread([this]() { MetricsThread(); });
	printf("[Metrics] Start (interval: %ums, file: %s)\n", intervalMs, csvPath.c_str());
}

void MetricsCollector::Stop()
{
	mIsRunning = false;
	if (mThread.joinable())
		mThread.join();

	PrintAndSave();

	if (mCsvFile.is_open())
		mCsvFile.close();
}

// #4 TLS 기반 Latency 기록 - Worker 스레드에서 호출
void MetricsCollector::RecordLatency(double latencyMs)
{
	// 스레드별 TLS 버퍼
	thread_local TLSLatencyBuffer* tlsBuffer = nullptr;

	if (tlsBuffer == nullptr)
	{
		auto uptr = std::make_unique<TLSLatencyBuffer>();
		tlsBuffer = uptr.get();
		tlsBuffer->samples.reserve(1024);

		// 소유권을 MetricsCollector에 이전 → 소멸자에서 자동 해제
		std::lock_guard<std::mutex> lock(mTLSRegistryMutex);
		mTLSBuffers.push_back(std::move(uptr));
	}

	// TLS 버퍼에 추가 (swap과의 동시성 보호)
	std::lock_guard<std::mutex> lock(tlsBuffer->mutex);
	tlsBuffer->samples.push_back(latencyMs);
}

// Metrics 스레드에서 1초마다 호출 - 모든 TLS 버퍼를 swap으로 수거
void MetricsCollector::CollectTLSLatencySamples()
{
	mCollectedSamples.clear();

	std::lock_guard<std::mutex> regLock(mTLSRegistryMutex);
	for (auto& buf : mTLSBuffers)
	{
		std::vector<double> swapped;
		{
			// TLS 버퍼와 빈 벡터를 swap (매우 빠름)
			std::lock_guard<std::mutex> lock(buf->mutex);
			swapped.swap(buf->samples);
		}
		mCollectedSamples.insert(mCollectedSamples.end(), swapped.begin(), swapped.end());
	}
}

void MetricsCollector::MetricsThread()
{
	while (mIsRunning)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(mIntervalMs));
		PrintAndSave();
	}
}

void MetricsCollector::PrintAndSave()
{
	uint64_t connects = mConnectCount.exchange(0, std::memory_order_relaxed);
	uint64_t connectFails = mConnectFailCount.exchange(0, std::memory_order_relaxed);
	uint64_t disconnects = mDisconnectCount.exchange(0, std::memory_order_relaxed);
	uint64_t sendPkts = mSendPacketCount.exchange(0, std::memory_order_relaxed);
	uint64_t recvPkts = mRecvPacketCount.exchange(0, std::memory_order_relaxed);
	uint64_t sendBytes = mSendBytes.exchange(0, std::memory_order_relaxed);
	uint64_t recvBytes = mRecvBytes.exchange(0, std::memory_order_relaxed);

	mTotalConnections += connects;
	mTotalDisconnections += disconnects;
	mTotalSendPackets += sendPkts;
	mTotalRecvPackets += recvPkts;

	// #4 TLS 버퍼에서 Latency 샘플 수거
	CollectTLSLatencySamples();

	double latMin = 0, latAvg = 0, latMax = 0, latP99 = 0;
	size_t latCount = mCollectedSamples.size();

	if (latCount > 0)
	{
		std::sort(mCollectedSamples.begin(), mCollectedSamples.end());
		latMin = mCollectedSamples.front();
		latMax = mCollectedSamples.back();
		latAvg = std::accumulate(mCollectedSamples.begin(), mCollectedSamples.end(), 0.0) / latCount;

		size_t p99Index = (size_t)(latCount * 0.99);
		if (p99Index >= latCount) p99Index = latCount - 1;
		latP99 = mCollectedSamples[p99Index];
	}

	auto elapsed = std::chrono::steady_clock::now() - mStartTime;
	auto elapsedSec = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();

	double sendKB = sendBytes / 1024.0;
	double recvKB = recvBytes / 1024.0;

	printf("=== [%llds] Conn:%llu(-%llu fail:%llu) | Send:%llu(%.1fKB) Recv:%llu(%.1fKB) | "
		"Total:%llu/%llu | Lat: %.2f/%.2f/%.2f/%.2f ms (%zu samples) ===\n",
		elapsedSec,
		connects, disconnects, connectFails,
		sendPkts, sendKB, recvPkts, recvKB,
		mTotalConnections, mTotalDisconnections,
		latMin, latAvg, latMax, latP99,
		latCount);

	if (mCsvFile.is_open())
	{
		mCsvFile << elapsedSec << ","
			<< connects << "," << connectFails << "," << disconnects << ","
			<< sendPkts << "," << recvPkts << ","
			<< sendKB << "," << recvKB << ","
			<< mTotalConnections << "," << mTotalDisconnections << ","
			<< latMin << "," << latAvg << "," << latMax << "," << latP99 << "," << latCount
			<< "\n";
		mCsvFile.flush();
	}
}
