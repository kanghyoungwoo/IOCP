#pragma once
#pragma comment(lib, "ws2_32")


#include "Define.h"
#include "ClientSession.h"
#include <thread>
#include <vector>



class IOCompletionPort
{
public:
	IOCompletionPort(void) {}
	
	~IOCompletionPort(void)
	{
		// ���� ��� ��
		WSACleanup();
	}

	// ������ �ʱ�ȭ �ϴ� �Լ�
	bool InitSocket()
	{
		WSADATA wsaData;

		int nRet = WSAStartup(MAKEWORD(2, 2), &wsaData);
		if (nRet != 0)
		{
			printf("[ERROR] WSAStartup() ���� : %d\n", WSAGetLastError());
			return false;
		}

		// TCP, Overlapped I/O ������ ����
		mListenSocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, NULL, WSA_FLAG_OVERLAPPED);

		if (mListenSocket == INVALID_SOCKET)
		{
			printf("[ERROR] socket() ���� : %d\n", WSAGetLastError());
			return false;
		}

		printf("SOCKET �ʱ�ȭ ����\n");
		return true;
	}

	// ------------------  ������ �Լ� ------------------------
	// ������ �ּ� ������ ���ϰ� �����Ű�� ���� ��û�� �ޱ� ����
	// ������ ����ϴ� �Լ�

	bool BindandListen(int nBindPort)
	{
		SOCKADDR_IN stServerAddr;
		stServerAddr.sin_family = AF_INET;
		stServerAddr.sin_port = htons(nBindPort);	// ���� ��Ʈ�� ����
		// � �ּҿ��� ������ �����̶� �ް� �ϴµ� ���� ������� �̷��� ����
		stServerAddr.sin_addr.s_addr = htonl(INADDR_ANY);

		// ������ ������ ���� �ּ� ������ cIOCompletionPort ������ ����
		int nRet = bind(mListenSocket, (SOCKADDR*)&stServerAddr, sizeof(SOCKADDR_IN));

		if (nRet != 0)
		{
			printf("[ERROR] bind() ���� : %d\n", WSAGetLastError());
			return false;
		}

		// ���� ��û�� �޾Ƶ��̱� ���� cIOCompletionPort ������ ����ϰ�
		// ���Ӵ��ť�� 5���� ����
		nRet = listen(mListenSocket, 5);
		if (nRet != 0)
		{
			printf("[ERROR] listen() ���� : %d\n", GetLastError());
			return false;
		}

		printf("���� ��� ���� ! \n");
		return true;
	}

	// ���� ��û�� �����ϰ� �޼����� �޾Ƽ� ó���ϴ� �Լ�
	bool StartServer(const int maxClientCount)
	{
		CreateClient(maxClientCount);

		// ó�� IOCP QUEUE���鶩 ���� NULL, ���������� 0�̸� OS�� �ñ�
		mIOCPHandle = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL, MAX_WORKERTHREAD);
		
		if (mIOCPHandle == NULL)
		{
			printf("[ERROR] CreateIoCompletionPort() ���� : %d\n", GetLastError());
			return false;
		}

		bool bRet = CreateWorkerThread();
		if (bRet == false)
		{
			return false;
		}

		bRet = CreateAccepterThread();
		if (bRet == false)
		{
			return false;
		}

		printf("���� ���� \n");
		return true;
	}

	// �����Ǿ� �ִ� �����带 �ı��Ѵ�
	void DestroyThread()
	{
		mIsWorkerRun = false;
		CloseHandle(mIOCPHandle);

		for (auto& th : mIOWorkerThreads)
		{
			if (th.joinable())
			{
				th.join();
			}
		}

		// Accepter �����带 ����
		mIsAccepterRun = false;
		closesocket(mListenSocket);
		
		if (mAccepterThread.joinable())
		{
			mAccepterThread.join();
		}
		
	}
	// Ŭ���̾�Ʈ�� ������ �޾Ƽ�
	// Ŭ���̾�Ʈ���� �޼����� send�ϴ� �Լ�
	bool SendMsg(const UINT32 ClientSessionIndex, const UINT32 dataSize, char* pMsg)
	{
		auto pClient = GetClientInfo(ClientSessionIndex);
		return pClient->SendMsg(dataSize, pMsg);
	}

	virtual void OnConnect(const int clientIndex){}
	virtual void OnClose(const int clientIndex){}
	virtual void OnReceive(const UINT32 clientIndex, const UINT32 size, char* pData){}

private:
	void CreateClient(const int maxClientCount)
	{
		for (int i = 0;i < maxClientCount;++i)
		{
			mClientInfos.emplace_back();
			mClientInfos[i].Init(i);
		}
	}

	// WaitingThread Queue���� ����� ������� ���� 
	bool CreateWorkerThread()
	{
		unsigned int uiThreadId = 0;
		// WaitingThread Queue�� ��� ���·� ���� �������, ���尹���� (cpu���� * 2) + 1
		for (int i = 0;i < MAX_WORKERTHREAD;i++)
		{
			mIOWorkerThreads.emplace_back([this]() {WorkerThread();});
		}

		printf("WorkerThread ���� \n");
		return true;
	}

	// accept ��û�� ó���ϴ� ������ ����
	bool CreateAccepterThread()
	{
		mAccepterThread = std::thread([this]() { AccepterThread(); });

		printf("AccepterThread ����\n");
		return true;
	}

	// ������� �ʴ� Ŭ���̾�Ʈ�� ���� ����ü�� ��ȯ
	ClientSession* GetEmptyClientInfo()
	{
		for (auto& client : mClientInfos)
		{
			if (client.IsConnected() == false)
			{
				return &client;
			}
		}
		return nullptr;
	}

	// Ŭ���̾�Ʈ�� index�� ������ client�� info�� �����ϴ� �Լ�
	ClientSession* GetClientInfo(const UINT32 clientSessionIndex)
	{
		return &mClientInfos[clientSessionIndex];
	}

	//// CompletionPort��ü�� ���ϰ� CompletionKey�� �����Ű�� ������ ��
	//bool BindIOCompletionPort(ClientSession* pClientInfo)
	//{
	//	// socket�� pClientInfo�� CompletionPort��ü�� �����Ŵ
	//	auto hIOCP = CreateIoCompletionPort(
	//		(HANDLE)pClientInfo->m_socketClient,
	//		mIOCPHandle,
	//		(ULONG_PTR)(pClientInfo), // KEY 
	//		0);

	//	if (NULL == hIOCP || mIOCPHandle != hIOCP)
	//	{
	//		printf("[ERROR] CreateIoCompletionPort() ���� : %d\n", GetLastError());
	//		return false;
	//	}

	//	return true;
	//}

	//bool BindRecv(ClientSession* pClientInfo)
	//{
	//	DWORD dwFlag = 0;
	//	DWORD dwRecvNumByter = 0;

	//	// Overlapped I/O�� ���� �� ������ ����
	//	pClientInfo->m_stRecvOverlappedEx.m_wsaBuf.len = MAX_SOCKBUF;
	//	pClientInfo->m_stRecvOverlappedEx.m_wsaBuf.buf = pClientInfo->mRecvBuf;
	//	pClientInfo->m_stRecvOverlappedEx.m_eOperation = IOOperation::RECV;

	//	int nRet = WSARecv(pClientInfo->m_socketClient,
	//		&(pClientInfo->m_stRecvOverlappedEx.m_wsaBuf),
	//		1,
	//		&dwRecvNumByter,
	//		&dwFlag,
	//		(LPWSAOVERLAPPED) & (pClientInfo->m_stRecvOverlappedEx),
	//		NULL);

	//	// socket_error �� client socket�� ������������ ó��
	//	if (nRet == SOCKET_ERROR && (WSAGetLastError() != ERROR_IO_PENDING))
	//	{
	//		printf("[ERROR] WSARecv() ���� : %d\n", WSAGetLastError());
	//		return false;
	//	}
	//	return true;
	//}

	//// WSASend Overlapped I/O �۾��� ����
	//bool SendMsg(ClientSession* pClientInfo, char* pMsg, int nLen)
	//{
	//	DWORD dwRecvNumBytes = 0;

	//	// ���۵� �޼����� ����
	//	CopyMemory(pClientInfo->mSendBuf, pMsg, nLen);

	//	// Overlapped I/O�� ���� �� ������ ����
	//	pClientInfo->m_stSendOverlappedEx.m_wsaBuf.len = nLen;
	//	pClientInfo->m_stSendOverlappedEx.m_wsaBuf.buf = pClientInfo->mSendBuf;
	//	pClientInfo->m_stSendOverlappedEx.m_eOperation = IOOperation::SEND;

	//	int nRet = WSASend(pClientInfo->m_socketClient,
	//		&(pClientInfo->m_stSendOverlappedEx.m_wsaBuf),
	//		1,
	//		&dwRecvNumBytes,
	//		0,
	//		(LPWSAOVERLAPPED) & (pClientInfo->m_stSendOverlappedEx),
	//		NULL);

	//	// socket_error�� client socket�� ������ ������ ó��
	//	if (nRet == SOCKET_ERROR && (WSAGetLastError() != ERROR_IO_PENDING))
	//	{
	//		printf("[ERROR] WSASend() ���� : %d\n", WSAGetLastError());
	//		return false;
	//	}
	//	return true;
	//}

	// Overlapped I/O �۾��� ���� �Ϸ� �뺸�� �޾� �׿� �ش��ϴ� ó���� �ϴ� �Լ�
	void WorkerThread()
	{
		// CompletionKey�� ���� ������ ���� 
		ClientSession* pClientSession = NULL;
		// �Լ� ȣ�� ���� ����
		BOOL bSuccess = TRUE;
		// Overlapped I/O �۾����� ���۵� ������ ũ��
		DWORD dwIoSize = 0;
		// I/O �۾��� ���� ��û�� Overlapped ����ü�� ���� ������
		LPOVERLAPPED lpOverlapped = NULL;

		while (mIsWorkerRun)
		{
			////////////////////////////////////
			// �� �Լ��� ���� ��������� WaitingThread Queue�� ��� ���·� ��
			// �Ϸ�� Overlapped I/O �۾��� �߻��ϸ� IOCP Queue���� �Ϸ�� �۾��� ������ ó��
			// �׸��� PostQeueuCompletionStatus()�� ���� ����� �޼����� �����Ǹ� ������ ����
			////////////////////////////////////
			bSuccess = GetQueuedCompletionStatus(
				mIOCPHandle,				// dequeue�� IOCP �ڵ�
				&dwIoSize,					// ���� ���۵� ����Ʈ
				(PULONG_PTR)&pClientSession,	// CompletionKey
				&lpOverlapped,				// Overlapped IO ��ü
				INFINITE);					// ����� �ð�

			// ����� ������ ���� �޼��� ó��
			if (bSuccess == TRUE && dwIoSize == 0 && lpOverlapped)
			{
				mIsWorkerRun = false;
				continue;
			}

			if (lpOverlapped == NULL)
			{
				continue;
			}

			// client�� ������ ��������
			if (bSuccess == FALSE || (dwIoSize == 0 && bSuccess == TRUE))
			{
				//printf("Socket(%d) ���� ����\n", (int)pClientInfo->m_socketClient);
				//OnClose(pClientSession->mIndex);
				CloseSocket(pClientSession);
				continue;
			}

			//auto pOverlappedEx = (stOverlappedEx*)lpOverlapped;
			stOverlappedEx* pOverlappedEx = (stOverlappedEx*)lpOverlapped;

			// Overlapped I/O Recv �۾� ��� �� ó��
			if (IOOperation::RECV == pOverlappedEx->m_eOperation)
			{
				//pClientSession->mRecvBuf[dwIoSize] = NULL;
				//printf("[����] bytes : %d , msg : %s\n", dwIoSize, pClientInfo->mRecvBuf);
				OnReceive(pClientSession->GetIndex(), dwIoSize, pClientSession->RecvBuff());

				// Ŭ���̾�Ʈ�� �޼����� echo
				//SendMsg(pClientInfo, dwIoSize ,pClientInfo->mRecvBuf); // send�� �и�
				pClientSession->BindRecv();
				//BindRecv(pClientInfo);
				
			}

			// Overlapped I/O Send �۾� ��� �� ó��
			else if (IOOperation::SEND == pOverlappedEx->m_eOperation)
			{
				//printf("[�۽�] bytes : %d, msg : %s\n", dwIoSize, pClientSession->SendBuff());
				delete[] pOverlappedEx->m_wsaBuf.buf;
				delete pOverlappedEx;
				pClientSession->SendComplete(dwIoSize);

			}
			// ����
			else
			{
				printf("Client Index : (%d)���� ���ܻ�Ȳ\n", (int)pClientSession->GetIndex());
			}
		}
	}

	//������� ������ �޴� ������
	void AccepterThread()
	{
		SOCKADDR_IN		stClientAddr;
		int nAddrLen = sizeof(SOCKADDR_IN);

		while (mIsAccepterRun)
		{
			// ������ ���� ����ü�� �ε����� ����
			ClientSession* pClientSession = GetEmptyClientInfo();
			//pClientSession->GetIndex();
			//pClientSession->mIndex = mClientCnt;

			if (pClientSession == NULL)
			{
				printf("[ERROR] Client FULL \n");
				return;
			}

			//Ŭ���̾�Ʈ ���� ��û�� ���� ������ ��ٸ�
			auto newSocket = accept(mListenSocket, (SOCKADDR*)&stClientAddr, &nAddrLen);

			//pClientInfo->m_socketClient = accept(mListenSocket, (SOCKADDR*)&stClientAddr, &nAddrLen);
			if (newSocket == INVALID_SOCKET) 
			{
				printf("[ERROR] accept() ���� : %d\n", WSAGetLastError());
				continue;
			}

			if (pClientSession->OnConnect(mIOCPHandle, newSocket) == false)
			{
				pClientSession->Closed(true);
				return;
			}
			//// I/O Completion Port�� ������ �����Ŵ
			//bool bRet = BindIOCompletionPort(pClientInfo);
			//if (bRet == false)
			//{
			//	return;
			//}

			////Recv Overlapped I/O�۾��� ��û�ص�
			//bRet = BindRecv(pClientInfo);
			//if (bRet == false)
			//{
			//	return;
			//}

			//char clientIP[32] = { 0 };
			//inet_ntop(AF_INET, &(stClientAddr.sin_addr), clientIP, 32 - 1);
			//printf("Client ���� : IP(%s) SOCKET(%d)", clientIP, (int)pClientInfo->m_socketClient);

			OnConnect(pClientSession->GetIndex());

			// Ŭ���̾�Ʈ ���� ����
			++mClientCnt;
		}
	}


	// ������ ������ ����
	void CloseSocket(ClientSession* pClientSession, bool bIsForce = false)
	{
		UINT32 ClientIndex = pClientSession->GetIndex();
		pClientSession->Closed(bIsForce);
		OnClose(ClientIndex);



	}

	// Ŭ���̾�Ʈ ���� ���� ����ü
	std::vector<ClientSession> mClientInfos;

	// Ŭ���̾�Ʈ�� ������ �ޱ� ���� ���� ����
	SOCKET mListenSocket = INVALID_SOCKET;

	// ���� �Ǿ��ִ� Ŭ���̾�Ʈ ��
	int mClientCnt = 0;

	std::vector<std::thread> mIOWorkerThreads;

	// Accept ������
	std::thread mAccepterThread;
	
	// CompletionPort��ü �ڵ� 
	HANDLE	mIOCPHandle = INVALID_HANDLE_VALUE;

	// �۾� ������ ���� �÷���
	bool	mIsWorkerRun = true;

	// ���� ������ ���� �÷���
	bool	mIsAccepterRun = true;
};