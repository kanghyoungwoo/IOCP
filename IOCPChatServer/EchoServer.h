#pragma once
#include"IOCP.h"

class EchoServer : public IOCompletionPort
{
	virtual void OnConnect(const int clientIndex) override
	{
		printf("[OnConnect] Client Index : %d\n", clientIndex);
	}

	virtual void OnClose(const int clientIndex) override
	{
		printf("[OnClosed] Client Index : %d\n", clientIndex);
	}

	virtual void OnReceive(const int clientIndex, int size, char* pData) override
	{
		printf("[OnReceive] Ciient Index : %d , DataSize : %d, Recv Data : %s\n", clientIndex, size, pData);
	}
	 
};