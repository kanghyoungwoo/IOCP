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
		// ���� ��� ��
		WSACleanup();
	}

	// ������ �ʱ�ȭ �ϴ� �Լ�

	bool Init(const UINT32 Max_IO_Worker_Threads_Count)
	{
		WSADATA wsaData;

		int nRet = WSAStartup(MAKEWORD(2, 2), &wsaData);
		if (nRet != 0)
		{
			LOG_ERROR("WSAStartup() 실패 : %d\n", WSAGetLastError());
			return false;
		}

		// TCP, Overlapped I/O ������ ����
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
	//		printf("[ERROR] WSAStartup() ���� : %d\n", WSAGetLastError());
	//		return false;
	//	}

	//	// TCP, Overlapped I/O ������ ����
	//	mListenSocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, NULL, WSA_FLAG_OVERLAPPED);

	//	if (mListenSocket == INVALID_SOCKET)
	//	{
	//		printf("[ERROR] socket() ���� : %d\n", WSAGetLastError());
	//		return false;
	//	}

	//	printf("SOCKET �ʱ�ȭ ����\n");
	//	return true;
	//}

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
			LOG_ERROR("bind() 실패 : %d\n", WSAGetLastError());
			return false;
		}

		// ���Ӵ��ť 5�� ����
		nRet = listen(mListenSocket, 5);

		if (nRet != 0)
		{
			LOG_ERROR("listen() 실패 : %d\n", WSAGetLastError());
			return false;
		}

		// ó�� IOCP QUEUE���鶩 ���� NULL, ���������� 0�̸� OS�� �ñ�
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

	// ���� ��û�� �����ϰ� �޼����� �޾Ƽ� ó���ϴ� �Լ�
	bool StartServer(const int maxClientCount)
	{
		CreateClient(maxClientCount);

		// mIOCPHandle = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL, MAX_WORKERTHREAD);
		
		bool bRet = CreateWorkerThread();
		if (bRet == false)
		{
			return false;
		}
		
		// �ʱ� AcceptEx 100�� �ɾ�α�
		for (UINT32 i = 0; i < MAX_PENDING_ACCEPT;++i)
		{
			// pop���� �ε��� ������
			UINT32 emptyIndex = PopFreeSessionIndex();
			if (emptyIndex != UINT32_MAX)
			{
				// �ش缼���� postImmediateȣ��
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

	// �����Ǿ� �ִ� �����带 �ı��Ѵ�
	void DestroyThread()
	{

		mIsTimeoutRun = false;
		if (mTimeoutThread.joinable())
			mTimeoutThread.join();
		LOG_DEBUG("TimeoutThread 종료 완료\n");

		//Todo: GracefunShutDown�����ϱ�
		
		// step1. Accept����
		//mIsAccepterRun = false;
		closesocket(mListenSocket);		// AcceptEx ��� ����
		mListenSocket = INVALID_SOCKET;
		//if (mAccepterThread.joinable())
		//	mAccepterThread.join();
		LOG_DEBUG("step1 Accept 종료 완료\n");
		
		// step2. �������� �������� + IO���
		for (auto& client : mClientInfos)
		{
			if (client->IsConnected())
			{
				// ����� Ŭ���̾�Ʈ�� �ִٸ�
				// �ش� ���� ��� �񵿱� IO ���
				CancelIoEx((HANDLE)client->GetSocket(), NULL);
				client->Closed(true);	// ���� ���� ����
			}
		}
		LOG_DEBUG("step2 모든 클라이언트 강제 종료 완료\n");


		// step3. �ܿ� IO Draining
		// ���� ������ OS �� �ܿ� �Ϸ� ��ȣ IOCP queue�� ����
		// ª�� ��� �� ó�� �ð���
		std::this_thread::sleep_for(std::chrono::milliseconds(500));
		LOG_DEBUG("step3: 잔여 IO Draining 완료\n");

		// step4. ��Ŀ ������ ��� - PQCS�� ����
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
		// step5. IOCP Handle ����
		CloseHandle(mIOCPHandle);
		mIOCPHandle = INVALID_HANDLE_VALUE;
		LOG_DEBUG("step5: 자원 정리 완료\n");


		// ���� ���� ���
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
	// Ŭ���̾�Ʈ�� ������ �޾Ƽ�
	// Ŭ���̾�Ʈ���� �޼����� send�ϴ� �Լ�
	bool SendMsg(const UINT32 ClientSessionIndex_, const UINT32 dataSize_, char* pMsg_)
	{
		auto pClient = GetClientInfo(ClientSessionIndex_);
		return pClient->SendMsg(dataSize_, pMsg_);
	}

	virtual void OnConnect(const int clientIndex){}
	virtual void OnClose(const int clientIndex){}
	virtual void OnReceive(const UINT32 clientIndex, const UINT32 size, char* pData){}

	// ������
	uint64_t GetSendPoolAllocFailCount() const { return mSendBufferPool.GetAllocFailCount(); }


private:
	void CreateClient(const int maxClientCount)
	{
		mSendBufferPool.Init(maxClientCount * 100);

		for (int i = 0;i < maxClientCount;++i)
		{
			auto client = std::make_unique<ClientSession>();
			client->Init(i, mIOCPHandle, &mSendBufferPool);
			mClientInfos.emplace_back(std::move(client));
		}

		// ����ȭ..���Ҵ� ������ ���� �޸� ������ �̸� Ȯ�� (Capacity = maxCount)
		mFreeSessionList.reserve(maxClientCount);

		// �������� ���ÿ� ä���ֱ�)
		for (UINT32 i = maxClientCount; i > 0; --i)
		{
			mFreeSessionList.push_back(i-1);
		}
		//for (int i = 0;i < maxClientCount;++i)
		//{
		//	auto client = std::make_unique<ClientSession>();
		//	client->Init(i, mIOCPHandle);
		//	mClientInfos.emplace_back(std::move(client));

		//	//auto client = new ClientSession(); // �޸� ���� ���ɼ�
		//	//client->Init(i,mIOCPHandle);
		//	//mClientInfos.emplace_back(client);

		//}
	}

	// WaitingThread Queue���� ����� ������� ���� 
	bool CreateWorkerThread()
	{
		mIsWorkerRun = true;
		// WaitingThread Queue�� ��� ���·� ���� �������, ���尹���� (cpu���� * 2) + 1
		for (int i = 0;i < MAX_WORKERTHREAD;i++)
		{
			mIOWorkerThreads.emplace_back([this]() {WorkerThread();});
		}

		LOG_DEBUG("WorkerThread 시작 \n");
		return true;
	}

	void TimeoutCheckThread()
	{
		const ULONGLONG TIMEOUT_MS = 60000; // 60�� ������ �� ���� ����
		const ULONGLONG PING_INTERVAL_MS = 30000; // 30�� ���� ������� PING
		const ULONGLONG CHECK_INTERVAL_MS = 10000; // 10�� ��ȸ�ֱ�
		while (mIsTimeoutRun)
		{
			ULONGLONG now = GetTickCount64();

			for (size_t i = 0;i<mClientInfos.size();i++)
			{
				ClientSession* pSession = GetClientInfo(i);

				// ������ �ȵ����� �ǳʶ�
				if (pSession == nullptr || !pSession->IsConnected())
					continue;

				ULONGLONG lastActivity = pSession->GetLastActivityTime();
				ULONGLONG elapsed = now - lastActivity;
				
				if (elapsed >= TIMEOUT_MS) // 60��
				{
					// 60�ʵ��� ���� ��� Disconnectȣ���Ͽ� ű
					LOG_DEBUG("[TimeoutThread] Client Index(%d) 타임아웃! (60초 무응답) -> 연결 종료 요청\n", pSession->GetIndex());
					// ���� ����
					//CloseSocket(pSession, true);
					pSession->DisconnectAsync();
				}
				else if (elapsed >= PING_INTERVAL_MS) // 30��
				{
					ULONGLONG lastping = pSession->GetLastPingTime();
					if (now - lastping >= PING_INTERVAL_MS)
					{
						pSession->SetLastPingTime(now);

						// ������ 30�� ������ ���� �����ϰ� PING����
						LOG_DEBUG("[TimeoutThread] Client Index(%d)에게 PING 발송 (무응답 %llu ms)\n", pSession->GetIndex(), elapsed);
						// �� ����
						PACKET_HEADER pingHeader;
						pingHeader.PacketLength = sizeof(PACKET_HEADER); // ��Ŷ ��ü ����
						pingHeader.PacketId = (UINT16)PACKET_ID::SYS_PING;
						pingHeader.PacketType = 0;
						pSession->SendMsg(pingHeader.PacketLength, (char*)&pingHeader);
					}
				}

				//�ֱ� Ȱ���� ������ �ƹ��͵� ���� �ʰ� �Ѿ
			}
			Sleep(CHECK_INTERVAL_MS); // 10��
		}
	}

	//// accept ��û�� ó���ϴ� ������ ����
	//bool CreateAccepterThread()
	//{
	//	mIsAccepterRun = true;
	//	mAccepterThread = std::thread([this]() { AccepterThread(); });

	//	printf("AccepterThread ����\n");
	//	return true;
	//}

	//void CreateSendThread()
	//{
	//	mIsSenderRun = true;
	//	mSendThread = std::thread([this]() { SendThread(); });
	//	printf("SendThread Start !\n");
	//}

	// FreeList�� ��ü��
	//// ������� �ʴ� Ŭ���̾�Ʈ�� ���� ����ü�� ��ȯ
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

	// Ŭ���̾�Ʈ�� index�� ������ client�� info�� �����ϴ� �Լ�
	ClientSession* GetClientInfo(const UINT32 clientSessionIndex)
	{
		return mClientInfos[clientSessionIndex].get();
	}

	// Overlapped I/O �۾��� ���� �Ϸ� �뺸�� �޾� �׿� �ش��ϴ� ó���� �ϴ� �Լ�
	void WorkerThread()
	{
		// �Ϸ� �׸� ���� �迭
		OVERLAPPED_ENTRY completionEntries[MAX_COMPLETION_ENTRIES];
		ULONG numEntriesRemoved = 0;

		//// CompletionKey�� ���� ������ ���� 
		//ClientSession* pClientSession = nullptr;
		//// �Լ� ȣ�� ���� ����
		BOOL bSuccess = TRUE;
		//// Overlapped I/O �۾����� ���۵� ������ ũ��
		//DWORD dwIoSize = 0;
		//// I/O �۾��� ���� ��û�� Overlapped ����ü�� ���� ������
		//LPOVERLAPPED lpOverlapped = NULL;

		while (mIsWorkerRun)
		{
			////////////////////////////////////
			// �� �Լ��� ���� ��������� WaitingThread Queue�� ��� ���·� ��
			// �Ϸ�� Overlapped I/O �۾��� �߻��ϸ� IOCP Queue���� �Ϸ�� �۾��� ������ ó��
			// �׸��� PostQeueuCompletionStatus()�� ���� ����� �޼����� �����Ǹ� ������ ����
			////////////////////////////////////
			//printf("[DEBUG] GQCS ���: bSuccess=%d, dwIoSize=%d, lpOverlapped=%p\n", bSuccess, dwIoSize, lpOverlapped);
			
			//bSuccess = GetQueuedCompletionStatus(
			//	mIOCPHandle,				// dequeue�� IOCP �ڵ�
			//	&dwIoSize,					// ���� ���۵� ����Ʈ
			//	(PULONG_PTR)&pClientSession,	// CompletionKey
			//	&lpOverlapped,				// Overlapped IO ��ü
			//	INFINITE);					// ����� �ð�

			bSuccess = GetQueuedCompletionStatusEx(
				mIOCPHandle,				// dequeue�� IOCP �ڵ�
				completionEntries,			// �Ϸ� �׸� �迭
				MAX_COMPLETION_ENTRIES,		// �Ϸ� �׸� �迭 ũ��
				&numEntriesRemoved,			// ���ŵ� �׸� ��
				INFINITE,					// ����� �ð�
				FALSE);						// ��������� ó��
			//printf("[DEBUG] GetQueuedCompletionStatusEx ���: bSuccess=%d, numEntries=%d, LastError=%d\n",bSuccess, numEntriesRemoved, GetLastError());
			if (!bSuccess)
			{
				DWORD error = GetLastError();

				if (error == TIMEOUT_WAIT)
				{
					continue;
				}
				else if (error == ERROR_ABANDONED_WAIT_0)
				{
					// IOCP�ڵ��� ���� - ���� ����
					mIsWorkerRun = false;
					break;
				}
				else
				{
					LOG_ERROR("GetQueuedCompletionStatusEx() 실패 : %d\n", error);
					continue;
				}
			}
			//printf("[DEBUG] %d�� �Ϸ� �̺�Ʈ ó�� ����\n", numEntriesRemoved);
			for (ULONG i = 0; i < numEntriesRemoved; ++i)
			{
				auto& entry = completionEntries[i];
				// ���⼭ �̹� ������ �޸𸮸� ����Ű�� �ȴٸ�..?
				// pClientSession�� �̹� ������ �޸𸮸� ����Ŵ -> crash �߻� (Dangling pointer)
				
				ClientSession* pClientSession = reinterpret_cast<ClientSession*>(entry.lpCompletionKey);
				
				DWORD dwIoSize = entry.dwNumberOfBytesTransferred;
				LPOVERLAPPED lpOverlapped = entry.lpOverlapped;

				// ����� ������ ���� �޼��� ó��
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
				// client�� ������ ��������
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
						// ���� ������ ���ÿ� ȣ���ص� ���� ������ atomicó��
						++mClientCnt;
						OnConnect(pClientSession->GetIndex());
						LOG_DEBUG("######################################### 접속됨 #################################\n");
					}
					else
					{
						CloseSocket(pClientSession, true);
					}

					// ������ AcceptEx 1�� �����ϱ� 
					UINT32 nextEmptyIndex = PopFreeSessionIndex();
					if (nextEmptyIndex != UINT32_MAX)
					{
						auto pNextClient = GetClientInfo(nextEmptyIndex);
						pNextClient->PostImmediateAccept(mListenSocket);
					}
				}

				// Overlapped I/O Recv �۾� ��� �� ó��
				else if (IOOperation::RECV == pOverlappedEx->m_eOperation)
				{

					// Stale I/O ����
					if (pOverlappedEx->generation != pClientSession->GetGeneration())
					{
						LOG_DEBUG("[Stale I/O] RECV 무시 - gen: %d vs %d\n",
							pOverlappedEx->generation, pClientSession->GetGeneration());
						continue;
					}

					// ���񼼼� Ȯ�� ���� �߰�
					pClientSession->UpdateActivity();

					OnReceive(pClientSession->GetIndex(), dwIoSize, pClientSession->RecvBuff());
					pClientSession->BindRecv(); // �ٽ� recv �ɾ���
				}

				else if (IOOperation::SEND == pOverlappedEx->m_eOperation) // ������ �Ϸ�Ǹ�
				{
					// Stale I/O ����
					auto pSendOvl = (SendOverlappedEx*)lpOverlapped;
					if (pSendOvl->generation != pClientSession->GetGeneration())
					{
						LOG_DEBUG("[Stale I/O] SEND 무시 - gen: %d vs %d\n",
							pSendOvl->generation, pClientSession->GetGeneration());
						mSendBufferPool.Free(pSendOvl);  // Ǯ���� �ݳ�
						continue;
					}
					pClientSession->SendComplete(dwIoSize);
				}
				// ����
				else
				{
					LOG_DEBUG("Client Index : (%d)에서 예외\n", pClientSession->GetIndex());
				}

			}	
		}
	}

	//������� ������ �޴� ������
	//void AccepterThread()
	//{
	//	while (mIsAccepterRun)
	//	{
	//		for (auto& client : mClientInfos)
	//		{
	//			if (client->IsConnected())
	//				continue;
	//			// ��� �ð� ���� �ٷ� AccpetEx
	//			client->PostImmediateAccept(mListenSocket);

	//			// �ּҴ��ð�
	//			std::this_thread::sleep_for(std::chrono::milliseconds(1));
	//		}
	//	}

	//}


	// ������ ������ ����
	void CloseSocket(ClientSession* pClientSession, bool bIsForce = false)
	{
		if (pClientSession->IsConnected() == false)
		{
			return;
		}
		UINT32 ClientIndex = pClientSession->GetIndex();
		pClientSession->Closed(bIsForce);
		OnClose(ClientIndex);

		// ���� ������ �������Ƿ� ��ȣǥ �ݳ� 
		PushFreeSessionIndex(ClientIndex);
		--mClientCnt;
	}

	// �� ���� �ϳ� ������ �Լ�
	UINT32 PopFreeSessionIndex()
	{
		// ���
		std::lock_guard<std::mutex> lock(mFreeListLock);
		
		// ť�� ����ִ��� Ȯ�� (������ �� �� ��� )
		if (mFreeSessionList.empty())
		{
			return UINT32_MAX;
		}
		auto index = mFreeSessionList.back();
		mFreeSessionList.pop_back();
		return index;
	}
	// ���� �ݳ� �Լ�
	void PushFreeSessionIndex(const UINT32 index)
	{
		// ���
		std::lock_guard<std::mutex>lock(mFreeListLock);

		// �� �� �ݳ�
		mFreeSessionList.push_back(index);
	}

	// Ŭ���̾�Ʈ ���� ���� ����ü
	//std::vector<ClientSession*> mClientInfos;

	// Ŭ���̾�Ʈ�� ������ �ޱ� ���� ���� ����
	SOCKET mListenSocket = INVALID_SOCKET;

	// ���� �Ǿ��ִ� Ŭ���̾�Ʈ ��
	std::atomic<int>mClientCnt = 0;

	// IO worker ������
	std::vector<std::thread> mIOWorkerThreads;

	//// Accept ������
	//std::thread mAccepterThread;

	// Send ������
	std::thread mSendThread;
	
	// CompletionPort��ü �ڵ� 
	HANDLE	mIOCPHandle = INVALID_HANDLE_VALUE;

	// �۾� ������ ���� �÷���
	bool	mIsWorkerRun = true;

	//// ���� ������ ���� �÷���
	//bool	mIsAccepterRun = true;

	//bool	mIsSenderRun = false;

	UINT32 MaxIOWorkerThreadCount = 0;

	// GetQueuedCompletionStatusEx ���� ���
	static const ULONG MAX_COMPLETION_ENTRIES = 64;  // �� ���� ó���� �ִ� �Ϸ� �׸� ��
	static const DWORD TIMEOUT_WAIT = 100;           // ��� Ÿ�Ӿƿ� (ms)

	// Ŭ���̾�Ʈ ���� ���� ����ü
	//std::vector<ClientSession*> mClientInfos;
	std::vector<std::unique_ptr<ClientSession>> mClientInfos;

	ObjectPool<SendOverlappedEx> mSendBufferPool;

	// FreeList
	std::vector<UINT32>mFreeSessionList;
	
	// FreeList ��ȣ ���ؽ�
	std::mutex mFreeListLock;

	// �ʱ� AcceptEx���� 100��
	static constexpr UINT32 MAX_PENDING_ACCEPT = 100;

	std::thread mTimeoutThread;
	std::atomic<bool> mIsTimeoutRun{ false };
};