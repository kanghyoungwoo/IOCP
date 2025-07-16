//#include "IOCP.h"
#include "EchoServer.h"
#include <string>
#include <iostream>
const UINT16 SERVER_PORT = 25000;
const UINT16 MAX_CLIENT = 3;	// 총 접속 가능한 클라이언트 수
const UINT32 MAX_IO_WORKER_THREAD = 4;

int main()
{
	//IOCompletionPort ioCompletionPort;
	EchoServer Server;

	// 소켓을 초기화
	//ioCompletionPort.InitSocket();
	//Server.InitSocket();
	Server.Init(MAX_IO_WORKER_THREAD);

	// 소켓과 서버 주소를 연결하고 등록
	//ioCompletionPort.BindandListen(SERVER_PORT);
	Server.BindandListen(SERVER_PORT);

	//ioCompletionPort.StartServer(MAX_CLIENT);
	//Server.StartServer(MAX_CLIENT);
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

	return 0;
}