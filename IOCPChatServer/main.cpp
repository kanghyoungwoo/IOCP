//#include "IOCP.h"
#include "EchoServer.h"
#include <string>
#include <iostream>
const UINT16 SERVER_PORT = 25000;
const UINT16 MAX_CLIENT = 100;	// �� ���� ������ Ŭ���̾�Ʈ ��

int main()
{
	//IOCompletionPort ioCompletionPort;
	EchoServer Server;

	// ������ �ʱ�ȭ
	//ioCompletionPort.InitSocket();
	Server.InitSocket();

	// ���ϰ� ���� �ּҸ� �����ϰ� ���
	//ioCompletionPort.BindandListen(SERVER_PORT);
	Server.BindandListen(SERVER_PORT);

	//ioCompletionPort.StartServer(MAX_CLIENT);
	Server.StartServer(MAX_CLIENT);

	printf("�ƹ�Ű�� ���� ������ ��� \n");
	getchar();
	while (true)
	{
		std::string input;
		std::getline(std::cin, input);
		
		if (input == "quit")
		{
			break;
		}
	}

	//ioCompletionPort.DestroyThread();
	Server.DestroyThread();

	return 0;
}