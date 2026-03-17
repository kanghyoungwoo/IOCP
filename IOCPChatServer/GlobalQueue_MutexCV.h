#pragma once
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>

class Room;

class GlobalQueue_MutexCV
{
public:
	GlobalQueue_MutexCV()
	{
		mShutdown = false;
	}
	~GlobalQueue_MutexCV() = default;

	void Push(Room* pRoom)
	{
		{
			std::lock_guard<std::mutex> lock(mLock);
			mQueue.push(pRoom);
		}
		mCV.notify_one();
	}

	Room* Pop()
	{
		std::unique_lock<std::mutex>lock(mLock);
		mCV.wait(lock, [this]() {return !mQueue.empty() || mShutdown.load();});
		if (mShutdown.load() && mQueue.empty())
		{
			return nullptr;
		}

		Room* room = mQueue.front();
		mQueue.pop();

		return room;
	}

	void Shutdown()
	{
		mShutdown.store(true);
		mCV.notify_all();
	}


private:
	std::queue<Room*>		mQueue;		// 실제 데이터
	std::mutex				mLock;		// 동기화
	std::condition_variable	mCV;		// Logic thread blocking
	std::atomic<bool>		mShutdown;	// 종료 신호
};