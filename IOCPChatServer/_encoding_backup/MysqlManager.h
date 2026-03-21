#pragma once
#include "MySQLTaskDefine.h"
#include "Define.h"

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

	}
	~MySQLManager()
	{
		End();
	}

	// ���� ���� ��� ������ ����
	void configure(const char* host_, const char* user_, const char* pass_, const char* db_, unsigned int port_)
	{
		mHost = host_ ? host_ : "chatserver-database.cts4w8y0e8qh.ap-northeast-2.rds.amazonaws.com";
		mUser = user_ ? user_ : "admin";
		mPass = pass_ ? pass_ : "12345678";
		mDB = db_ ? db_ : "chatserver-database";
		mPort = port_ ? port_ : 3306;
	}

	// ���� -> �غ� -> ������ ���� ���� 
	bool Run(UINT32 threadCount_)
	{
		mIsTaskRun = true;

		for (UINT32 i = 0;i < threadCount_;++i)
		{
			mTaskThreads.emplace_back([this]() {TaskProcessThread();});
		}

		// ���� ����
		//// 1. MySQL ������ ����
		//if (!InitandConnect())
		//{
		//	printf("MySQL connection failed !\n");
		//	return false;
		//}
		//// 2. Prepared Statement �غ�(���̺����� + ���� �̸� �Ľ�)
		//if (!PrepareStatements())
		//{
		//	printf("MySQL statement prepare fail\n");
		//	return false;
		//}
		//
		//// 3. �۾� ������ ����
		//mIsTaskRun = true;
		//// �����带 ����� ��� �� 
		//for (UINT32 i = 0; i < threadCount_; ++i)
		//{
		//	mTaskThreads.emplace_back([this]() { TaskProcessThread();});
		//}
		LOG_DEBUG("MySQL task worker started\n");
		return true;
	}

	void End()
	{
		// ���� ����
		{
			std::lock_guard<std::mutex> guard(mReqLock);
			mIsTaskRun = false;
		}
		// �ڰ��ִ� ������ �����
		mReqCV.notify_all();	// cv ���� �߰�

		// ��� ������ join(ť ��ﶧ���� ��ٸ�)
		for (auto& thread : mTaskThreads)
		{
			if (thread.joinable())
			{
				thread.join();
			}
		}
		// ����
		mTaskThreads.clear();
		//CleanupStatements();	// state����
		//CloseConnection();		// ���� ����


		//���� ���� ���
		//mIsTaskRun = false;
		//for (auto& thread : mTaskThreads)
		//{
		//	if (thread.joinable())
		//	{
		//		thread.join();
		//	}
		//}
	}
	// �۾� �ֱ� 
	void PushTask(MySQLTask task_)	// producer
	{
		std::lock_guard<std::mutex> guard(mReqLock); // ť ���
		mRequestTask.push_back(task_); // ť�� �߰�
		mReqCV.notify_one(); // ������� ������ �����
	}


private:
	struct MySQLConnection
	{
		MYSQL*		connection = nullptr;
		MYSQL_STMT* stmtLogin = nullptr;
		MYSQL_STMT* stmtRoom = nullptr;
		MYSQL_STMT* stmtChat = nullptr;
	};


	bool InitandConnect(MySQLConnection& conn)
	{
		if (mHost.empty())
		{
			mHost = "chatserver-database.cts4w8y0e8qh.ap-northeast-2.rds.amazonaws.com";
			mUser = "admin";
			mPass = "12345678";
			mDB = "chatserver-database";
			mPort = 3306;
		}

		conn.connection = mysql_init(nullptr);

		if (!conn.connection)
		{
			LOG_ERROR("MySQL init fail!\n");
			return false;
		}

		mysql_options(conn.connection, MYSQL_SET_CHARSET_NAME, "utf8mb4");
		mysql_options(conn.connection, MYSQL_OPT_CONNECT_TIMEOUT, &mConnectTimeoutSec);

		if (!mysql_real_connect(conn.connection, mHost.c_str(), mUser.c_str(), mPass.c_str(), mDB.c_str(), mPort, nullptr, 0))
		{
			LOG_ERROR("MySQL connection error: %s\n", mysql_error(conn.connection));
			return false;
		}
		return true;
	}

	void CloseConnection(MySQLConnection& conn)
	{
		if (conn.connection)
		{
			mysql_close(conn.connection);
			conn.connection = nullptr;
		}
	}
	
	bool EnsureConnection(MySQLConnection& conn)
	{
		// Ŀ�ؼ��� ��������� ok
		if (conn.connection && mysql_ping(conn.connection) == 0)
		{
			return true;
		}
		// ���� �͵��� ���� �ؾ���
		CleanupStatements(conn);

		// �׾��ٸ� �ݰ� �翬��.. (mysql ������ ���� �Ⱦ��� ������ ����)
		CloseConnection(conn);

		// ���� ����
		if (!InitandConnect(conn))
			return false;
		return PrepareStatements(conn);
	}

	// ���̺� ���� + ���� �غ�
	bool PrepareStatements(MySQLConnection& conn)
	{
		const char* createLogin =
			"CREATE TABLE IF NOT EXISTS login_events("
			"id INT PRIMARY KEY AUTO_INCREMENT,"
			"user_id VARCHAR(64) NOT NULL,"
			"timestamp BIGINT NOT NULL"
			")";
		const char* createRoom =
			"CREATE TABLE IF NOT EXISTS room_events("
			"id INT PRIMARY KEY AUTO_INCREMENT,"
			"user_id VARCHAR(64) NOT NULL,"
			"room_number INT NOT NULL,"
			"event TINYINT NOT NULL,"
			"timestamp BIGINT NOT NULL"
			")";
		const char* createChat =
			"CREATE TABLE IF NOT EXISTS chat_messages("
			"id INT PRIMARY KEY AUTO_INCREMENT,"
			"user_id VARCHAR(64) NOT NULL,"
			"room_number INT NOT NULL,"
			"msg VARCHAR(256) NOT NULL,"
			"timestamp BIGINT NOT NULL"
			")";
		// 1. ���̺� ������ ����
		if (mysql_query(conn.connection, createLogin) != 0)
		{
			LOG_ERROR("MySQL create login_events fail: %s\n", mysql_error(conn.connection));
			return false;
		}
		if (mysql_query(conn.connection, createRoom) != 0)
		{
			LOG_ERROR("MySQL create room_events fail: %s\n", mysql_error(conn.connection));
			return false;
		}
		if (mysql_query(conn.connection, createChat) != 0)
		{
			LOG_ERROR("MySQL create chat_messages fail: %s\n", mysql_error(conn.connection));
			return false;
		}
		// prepared statement �ʱ�ȭ
		conn.stmtLogin = mysql_stmt_init(conn.connection);
		conn.stmtRoom = mysql_stmt_init(conn.connection);
		conn.stmtChat = mysql_stmt_init(conn.connection);
		if (!conn.stmtLogin || !conn.stmtRoom || !conn.stmtChat)
		{
			LOG_ERROR("MySQL stmt init failed!\n");
			return false;
		}

		const char* insLogin = "INSERT INTO login_events(user_id, timestamp) VALUES(?, ?)";
		const char* insRoom = "INSERT INTO room_events(user_id, room_number, event, timestamp) VALUES(?, ?, ?, ?)";
		const char* insChat = "INSERT INTO chat_messages(user_id, room_number, msg, timestamp) VALUES(?, ?, ?, ?)";
		// 3. SQL �̸� �Ľ�
		if (mysql_stmt_prepare(conn.stmtLogin, insLogin, (unsigned long)strlen(insLogin)) != 0)
		{
			LOG_ERROR("MySQL prepare login_events failed: %s\n", mysql_stmt_error(conn.stmtLogin));
			return false;
		}
		if (mysql_stmt_prepare(conn.stmtRoom, insRoom, (unsigned long)strlen(insRoom)) != 0)
		{
			LOG_ERROR("MySQL prepare room_events failed: %s\n", mysql_stmt_error(conn.stmtRoom));
			return false;
		}
		if (mysql_stmt_prepare(conn.stmtChat, insChat, (unsigned long)strlen(insChat)) != 0)
		{
			LOG_ERROR("MySQL prepare chat_messages failed: %s\n", mysql_stmt_error(conn.stmtChat));
			return false;
		}
		return true;
	}

	void CleanupStatements(MySQLConnection& conn)
	{
		if (conn.stmtLogin)
		{
			mysql_stmt_close(conn.stmtLogin);
			conn.stmtLogin = nullptr;
		}
		if (conn.stmtRoom)
		{
			mysql_stmt_close(conn.stmtRoom);
			conn.stmtRoom = nullptr;
		}
		if (conn.stmtChat)
		{
			mysql_stmt_close(conn.stmtChat);
			conn.stmtChat = nullptr;
		}
	}


	void TaskProcessThread()	// consumer
	{
		MySQLConnection myConn;
		InitandConnect(myConn);
		PrepareStatements(myConn);

		while (true) // mIsTaskRun->true
		{
			MySQLTask task;
			{
				// ť���� �۾� ������ (������ ���)
				std::unique_lock<std::mutex> lock(mReqLock);
				mReqCV.wait(lock, [this]() { return !mRequestTask.empty() || !mIsTaskRun; }); // ť�� ���� �����ų� ��ȣ ���� ��ȣ�� �� �� ���� ���
				//�����ȣ + ť ��������� -> ����Ż��
				if (!mIsTaskRun && mRequestTask.empty())
					break;
				// �۾� ������
				task = mRequestTask.front();
				mRequestTask.pop_front();
			}
			// lock ����
			if (task.TaskID == MySQLTaskID::INVALID)
			{
				task.release();
				continue;
			}

			// Ŀ�ؼ� ����ִ��� Ȯ���ϰ� �������� �翬��
			if (!EnsureConnection(myConn))
			{
				LOG_ERROR("MySQL ensure connection failed: %s\n", mysql_error(myConn.connection));
				task.release();
				continue;
			}

			switch (task.TaskID)
			{
				case MySQLTaskID::INSERT_LOGIN_EVENT:
					HandleInsertLogin(myConn, task);
					break;
				case MySQLTaskID::INSERT_ROOM_EVENT:
					HandleInsertRoom(myConn, task);
					break;
				case MySQLTaskID::INSERT_CHAT_MESSAGE:
					HandleInsertChat(myConn, task);
					break;
				default:
					break;
			}
			task.release();
		}
		CleanupStatements(myConn);
		CloseConnection(myConn);
		mysql_thread_end();	// ���ҽ� �޸� ����
	}
	// ���� insert ����
	void HandleInsertLogin(MySQLConnection& conn, const MySQLTask& task)
	{
		auto pLoginReqPacket = reinterpret_cast<const MySQLLoginEventReq*>(task.pData);

		// mysql_bind: mysql_stmt_prepare�� ? �ڸ��� ���� �� ����
		MYSQL_BIND bind[2] = {};

		// bind[0] = user_id(���ڿ�)
		unsigned long useridLen = (unsigned long)strnlen(pLoginReqPacket->UserID, sizeof(pLoginReqPacket->UserID));
		bind[0].buffer_type = MYSQL_TYPE_STRING;
		bind[0].buffer = (void*)pLoginReqPacket->UserID;
		bind[0].buffer_length = useridLen;
		bind[0].length = &useridLen;

		// bind[1] = timestamp(����)
		unsigned long long timestamp = (unsigned long long)pLoginReqPacket->TimestampSec;
		bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
		bind[1].buffer = (void*)&timestamp;

		// ? �ڸ��� �� ���ε� + ����
		if (mysql_stmt_bind_param(conn.stmtLogin, bind) != 0 || mysql_stmt_execute(conn.stmtLogin) != 0)
		{
			LOG_ERROR("MySQL insert login failed: %s\n", mysql_stmt_error(conn.stmtLogin));
			mysql_stmt_reset(conn.stmtLogin); // ���� ������ ���� �ʱ�ȭ
			return;
		}
		mysql_stmt_reset(conn.stmtLogin);
	}

	void HandleInsertRoom(MySQLConnection& conn, const MySQLTask& task)
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

		if (mysql_stmt_bind_param(conn.stmtRoom, bind) != 0 || mysql_stmt_execute(conn.stmtRoom) != 0)
		{
			LOG_ERROR("MySQL insert room failed: %s\n", mysql_stmt_error(conn.stmtRoom));
			mysql_stmt_reset(conn.stmtRoom);
			return;
		}
		mysql_stmt_reset(conn.stmtRoom);

	}

	void HandleInsertChat(MySQLConnection& conn, const MySQLTask& task)
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

		if (mysql_stmt_bind_param(conn.stmtChat, bind) != 0 || mysql_stmt_execute(conn.stmtChat) != 0)
		{
			LOG_ERROR("MySQL insert chat failed: %s\n", mysql_stmt_error(conn.stmtChat));
			mysql_stmt_reset(conn.stmtChat);
			return;
		}
		mysql_stmt_reset(conn.stmtChat);
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
	
	//// ����
	//MYSQL* conn.connection;
	//MYSQL_STMT* conn.stmtLogin;
	//MYSQL_STMT* conn.stmtRoom;
	//MYSQL_STMT* conn.stmtChat;

	std::string mHost;
	std::string mUser;
	std::string mPass;
	std::string mDB;
	unsigned int mPort = 0;
	unsigned mConnectTimeoutSec = 5;
};