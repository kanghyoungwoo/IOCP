#include "ChaosProxy.h"
#include "BotEngine.h"
#include "HighResTimer.h"
#include "Statistics.h"
#include <random>
#include <algorithm>

ChaosProxy::~ChaosProxy()
{
    Stop();
}

bool ChaosProxy::Init(const ChaosConfig& config, BotEngine* engine)
{
    mConfig = config;
    mEngine = engine;
    return true;
}

void ChaosProxy::Start()
{
    if (!mConfig.proxy.enabled) return;
    mIsRunning = true;
    mSchedulerThread = std::thread(&ChaosProxy::SchedulerThread, this);
}

void ChaosProxy::Stop()
{
    if (!mIsRunning) return;
    mIsRunning = false;
    mDelayCv.notify_all(); // Wake up scheduler thread on exit
    if (mSchedulerThread.joinable()) mSchedulerThread.join();
}

bool ChaosProxy::ProcessSend(uint32_t botIndex, const char* data, uint32_t size)
{
    if (!mConfig.proxy.enabled) return mEngine->PostSend(botIndex, data, size);

    static thread_local std::mt19937 gen(std::random_device{}());
    std::uniform_real_distribution<double> dist(0.0, 1.0);

    // 1. Drop Check
    if (mConfig.proxy.dropEnabled && dist(gen) < mConfig.proxy.dropProbability)
    {
        GetStats().proxyDrops++;
        return true;
    }

    // 2. Hold Check (시나리오 A)
    if (mConfig.proxy.holdEnabled)
    {
        auto* header = reinterpret_cast<const PACKET_HEADER*>(data);
        if (header->PacketId == mConfig.proxy.holdPacketId && botIndex == mConfig.proxy.holdTargetSessionIndex)
        {
            GetStats().proxyHolds++;
            int64_t targetTimeUs = HiResTimer::NowMicroseconds() + (mConfig.proxy.holdDurationMs * 1000LL);
            std::lock_guard<std::mutex> lock(mDelayMutex);
            mDelayQueue.push({ botIndex, std::vector<char>(data, data + size), targetTimeUs, mEngine->GetBot(botIndex)->GetSessionId() });
            mDelayCv.notify_one();
            return true;
        }
    }

    // 3. Fragmentation Check
    if (mConfig.proxy.fragmentEnabled)
    {
        GetStats().proxyFragments++;
        uint32_t offset = 0;
        uint32_t chunkIndex = 0;
        int64_t currentTimeUs = HiResTimer::NowMicroseconds();

        while (offset < size)
        {
            uint32_t chunkSize = (std::min)(mConfig.proxy.fragmentSize, size - offset);
            std::vector<char> chunk(data + offset, data + offset + chunkSize);
            int64_t targetTimeUs = currentTimeUs + (chunkIndex * mConfig.proxy.fragmentDelayUs);
            
            std::lock_guard<std::mutex> lock(mDelayMutex);
            mDelayQueue.push({ botIndex, chunk, targetTimeUs, mEngine->GetBot(botIndex)->GetSessionId() });
            mDelayCv.notify_one();
            offset += chunkSize;
            chunkIndex++;
        }
        return true;
    }

    // 4. Delay Check
    if (mConfig.proxy.delayEnabled && dist(gen) < mConfig.proxy.delayProbability)
    {
        GetStats().proxyDelays++;
        std::uniform_int_distribution<uint32_t> delayDist(mConfig.proxy.delayMinUs, mConfig.proxy.delayMaxUs);
        int64_t targetTimeUs = HiResTimer::NowMicroseconds() + delayDist(gen);

        std::lock_guard<std::mutex> lock(mDelayMutex);
        mDelayQueue.push({ botIndex, std::vector<char>(data, data + size), targetTimeUs, mEngine->GetBot(botIndex)->GetSessionId() });
        mDelayCv.notify_one();
        return true;
    }

    // 5. Reorder
    if (mConfig.proxy.reorderEnabled && dist(gen) < mConfig.proxy.reorderProbability)
    {
        GetStats().proxyReorders++;
        int64_t targetTimeUs = HiResTimer::NowMicroseconds() + 5000;
        std::lock_guard<std::mutex> lock(mDelayMutex);
        mDelayQueue.push({ botIndex, std::vector<char>(data, data + size), targetTimeUs, mEngine->GetBot(botIndex)->GetSessionId() });
        mDelayCv.notify_one();
        return true;
    }

    return mEngine->PostSend(botIndex, data, size);
}

void ChaosProxy::SchedulerThread()
{
    while (mIsRunning)
    {
        int64_t nowUs = HiResTimer::NowMicroseconds();
        std::vector<DelayedPacket> toSend;
        
        {
            std::lock_guard<std::mutex> lock(mDelayMutex);
            while (!mDelayQueue.empty() && mDelayQueue.top().targetTimeUs <= nowUs)
            {
                toSend.push_back(mDelayQueue.top());
                mDelayQueue.pop();
            }
        }

        for (const auto& pkt : toSend)
        {
            if (mEngine->GetBot(pkt.botIndex)->GetSessionId() == pkt.sessionId) {
                mEngine->PostSend(pkt.botIndex, pkt.data.data(), static_cast<uint32_t>(pkt.data.size()));
            }
        }

        if (toSend.empty())
        {
            int64_t nextTimeUs = -1;
            {
                std::unique_lock<std::mutex> lock(mDelayMutex);
                if (!mDelayQueue.empty()) {
                    nextTimeUs = mDelayQueue.top().targetTimeUs;
                    auto waitUs = (std::max)(0LL, nextTimeUs - nowUs);
                    mDelayCv.wait_for(lock, std::chrono::microseconds(waitUs));
                } else {
                    mDelayCv.wait_for(lock, std::chrono::milliseconds(1), [this]() { return !mIsRunning; });
                }
            }
        }
    }
}
