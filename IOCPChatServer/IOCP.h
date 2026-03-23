#pragma once
#pragma comment(lib, "ws2_32")
#pragma comment(lib, "mswsock.lib")

#include "Define.h"
#include "ClientSession.h"
#include "Packet.h"
#include <thread>
#include <vector>
#include <mswsock.h>


class IOCompletionPort
{
public:
	IOCompletionPort(void) {}
	
	virtual ~IOCompletionPort(void)
	{
		// 윈속 해제 등
		WSACleanup();
	}

	// 소켓을 초기화 하는 함수

	bool Init(const UINT32 Max_IO_Worker_Threads_Count)
	{
		WSADATA wsaData;

		int nRet = WSAStartup(MAKEWORD(2, 2), &wsaData);
		if (nRet != 0)
		{
			LOG_ERROR("WSAStartup() 실패 : %d\n", WSAGetLastError());
			return false;
		}

		// TCP, Overlapped I/O 소켓을 생성
		mListenSocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, NULL, WSA_FLAG_OVERLAPPED);

		if (mListenSocket == INVALID_SOCKET)
		{
			LOG_ERROR("socket() 실패 : %d\n", WSAGetLastError());
			return false;
		}

		MaxIOWorkerThreadCount = Max_IO_Worker_Threads_Count;

		LOG_DEBUG("SOCKET 초기화 성공\n");
		return true;
	}
	//bool InitSocket()
	//{
	//	WSADATA wsaData;

	//	int nRet = WSAStartup(MAKEWORD(2, 2), &wsaData);
	//	if (nRet != 0)
	//	{
	//		printf("[ERROR] WSAStartup() 실패 : %%d", WSAGetLastError());
	//		return false;
	//	}

	//	// TCP, Overlapped I/O 소켓을 생성
	//	mListenSocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, NULL, WSA_FLAG_OVERLAPPED);

	//	if (mListenSocket == INVALID_SOCKET)
	//	{
	//		printf("[ERROR] socket() 실패 : %%d", WSAGetLastError());
	//		return false;
	//	}

	//	printf("SOCKET 초기화 성공");
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
		// 어떤 주소에서 들어오는 접속이라도 받겠다 하는데 가장 기본적으로 이렇게 설정
		stServerAddr.sin_addr.s_addr = htonl(INADDR_ANY);

		// 지정한 소켓의 주소 정보를 cIOCompletionPort 소켓에 설정
		int nRet = bind(mListenSocket, (SOCKADDR*)&stServerAddr, sizeof(SOCKADDR_IN));

		if (nRet != 0)
		{
			LOG_ERROR("bind() 실패 : %d\n", WSAGetLastError());
			return false;
		}

		// 접속대기큐 5개 설정
		nRet = listen(mListenSocket, 5);

		if (nRet != 0)
		{
			LOG_ERROR("listen() 실패 : %d\n", WSAGetLastError());
			return false;
		}

		// 처음 IOCP QUEUE를 만들때에는 핸들 NULL, 동시실행수 0이면 OS에 맡김
		mIOCPHandle = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL, MaxIOWorkerThreadCount);

		if (mIOCPHandle == NULL)
		{
			LOG_ERROR("CreateIoCompletionPort() 실패: %d\n", GetLastError());
			return false;
		}

		auto hIOCPHandle = CreateIoCompletionPort((HANDLE)mListenSocket, mIOCPHandle, (UINT32)0, 0);

		if (hIOCPHandle == nullptr)
		{
			LOG_ERROR("listen socket IOCP bind 실패 : %d\n", WSAGetLastError());
			return false;
		}

		LOG_DEBUG("서버 등록 성공 ! \n");
		return true;
	}

	// 접속 요청을 받아들이고 메세지를 받아서 처리하는 함수
	bool StartServer(const int maxClientCount)
	{
		CreateClient(maxClientCount);

		// mIOCPHandle = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL, MAX_WORKERTHREAD);
		
		bool bRet = CreateWorkerThread();
		if (bRet == false)
		{
			return false;
		}
		
		// 초기 AcceptEx 100개 걸어놓기
		for (UINT32 i = 0; i < MAX_PENDING_ACCEPT;++i)
		{
			// pop해서 인덱스 얻어옴
			UINT32 emptyIndex = PopFreeSessionIndex();
			if (emptyIndex != UINT32_MAX)
			{
				// 해당세션에 postImmediate 호출
				auto pClient = GetClientInfo(emptyIndex);
				pClient->PostImmediateAccept(mListenSocket);
			}
		}
		
		mIsTimeoutRun = true;
		mTimeoutThread = std::thread([this]() { TimeoutCheckThread(); });
		//bRet = CreateAccepterThread();
		//if (bRet == false)
		//{
		//	return false;
		//}

		//CreateSendThread();

		LOG_DEBUG("서버 시작 \n");
		return true;
	}

	// 생성되어 있는 스레드를 파괴한다
	void DestroyThread()
	{

		mIsTimeoutRun = false;
		if (mTimeoutThread.joinable())
			mTimeoutThread.join();
		LOG_DEBUG("TimeoutThread 종료 완료\n");

		//Todo: GracefulShutDown 구현하기
		
		// step1. Accept 종료
		//mIsAccepterRun = false;
		closesocket(mListenSocket);		// AcceptEx 대기 해제
		mListenSocket = INVALID_SOCKET;
		//if (mAccepterThread.joinable())
		//	mAccepterThread.join();
		LOG_DEBUG("step1 Accept 종료 완료\n");
		
		// step2. 연결중인 클라이언트 + IO 취소
		for (auto& client : mClientInfos)
		{
			if (client->IsConnected())
			{
				// 연결된 클라이언트가 있다면
				// 해당 소켓 취소 비동기 IO 취소
				CancelIoEx((HANDLE)client->GetSocket(), NULL);
				client->Closed(true);	// 강제 연결 종료
			}
		}
		LOG_DEBUG("step2 모든 클라이언트 강제 종료 완료\n");


		// step3. 잔여 IO Draining
		// 연결 해제시 OS 가 잔여 완료 신호 IOCP queue에 넣음
		// 짧은 대기 후 처리 시간줌
		std::this_thread::sleep_for(std::chrono::milliseconds(500));
		LOG_DEBUG("step3: 잔여 IO Draining 완료\n");

		// step4. 워커 스레드 종료 - PQCS로 신호
		mIsWorkerRun = false;
		for (size_t i = 0;i < mIOWorkerThreads.size();++i)
		{
			PostQueuedCompletionStatus(mIOCPHandle, 0, 0, NULL);
		}
		for (auto& th : mIOWorkerThreads)
		{
			if (th.joinable())
				th.join();
		}
		LOG_DEBUG("step4: worker thread 종료 완료\n");
		// step5. IOCP Handle 종료
		CloseHandle(mIOCPHandle);
		mIOCPHandle = INVALID_HANDLE_VALUE;
		LOG_DEBUG("step5: 자원 정리 완료\n");


		// 기존 종료 로직
		//mIsWorkerRun = false;

		//CloseHandle(mIOCPHandle);

		//for (auto& th : mIOWorkerThreads)
		//{
		//	if (th.joinable())
		//	{
		//		th.join();
		//	}
		//}

		//mIsAccepterRun = false;
		//closesocket(mListenSocket);
		//if (mAccepterThread.joinable())
		//{
		//	mAccepterThread.join();
		//}
	}
	// 클라이언트의 접속을 받아서
	// 클라이언트에게 메세지를 send하는 함수
	bool SendMsg(const UINT32 ClientSessionIndex_, const UINT32 dataSize_, char* pMsg_)
	{
		auto pClient = GetClientInfo(ClientSessionIndex_);
		return pClient->SendMsg(dataSize_, pMsg_);
	}

	virtual void OnConnect(const int clientIndex){}
	virtual void OnClose(const int clientIndex){}
	virtual void OnReceive(const UINT32 clientIndex, const UINT32 size, char* pData){}

	// 모니터링
	uint64_t GetSendPoolAllocFailCount() const { return mSendBufferPool.GetAllocFailCount(); }

private:
	void CreateClient(const int maxClientCount)
	{
		mSendBufferPool.Init(maxClientCount * 500);

		for (int i = 0;i < maxClientCount;++i)
		{
			auto client = std::make_unique<ClientSession>();
			client->Init(i, mIOCPHandle, &mSendBufferPool);
			mClientInfos.emplace_back(std::move(client));
		}

		// 최적화..재할당 방지를 위한 메모리 공간을 미리 확보 (Capacity = maxCount)
		mFreeSessionList.reserve(maxClientCount);

		// 역순으로 스택에 채워넣기)
		for (UINT32 i = maxClientCount; i > 0; --i)
		{
			mFreeSessionList.push_back(i-1);
		}
		//for (int i = 0;i < maxClientCount;++i)
		//{
		//	auto client = std::make_unique<ClientSession>();
		//	client->Init(i, mIOCPHandle);
		//	mClientInfos.emplace_back(std::move(client));

		//	//auto client = new ClientSession(); // 메모리 누수 가능성
		//	//client->Init(i,mIOCPHandle);
		//	//mClientInfos.emplace_back(client);

		//}
	}

	// WaitingThread Queue에서 사용할 스레드들을 생성
	bool CreateWorkerThread()
	{
		mIsWorkerRun = true;
		// WaitingThread Queue에 대기 상태로 넣을 스레드, 권장수는 (cpu코어 * 2) + 1
		for (int i = 0;i < MAX_WORKERTHREAD;i++)
		{
			mIOWorkerThreads.emplace_back([this]() {WorkerThread();});
		}

		LOG_DEBUG("WorkerThread 시작 \n");
		return true;
	}

	void TimeoutCheckThread()
	{
		const ULONGLONG TIMEOUT_MS = 60000; // 60초 동안 응답 없는 경우 종료
		const ULONGLONG PING_INTERVAL_MS = 30000; // 30초 이상 비활동시 PING
		const ULONGLONG CHECK_INTERVAL_MS = 10000; // 10초 순회주기
		while (mIsTimeoutRun)
		{
			ULONGLONG now = GetTickCount64();

			for (size_t i = 0;i<mClientInfos.size();i++)
			{
				ClientSession* pSession = GetClientInfo(i);

				// 연결이 안됐으면 건너뜀
				if (pSession == nullptr || !pSession->IsConnected())
					continue;

				ULONGLONG lastActivity = pSession->GetLastActivityTime();
				ULONGLONG elapsed = now - lastActivity;
				
				if (elapsed >= TIMEOUT_MS) // 60초
				{
					// 60초동안 무응답 시 Disconnect 호출하여 끊김
					LOG_DEBUG("[TimeoutThread] Client Index(%d) 타임아웃! (60초 무응답) -> 강제 종료 예정\n", pSession->GetIndex());
					// 강제 종료
					//CloseSocket(pSession, true);
					pSession->DisconnectAsync();
				}
				else if (elapsed >= PING_INTERVAL_MS) // 30초
				{
					ULONGLONG lastping = pSession->GetLastPingTime();
					if (now - lastping >= PING_INTERVAL_MS)
					{
						pSession->SetLastPingTime(now);

						// 비활동 30초 이상인 경우 접속확인용 PING 전송
						LOG_DEBUG("[TimeoutThread] Client Index(%d)에게 PING 발송 (비활동 %llu ms)\n", pSession->GetIndex(), elapsed);
						// 핑 전송
						PACKET_HEADER pingHeader;
						pingHeader.PacketLength = sizeof(PACKET_HEADER); // 패킷 전체 크기
						pingHeader.PacketId = (UINT16)PACKET_ID::SYS_PING;
						pingHeader.PacketType = 0;
						pSession->SendMsg(pingHeader.PacketLength, (char*)&pingHeader);
					}
				}

				//최근 활동이 있으면 아무것도 하지 않고 넘어감
			}
			Sleep(CHECK_INTERVAL_MS); // 10초
		}
	}

	//// accept 요청을 처리하는 스레드 생성
	//bool CreateAccepterThread()
	//{
	//	mIsAccepterRun = true;
	//	mAccepterThread = std::thread([this]() { AccepterThread(); });

	//	printf("AccepterThread 시작");
	//	return true;
	//}

	//void CreateSendThread()
	//{
	//	mIsSenderRun = true;
	//	mSendThread = std::thread([this]() { SendThread(); });
	//	printf("SendThread Start !\n");
	//}

	// FreeList로 대체됨
	//// 사용되지 않는 클라이언트의 접속 구조체를 반환
	//ClientSession* GetEmptyClientInfo()
	//{
	//	for (auto& client : mClientInfos)
	//	{
	//		if (client->IsConnected() == false)
	//		{
	//			return client.get();
	//		}
	//	}
	//	return nullptr;
	//}

	// 클라이언트의 index로 해당 client의 info를 반환하는 함수
	ClientSession* GetClientInfo(const UINT32 clientSessionIndex)
	{
		return mClientInfos[clientSessionIndex].get();
	}

	// Overlapped I/O 작업에 대한 완료 통보를 받아 그에 해당하는 처리를 하는 함수
	void WorkerThread()
	{
		// 완료 항목 수신 배열
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
			// 이 함수로 인해 스레드들은 WaitingThread Queue에 대기 상태로 됨
			// 완료된 Overlapped I/O 작업이 발생하면 IOCP Queue에서 완료된 작업을 가져와 처리
			// 그리고 PostQueueCompletionStatus()에 의해 사용자 메세지가 게시되면 게시물 처리
			////////////////////////////////////
			//printf("[DEBUG] GQCS 결과: bSuccess=%%d, dwIoSize=%%d, lpOverlapped=%%p", bSuccess, dwIoSize, lpOverlapped);
			
			//bSuccess = GetQueuedCompletionStatus(
			//	mIOCPHandle,				// dequeue할 IOCP 핸들
			//	&dwIoSize,					// 실제 전송된 바이트
			//	(PULONG_PTR)&pClientSession,	// CompletionKey
			//	&lpOverlapped,				// Overlapped IO 객체
			//	INFINITE);					// 무한대기 시간

			bSuccess = GetQueuedCompletionStatusEx(
				mIOCPHandle,				// dequeue할 IOCP 핸들
				completionEntries,			// 완료 항목 배열
				MAX_COMPLETION_ENTRIES,		// 완료 항목 배열 크기
				&numEntriesRemoved,			// 제거된 항목 수
				INFINITE,					// 무한대기 시간
				FALSE);						// 알림가능대기 처리
			//printf("[DEBUG] GetQueuedCompletionStatusEx 결과: bSuccess=%%d, numEntries=%%d, LastError=%%d",bSuccess, numEntriesRemoved, GetLastError());
			if (!bSuccess)
			{
				DWORD error = GetLastError();

				if (error == TIMEOUT_WAIT)
				{
					continue;
				}
				else if (error == ERROR_ABANDONED_WAIT_0)
				{
					// IOCP핸들이 닫힘 - 종료 신호
					mIsWorkerRun = false;
					break;
				}
				else
				{
					LOG_ERROR("GetQueuedCompletionStatusEx() 실패 : %d\n", error);
					continue;
				}
			}
			//printf("[DEBUG] %%d개 완료 이벤트 처리 시작", numEntriesRemoved);
			for (ULONG i = 0; i < numEntriesRemoved; ++i)
			{
				auto& entry = completionEntries[i];
				// 여기서 이미 해제된 메모리를 가리키게 된다면..?
				// pClientSession이 이미 해제된 메모리를 가리킴 -> crash 발생 (Dangling pointer)
				
				ClientSession* pClientSession = reinterpret_cast<ClientSession*>(entry.lpCompletionKey);
				
				DWORD dwIoSize = entry.dwNumberOfBytesTransferred;
				LPOVERLAPPED lpOverlapped = entry.lpOverlapped;

				// 사용자 종료 위한 메세지 처리
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
				// client가 접속을 끊었으면
				if (dwIoSize == 0 && pOverlappedEx->m_eOperation != IOOperation::ACCEPT)
				{
					CloseSocket(pClientSession);
					continue;
				}
				//printf("[DEBUG] Operation=%d, ClientSessionIndex=%d\n",
				//	(int)pOverlappedEx->m_eOperation, pOverlappedEx->clientSessionIndex);
				if (IOOperation::ACCEPT == pOverlappedEx->m_eOperation)
				{
					pClientSession = GetClientInfo(pOverlappedEx->clientSessionIndex);
					
					if (pClientSession->AcceptCompletion(mListenSocket))
					{
						// 여러 스레드가 동시에 호출해도 안전 원자적 atomic처리
						++mClientCnt;
						OnConnect(pClientSession->GetIndex());
						LOG_DEBUG("######################################### 접속됨 #################################\n");
					}
					else
					{
						CloseSocket(pClientSession, true);
					}

					// 새로운 AcceptEx 1개 등록하기
					UINT32 nextEmptyIndex = PopFreeSessionIndex();
					if (nextEmptyIndex != UINT32_MAX)
					{
						auto pNextClient = GetClientInfo(nextEmptyIndex);
						pNextClient->PostImmediateAccept(mListenSocket);
					}
				}

				// Overlapped I/O Recv 작업 결과 후 처리
				else if (IOOperation::RECV == pOverlappedEx->m_eOperation)
				{

					// Stale I/O 체크
					if (pOverlappedEx->generation != pClientSession->GetGeneration())
					{
						LOG_DEBUG("[Stale I/O] RECV 무시 - gen: %d vs %d\n",
							pOverlappedEx->generation, pClientSession->GetGeneration());
						continue;
					}

					// 연결세션 확인 로직 추가
					pClientSession->UpdateActivity();

					OnReceive(pClientSession->GetIndex(), dwIoSize, pClientSession->RecvBuff());
					pClientSession->BindRecv(); // 다시 recv 걸어줌
				}

				else if (IOOperation::SEND == pOverlappedEx->m_eOperation) // 송신이 완료되면
				{
					// Stale I/O 체크
					auto pSendOvl = (SendOverlappedEx*)lpOverlapped;
					if (pSendOvl->generation != pClientSession->GetGeneration())
					{
						LOG_DEBUG("[Stale I/O] SEND 무시 - gen: %d vs %d\n",
							pSendOvl->generation, pClientSession->GetGeneration());
						mSendBufferPool.Free(pSendOvl);  // 풀에다 반납
						continue;
					}
					pClientSession->SendComplete(dwIoSize);
				}
				// 예외
				else
				{
					LOG_DEBUG("Client Index : (%d)에서 예외\n", pClientSession->GetIndex());
				}

			}	
		}
	}

	//접속되지 않은 클라이언트를 찾는 스레드
	//void AccepterThread()
	//{
	//	while (mIsAccepterRun)
	//	{
	//		for (auto& client : mClientInfos)
	//		{
	//			if (client->IsConnected())
	//				continue;
		//		// 대기 시간 없이 바로 AcceptEx
	//			client->PostImmediateAccept(mListenSocket);

		//		// 최소대기시간
	//			std::this_thread::sleep_for(std::chrono::milliseconds(1));
	//		}
	//	}

	//}


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

		// 이제 사용이 끝났으므로 번호표 반납
		PushFreeSessionIndex(ClientIndex);
		--mClientCnt;
	}

	// 빈 세션 하나 꺼내는 함수
	UINT32 PopFreeSessionIndex()
	{
		// 잠금
		std::lock_guard<std::mutex> lock(mFreeListLock);
		
		// 큐가 비어있는지 확인 (없으면 전부 다 사용중)
		if (mFreeSessionList.empty())
		{
			return UINT32_MAX;
		}
		auto index = mFreeSessionList.back();
		mFreeSessionList.pop_back();
		return index;
	}
	// 세션 반납 함수
	void PushFreeSessionIndex(const UINT32 index)
	{
		// 잠금
		std::lock_guard<std::mutex>lock(mFreeListLock);

		// 빈 칸 반납
		mFreeSessionList.push_back(index);
	}

	// 클라이언트 접속 관리 구조체
	//std::vector<ClientSession*> mClientInfos;

	// 클라이언트의 접속을 받기 위한 리슨 소켓
	SOCKET mListenSocket = INVALID_SOCKET;

	// 접속 되어있는 클라이언트 수
	std::atomic<int>mClientCnt = 0;

	// IO worker 스레드
	std::vector<std::thread> mIOWorkerThreads;

	//// Accept 스레드
	//std::thread mAccepterThread;

	// Send 스레드
	std::thread mSendThread;
	
	// CompletionPort 객체 핸들
	HANDLE	mIOCPHandle = INVALID_HANDLE_VALUE;

	// 작업 스레드 동작 플래그
	bool	mIsWorkerRun = true;

	//// 접속 스레드 동작 플래그
	//bool	mIsAccepterRun = true;

	//bool	mIsSenderRun = false;

	UINT32 MaxIOWorkerThreadCount = 0;

	// GetQueuedCompletionStatusEx 관련 상수
	static const ULONG MAX_COMPLETION_ENTRIES = 64;  // 한 번에 처리할 최대 완료 항목 수
	static const DWORD TIMEOUT_WAIT = 100;           // 대기 타임아웃 (ms)

	// 클라이언트 접속 관리 구조체
	//std::vector<ClientSession*> mClientInfos;
	std::vector<std::unique_ptr<ClientSession>> mClientInfos;

	ObjectPool<SendOverlappedEx> mSendBufferPool;

	// FreeList
	std::vector<UINT32>mFreeSessionList;
	
	// FreeList 보호 뮤텍스
	std::mutex mFreeListLock;

	// 초기 AcceptEx 대기 100개
	static constexpr UINT32 MAX_PENDING_ACCEPT = 100;

	std::thread mTimeoutThread;
	std::atomic<bool> mIsTimeoutRun{ false };
};