//#include "IOCP.h"
#include "ChatServer.h"
#include "CrashDump.h"
#include "ObjectPool.h"
#include "ConfigManager.h"
#include <string>
#include <iostream>
//const UINT16 SERVER_PORT = 11021;
//const UINT16 MAX_CLIENT = 10000;	// 한 번에 접속할 클라이언트 수
//const UINT32 MAX_IO_WORKER_THREAD =8;


// Ctrl+c, 콘솔 닫기 버튼에서 graceful shutdown을 호출하기 위한 전역변수
ChatServer* g_pServer = nullptr;

BOOL WINAPI ConsoleCtrlHandler(DWORD ctrlType)
{
	switch (ctrlType)
	{
		case CTRL_C_EVENT:			// Ctrl+c
			LOG_DEBUG("Ctrl+c signal received\n");
			break;
		case CTRL_CLOSE_EVENT:		// console x button
			LOG_DEBUG("Console close signal received\n");
			break;
		case CTRL_SHUTDOWN_EVENT:	// system shutdown
			LOG_DEBUG("System shutdown signal received\n");
			break;
		default:
			return FALSE;
	}
	if (g_pServer)
	{
		LOG_ERROR("[종료] ConsoleCtrlHandler에 의한 종료! ctrlType=%d\n", ctrlType);
		g_pServer->End();
	}
	return TRUE;
}
int main()
{
	CrashDump::Init();

	ConfigManager::GetInstance().Load("config.json");
	const auto& config = ConfigManager::GetInstance().Get();
	
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
	
	ChatServer Server;
	g_pServer = &Server;

	// 서버 초기화
	if (!Server.Init(config.MaxIOWorkerThread))
	{
		LOG_ERROR("서버 초기화 실패\n");
		return -1;
	}
	// 소켓과 서버 주소를 바인딩하고 리슨
	if (!Server.BindandListen(config.ServerPort))
	{
		LOG_ERROR("BindandListen 실패\n");
		return -1;
	}
	Server.Run(config.MaxClient);

	printf("'quit' 입력으로 서버 종료.\n");

	while (true)
	{
		std::string input;
		if (!std::getline(std::cin, input))
		{
			// stdin EOF 또는 오류여도 서버는 계속 실행
			LOG_ERROR("[경고] stdin EOF/오류 감지 - 서버는 계속 실행 중\n");
			std::cin.clear();       // fail 상태 초기화
			Sleep(1000);            // 스핀 방지
			continue;               // 종료하지 않고 유지
		}

		if (input == "quit")
		{
			LOG_ERROR("[종료] quit 명령에 의한 종료\n");
			break;
		}
	}

	Server.End();
	g_pServer = nullptr; // 핸들러에서 이중 호출 방지

	return 0;
}
