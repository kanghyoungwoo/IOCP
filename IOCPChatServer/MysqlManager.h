#pragma once
#include "MySQLTaskDefine.h"


#include<mysql.h>
#include <thread>
#include<string>
#include<vector>
#include<deque>
#include<mutex>


class MySQLManager
{
public:
	MySQLManager() = default;
	~MySQLManager() = default;


	bool Run(UINT32 threadCount_)
	{
		mIsTaskRun = true;
		// 쓰레드를 만들어 줘야 함 
		for (UINT32 i = 0; i < threadCount_; ++i)
		{
			mTaskThreads.emplace_back([this]() { TaskProcessThread();});
		}
		printf("MySQL task worker started\n");
		return true;
	}

	void End()
	{
		mIsTaskRun = false;
		for (auto& thread : mTaskThreads)
		{
			if (thread.joinable())
			{
				thread.join();
			}
		}
	}

	void PushTask(MySQLTask task_)
	{
		std::lock_guard<std::mutex> guard(mReqLock);
		mRequestTask.push_back(task_);
	}


private:
	void TaskProcessThread()
	{
		while (mIsTaskRun)
		{
			bool isIdle = true;
			if (auto task = TakeRequestTask(); task.TaskID != MySQLTaskID::INVALID)
			{
				isIdle = false;

				//if (task.TaskID == MySQLTaskID::INSERT_LOGIN_EVENT)
				//{
				//	auto pReq = reinterpret_cast<MySQLLoginEventReq*>(task.pData);
				//	printf("\n");
				//}
				switch (task.TaskID)
				{
				case MySQLTaskID::INSERT_LOGIN_EVENT:
				{
					auto pReq = reinterpret_cast<MySQLLoginEventReq*>(task.pData);
					printf("[MySQL] INSERT_LOGIN_EVENT user='%s' ts=%llu\n", pReq->UserID, (unsigned long long)pReq->TimestampSec);
				}
				break;
				case MySQLTaskID::INSERT_ROOM_EVENT:
				{
					auto pReq = reinterpret_cast<MySQLRoomEventReq*>(task.pData);
					printf("[MySQL] INSERT_ROOM_EVENT user='%s' room=%d type=%d ts=%llu\n",
						pReq->UserID, pReq->RoomNumber, (int)pReq->EventType, (unsigned long long)pReq->TimeStampSec);
				}
				break;
				case MySQLTaskID::INSERT_CHAT_MESSAGE:
				{
					auto pReq = reinterpret_cast<MySQLChatMsgReq*>(task.pData);
					printf("[MySQL] INSERT_CHAT_MESSAGE user='%s' room=%d msg='%s' ts=%llu\n",
						pReq->UserID, pReq->RoomNumber, pReq->Message, (unsigned long long)pReq->TimeStampSec);
				}
				break;
				default:
					break;
				}

				task.release();

				
			}
		}
	}


	MySQLTask TakeRequestTask()
	{
		std::lock_guard<std::mutex> guard(mReqLock);
		if (mRequestTask.empty())
		{
			return MySQLTask();
		}
		auto task = mRequestTask.front();
		mRequestTask.pop_front();
		return task;
	}


	//MYSQL* mConnection;
	std::vector<std::thread> mTaskThreads;
	std::mutex mReqLock;
	std::deque<MySQLTask> mRequestTask;
	//std::deque<MySQLTask> mResponseTask;

	bool mIsTaskRun = false;
	

};