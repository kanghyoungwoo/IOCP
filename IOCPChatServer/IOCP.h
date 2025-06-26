#pragma once
#pragma comment(lib, "ws2_32")
#include <winsock2.h>
#include <Ws2tcpip.h>

#include <thread>
#include <vector>

#define MAX_SOCKBUF 1024	// 패킷 크기
#define MAX_WORKERTHREAD 4	// 쓰레드 풀에 넣을 쓰레드 수

enum class IOOperation
{
	RECV,
	SEND
};

//WSAOVERLAPPED 구조체를 확장 시켜 필요한 정보를 더 넣음
typedef struct _stOverlappedEx
{
	WSAOVERLAPPED	m_wsaOverlapped;		// Overlapped I/O 구조체
	SOCKET			m_socketClient;			// Client 소켓
	WSABUF			m_wsaBuf;				// Overlapped I/O작업 버퍼
	IOOperation		m_eOperation;			// 작업 동작 종류
}stOverlappedEx;


// 클라이언트 정보를 담기 위한 구조체
typedef struct _stClientInfo
{
	SOCKET			m_socketClient;			// Client와 연결되는 소켓
	stOverlappedEx	m_stRecvOverlappedEx;	// RECV Overlapped I/O 작업을 위한 변수
	stOverlappedEx	m_stSendOverlappedEx;	// SEND Overlapped I/O 작업을 위한 변수

	char mRecvBuf[MAX_SOCKBUF];	// 데이터 버퍼
	char mSendBuf[MAX_SOCKBUF]; // 데이터 버퍼

	_stClientInfo()
	{
		ZeroMemory(&m_stRecvOverlappedEx, sizeof(_stOverlappedEx));
		ZeroMemory(&m_stSendOverlappedEx, sizeof(_stOverlappedEx));
		m_socketClient = INVALID_SOCKET;
	}
}stClientInfo;


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

		if (INVALID_SOCKET == mListenSocket)
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

private:
	void CreateClient(const int maxClientCount)
	{
		for (int i = 0;i < maxClientCount;++i)
		{
			mClientInfos.emplace_back();
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
		mAccepterThread = std::thread([this]() {AccepterThread();});

		printf("AccepterThread 시작\n");
		return true;
	}

	// 사용하지 않는 클라이언트의 정보 구조체를 반환
	stClientInfo* GetEmptyClientInfo()
	{
		for (auto& client : mClientInfos)
		{
			if (INVALID_SOCKET == client.m_socketClient)
			{
				return &client;
			}
		}

		return nullptr;
	}

	// CompletionPort객체와 소켓과 CompletionKey를 연결시키는 역할을 함
	bool BindIOCompletionPort(stClientInfo* pClientInfo)
	{
		// socket과 pClientInfo를 CompletionPort객체와 연결시킴
		auto hIOCP = CreateIoCompletionPort(
			(HANDLE)pClientInfo->m_socketClient,
			mIOCPHandle,
			(ULONG_PTR)(pClientInfo),
			0);

		if (NULL == hIOCP || mIOCPHandle != hIOCP)
		{
			printf("[ERROR] CreateIoCompletionPort() 실패 : %d\n", GetLastError());
			return false;
		}

		return false;
	}

	bool BindRecv(stClientInfo* pClientInfo)
	{
		DWORD dwFlag = 0;
		DWORD dwRecvNumByter = 0;

		// Overlapped I/O를 위해 각 정보를 세팅
		pClientInfo->m_stRecvOverlappedEx.m_wsaBuf.len = MAX_SOCKBUF;
		pClientInfo->m_stRecvOverlappedEx.m_wsaBuf.buf = pClientInfo->mRecvBuf;
		pClientInfo->m_stRecvOverlappedEx.m_eOperation = IOOperation::RECV;

		int nRet = WSARecv(pClientInfo->m_socketClient,
			&(pClientInfo->m_stRecvOverlappedEx.m_wsaBuf),
			1,
			&dwRecvNumByter,
			&dwFlag,
			(LPWSAOVERLAPPED) & (pClientInfo->m_stRecvOverlappedEx),
			NULL);

		// socket_error 면 client socket이 끊어진것으로 처리
		if (nRet == SOCKET_ERROR && (WSAGetLastError() != ERROR_IO_PENDING))
		{
			printf("[ERROR] WSARecv() 실패 : %d\n", WSAGetLastError());
			return false;
		}
		return true;
	}

	// WSASend Overlapped I/O 작업을 시작
	bool SendMsg(stClientInfo* pClientInfo, char* pMsg, int nLen)
	{
		DWORD dwRecvNumBytes = 0;

		// 전송될 메세지를 복사
		CopyMemory(pClientInfo->mSendBuf, pMsg, nLen);

		// Overlapped I/O를 위해 각 정보를 세팅
		pClientInfo->m_stSendOverlappedEx.m_wsaBuf.len = nLen;
		pClientInfo->m_stSendOverlappedEx.m_wsaBuf.buf = pClientInfo->mSendBuf;
		pClientInfo->m_stSendOverlappedEx.m_eOperation = IOOperation::SEND;

		int nRet = WSASend(pClientInfo->m_socketClient,
			&(pClientInfo->m_stSendOverlappedEx.m_wsaBuf),
			1,
			&dwRecvNumBytes,
			0,
			(LPWSAOVERLAPPED) & (pClientInfo->m_stSendOverlappedEx),
			NULL);

		// socket_error면 client socket이 끊어진 것으로 처리
		if (nRet == SOCKET_ERROR && (WSAGetLastError() != ERROR_IO_PENDING))
		{
			printf("[ERROR] WSASend() 실패 : %d\n", WSAGetLastError());
			return false;
		}
		return true;
	}

	// Overlapped I/O 작업에 대한 완료 통보를 받아 그에 해당하는 처리를 하는 함수
	void WorkerThread()
	{
		// CompletionKey를 받을 포인터 변수 
		stClientInfo* pClientInfo = NULL;
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
				(PULONG_PTR)&pClientInfo,	// CompletionKey
				&lpOverlapped,				// Overlapped IO 객체
				INFINITE);					// 대기할 시간

			// 사용자 쓰레드 종료 메세지 처리
			if (bSuccess == TRUE && dwIoSize == 0 && lpOverlapped)
			{
				mIsWorkerRun = false;
				continue;
			}

			if (NULL == lpOverlapped)
			{
				continue;
			}

			// client가 접속을 끊었을때
			if (bSuccess == FALSE || (dwIoSize == 0 && bSuccess == TRUE))
			{
				printf("Socket(%d) 접속 끊김\n", (int)pClientInfo->m_socketClient);
				CloseSocket(pClientInfo);
				continue;
			}

			stOverlappedEx* pOverlappedEx = (stOverlappedEx*)lpOverlapped;

			// Overlapped I/O Recv 작업 결과 뒤 처리
			if (IOOperation::RECV == pOverlappedEx->m_eOperation)
			{
				pClientInfo->mRecvBuf[dwIoSize] = NULL;
				printf("[수신] bytes : %d , msg : %s\n", dwIoSize, pClientInfo->mRecvBuf);

				// 클라이언트에 메세지를 echo
				SendMsg(pClientInfo, pClientInfo->mRecvBuf, dwIoSize);
				BindRecv(pClientInfo);
				
			}

			// Overlapped I/O Send 작업 결과 뒤 처리
			else if (IOOperation::SEND == pOverlappedEx->m_eOperation)
			{
				//printf("[송신] bytes : %d, msg : %s\n", dwIoSize, pOverlappedEx->m_szBuf);
				printf("[송신] bytes : %d, msg : %s\n", dwIoSize, pClientInfo->mSendBuf);

			}
			// 예외
			else
			{
				printf("socket(%d)에서 예외상황\n", (int)pClientInfo->m_socketClient);
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
			stClientInfo* pClientInfo = GetEmptyClientInfo();
			if (NULL == pClientInfo)
			{
				printf("[ERROR] Client FULL \n");
				return;
			}

			//클라이언트 접속 요청이 들어올 때까지 기다림
			pClientInfo->m_socketClient = accept(mListenSocket, (SOCKADDR*)&stClientAddr, &nAddrLen);
			if (INVALID_SOCKET == pClientInfo->m_socketClient)
			{
				continue;
			}

			// I/O Completion Port와 소켓을 연결시킴
			bool bRet = BindIOCompletionPort(pClientInfo);
			if (false == bRet)
			{
				return;
			}

			char clientIP[32] = { 0 };
			inet_ntop(AF_INET, &(stClientAddr.sin_addr), clientIP, 32 - 1);
			printf("Client 접속 : IP(%s) SOCKET(%d)", clientIP, (int)pClientInfo->m_socketClient);

			// 클라이언트 갯수 증가
			++mClientCnt;
		}
	}


	// 소켓의 연결을 종료
	void CloseSocket(stClientInfo* pClientInfo, bool bIsForce = false)
	{
		// SO_LINGER를 사용하면 소켓을 close 했을 때 전송되지 않은 데이터를 어떻게 처리할 것인지 조정함
		struct linger stLinger = { 0,0 };	//SO_DONTLINGER로 설정

		// bIsForce가 true면 SO_LINGER, timeout = 0으로 설정하여 강제 종료시킴
		if (bIsForce == true)
		{
			stLinger.l_onoff = 1;
		}

		// socketClose소켓의 데이터 송수신을 모두 중단
		shutdown(pClientInfo->m_socketClient, SD_BOTH);

		// 소켓 옵션을 설정
		setsockopt(pClientInfo->m_socketClient, SOL_SOCKET, SO_LINGER, (char*)&stLinger, sizeof(stLinger));

		// 소켓 연결을 종료
		closesocket(pClientInfo->m_socketClient);

		pClientInfo->m_socketClient = INVALID_SOCKET;
	}

	// 클라이언트 정보 저장 구조체
	std::vector<stClientInfo> mClientInfos;

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