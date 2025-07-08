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
		// 윈속 사용 끝
		WSACleanup();
	}

	// 소켓을 초기화 하는 함수
	bool InitSocket()
	{
		WSADATA wsaData;

		int nRet = WSAStartup(MAKEWORD(2, 2), &wsaData);
		if (nRet != 0)
		{
			printf("[ERROR] WSAStartup() 실패 : %d\n", WSAGetLastError());
			return false;
		}

		// TCP, Overlapped I/O 소켓을 생성
		mListenSocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, NULL, WSA_FLAG_OVERLAPPED);

		if (mListenSocket == INVALID_SOCKET)
		{
			printf("[ERROR] socket() 실패 : %d\n", WSAGetLastError());
			return false;
		}

		printf("SOCKET 초기화 성공\n");
		return true;
	}

	// ------------------  서버용 함수 ------------------------
	// 서버의 주소 정보를 소켓과 연결시키고 접속 요청을 받기 위해
	// 소켓을 등록하는 함수

	bool BindandListen(int nBindPort)
	{
		SOCKADDR_IN stServerAddr;
		stServerAddr.sin_family = AF_INET;
		stServerAddr.sin_port = htons(nBindPort);	// 서버 포트를 설정
		// 어떤 주소에서 들어오는 접속이라도 받게 하는데 보통 서버라면 이렇게 설정
		stServerAddr.sin_addr.s_addr = htonl(INADDR_ANY);

		// 위에서 지정한 서버 주소 정보와 cIOCompletionPort 소켓을 연결
		int nRet = bind(mListenSocket, (SOCKADDR*)&stServerAddr, sizeof(SOCKADDR_IN));

		if (nRet != 0)
		{
			printf("[ERROR] bind() 실패 : %d\n", WSAGetLastError());
			return false;
		}

		// 접속 요청을 받아들이기 위한 cIOCompletionPort 소켓을 등록하고
		// 접속대기큐를 5개로 설정
		nRet = listen(mListenSocket, 5);
		if (nRet != 0)
		{
			printf("[ERROR] listen() 실패 : %d\n", GetLastError());
			return false;
		}

		printf("서버 등록 성공 ! \n");
		return true;
	}

	// 접속 요청을 수락하고 메세지를 받아서 처리하는 함수
	bool StartServer(const int maxClientCount)
	{
		CreateClient(maxClientCount);

		// 처음 IOCP QUEUE만들땐 인자 NULL, 마지막인자 0이면 OS에 맡김
		mIOCPHandle = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL, MAX_WORKERTHREAD);
		
		if (mIOCPHandle == NULL)
		{
			printf("[ERROR] CreateIoCompletionPort() 실패 : %d\n", GetLastError());
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

		printf("서버 시작 \n");
		return true;
	}

	// 생성되어 있는 쓰레드를 파괴한다
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

		// Accepter 쓰레드를 종료
		mIsAccepterRun = false;
		closesocket(mListenSocket);
		
		if (mAccepterThread.joinable())
		{
			mAccepterThread.join();
		}
		
	}
	// 클라이언트의 정보를 받아서
	// 클라이언트에게 메세지를 send하는 함수
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

	// WaitingThread Queue에서 대기할 쓰레드들 생성 
	bool CreateWorkerThread()
	{
		unsigned int uiThreadId = 0;
		// WaitingThread Queue에 대기 상태로 넣을 쓰레드들, 권장갯수는 (cpu갯수 * 2) + 1
		for (int i = 0;i < MAX_WORKERTHREAD;i++)
		{
			mIOWorkerThreads.emplace_back([this]() {WorkerThread();});
		}

		printf("WorkerThread 시작 \n");
		return true;
	}

	// accept 요청을 처리하는 쓰레드 생성
	bool CreateAccepterThread()
	{
		mAccepterThread = std::thread([this]() { AccepterThread(); });

		printf("AccepterThread 시작\n");
		return true;
	}

	// 사용하지 않는 클라이언트의 정보 구조체를 반환
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

	// 클라이언트의 index를 넣으면 client의 info를 리턴하는 함수
	ClientSession* GetClientInfo(const UINT32 clientSessionIndex)
	{
		return &mClientInfos[clientSessionIndex];
	}

	//// CompletionPort객체와 소켓과 CompletionKey를 연결시키는 역할을 함
	//bool BindIOCompletionPort(ClientSession* pClientInfo)
	//{
	//	// socket과 pClientInfo를 CompletionPort객체와 연결시킴
	//	auto hIOCP = CreateIoCompletionPort(
	//		(HANDLE)pClientInfo->m_socketClient,
	//		mIOCPHandle,
	//		(ULONG_PTR)(pClientInfo), // KEY 
	//		0);

	//	if (NULL == hIOCP || mIOCPHandle != hIOCP)
	//	{
	//		printf("[ERROR] CreateIoCompletionPort() 실패 : %d\n", GetLastError());
	//		return false;
	//	}

	//	return true;
	//}

	//bool BindRecv(ClientSession* pClientInfo)
	//{
	//	DWORD dwFlag = 0;
	//	DWORD dwRecvNumByter = 0;

	//	// Overlapped I/O를 위해 각 정보를 세팅
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

	//	// socket_error 면 client socket이 끊어진것으로 처리
	//	if (nRet == SOCKET_ERROR && (WSAGetLastError() != ERROR_IO_PENDING))
	//	{
	//		printf("[ERROR] WSARecv() 실패 : %d\n", WSAGetLastError());
	//		return false;
	//	}
	//	return true;
	//}

	//// WSASend Overlapped I/O 작업을 시작
	//bool SendMsg(ClientSession* pClientInfo, char* pMsg, int nLen)
	//{
	//	DWORD dwRecvNumBytes = 0;

	//	// 전송될 메세지를 복사
	//	CopyMemory(pClientInfo->mSendBuf, pMsg, nLen);

	//	// Overlapped I/O를 위해 각 정보를 세팅
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

	//	// socket_error면 client socket이 끊어진 것으로 처리
	//	if (nRet == SOCKET_ERROR && (WSAGetLastError() != ERROR_IO_PENDING))
	//	{
	//		printf("[ERROR] WSASend() 실패 : %d\n", WSAGetLastError());
	//		return false;
	//	}
	//	return true;
	//}

	// Overlapped I/O 작업에 대한 완료 통보를 받아 그에 해당하는 처리를 하는 함수
	void WorkerThread()
	{
		// CompletionKey를 받을 포인터 변수 
		ClientSession* pClientSession = NULL;
		// 함수 호출 성공 여부
		BOOL bSuccess = TRUE;
		// Overlapped I/O 작업에서 전송된 데이터 크기
		DWORD dwIoSize = 0;
		// I/O 작업을 위해 요청한 Overlapped 구조체를 받을 포인터
		LPOVERLAPPED lpOverlapped = NULL;

		while (mIsWorkerRun)
		{
			////////////////////////////////////
			// 이 함수로 인해 쓰레드들은 WaitingThread Queue에 대기 상태로 들어감
			// 완료된 Overlapped I/O 작업이 발생하면 IOCP Queue에서 완료된 작업을 가져와 처리
			// 그리고 PostQeueuCompletionStatus()에 의해 사용자 메세지가 도착되면 쓰레드 종료
			////////////////////////////////////
			bSuccess = GetQueuedCompletionStatus(
				mIOCPHandle,				// dequeue할 IOCP 핸들
				&dwIoSize,					// 실제 전송된 바이트
				(PULONG_PTR)&pClientSession,	// CompletionKey
				&lpOverlapped,				// Overlapped IO 객체
				INFINITE);					// 대기할 시간

			// 사용자 쓰레드 종료 메세지 처리
			if (bSuccess == TRUE && dwIoSize == 0 && lpOverlapped)
			{
				mIsWorkerRun = false;
				continue;
			}

			if (lpOverlapped == NULL)
			{
				continue;
			}

			// client가 접속을 끊었을때
			if (bSuccess == FALSE || (dwIoSize == 0 && bSuccess == TRUE))
			{
				//printf("Socket(%d) 접속 끊김\n", (int)pClientInfo->m_socketClient);
				//OnClose(pClientSession->mIndex);
				CloseSocket(pClientSession);
				continue;
			}

			//auto pOverlappedEx = (stOverlappedEx*)lpOverlapped;
			stOverlappedEx* pOverlappedEx = (stOverlappedEx*)lpOverlapped;

			// Overlapped I/O Recv 작업 결과 뒤 처리
			if (IOOperation::RECV == pOverlappedEx->m_eOperation)
			{
				//pClientSession->mRecvBuf[dwIoSize] = NULL;
				//printf("[수신] bytes : %d , msg : %s\n", dwIoSize, pClientInfo->mRecvBuf);
				OnReceive(pClientSession->GetIndex(), dwIoSize, pClientSession->RecvBuff());

				// 클라이언트에 메세지를 echo
				//SendMsg(pClientInfo, dwIoSize ,pClientInfo->mRecvBuf); // send를 분리
				pClientSession->BindRecv();
				//BindRecv(pClientInfo);
				
			}

			// Overlapped I/O Send 작업 결과 뒤 처리
			else if (IOOperation::SEND == pOverlappedEx->m_eOperation)
			{
				//printf("[송신] bytes : %d, msg : %s\n", dwIoSize, pClientSession->SendBuff());
				delete[] pOverlappedEx->m_wsaBuf.buf;
				delete pOverlappedEx;
				pClientSession->SendComplete(dwIoSize);

			}
			// 예외
			else
			{
				printf("Client Index : (%d)에서 예외상황\n", (int)pClientSession->GetIndex());
			}
		}
	}

	//사용자의 접속을 받는 쓰레드
	void AccepterThread()
	{
		SOCKADDR_IN		stClientAddr;
		int nAddrLen = sizeof(SOCKADDR_IN);

		while (mIsAccepterRun)
		{
			// 접속을 받을 구조체의 인덱스를 얻어옴
			ClientSession* pClientSession = GetEmptyClientInfo();
			//pClientSession->GetIndex();
			//pClientSession->mIndex = mClientCnt;

			if (pClientSession == NULL)
			{
				printf("[ERROR] Client FULL \n");
				return;
			}

			//클라이언트 접속 요청이 들어올 때까지 기다림
			auto newSocket = accept(mListenSocket, (SOCKADDR*)&stClientAddr, &nAddrLen);

			//pClientInfo->m_socketClient = accept(mListenSocket, (SOCKADDR*)&stClientAddr, &nAddrLen);
			if (newSocket == INVALID_SOCKET) 
			{
				printf("[ERROR] accept() 실패 : %d\n", WSAGetLastError());
				continue;
			}

			if (pClientSession->OnConnect(mIOCPHandle, newSocket) == false)
			{
				pClientSession->Closed(true);
				return;
			}
			//// I/O Completion Port와 소켓을 연결시킴
			//bool bRet = BindIOCompletionPort(pClientInfo);
			//if (bRet == false)
			//{
			//	return;
			//}

			////Recv Overlapped I/O작업을 요청해둠
			//bRet = BindRecv(pClientInfo);
			//if (bRet == false)
			//{
			//	return;
			//}

			//char clientIP[32] = { 0 };
			//inet_ntop(AF_INET, &(stClientAddr.sin_addr), clientIP, 32 - 1);
			//printf("Client 접속 : IP(%s) SOCKET(%d)", clientIP, (int)pClientInfo->m_socketClient);

			OnConnect(pClientSession->GetIndex());

			// 클라이언트 갯수 증가
			++mClientCnt;
		}
	}


	// 소켓의 연결을 종료
	void CloseSocket(ClientSession* pClientSession, bool bIsForce = false)
	{
		UINT32 ClientIndex = pClientSession->GetIndex();
		pClientSession->Closed(bIsForce);
		OnClose(ClientIndex);



	}

	// 클라이언트 정보 저장 구조체
	std::vector<ClientSession> mClientInfos;

	// 클라이언트의 접속을 받기 위한 리슨 소켓
	SOCKET mListenSocket = INVALID_SOCKET;

	// 접속 되어있는 클라이언트 수
	int mClientCnt = 0;

	std::vector<std::thread> mIOWorkerThreads;

	// Accept 쓰레드
	std::thread mAccepterThread;
	
	// CompletionPort객체 핸들 
	HANDLE	mIOCPHandle = INVALID_HANDLE_VALUE;

	// 작업 쓰레드 동작 플래그
	bool	mIsWorkerRun = true;

	// 접속 쓰레드 동작 플래그
	bool	mIsAccepterRun = true;
};