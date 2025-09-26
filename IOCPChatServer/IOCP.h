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
	
	virtual ~IOCompletionPort(void)
	{
		// 윈속 사용 끝
		WSACleanup();
	}

	// 소켓을 초기화 하는 함수

	bool Init(const UINT32 Max_IO_Worker_Threads_Count)
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

		MaxIOWorkerThreadCount = Max_IO_Worker_Threads_Count;

		printf("SOCKET 초기화 성공\n");
		return true;
	}
	//bool InitSocket()
	//{
	//	WSADATA wsaData;

	//	int nRet = WSAStartup(MAKEWORD(2, 2), &wsaData);
	//	if (nRet != 0)
	//	{
	//		printf("[ERROR] WSAStartup() 실패 : %d\n", WSAGetLastError());
	//		return false;
	//	}

	//	// TCP, Overlapped I/O 소켓을 생성
	//	mListenSocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, NULL, WSA_FLAG_OVERLAPPED);

	//	if (mListenSocket == INVALID_SOCKET)
	//	{
	//		printf("[ERROR] socket() 실패 : %d\n", WSAGetLastError());
	//		return false;
	//	}

	//	printf("SOCKET 초기화 성공\n");
	//	return true;
	//}

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

		// 접속대기큐 5개 설정
		nRet = listen(mListenSocket, 5);

		if (nRet != 0)
		{
			printf("[ERROR] listen() 실패 : %d\n", WSAGetLastError());
			return false;
		}

		// 처음 IOCP QUEUE만들땐 인자 NULL, 마지막인자 0이면 OS에 맡김
		mIOCPHandle = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL, MaxIOWorkerThreadCount);

		if (mIOCPHandle == NULL)
		{
			printf("[에러] CreateIoCompletionPort()함수 실패: %d\n", GetLastError());
			return false;
		}

		auto hIOCPHandle = CreateIoCompletionPort((HANDLE)mListenSocket, mIOCPHandle, (UINT32)0, 0);

		if (hIOCPHandle == nullptr)
		{
			printf("[에러] listen socket IOCP bind 실패 : %d\n", WSAGetLastError());
			return false;
		}

		printf("서버 등록 성공 ! \n");
		return true;
	}

	// 접속 요청을 수락하고 메세지를 받아서 처리하는 함수
	bool StartServer(const int maxClientCount)
	{
		CreateClient(maxClientCount);

		// mIOCPHandle = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL, MAX_WORKERTHREAD);
		
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

		//CreateSendThread();

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

		mIsAccepterRun = false;
		closesocket(mListenSocket);
		if (mAccepterThread.joinable())
		{
			mAccepterThread.join();
		}
	}
	// 클라이언트의 정보를 받아서
	// 클라이언트에게 메세지를 send하는 함수
	bool SendMsg(const UINT32 ClientSessionIndex_, const UINT32 dataSize_, char* pMsg_)
	{
		auto pClient = GetClientInfo(ClientSessionIndex_);
		return pClient->SendMsg(dataSize_, pMsg_);
	}

	virtual void OnConnect(const int clientIndex){}
	virtual void OnClose(const int clientIndex){}
	virtual void OnReceive(const UINT32 clientIndex, const UINT32 size, char* pData){}

private:
	void CreateClient(const int maxClientCount)
	{
		for (int i = 0;i < maxClientCount;++i)
		{
			auto client = std::make_unique<ClientSession>();
			client->Init(i, mIOCPHandle);
			mClientInfos.emplace_back(std::move(client));

			//auto client = new ClientSession(); // 메모리 누수 가능성
			//client->Init(i,mIOCPHandle);
			//mClientInfos.emplace_back(client);

		}
	}

	// WaitingThread Queue에서 대기할 쓰레드들 생성 
	bool CreateWorkerThread()
	{
		mIsWorkerRun = true;
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
		mIsAccepterRun = true;
		mAccepterThread = std::thread([this]() { AccepterThread(); });

		printf("AccepterThread 시작\n");
		return true;
	}

	//void CreateSendThread()
	//{
	//	mIsSenderRun = true;
	//	mSendThread = std::thread([this]() { SendThread(); });
	//	printf("SendThread Start !\n");
	//}

	// 사용하지 않는 클라이언트의 정보 구조체를 반환
	ClientSession* GetEmptyClientInfo()
	{
		for (auto& client : mClientInfos)
		{
			if (client->IsConnected() == false)
			{
				return client.get();
			}
		}
		return nullptr;
	}

	// 클라이언트의 index를 넣으면 client의 info를 리턴하는 함수
	ClientSession* GetClientInfo(const UINT32 clientSessionIndex)
	{
		return mClientInfos[clientSessionIndex].get();
	}

	// Overlapped I/O 작업에 대한 완료 통보를 받아 그에 해당하는 처리를 하는 함수
	void WorkerThread()
	{
		// 완료 항목 저장 배열
		OVERLAPPED_ENTRY completionEntries[MAX_COMPLETION_ENTRIES];
		ULONG numEntriesRemoved = 0;

		//// CompletionKey를 받을 포인터 변수 
		//ClientSession* pClientSession = nullptr;
		//// 함수 호출 성공 여부
		BOOL bSuccess = TRUE;
		//// Overlapped I/O 작업에서 전송된 데이터 크기
		//DWORD dwIoSize = 0;
		//// I/O 작업을 위해 요청한 Overlapped 구조체를 받을 포인터
		//LPOVERLAPPED lpOverlapped = NULL;

		while (mIsWorkerRun)
		{
			////////////////////////////////////
			// 이 함수로 인해 쓰레드들은 WaitingThread Queue에 대기 상태로 들어감
			// 완료된 Overlapped I/O 작업이 발생하면 IOCP Queue에서 완료된 작업을 가져와 처리
			// 그리고 PostQeueuCompletionStatus()에 의해 사용자 메세지가 도착되면 쓰레드 종료
			////////////////////////////////////
			//printf("[DEBUG] GQCS 결과: bSuccess=%d, dwIoSize=%d, lpOverlapped=%p\n", bSuccess, dwIoSize, lpOverlapped);
			
			//bSuccess = GetQueuedCompletionStatus(
			//	mIOCPHandle,				// dequeue할 IOCP 핸들
			//	&dwIoSize,					// 실제 전송된 바이트
			//	(PULONG_PTR)&pClientSession,	// CompletionKey
			//	&lpOverlapped,				// Overlapped IO 객체
			//	INFINITE);					// 대기할 시간

			bSuccess = GetQueuedCompletionStatusEx(
				mIOCPHandle,				// dequeue할 IOCP 핸들
				completionEntries,			// 완료 항목 배열
				MAX_COMPLETION_ENTRIES,		// 완료 항목 배열 크기
				&numEntriesRemoved,			// 제거된 항목 수
				INFINITE,					// 대기할 시간
				FALSE);						// 동기식으로 처리

			if (!bSuccess)
			{
				DWORD error = GetLastError();

				if (error == TIMEOUT_WAIT)
				{
					continue;
				}
				else if (error == ERROR_ABANDONED_WAIT_0)
				{
					// IOCP핸들이 닫힘 - 서버 종료
					mIsWorkerRun = false;
					break;
				}
				else
				{
					printf("[ERROR] GetQueuedCompletionStatusEx() 실패 : %d\n", error);
					continue;
				}
			}

			for (ULONG i = 0; i < numEntriesRemoved; ++i)
			{
				auto& entry = completionEntries[i];
				// 여기서 이미 삭제된 메모리를 가리키게 된다면..?
				// pClientSession이 이미 삭제된 메모리를 가리킴 -> crash 발생 (Dangling pointer)
				
				ClientSession* pClientSession = reinterpret_cast<ClientSession*>(entry.lpCompletionKey);
				if (!pClientSession || !pClientSession->IsConnected())
				{
					continue;
				}
				
				DWORD dwIoSize = entry.dwNumberOfBytesTransferred;
				LPOVERLAPPED lpOverlapped = entry.lpOverlapped;

				// 사용자 쓰레드 종료 메세지 처리
				if (dwIoSize == 0 && lpOverlapped == NULL)
				{
					mIsWorkerRun = false;
					continue;
				}

				if (lpOverlapped == NULL)
				{
					continue;
				}

				auto pOverlappedEx = (stOverlappedEx*)lpOverlapped;

				// client가 접속을 끊었을때
				if (dwIoSize == 0 && pOverlappedEx->m_eOperation != IOOperation::ACCEPT)
				{
					CloseSocket(pClientSession);
					continue;
				}

				if (IOOperation::ACCEPT == pOverlappedEx->m_eOperation)
				{
					pClientSession = GetClientInfo(pOverlappedEx->clientSessionIndex);
					
					if (pClientSession->AcceptCompletion(mListenSocket))
					{
						++mClientCnt;
						OnConnect(pClientSession->GetIndex());
						printf("######################################### 접속됨 #################################\n");
					}
					else
					{
						CloseSocket(pClientSession, true);
					}
				}

				// Overlapped I/O Recv 작업 결과 뒤 처리
				else if (IOOperation::RECV == pOverlappedEx->m_eOperation)
				{
					OnReceive(pClientSession->GetIndex(), dwIoSize, pClientSession->RecvBuff());
					pClientSession->BindRecv(); // 다시 recv 걸어줌
				}

				else if (IOOperation::SEND == pOverlappedEx->m_eOperation) // 연결이 완료되면
				{
					pClientSession->SendComplete(dwIoSize);
				}
				// 예외
				else
				{
					printf("Client Index : (%d)에서 예외\n", pClientSession->GetIndex());
				}

			}	
		}
	}

	//사용자의 접속을 받는 쓰레드
	void AccepterThread()
	{
		while (mIsAccepterRun)
		{
			for (auto& client : mClientInfos)
			{
				if (client->IsConnected())
					continue;
				// 대기 시간 없이 바로 AccpetEx
				client->PostImmediateAccept(mListenSocket);

				// 최소대기시간
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
		}

	}


	// 소켓의 연결을 종료
	void CloseSocket(ClientSession* pClientSession, bool bIsForce = false)
	{
		if (pClientSession->IsConnected() == false)
		{
			return;
		}
		UINT32 ClientIndex = pClientSession->GetIndex();
		pClientSession->Closed(bIsForce);
		OnClose(ClientIndex);
	}

	// 클라이언트 정보 저장 구조체
	//std::vector<ClientSession*> mClientInfos;

	// 클라이언트의 접속을 받기 위한 리슨 소켓
	SOCKET mListenSocket = INVALID_SOCKET;

	// 접속 되어있는 클라이언트 수
	int mClientCnt = 0;

	// IO worker 쓰레드
	std::vector<std::thread> mIOWorkerThreads;

	// Accept 쓰레드
	std::thread mAccepterThread;

	// Send 쓰레드
	std::thread mSendThread;
	
	// CompletionPort객체 핸들 
	HANDLE	mIOCPHandle = INVALID_HANDLE_VALUE;

	// 작업 쓰레드 동작 플래그
	bool	mIsWorkerRun = true;

	// 접속 쓰레드 동작 플래그
	bool	mIsAccepterRun = true;

	//bool	mIsSenderRun = false;

	UINT32 MaxIOWorkerThreadCount = 0;

	// GetQueuedCompletionStatusEx 관련 상수
	static const ULONG MAX_COMPLETION_ENTRIES = 64;  // 한 번에 처리할 최대 완료 항목 수
	static const DWORD TIMEOUT_WAIT = 100;           // 대기 타임아웃 (ms)

	// 클라이언트 정보 저장 구조체
	//std::vector<ClientSession*> mClientInfos;
	std::vector<std::unique_ptr<ClientSession>> mClientInfos;
};