//#include "IOCP.h"
#include "ChatServer.h"
#include "CrashDump.h"
#include "ObjectPool.h"
#include <string>
#include <iostream>
const UINT16 SERVER_PORT = 11021;
const UINT16 MAX_CLIENT = 10000;	// 한 번에 접속할 클라이언트 수
const UINT32 MAX_IO_WORKER_THREAD =8;


// Ctrl+c, 콘솔 닫기 버튼에서 graceful shutdown을 호출하기 위한 전역변수
ChatServer* g_pServer = nullptr;

BOOL WINAPI ConsoleCtrlHandler(DWORD ctrlType)
{
	switch (ctrlType)
	{
		case CTRL_C_EVENT:			// Ctrl+c
			LOG_DEBUG("Ctrl+c signal received\n");
		case CTRL_CLOSE_EVENT:		// console x button
			LOG_DEBUG("Console close signal received\n");
		case CTRL_SHUTDOWN_EVENT:	// system shutdown
			LOG_DEBUG("System shutdown signal received\n");
			if (g_pServer)
				g_pServer->End();
			return TRUE;
	}
	return FALSE;
}
int main()
{
	CrashDump::Init();
	SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);
	ObjectPool<SendOverlappedEx> pool;
	if (pool.IsLockFree())
	{
		LOG_DEBUG("Pool: Lock-Free mode\n");
	}
	else
	{
		LOG_DEBUG("Pool: Lock-based mode\n");
	}
	//IOCompletionPort ioCompletionPort;
	ChatServer Server;
	g_pServer = &Server;

	// 서버 초기화
	Server.Init(MAX_IO_WORKER_THREAD);

	// 소켓과 서버 주소를 바인딩하고 리슨
	Server.BindandListen(SERVER_PORT);

	Server.Run(MAX_CLIENT);

	printf("Type 'quit' to stop the server.\n");

	while (true)
	{
		std::string input;
		std::getline(std::cin, input);

		if (input == "quit")
		{
			break;
		}
	}

	Server.End();
	g_pServer = nullptr; // 핸들러에서 이중 호출 방지

	return 0;
}
