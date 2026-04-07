#include "TimerScheduler.h"
#include "DebugLog.h"

TimerScheduler::~TimerScheduler()
{
	Stop();
}

void TimerScheduler::Start()
{
	mIsRunning = true;
	mThread = std::thread([this]() { TimerThread(); });
	DEBUG_LOG("[TimerScheduler] Start\n");
}

void TimerScheduler::Stop()
{
	{
		std::lock_guard<std::mutex> lock(mQueueMutex);
		mIsRunning = false;
	}
	mCondVar.notify_one();

	if (mThread.joinable())
		mThread.join();
}

void TimerScheduler::Schedule(uint32_t botIndex, TimerEventType eventType, uint32_t delayMs)
{
	auto fireTime = SteadyClock::now() + std::chrono::milliseconds(delayMs);
	ScheduleAt(botIndex, eventType, fireTime);
}

void TimerScheduler::ScheduleAt(uint32_t botIndex, TimerEventType eventType, TimePoint fireTime)
{
	{
		std::lock_guard<std::mutex> lock(mQueueMutex);
		mEventQueue.push({ fireTime, botIndex, eventType });
	}
	mCondVar.notify_one();
}

size_t TimerScheduler::GetPendingCount()
{
	std::lock_guard<std::mutex> lock(mQueueMutex);
	return mEventQueue.size();
}

void TimerScheduler::TimerThread()
{
	while (mIsRunning)
	{
		std::vector<TimerEvent> readyEvents;

		{
			std::unique_lock<std::mutex> lock(mQueueMutex);

			// 현재 시각 기준으로 꺼낼 이벤트 전부 꺼냄
			auto now = SteadyClock::now();
			while (!mEventQueue.empty() && mEventQueue.top().fireTime <= now)
			{
				readyEvents.push_back(mEventQueue.top());
				mEventQueue.pop();
			}

			// 꺼낼 게 없었으면 대기
			if (readyEvents.empty())
			{
				if (mEventQueue.empty())
				{
					// 큐가 비었으면 새 이벤트가 들어올 때까지 대기
					mCondVar.wait(lock, [this]() {
						return !mIsRunning || !mEventQueue.empty();
					});
				}
				else
				{
					// 다음 이벤트 시각까지 대기 (Predicate 없음)
					mCondVar.wait_until(lock, mEventQueue.top().fireTime);
				}

				// 깨어나면 이유 불문 루프 상단으로 → 이벤트 꺼내기 → nextTime 갱신
				continue;
			}
		}

		// 핸들러 호출 (락 밖에서)
		for (auto& evt : readyEvents)
		{
			if (mHandler)
				mHandler(evt.botIndex, evt.eventType);
		}
	}
}
