#include "IOCP.h"
#include "ConfigManager.h"

bool IOCompletionPort::Init(const UINT32 Max_IO_Worker_Threads_Count)
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

bool IOCompletionPort::BindandListen(int nBindPort)
{
	SOCKADDR_IN stServerAddr;
	stServerAddr.sin_family = AF_INET;
	stServerAddr.sin_port = htons(nBindPort);	// 서버 포트를 설정
	// 어떤 주소에서 오는 접속이라도 받겠다 하는데 실제 서버에서는 이렇게 설정
	stServerAddr.sin_addr.s_addr = htonl(INADDR_ANY);

	// 지정한 소켓의 로컬 주소 정보를 cIOCompletionPort 소켓에 연결
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

	// 처음 IOCP QUEUE만들때는 핸들 NULL, 동시실행수 0이면 OS에 맡김
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

bool IOCompletionPort::StartServer(const int maxClientCount)
{
	CreateClient(maxClientCount);

	// mIOCPHandle = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL, MAX_WORKERTHREAD);

	bool bRet = CreateWorkerThread();
	if (bRet == false)
	{
		return false;
	}

	const auto& config = ConfigManager::GetInstance().Get();

	// 초기 AcceptEx 100개 걸어놓기
	for (UINT32 i = 0; i < config.MaxPendingAccept;++i)
	{
		// pop으로 빈 인덱스 가져옴
		UINT32 emptyIndex = PopFreeSessionIndex();
		if (emptyIndex != UINT32_MAX)
		{
			// 해당 세션에 postImmediate 호출
			auto pClient = GetClientInfo(emptyIndex);
			if (pClient->PostImmediateAccept(mListenSocket))
			{
				++mPendingAcceptCount;
			}
			else
			{
				PushFreeSessionIndex(emptyIndex);
			}
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
void IOCompletionPort::DestroyThread()
{

	mIsTimeoutRun = false;
	if (mTimeoutThread.joinable())
		mTimeoutThread.join();
	LOG_DEBUG("TimeoutThread 종료 완료\n");

	//Todo: GracefulShutDown 구현하기

	// step1. Accept 중지
	//mIsAccepterRun = false;
	closesocket(mListenSocket);		// AcceptEx 대기 해제
	mListenSocket = INVALID_SOCKET;
	//if (mAccepterThread.joinable())
	//	mAccepterThread.join();
	LOG_DEBUG("step1 Accept 종료 완료\n");

	// step2. 클라이언트 강제종료 + IO 취소
	for (auto& client : mClientInfos)
	{
		// 1. 소켓이 아직 살아있다면, 걸려있는 비동기 I/O들을 강제로 취소
		// 이후 워커 스레드에서 dwIoSize == 0 또는 ERROR_OPERATION_ABORTED 로 뱉어냄
		if (client->GetSocket() != INVALID_SOCKET)
		{
			CancelIoEx((HANDLE)client->GetSocket(), NULL);
		}

		// 2. Base Ref 해제 및 뒷정리
		CloseSocket(client.get(), true);
	}
	LOG_DEBUG("step2 모든 클라이언트 강제 종료 완료\n");


	// step3. 잔여 IO Draining
	// 이미 커널에서 OS 한 잔여 완료 신호가 IOCP queue에 도착
	// 짧은 대기 후 처리 시간줌
	//std::this_thread::sleep_for(std::chrono::milliseconds(500));
	LOG_DEBUG("step3: 잔여 IO Draining 완료\n");

	const int MAX_DRAIN_WAIT_MS = 5000;
	bool allDrained = false;
	int elapsed = 0;
	while (elapsed < MAX_DRAIN_WAIT_MS)
	{
		allDrained = true;
		for (auto& client : mClientInfos)
		{
			if (client->GetRefCount() > 0)
			{
				allDrained = false;
				break;
			}
		}
		if (allDrained)
			break;
		Sleep(10);
		elapsed += 10;
	}
	if (!allDrained)
		LOG_ERROR("step3: IO Draining 타임아웃\n");

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
	// step5. IOCP Handle 닫기
	CloseHandle(mIOCPHandle);
	mIOCPHandle = INVALID_HANDLE_VALUE;
	LOG_DEBUG("step5: 자원 정리 완료\n");


	// 기존 종료 방식
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

// 클라이언트의 데이터를 받아서
// 클라이언트에게 메시지를 send하는 함수
bool IOCompletionPort::SendMsg(const UINT32 ClientSessionIndex_, UINT32 generation_, const UINT32 dataSize_, char* pMsg_)
{
	auto pClient = GetClientInfo(ClientSessionIndex_);
	if (pClient->GetGeneration() != generation_)
		return false;	// 세대 불일치-> 다른사람
	return pClient->SendMsg(dataSize_, pMsg_);
}

void IOCompletionPort::DisconnectClient(const UINT32 clientIndex)
{
	auto pClient = GetClientInfo(clientIndex);
	if (pClient && pClient->IsConnected())
	{
		LOG_ERROR("[DisconnectClient] Client(%d) 강제 연결 해제\n", clientIndex);
		UINT32 gen = pClient->GetGeneration();	// gen 캡쳐
		pClient->DisconnectAsync(gen);  // shutdown(SD_BOTH) → WorkerThread가 자연스럽게 CloseSocket 호출
	}
}

void IOCompletionPort::UpdateClientActivity(const UINT32 clientIndex)
{
	auto pClient = GetClientInfo(clientIndex);
	if (pClient && pClient->IsConnected())
	{
		pClient->UpdateActivity();
	}
}

void IOCompletionPort::TryPostAcceptEx()
{
	if (mListenSocket == INVALID_SOCKET)
		return;

	const auto& config = ConfigManager::GetInstance().Get();
	if (mPendingAcceptCount.load(std::memory_order_relaxed) >= (int)config.MaxPendingAccept)
		return;

	UINT32 emptyIndex = PopFreeSessionIndex();
	if (emptyIndex == UINT32_MAX)
		return;

	auto pClient = GetClientInfo(emptyIndex);
	if (pClient->PostImmediateAccept(mListenSocket))
	{
		++mPendingAcceptCount;
	}
	else
	{
		PushFreeSessionIndex(emptyIndex);
	}
}

void IOCompletionPort::CreateClient(const int maxClientCount)
{
	mSendBufferPool.Init(maxClientCount * 50); // maxclient 2000일때 2000, 10000이면 400

	for (int i = 0;i < maxClientCount;++i)
	{
		auto client = std::make_unique<ClientSession>();
		client->Init(i, mIOCPHandle, &mSendBufferPool);
		mClientInfos.emplace_back(std::move(client));
	}

	// 최적화..재할당 방지를 위해 메모리 공간을 미리 확보 (Capacity = maxCount)
	mFreeSessionList.reserve(maxClientCount);

	// 역순으로 스택에 채워넣기)
	for (UINT32 i = maxClientCount; i > 0; --i)
	{
		mFreeSessionList.push_back(i - 1);
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

// WaitingThread Queue에서 대기할 스레드들을 생성
bool IOCompletionPort::CreateWorkerThread()
{
	const auto& config = ConfigManager::GetInstance().Get();

	mIsWorkerRun = true;
	// WaitingThread Queue에 대기 상태로 만들 스레드들, 권장갯수는 (cpu코어 * 2) + 1
	for (int i = 0;i < config.MaxWorkerThread;i++)
	{
		mIOWorkerThreads.emplace_back([this]() {WorkerThread();});
	}

	LOG_DEBUG("WorkerThread 시작 \n");
	return true;
}

void IOCompletionPort::TimeoutCheckThread()
{
	const auto& config = ConfigManager::GetInstance().Get();
	const ULONGLONG TIMEOUT_MS = config.TimeoutMs; // 60초 동안 무응답 시 연결 종료
	const ULONGLONG PING_INTERVAL_MS = config.PingIntervalMs; // 30초 동안 무응답이면 PING
	const ULONGLONG CHECK_INTERVAL_MS = config.CheckIntervalMs; // 10초 순회주기
	while (mIsTimeoutRun)
	{
		ULONGLONG now = GetTickCount64();

		for (size_t i = 0;i < mClientInfos.size();i++)
		{
			ClientSession* pSession = GetClientInfo(i);

			// 연결이 안되었으면 건너뜀
			if (pSession == nullptr || !pSession->IsConnected())
				continue;

			UINT32 genBefore = pSession->GetGeneration();	// gen 캡쳐

			ULONGLONG lastActivity = pSession->GetLastActivityTime();
			ULONGLONG elapsed = now - lastActivity;

			if (elapsed >= config.TimeoutMs)
			{
				// 60초동안 무응답 시 Disconnect 호출하여 끊기
				LOG_DEBUG("[TimeoutThread] Client Index(%d) 타임아웃! (60초 무응답) -> 연결 종료 요청\n", pSession->GetIndex());
				// 연결 종료
				//CloseSocket(pSession, true);
				pSession->DisconnectAsync(genBefore); // shutdown + CancelIoEx
			}
			else if (elapsed >= config.PingIntervalMs) // 30초
			{
				ULONGLONG lastping = pSession->GetLastPingTime();
				if (now - lastping >= config.PingIntervalMs)
				{
					pSession->SetLastPingTime(now);

					// 최소한 30초 동안은 살아 있는지 확인하고 PING 발송
					LOG_DEBUG("[TimeoutThread] Client Index(%d)에게 PING 발송 (무응답 %llu ms)\n", pSession->GetIndex(), elapsed);
					// 핑 전송
					PACKET_HEADER pingHeader;
					pingHeader.PacketLength = sizeof(PACKET_HEADER); // 패킷 전체 길이
					pingHeader.PacketId = (UINT16)PACKET_ID::SYS_PING;
					pingHeader.PacketType = 0;
					pSession->SendMsg(pingHeader.PacketLength, (char*)&pingHeader);
				}
			}

			// 최근 활동이 있으면 아무것도 하지 않고 넘어감
		}
		Sleep(config.CheckIntervalMs); // 10초
	}
}


// Overlapped I/O 작업에 대한 완료 통보를 받아 그에 해당하는 처리를 하는 함수
void IOCompletionPort::WorkerThread()
{
	// 완료 항목 수신 배열
	OVERLAPPED_ENTRY completionEntries[MAX_COMPLETION_ENTRIES];
	ULONG numEntriesRemoved = 0;

	//// CompletionKey를 통한 클라이언트 정보
	//ClientSession* pClientSession = nullptr;
	//// 함수 호출 성공 여부
	BOOL bSuccess = TRUE;
	//// Overlapped I/O 작업에서 전송된 데이터의 크기
	//DWORD dwIoSize = 0;
	//// I/O 작업을 위해 요청된 Overlapped 구조체를 가져올 포인터
	//LPOVERLAPPED lpOverlapped = NULL;

	while (mIsWorkerRun)
	{
		////////////////////////////////////
		// 이 함수를 통해 스레드들은 WaitingThread Queue에 대기 상태로 됨
		// 완료된 Overlapped I/O 작업이 발생하면 IOCP Queue에서 완료된 작업을 가져와 처리
		// 그리고 PostQueueCompletionStatus()에 의해 사용자 메시지가 게시되면 게시된 내용
		////////////////////////////////////
		//printf("[DEBUG] GQCS 대기: bSuccess=%d, dwIoSize=%d, lpOverlapped=%p\n", bSuccess, dwIoSize, lpOverlapped);

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
			FALSE);						// 얼러터블 처리
		//printf("[DEBUG] GetQueuedCompletionStatusEx 결과: bSuccess=%d, numEntries=%d, LastError=%d\n",bSuccess, numEntriesRemoved, GetLastError());
		if (!bSuccess)
		{
			DWORD error = GetLastError();

			if (error == TIMEOUT_WAIT)
			{
				continue;
			}
			else if (error == ERROR_ABANDONED_WAIT_0)
			{
				// IOCP 핸들이 닫힘 - 종료 진행
				mIsWorkerRun = false;
				break;
			}
			else
			{
				LOG_ERROR("GetQueuedCompletionStatusEx() 실패 : %d\n", error);
				continue;
			}
		}
		//printf("[DEBUG] %d개 완료 이벤트 처리 시작\n", numEntriesRemoved);
		for (ULONG i = 0; i < numEntriesRemoved; ++i)
		{
			auto& entry = completionEntries[i];
			// 여기서 이미 해제된 메모리를 가리키게 된다면..?
			// pClientSession이 이미 해제된 메모리를 가리킴 -> crash 발생 (Dangling pointer)

			ClientSession* pClientSession = reinterpret_cast<ClientSession*>(entry.lpCompletionKey);

			DWORD dwIoSize = entry.dwNumberOfBytesTransferred;
			LPOVERLAPPED lpOverlapped = entry.lpOverlapped;

			// 사용자 정의 종료 메시지 처리
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
			//// client의 접속이 끊어졌을때(0바이트 수신(연결종료)
			//if (dwIoSize == 0 && pOverlappedEx->m_eOperation != IOOperation::ACCEPT)
			//{
			//	CloseSocket(pClientSession);

			//	if (pClientSession->ReleaseRef())
			//	{
			//		PushFreeSessionIndex(pClientSession->GetIndex());
			//		TryPostAcceptEx();
			//	}
			//	continue;
			//}
			//printf("[DEBUG] Operation=%d, ClientSessionIndex=%d\n",
			//	(int)pOverlappedEx->m_eOperation, pOverlappedEx->clientSessionIndex);
			if (IOOperation::ACCEPT == pOverlappedEx->m_eOperation)
			{
				pClientSession = GetClientInfo(pOverlappedEx->clientSessionIndex);
				--mPendingAcceptCount;

				// 서버 종료중이거나 Listen Socket이 닫혀있다면 즉시 탈출
				if (!mIsWorkerRun || mListenSocket == INVALID_SOCKET)
				{
					pClientSession->CloseAcceptSocket();	// 혹시 열려있을 수 있는 Accept 소켓 닫기

					if (pClientSession->ReleaseRef())
					{
						PushFreeSessionIndex(pOverlappedEx->clientSessionIndex);
					}
					continue;
				}

				if (pClientSession->AcceptCompletion(mListenSocket))
				{
					// 여러 스레드가 동시에 호출해도 안전하게 atomic 처리
					++mClientCnt;
					OnConnect(pClientSession->GetIndex(), pClientSession->GetGeneration());
					LOG_DEBUG("######################################### 접속됨 #################################\n");
				}
				else
				{
					// Accept 실패, 소켓 정리 + 인덱스 반납
					if (pClientSession->IsConnected())
					{
						pClientSession->TryMarkDisconnected();
						pClientSession->Closed(true);
					}
					else
					{
						pClientSession->CloseAcceptSocket();
					}

					// Accept IO의 ReleaseRef (PostImmediateAccept에서 AddRef한 것)
					// 실패했으므로 OnConnect의 store(1)은 안 일어남 → 정상적으로 해제
					pClientSession->ReleaseRef();
					PushFreeSessionIndex(pOverlappedEx->clientSessionIndex);

				}

				// AcceptEx 재등록하기
				TryPostAcceptEx();
			}

			// Overlapped I/O Recv 작업 완료 시 처리
			// workerthread accept 처리
			else if (IOOperation::RECV == pOverlappedEx->m_eOperation)
			{
				// recv종료 처리
				if (dwIoSize == 0)
				{
					CloseSocket(pClientSession);
					if (pClientSession->ReleaseRef())
					{
						PushFreeSessionIndex(pClientSession->GetIndex());
						TryPostAcceptEx();
					}
					continue;
				}
				// Stale I/O 방지
				if (pOverlappedEx->generation != pClientSession->GetGeneration())
				{
					LOG_DEBUG("[Stale I/O] RECV 무시 - gen: %d vs %d\n",
						pOverlappedEx->generation, pClientSession->GetGeneration());
					if (pClientSession->ReleaseRef())
					{
						PushFreeSessionIndex(pClientSession->GetIndex());
						TryPostAcceptEx();
					}
					continue;
				}

				//// 타임아웃 확인 위한 갱신
				//pClientSession->UpdateActivity();

				OnReceive(pClientSession->GetIndex(), pClientSession->GetGeneration(), dwIoSize, pClientSession->RecvBuff());
				// 새 recv 등록
				if (!pClientSession->BindRecv())
				{
					CloseSocket(pClientSession, true);
				}

				// 이 Recv 완료에 대한 ReleaseRef
				if (pClientSession->ReleaseRef())
				{
					PushFreeSessionIndex(pClientSession->GetIndex());
					TryPostAcceptEx();
				}

			}

			else if (IOOperation::SEND == pOverlappedEx->m_eOperation) // 송신이 완료되면
			{
				auto pSendOvl = (SendOverlappedEx*)lpOverlapped;

				// Stale 여부 상관없이 무조건 SendComplete로 넘김
				// 메모리 해제 + 큐 정리는 항상 다음 전송만 조건부
				// 0바이트 취소든, 정상이든 일단 풀 반납
				pClientSession->SendComplete(pSendOvl);

				// 반납 후 끊김 처리
				if (dwIoSize == 0)
				{
					CloseSocket(pClientSession);
				}

				if (pClientSession->ReleaseRef())
				{
					PushFreeSessionIndex(pClientSession->GetIndex());
					TryPostAcceptEx();
				}
			}

			else if (IOOperation::ZOMBIE_CLEANUP == pOverlappedEx->m_eOperation)
			{
				// 세대 검증 : 이미 새 유저가 접속 했으면 무시
				if (pOverlappedEx->generation == pClientSession->GetGeneration())
				{
					CloseSocket(pClientSession);
				}

				delete pOverlappedEx;

				// 가짜 I/O 처리가 끝났으므로 참조 카운트 감소
				if (pClientSession->ReleaseRef())
				{
					PushFreeSessionIndex(pClientSession->GetIndex());
					TryPostAcceptEx();
				}
			}


			// 예외
			else
			{
				LOG_DEBUG("Client Index : (%d)에서 예외\n", pClientSession->GetIndex());
			}

		}
	}
}

// 소켓의 연결을 종료
void IOCompletionPort::CloseSocket(ClientSession* pClientSession, bool bIsForce)
{
	//if (pClientSession->IsConnected() == false)
	//{
	//	return;
	//}
	if (pClientSession->TryMarkDisconnected() == false)
	{
		// 이미 다른 쓰레드가 닫았으므로 무시하고 돌아감
		return;
	}

	UINT32 ClientIndex = pClientSession->GetIndex();
	pClientSession->Closed(bIsForce);
	OnClose(ClientIndex, pClientSession->GetGeneration());
	--mClientCnt;

	// Base Ref(1) 해제
	if (pClientSession->ReleaseRef())
	{
		// 잔여 IO없으므로 즉시 반납
		PushFreeSessionIndex(ClientIndex);
		TryPostAcceptEx();
	}
	// else: 잔여 IO있으므로 workerThread의 마짐가 ReleaseRef가 반납
	// 
}

// 빈 세션 하나 가져오는 함수
UINT32 IOCompletionPort::PopFreeSessionIndex()
{
	// 잠금
	std::lock_guard<std::mutex> lock(mFreeListLock);

	// 큐가 비어있는지 확인 (빈거면 더 줄 수 없음 )
	if (mFreeSessionList.empty())
	{
		return UINT32_MAX;
	}
	auto index = mFreeSessionList.back();
	mFreeSessionList.pop_back();
	return index;
}

// 세션 반납 함수
void IOCompletionPort::PushFreeSessionIndex(const UINT32 index)
{
	// 잠금
	std::lock_guard<std::mutex>lock(mFreeListLock);

	// 빈 칸 반납
	mFreeSessionList.push_back(index);
}