#pragma once

#include "BotState.h"
#include <cstdint>
#include <chrono>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <functional>

using SteadyClock = std::chrono::steady_clock;
using TimePoint = std::chrono::steady_clock::time_point;

struct TimerEvent
{
	TimePoint		fireTime;
	uint32_t		botIndex;
	TimerEventType	eventType;

	bool operator>(const TimerEvent& other) const
	{
		return fireTime > other.fireTime;
	}
};

using TimerEventHandler = std::function<void(uint32_t botIndex, TimerEventType eventType)>;

class TimerScheduler
{
public:
	TimerScheduler() = default;
	~TimerScheduler();

	void Start();
	void Stop();

	void Schedule(uint32_t botIndex, TimerEventType eventType, uint32_t delayMs);
	void ScheduleAt(uint32_t botIndex, TimerEventType eventType, TimePoint fireTime);

	void SetEventHandler(TimerEventHandler handler) { mHandler = handler; }

	size_t GetPendingCount();

private:
	void TimerThread();

	std::priority_queue<TimerEvent, std::vector<TimerEvent>, std::greater<TimerEvent>> mEventQueue;
	std::mutex mQueueMutex;
	std::condition_variable mCondVar;

	std::thread mThread;
	std::atomic<bool> mIsRunning{ false };
	TimerEventHandler mHandler;
};
