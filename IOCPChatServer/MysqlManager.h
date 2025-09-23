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
	MySQLManager()
	{
		mConnection = nullptr;
		mStmtLogin = nullptr;
		mStmtRoom = nullptr;
		mStmtChat = nullptr;
	}
	~MySQLManager()
	{
		End();
	}

	void configure(const char* host_, const char* user_, const char* pass_, const char* db_, unsigned int port_)
	{
		mHost = host_ ? host_ : "127.0.0.1";
		mUser = user_ ? user_ : "root";
		mPass = pass_ ? pass_ : "";
		mDB = db_ ? db_ : "chatdb";
		mPort = port_ ? port_ : 3306;
	}

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
	bool InitandConnect()
	{
		if (mHost.empty())
		{
			mHost = "127.0.0.1";
			mUser = "root";
			mPass = "";
			mDB = "chatdb";
			mPort = 3306;
		}

		mConnection = mysql_init(nullptr);

		if (!mConnection)
		{
			printf("MySQL init fail! \n");
			return false;
		}

		mysql_options(mConnection, MYSQL_SET_CHARSET_NAME, "utf8mb4");
		mysql_options(mConnection, MYSQL_OPT_CONNECT_TIMEOUT, &mConnectTimeoutSec);

		if (!mysql_real_connect(mConnection, mHost.c_str(), mUser.c_str(), mPass.c_str(), mDB.c_str(), mPort, nullptr, 0))
		{
			printf("MySQL connection error :s ! \n", mysql_error(mConnection));
			return false;
		}
		return true;
	}

	void CloseConnection()
	{
		if (mConnection)
		{
			mysql_close(mConnection);
			mConnection = nullptr;
		}
	}
	
	bool EnsureConnection()
	{
		if (mConnection && mysql_ping(mConnection) == 0)
		{
			return true;
		}

		CloseConnection();
		return InitandConnect();
	}

	bool PrepareStatements()
	{
		const char* createLogin =
			"CREATE TALBE IF NOT EXISTS login_events("
			"id INT PRIMARY KEY AUTO_INCREMENT,"
			"user_id VARCHAR(64) NOT NULL,"
			"timestamp BIGINT NOT NULL"
			")";
		const char* createRoom =
			"CREATE TALBE IF NOT EXISTS room_events("
			"id INT PRIMARY KEY AUTO_INCREMENT,"
			"user_id VARCHAR(64) NOT NULL,"
			"room INT NOT NULL,"
			"event TINYINT NOT NULL,"
			"timestamp BIGINT NOT NULL"
			")";
		const char* createChat =
			"CREATE TALBE IF NOT EXISTS chat_messages("
			"id INT PRIMARY KEY AUTO_INCREMENT,"
			"user_id VARCHAR(64) NOT NULL,"
			"room INT NOT NULL,"
			"msg VARCHAR(256) NOT NULL,"
			"timestamp BIGINT NOT NULL"
			")";

		if (mysql_query(mConnection, createLogin) != 0)
		{
			printf("MySQL create login_events fail ! : %s \n", mysql_error(mConnection));
			return false;
		}
		if (mysql_query(mConnection, createRoom) != 0)
		{
			printf("MySQL create room_events fail ! : %s \n", mysql_error(mConnection));
			return false;
		}
		if (mysql_query(mConnection, createChat) != 0)
		{
			printf("MySQL create chat_messages fail ! : %s \n", mysql_error(mConnection));
			return false;
		}

		mStmtLogin = mysql_stmt_init(mConnection);
		mStmtRoom = mysql_stmt_init(mConnection);
		mStmtChat = mysql_stmt_init(mConnection);
		if (!mStmtLogin || !mStmtRoom || !mStmtChat)
		{
			printf("MySQL stmt init faile !! \n");
			return false;
		}

		const char* insLogin = "INSERT INTO login_events(user_id, timestamp) VALUES(?, ?)";
		const char* insRoom = "INSERT INTO login_events(user_id, room, event, timestamp) VALUES(?, ?, ?, ?)";
		const char* insChat = "INSERT INTO login_events(user_id, room, msg, timestamp) VALUES(?, ?, ?, ?)";

		if (mysql_stmt_prepare(mStmtLogin, insLogin, (unsigned long)strlen(insLogin)) != 0)
		{
			printf("MySQL prepare login_events failed ! \n", mysql_stmt_error(mStmtLogin));
			return false;
		}
		if (mysql_stmt_prepare(mStmtRoom, insRoom, (unsigned long)strlen(insRoom)) != 0)
		{
			printf("MySQL prepare room_events failed ! \n", mysql_stmt_error(mStmtRoom));
			return false;
		}
		if (mysql_stmt_prepare(mStmtChat, insLogin, (unsigned long)strlen(insChat)) != 0)
		{
			printf("MySQL prepare Chat_message failed ! \n", mysql_stmt_error(mStmtChat));
			return false;
		}
		return true;
	}

	void CleanupStatements()
	{
		if (mStmtLogin)
		{
			mysql_stmt_close(mStmtLogin);
			mStmtLogin = nullptr;
		}
		if (mStmtRoom)
		{
			mysql_stmt_close(mStmtRoom);
			mStmtRoom = nullptr;
		}
		if (mStmtRoom)
		{
			mysql_stmt_close(mStmtChat);
			mStmtChat = nullptr;
		}
	}


	void TaskProcessThread()
	{
		while (mIsTaskRun)
		{
			MySQLTask task;
			{
				std::unique_lock<std::mutex> lock(mReqLock);
				mReqCV.wait(lock, [this]() { return !mRequestTask.empty() || !mIsTaskRun; });
				if (!mIsTaskRun && mRequestTask.empty())
					break;
				task = mRequestTask.front();
				mRequestTask.pop_front();
			}

			if (task.TaskID == MySQLTaskID::INVALID)
			{
				task.release();
				continue;
			}
			if (!EnsureConnection())
			{
				printf("mysql ensuer connection failed! : %s\n", mysql_error(mConnection));
				task.release();
				continue;
			}

			switch (task.TaskID)
			{
				case MySQLTaskID::INSERT_LOGIN_EVENT:
					HandleInsertLogin(task);
					break;
				case MySQLTaskID::INSERT_ROOM_EVENT:
					HandleInsertRoom(task);
					break;
				case MySQLTaskID::INSERT_CHAT_MESSAGE:
					HandleInsertChat(task);
					break;
				default:
					break;
			}
			task.release();

			//bool isIdle = true;
			//if (auto task = TakeRequestTask(); task.TaskID != MySQLTaskID::INVALID)
			//{
			//	isIdle = false;

			//	//if (task.TaskID == MySQLTaskID::INSERT_LOGIN_EVENT)
			//	//{
			//	//	auto pReq = reinterpret_cast<MySQLLoginEventReq*>(task.pData);
			//	//	printf("\n");
			//	//}
			//	switch (task.TaskID)
			//	{
			//	case MySQLTaskID::INSERT_LOGIN_EVENT:
			//	{
			//		auto pReq = reinterpret_cast<MySQLLoginEventReq*>(task.pData);
			//		printf("[MySQL] INSERT_LOGIN_EVENT user='%s' ts=%llu\n", pReq->UserID, (unsigned long long)pReq->TimestampSec);
			//	}
			//	break;
			//	case MySQLTaskID::INSERT_ROOM_EVENT:
			//	{
			//		auto pReq = reinterpret_cast<MySQLRoomEventReq*>(task.pData);
			//		printf("[MySQL] INSERT_ROOM_EVENT user='%s' room=%d type=%d ts=%llu\n",
			//			pReq->UserID, pReq->RoomNumber, (int)pReq->EventType, (unsigned long long)pReq->TimeStampSec);
			//	}
			//	break;
			//	case MySQLTaskID::INSERT_CHAT_MESSAGE:
			//	{
			//		auto pReq = reinterpret_cast<MySQLChatMsgReq*>(task.pData);
			//		printf("[MySQL] INSERT_CHAT_MESSAGE user='%s' room=%d msg='%s' ts=%llu\n",
			//			pReq->UserID, pReq->RoomNumber, pReq->Message, (unsigned long long)pReq->TimeStampSec);
			//	}
			//	break;
			//	default:
			//		break;
			//	}

			//	task.release();

			//	
			//}
		}
	}

	void HandleInsertLogin(const MySQLTask& task)
	{
		auto pLoginReqPacket = reinterpret_cast<const MySQLLoginEventReq*>(task.pData);
		MYSQL_BIND bind[2] = {};

		unsigned long useridLen = (unsigned long)strnlen(pLoginReqPacket->UserID, sizeof(pLoginReqPacket->UserID));
		bind[0].buffer_type = MYSQL_TYPE_STRING;
		bind[0].buffer = (void*)pLoginReqPacket;
		bind[0].buffer_length = useridLen;

		unsigned long long timestamp = (unsigned long long)pLoginReqPacket->TimestampSec;
		bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
		bind[1].buffer = (void*)&timestamp;

		if (mysql_stmt_bind_param(mStmtLogin, bind) != 0 || mysql_stmt_execute(mStmtLogin) != 0)
		{
			printf("mysql insert login failed !: %s \n", mysql_stmt_error(mStmtLogin));
			mysql_stmt_reset(mStmtLogin);
			return;
		}
		mysql_stmt_reset(mStmtLogin);
	}

	void HandleInsertRoom(const MySQLTask& task)
	{
		auto pRoomEventReqPacket = reinterpret_cast<const MySQLRoomEventReq*>(task.pData);
		MYSQL_BIND bind[4] = {};

		unsigned long useridLen = (unsigned long)strnlen(pRoomEventReqPacket->UserID, sizeof(pRoomEventReqPacket->UserID));
		bind[0].buffer_type = MYSQL_TYPE_STRING;
		bind[0].buffer = (void*)pRoomEventReqPacket->UserID;
		bind[0].buffer_length = useridLen;
		bind[0].length = &useridLen;

		int room = (int)pRoomEventReqPacket->RoomNumber;
		bind[1].buffer_type = MYSQL_TYPE_LONG;
		bind[1].buffer = (void*)&room;

		unsigned int event = (unsigned int)pRoomEventReqPacket->EventType;
		bind[2].buffer_type = MYSQL_TYPE_LONG;
		bind[2].buffer = (void*)&event;

		unsigned long long timestamp = (unsigned long long)pRoomEventReqPacket->TimeStampSec;
		bind[3].buffer_type = MYSQL_TYPE_LONGLONG;
		bind[3].buffer = (void*)&timestamp;

		if (mysql_stmt_bind_param(mStmtRoom, bind) != 0 || mysql_stmt_execute(mStmtRoom) != 0)
		{
			printf("mysql insert room fail ! : %s\n", mysql_stmt_error(mStmtRoom));
			mysql_stmt_reset(mStmtRoom);
			return;
		}
		mysql_stmt_reset(mStmtRoom);

	}

	void HandleInsertChat(const MySQLTask& task)
	{
		auto pChatMessageReqPacket = reinterpret_cast<const MySQLChatMsgReq*>(task.pData);
		MYSQL_BIND bind[4] = {};

		unsigned long useridLen = (unsigned long)strnlen(pChatMessageReqPacket->UserID, sizeof(pChatMessageReqPacket->UserID));
		bind[0].buffer_type = MYSQL_TYPE_STRING;
		bind[0].buffer = (void*)pChatMessageReqPacket->UserID;
		bind[0].buffer_length = useridLen;
		bind[0].length = &useridLen;

		int room = (int)pChatMessageReqPacket->RoomNumber;
		bind[1].buffer_type = MYSQL_TYPE_LONG;
		bind[1].buffer = (void*)&room;

		unsigned long msgLen = (unsigned long)strnlen(pChatMessageReqPacket->Message, sizeof(pChatMessageReqPacket->Message));
		bind[2].buffer_type = MYSQL_TYPE_STRING;
		bind[2].buffer = (void*)pChatMessageReqPacket->Message;
		bind[2].buffer_length = msgLen;
		bind[2].length = &msgLen;

		unsigned long long timestamp = (unsigned long long)pChatMessageReqPacket->TimeStampSec;
		bind[3].buffer_type = MYSQL_TYPE_LONGLONG;
		bind[3].buffer = (void*)&timestamp;

		if (mysql_stmt_bind_param(mStmtChat, bind) != 0 || mysql_stmt_execute(mStmtChat) != 0)
		{
			printf("MySQL insert chat failed ! : %s\n", mysql_stmt_error(mStmtChat));
			mysql_stmt_reset(mStmtChat);
			return;
		}
		mysql_stmt_reset(mStmtChat);
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


	std::vector<std::thread> mTaskThreads;
	std::mutex mReqLock;
	std::deque<MySQLTask> mRequestTask;
	std::condition_variable mReqCV;
	//std::deque<MySQLTask> mResponseTask;

	bool mIsTaskRun = false;
	
	// 연결
	MYSQL* mConnection;
	MYSQL_STMT* mStmtLogin;
	MYSQL_STMT* mStmtRoom;
	MYSQL_STMT* mStmtChat;

	std::string mHost;
	std::string mUser;
	std::string mPass;
	std::string mDB;
	unsigned int mPort = 0;
	unsigned mConnectTimeoutSec = 5;
};