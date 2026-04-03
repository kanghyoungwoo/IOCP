#pragma once
// ================================================================
// ChaosProxy.h - 카오스 룰(Fragment, Delay, Drop, Reorder) 적용
// ================================================================

#include "Protocol.h"
#include "ChaosConfig.h"
#include <vector>
#include <mutex>
#include <queue>
#include <thread>

class BotEngine;

struct DelayedPacket
{
    uint32_t botIndex;
    std::vector<char> data;
    int64_t targetTimeUs;
    uint32_t sessionId;

    bool operator>(const DelayedPacket& other) const {
        return targetTimeUs > other.targetTimeUs;
    }
};

class ChaosProxy
{
public:
    ChaosProxy() = default;
    ~ChaosProxy();

    bool Init(const ChaosConfig& config, BotEngine* engine);
    void Start();
    void Stop();

    // 엔진에서 Send 호출 시 프록시를 거침
    bool ProcessSend(uint32_t botIndex, const char* data, uint32_t size);

private:
    void SchedulerThread();

    ChaosConfig mConfig;
    BotEngine* mEngine = nullptr;
    std::atomic<bool> mIsRunning{false};

    std::thread mSchedulerThread;
    std::priority_queue<DelayedPacket, std::vector<DelayedPacket>, std::greater<DelayedPacket>> mDelayQueue;
    std::mutex mDelayMutex;
    std::condition_variable mDelayCv;
};
