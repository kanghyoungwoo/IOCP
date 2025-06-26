#include "IOCP.h"

const UINT16 SERVER_PORT = 25000;
const UINT16 MAX_CLIENT = 100;	// �� ���� ������ Ŭ���̾�Ʈ ��

int main()
{
	IOCompletionPort ioCompletionPort;

	// ������ �ʱ�ȭ
	ioCompletionPort.InitSocket();

	// ���ϰ� ���� �ּҸ� �����ϰ� ���
	ioCompletionPort.BindandListen(SERVER_PORT);

	ioCompletionPort.StartServer(MAX_CLIENT);

	printf("�ƹ�Ű�� ���� ������ ��� \n");
	getchar();

	ioCompletionPort.DestroyThread();

	return 0;
}