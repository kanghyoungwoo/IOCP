#pragma once

#include "RedisTaskDefine.h"
#include "ErrorCode.h"
#include "Define.h"

#include "../thirdparty/CRedisConn.h"

#include <vector>
#include <deque>
#include <thread>
#include <mutex>
#include <chrono>
#include <functional>

class RedisManager
{
public:
	RedisManager() = default;
	~RedisManager() = default;

	std::function<void()> OnResponsePushed;

	bool Run(std::string ip_, UINT16 port_, const UINT32 threadCount_)
	{
		mIP = ip_;
		mPort = port_;
		//if (Connect(ip_, port_) == false)
		//{
		//	printf("Redis connection failed: %s\n", mConn.getErrorStr().c_str());
		//	return false;
		//}
		mIsTaskRun = true;
		// �����带 ����� ��� �� 
		for (UINT32 i = 0; i < threadCount_; ++i)
		{
			mTaskThreads.emplace_back([this]() { TaskProcessThread();});
		}

		LOG_DEBUG("Redis thread Working...\n");
		return true;
	}

	void End()
	{
		{
			std::lock_guard<std::mutex> g(mReqLock);
			mIsTaskRun = false;
		}
		mReqCV.notify_all();

		for (auto& thread : mTaskThreads)
		{
			if (thread.joinable())
			{
				thread.join();
			}
		}
		mTaskThreads.clear();

		//mConn.disConnect();
	}

	void PushTask(RedisTask task_)	// producer
	{
		{
			std::lock_guard<std::mutex> guard(mReqLock);
			mRequestTask.push_back(task_);
		}
		mReqCV.notify_one();
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

	bool HasResponseTask()
	{
		std::lock_guard<std::mutex> guard(mResLock);
		return !mResponseTask.empty();
	}


private:

	//bool Connect(std::string ip_, UINT16 port_)
	//{
	//	mConn.init(ip_, port_);
	//	if (mConn.connect() == false)
	//	{
	//		return false;
	//	}
	//	else
	//	{
	//		printf("Redis connection successful\n");
	//	}
	//	return true;
	//}

	// Redis ��û�� ó����
	void TaskProcessThread()
	{
		RedisCpp::CRedisConn Conn; // ����
		Conn.init(mIP,mPort); // �ʱ�ȭ
		Conn.connect();// ���� 

		LOG_DEBUG("Redis thread connected\n");
		while (true) // mIsTaskRun -> true
		{
			RedisTask task;
			{
				std::unique_lock<std::mutex>lock(mReqLock);
				mReqCV.wait(lock, [this]() {return !mRequestTask.empty() || !mIsTaskRun;});
				// �����ȣ + ť ��������� Ż��
				if (!mIsTaskRun && mRequestTask.empty())
					break;
				task = mRequestTask.front();
				mRequestTask.pop_front();
			}

			if (task.TaskID == RedisTaskID::INVALID)
			{
				task.release();
				continue;
			}

			if (task.TaskID == RedisTaskID::REQUEST_LOGIN)
			{
				auto pRequest = (RedisLoginReq*)task.pData;

				RedisLoginRes bodyData;
				bodyData.Result = (UINT16)ERROR_CODE::LOGIN_USER_INVALID_PW;
				strcpy_s(bodyData.UserID, sizeof(bodyData.UserID), pRequest->UserID);

				std::string value;
				bool got = Conn.get(pRequest->UserID, value);
				
				if (got)
				{
					LOG_DEBUG("UserID : %s\n", pRequest->UserID);
					LOG_DEBUG("Input Pw : %s\n", pRequest->UserPW);
					LOG_DEBUG("Redis PW: %s\n", value.c_str());
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

			//bool isIdle = true;
			//// ��û�� queue�� ���ؼ� ���� �ְ����� mRequestTask
			//if (auto task = TakeRequestTask(); task.TaskID != RedisTaskID::INVALID)
			//{
			//	isIdle = false;
			//	
			//	if (task.TaskID == RedisTaskID::REQUEST_LOGIN)
			//	{
			//		auto pRequest = (RedisLoginReq*)task.pData;

			//		RedisLoginRes bodyData;
			//		bodyData.Result = (UINT16)ERROR_CODE::LOGIN_USER_INVALID_PW;

			//		strcpy_s(bodyData.UserID, sizeof(bodyData.UserID), pRequest->UserID);

			//		std::string value;
			//		if (mConn.get(pRequest->UserID, value))
			//		{
			//			printf("UserID: '%s'\n", pRequest->UserID);
			//			printf("Input PW: '%s'\n", pRequest->UserPW);
			//			printf("Redis PW: '%s'\n", value.c_str());
			//			printf("Redis get result: %s\n", mConn.get(pRequest->UserID, value) ? "success" : "failed");

			//			if (value.compare(pRequest->UserPW) == 0)
			//			{
			//				bodyData.Result = (UINT16)ERROR_CODE::NONE;
			//			}
			//		}
			//		RedisTask resTask;
			//		resTask.UserIndex = task.UserIndex;
			//		resTask.TaskID = RedisTaskID::RESPONSE_LOGIN;
			//		//printf("######################### TaskID : %s #############################################\n", resTask.TaskID);
			//		resTask.DataSize = sizeof(RedisLoginRes);
			//		resTask.pData = new char[resTask.DataSize];
			//		CopyMemory(resTask.pData, (char*)&bodyData, resTask.DataSize);

			//		PushResponse(resTask);
			//	}

			//	task.release();

			//	// �̷����� if/switch���� ����Ͽ� task�� ó���� �ڵ尡 �ʹ� Ŀ���� ������ ���� ó���� ���� ���� dictionary�� array�� ����Ͽ� ó���ϴ� ���� ����
			//	//switch (task.TaskID)
			//	//{
			//	//	case RedisTaskID::REQUEST_LOGIN:
			//	//		{
			//	//			// �α��� ��û ó��
			//	//			if (mConn.login(task.UserIndex, task.LoginReq) == true)
			//	//			{
			//	//				task.ResultCode = ERROR_CODE::SUCCESS;
			//	//			}
			//	//			else
			//	//			{
			//	//				task.ResultCode = ERROR_CODE::LOGIN_FAILED;
			//	//			}
			//	//		}
			//	//		break;
			//	//	case RedisTaskID::REQUEST_LOGOUT:
			//	//		{
			//	//			// �α׾ƿ� ��û ó��
			//	//			if (mConn.logout(task.UserIndex) == true)
			//	//			{
			//	//				task.ResultCode = ERROR_CODE::SUCCESS;
			//	//			}
			//	//			else
			//	//			{
			//	//				task.ResultCode = ERROR_CODE::LOGOUT_FAILED;
			//	//			}
			//	//		}
			//	//		break;
			//	//	default:
			//	//		task.ResultCode = ERROR_CODE::INVALID_TASK_ID;
			//	//		break;
			//	//}
			//}

			//if (isIdle)
			//{
			//	std::this_thread::sleep_for(std::chrono::milliseconds(1)); 
			//}
			
		}
		Conn.disConnect(); // ����
	}

	void PushResponse(RedisTask task_)
	{
		{
			std::lock_guard<std::mutex> guard(mResLock);
			mResponseTask.push_back(task_);
		}

		// ���� ���� ��Ŷ ó�� �����忡 �˷���
		if (OnResponsePushed)
		{
			OnResponsePushed();
		}
	}

	RedisTask TakeRequestTask()
	{
		std::lock_guard<std::mutex> guard(mReqLock);
		if (mRequestTask.empty())
		{
			return RedisTask();
		}
		auto task = mRequestTask.front();
		mRequestTask.pop_front();
		return task;
	}

	std::vector<std::thread> mTaskThreads;

	//RedisCpp::CRedisConn mConn;
	bool mIsTaskRun = false;

	std::mutex mReqLock;
	std::deque<RedisTask> mRequestTask;
	std::condition_variable mReqCV;

	std::mutex mResLock;
	std::deque<RedisTask> mResponseTask;

	std::string mIP;
	UINT16 mPort;
};