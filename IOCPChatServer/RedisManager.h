#pragma once

#include "RedisTaskDefine.h"
#include "ErrorCode.h"

#include "../thirdparty/CRedisConn.h"

#include <vector>
#include <deque>
#include <thread>
#include <mutex>


class RedisManager
{
public:
	RedisManager() = default;
	~RedisManager() = default;

	bool Run(std::string ip_, UINT16 port_, const UINT32 threadCount_)
	{
		if (Connect(ip_, port_) == false)
		{
			printf("Redis connection failed: %s\n", mConn.getErrorStr().c_str());
			return false;
		}
		mIsTaskRun = true;
		// 쓰레드를 만들어 줘야 함 
		for (UINT32 i = 0; i < threadCount_; ++i)
		{
			mTaskThreads.emplace_back([this]() { TaskProcessThread();});
		}

		printf("Working... \n");
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

	void PushTask(RedisTask task_)
	{
		std::lock_guard<std::mutex> guard(mReqLock);
		mRequestTask.push_back(task_);
	}

	RedisTask TakeResponseTask()
	{
		std::lock_guard<std::mutex> guard(mResLock);
		if (mResponseTask.empty())
		{
			return RedisTask();
		}
		auto task = mResponseTask.front();
		mResponseTask.pop_front();

		return task;
	}


private:

	bool Connect(std::string ip_, UINT16 port_)
	{
		mConn.init(ip_, port_);
		if (mConn.connect() == false)
		{
			return false;
		}
		else
		{
			printf("Redis connection successful\n");
		}
		return true;
	}

	// Redis 요청을 처리함
	void TaskProcessThread()
	{
		printf("Redis 쓰레드 시작\n");
		while (mIsTaskRun)
		{
			bool isIdle = true;
			// 요청을 queue를 통해서 서로 주고받음 mRequestTask
			if (auto task = TakeResponseTask(); task.TaskID != RedisTaskID::INVALID)
			{
				isIdle = false;
				
				if (task.TaskID == RedisTaskID::REQUEST_LOGIN)
				{
					auto pRequest = (RedisLoginReq*)task.pData;

					RedisLoginRes bodyData;
					bodyData.Result = (UINT16)ERROR_CODE::LOGIN_USER_INVALID_PW;

					std::string value;
					if (mConn.get(pRequest->UserID, value))
					{
						bodyData.Result = (UINT16)ERROR_CODE::NONE;

						if (value.compare(pRequest->UserPW) == 0)
						{
							bodyData.Result = (UINT16)ERROR_CODE::NONE;
						}
					}
					RedisTask resTask;
					resTask.UserIndex = task.UserIndex;
					resTask.TaskID = RedisTaskID::RESPONSE_LOGIN;
					resTask.DataSize = sizeof(RedisLoginRes);
					resTask.pData = new char[resTask.DataSize];
					CopyMemory(resTask.pData, (char*)&bodyData, resTask.DataSize);

					PushResponse(resTask);
				}

				task.release();

				// 이런식의 if/switch문을 사용하여 task를 처리는 코드가 너무 커지기 때문에 소켓 처리할 때와 같이 dictionary나 array를 사용하여 처리하는 것이 좋음
				//switch (task.TaskID)
				//{
				//	case RedisTaskID::REQUEST_LOGIN:
				//		{
				//			// 로그인 요청 처리
				//			if (mConn.login(task.UserIndex, task.LoginReq) == true)
				//			{
				//				task.ResultCode = ERROR_CODE::SUCCESS;
				//			}
				//			else
				//			{
				//				task.ResultCode = ERROR_CODE::LOGIN_FAILED;
				//			}
				//		}
				//		break;
				//	case RedisTaskID::REQUEST_LOGOUT:
				//		{
				//			// 로그아웃 요청 처리
				//			if (mConn.logout(task.UserIndex) == true)
				//			{
				//				task.ResultCode = ERROR_CODE::SUCCESS;
				//			}
				//			else
				//			{
				//				task.ResultCode = ERROR_CODE::LOGOUT_FAILED;
				//			}
				//		}
				//		break;
				//	default:
				//		task.ResultCode = ERROR_CODE::INVALID_TASK_ID;
				//		break;
				//}
			}

			if (isIdle)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(1)); 
			}
			
		}
	}

	void PushResponse(RedisTask task_)
	{
		std::lock_guard<std::mutex> guard(mResLock);
		mResponseTask.push_back(task_);
	}


	std::vector<std::thread> mTaskThreads;

	RedisCpp::CRedisConn mConn;
	bool mIsTaskRun = false;

	std::mutex mReqLock;
	std::deque<RedisTask> mRequestTask;

	std::mutex mResLock;
	std::deque<RedisTask> mResponseTask;
};