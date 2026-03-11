//#include "IOCP.h"
#include "ChatServer.h"
#include "CrashDump.h"
#include <string>
#include <iostream>
const UINT16 SERVER_PORT = 11021;
const UINT16 MAX_CLIENT = 2000;	// 총 접속 가능한 클라이언트 수
const UINT32 MAX_IO_WORKER_THREAD =8;


// Ctrl+c, 콘솔 종료 버튼에서 gracefunshutdown을 호출하기 위한 포인터
ChatServer* g_pServer = nullptr;

BOOL WINAPI ConsoleCtrlHandler(DWORD ctrlType)
{
	switch (ctrlType)
	{
		case CTRL_C_EVENT:			// Ctrl+c
			printf("Ctrl+c 종료 신호 수신\n");
		case CTRL_CLOSE_EVENT:		// 콘솔 x버튼
			printf("콘솔x 종료 신호 수신\n");
		case CTRL_SHUTDOWN_EVENT:	// 시스템 종료
			printf("시스템 종료 신호 수신\n");
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

	//IOCompletionPort ioCompletionPort;
	ChatServer Server;
	g_pServer = &Server;

	// 소켓을 초기화
	Server.Init(MAX_IO_WORKER_THREAD);

	// 소켓과 서버 주소를 연결하고 등록
	Server.BindandListen(SERVER_PORT);

	Server.Run(MAX_CLIENT);

	printf("아무키나 누를 때까지 대기 \n");
	
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