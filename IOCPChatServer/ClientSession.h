#pragma once
#pragma comment(lib, "mswsock.lib")

#include <MSWSock.h>
#include "Define.h"
#include "ObjectPool.h"
#include <stdio.h>
#include <mutex>
#include <queue>

class ClientSession
{
public:
	ClientSession()
	{
		ZeroMemory(&m_stRecvOverlappedEx, sizeof(_stOverlappedEx));
		m_socketClient = INVALID_SOCKET;
	}

	void Init(const UINT32 index, HANDLE iocpHandle, ObjectPool<SendOverlappedEx>* pSendPool)
	{
		mIndex = index;
		mIOCPHandle = iocpHandle;
		mSendPool = pSendPool;
	}

	UINT32 GetIndex()
	{
		return mIndex;
	}

	bool IsConnected()
	{
		return mIsConnected.load(std::memory_order_acquire);
	}

	SOCKET GetSocket()
	{
		return m_socketClient;
	}

	UINT32 GetGeneration() const
	{
		return mGeneration.load(std::memory_order_acquire);
	}


	char* RecvBuff()
	{
		return mRecvBuf;
	}

	char* SendBuff()
	{
		return mSendBuf;
	}

	UINT64 GetLatestClosedTimeSec()
	{
		return mLatestClosedTimeSec;
	}


	bool OnConnect(HANDLE iocpHandle, SOCKET socket)
	{
		m_socketClient = socket;
		//++mGeneration;
		mGeneration.fetch_add(1, std::memory_order_acq_rel);


		// 접속 직후 타임아웃 판정을 위한 시간 기록
		UpdateActivity();

		Clear();
		// 모든 준비가 끝난 후 문 엶(이후 부턴 CloseSocket으로만 닫아야함)
		mIsConnected.store(true, std::memory_order_release);

		// IOCP등록 및 Recv대기
		if (BindIOCompletionPort(iocpHandle) == false)
		{
			return false;
		}

		// BindRecv()가 실패할때도 알아서 CloseSocket()이 불림
		// BindRecv 내부에서 AddRef-> RefCount = 2
		return BindRecv();
	}

	void Closed(bool bIsForced = false)
	{
		// SO_LINGER를 사용하면 소켓의 close 이전에 전송되지 않은 데이터를 모두 처리할 시간을 제공
		struct linger stLinger = { 0,0 };	// SO_DONTLINGER로 설정

		// bIsForce가 true면 SO_LINGER, timeout = 0으로 설정하여 즉시 닫히게 함
		if (bIsForced == true)
		{
			stLinger.l_onoff = 1;
		}

		// socketClose 이전에 송수신이 모두 중단
		shutdown(m_socketClient, SD_BOTH);

		// 소켓 옵션을 설정
		setsockopt(m_socketClient, SOL_SOCKET, SO_LINGER, (char*)&stLinger, sizeof(stLinger));

		//mIsConnected = false;// TryMarkDisconnect가 대신함

		mLatestClosedTimeSec = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now().time_since_epoch()).count();

		// 소켓 연결을 닫기
		closesocket(m_socketClient);

		m_socketClient = INVALID_SOCKET;

		Clear();
	}

	void Clear()
	{
		// 송신 큐 정리
		std::lock_guard<std::mutex> gurad(mSendLock);

		while (!mSendDataqueue.empty())
		{
			//delete[] mSendDataqueue.front()->m_wsaBuf.buf;
			//delete mSendDataqueue.front();
			//mSendDataqueue.pop();
			mSendPool->Free(mSendDataqueue.front());	// 풀에 반납
			mSendDataqueue.pop();
		}

		mSendPos = 0;
		mIsSending = false;

		// 버퍼 초기화
		ZeroMemory(mRecvBuf, sizeof(mRecvBuf));
		ZeroMemory(mSendBuf, sizeof(mSendBuf));

	}

	// WSASend Overlapped I/O 작업을 수행
	bool SendMsg(const UINT32 dataSize, char* pMsg)
	{
		// 
		if (!IsConnected()) 
			return false;
		// 풀에서 SendOverlappedEx 하나를 가져옴 (힙 할당 제거)
		auto pSendOvl = mSendPool->Alloc();
		if (pSendOvl == nullptr)
		{
			// 풀 소진
			mSendPool->IncrementAllocFail();
			LOG_ERROR_ONCE("SendPool 소진! dataSize=%d\n", dataSize);
			return false;
		}
		ZeroMemory(&pSendOvl->wsaOverlapped, sizeof(WSAOVERLAPPED));
		pSendOvl->wsaBuf.len = dataSize;
		pSendOvl->wsaBuf.buf = pSendOvl->buffer;	// 내부 버퍼를 가리킴
		CopyMemory(pSendOvl->buffer, pMsg, dataSize);
		pSendOvl->operation = IOOperation::SEND;
		//pSendOvl->generation = mGeneration;
		pSendOvl->generation = mGeneration.load(std::memory_order_acquire);
		std::lock_guard<std::mutex> guard(mSendLock);
		mSendDataqueue.push(pSendOvl);

		//// overlapped 구조체 세팅
		//auto sendOverlappedEx = new stOverlappedEx;
		//ZeroMemory(sendOverlappedEx, sizeof(stOverlappedEx));
		//sendOverlappedEx->m_wsaBuf.len = dataSize;
		//sendOverlappedEx->m_wsaBuf.buf = new char[dataSize];
		//CopyMemory(sendOverlappedEx->m_wsaBuf.buf, pMsg, dataSize);
		//sendOverlappedEx->m_eOperation = IOOperation::SEND;

		//std::lock_guard<std::mutex> guard(mSendLock);
		//mSendDataqueue.push(sendOverlappedEx);

		// 데이터가 1개이면 앞에 보내는 데이터가 없으니 바로 wsasend
		if (mSendDataqueue.size() == 1)
		{
			if (!SendIO())
			{
				DisconnectAsync(GetGeneration());
			}
		}

		// buffer를 이용한 1-send
		/*
		std::lock_guard<std::mutex>guard(mSendLock);
		if ((mSendPos + dataSize) > MAX_SOCK_SENDBUF)
		{
			mSendPos = 0;
		}
		auto pSendBuf = &mSendBuf[mSendPos];

		// 보낼 메시지 복사
		CopyMemory(pSendBuf, pMsg, dataSize);
		mSendPos += dataSize;
		*/


		return true;
	}

	bool BindRecv()
	{
		DWORD dwFlag = 0;
		DWORD dwRecvNumBytes = 0;

		// Overlapped I/O를 위한 각 정보를 세팅
		m_stRecvOverlappedEx.m_wsaBuf.len = MAX_SOCKBUF;
		m_stRecvOverlappedEx.m_wsaBuf.buf = mRecvBuf;
		m_stRecvOverlappedEx.m_eOperation = IOOperation::RECV;
		m_stRecvOverlappedEx.generation = mGeneration.load(std::memory_order_acquire);
		//m_stRecvOverlappedEx.generation = mGeneration;

		AddRef();

		int nRet = WSARecv(m_socketClient,
			&(m_stRecvOverlappedEx.m_wsaBuf),
			1,
			&dwRecvNumBytes,
			&dwFlag,
			(LPWSAOVERLAPPED) & (m_stRecvOverlappedEx),
			NULL);

		// socket_error 시 client socket이 끊어졌으므로 처리
		if (nRet == SOCKET_ERROR && (WSAGetLastError() != ERROR_IO_PENDING))
		{
			LOG_ERROR("WSARecv() 실패 : %d\n", WSAGetLastError());
			ReleaseRef();	//IO 등록 실패 + 올렸던 카운트 되돌림
			return false;
		}
		LOG_DEBUG("bind recv 성공\n");
		return true;
	}

	// CompletionPort 객체와 소켓과 CompletionKey를 연결시키는 연결용 함수
	bool BindIOCompletionPort(HANDLE iocpHandle)
	{
		// socket과 pClientInfo를 CompletionPort 객체에 연결시킴
		auto hIOCP = CreateIoCompletionPort((HANDLE)GetSocket()
			, iocpHandle
			, (ULONG_PTR)(this), 0);

		if (hIOCP == INVALID_HANDLE_VALUE)
		{
			LOG_ERROR("CreateIoCompletionPort() 실패 : %d\n", GetLastError());
			return false;
		}
		LOG_DEBUG("BindIOCompletionport 성공!\n");

		return true;
	}

	bool SendIO()
	{
		auto sendOverlappedEx = mSendDataqueue.front();
		DWORD dwRecvNumBytes = 0;

		AddRef();

		int nRet = WSASend(
			m_socketClient,
			&(sendOverlappedEx->wsaBuf),
			//&(sendOverlappedEx->m_wsaBuf),
			1,
			&dwRecvNumBytes,
			0,
			(LPWSAOVERLAPPED)sendOverlappedEx,
			NULL
		);

		if (nRet == SOCKET_ERROR && (WSAGetLastError() != ERROR_IO_PENDING))
		{
			ReleaseRef();
			// 큐정리 안함- > false -> 호출부에서 DisconnectAsync() -> CloseSocket -> Closed -> Clear()가 안전하게 큐 정리
			LOG_ERROR_ONCE("WSASend() 실패 : %d\n", WSAGetLastError());
			return false;
		}

		// buffer 방식의 1-send
		/*
		if (mSendPos <= 0 || mIsSending)
		{
			return true;
		}
		std::lock_guard<std::mutex> gurar(mSendLock);
		mIsSending = true;
		//CopyMemory(mSendingBuf, &mSendBuf[0], mSendPos);
		CopyMemory(mSendingBuf, mSendBuf, mSendPos);

		DWORD dwRecvNumBytes = 0;

		m_stSendOverlappedEx.m_wsaBuf.len = mSendPos;
		m_stSendOverlappedEx.m_wsaBuf.buf = mSendingBuf;
		m_stSendOverlappedEx.m_eOperation = IOOperation::SEND;

		int nRet = WSASend(m_socketClient,
			&(m_stSendOverlappedEx.m_wsaBuf),
			1,
			&dwRecvNumBytes,
			0,
			(LPWSAOVERLAPPED)&(m_stSendOverlappedEx),
			NULL);

		// socket_error시 client socket이 끊어진 경우의 처리
		if (nRet == SOCKET_ERROR && (WSAGetLastError() != ERROR_IO_PENDING))
		{
			LOG_ERROR_ONCE("WSASend() 실패 : %d\n", WSAGetLastError());
			return false;
		}
		mSendPos = 0;
		*/

		return true;
	}

	void SendComplete(const UINT32 dataSize_)
	{
		// buffer 방식을 이용한 1-send
		/*
		mIsSending = false;
		*/
		LOG_DEBUG("[송신 완료] bytes : %d\n", dataSize_);
		std::lock_guard<std::mutex> guard(mSendLock);
		//delete[] mSendDataqueue.front()->m_wsaBuf.buf;
		//delete mSendDataqueue.front();
		mSendPool->Free(mSendDataqueue.front());
		mSendDataqueue.pop();
		if (mSendDataqueue.empty() == false)
		{
			if (!SendIO())
			{
				DisconnectAsync(GetGeneration());
			}
		}
	}

	bool AcceptCompletion(SOCKET listenSock_)
	{
		if (setsockopt(m_socketClient, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT,
			(char*)&listenSock_, sizeof(SOCKET)) == SOCKET_ERROR)
		{
			LOG_ERROR("SO_UPDATE_ACCEPT_CONTEXT 실패 : %d \n",WSAGetLastError());
			return false;
		}


		LOG_DEBUG("AcceptCompletion : SessionIndex(%d)\n", mIndex);

		if (OnConnect(mIOCPHandle, m_socketClient) == false)
		{
			return false;
		}
		SOCKADDR_IN stClientAddr;
		int nAddrLen = sizeof(SOCKADDR_IN);
		char clientIP[32] = { 0 };
		inet_ntop(AF_INET, &(stClientAddr.sin_addr), clientIP, 32 - 1);
		LOG_DEBUG("Client IP : %s, SOCKET(%d)\n", clientIP, (int)m_socketClient);

		//mAcceptPendingg = false;

		return true;
	}

	bool PostAccept(SOCKET listenSock, const UINT64 curTimeSec)
	{
		LOG_DEBUG("PostAccept Client Index : %d\n", GetIndex());

		mLatestClosedTimeSec = UINT32_MAX;
		m_socketClient = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_IP, NULL, 0, WSA_FLAG_OVERLAPPED);
		if (m_socketClient == INVALID_SOCKET)
		{
			LOG_ERROR("Client Socket Error : %d \n", GetLastError());
			return false;
		}

		ZeroMemory(&mAcceptContext, sizeof(stOverlappedEx));
		DWORD bytes = 0;
		DWORD flags = 0;
		mAcceptContext.m_wsaBuf.len = 0;
		mAcceptContext.m_wsaBuf.buf = nullptr;
		mAcceptContext.m_eOperation = IOOperation::ACCEPT;
 		mAcceptContext.clientSessionIndex = mIndex;

		bool bRet = AcceptEx(listenSock, m_socketClient, mAcceptbuf, 0, sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16, &bytes, (LPWSAOVERLAPPED) & (mAcceptContext));

		if (bRet == FALSE)
		{
			if(WSAGetLastError() != WSA_IO_PENDING)

			{LOG_ERROR("AcceptEx 실패! 에러 코드: %d\n", GetLastError());

				return false;
			}
		}
		return true;
	}

	bool PostImmediateAccept(SOCKET listenSock)
	{
		//printf("PostAccept Client Index : %d\n", GetIndex());

		mLatestClosedTimeSec = UINT32_MAX;
		m_socketClient = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_IP, NULL, 0, WSA_FLAG_OVERLAPPED);
		if (m_socketClient == INVALID_SOCKET)
		{
			LOG_ERROR("Client Socket Error : %d \n", GetLastError());
			return false;
		}

		AddRef();	// AcceptEx 비동기 IO에 대한 참조 카운트

		ZeroMemory(&mAcceptContext, sizeof(stOverlappedEx));
		DWORD bytes = 0;
		DWORD flags = 0;
		mAcceptContext.m_wsaBuf.len = 0;
		mAcceptContext.m_wsaBuf.buf = nullptr;
		mAcceptContext.m_eOperation = IOOperation::ACCEPT;
		mAcceptContext.clientSessionIndex = mIndex;
		bool bRet = AcceptEx(listenSock, m_socketClient, mAcceptbuf, 0, sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16, &bytes, (LPWSAOVERLAPPED) & (mAcceptContext));

		if (bRet == FALSE)
		{
			if (WSAGetLastError() != WSA_IO_PENDING)

			{
				LOG_ERROR("AcceptEx failed! Error code: %d\n", GetLastError());
				ReleaseRef();
				closesocket(m_socketClient);
				m_socketClient = INVALID_SOCKET;
				// index 반납은 호출부에서 처리
				return false;
			}
		}
		return true;
	}

	bool SetSockOption()
	{
		/*if (SOCKET_ERROR == setsockopt(mSock, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, (char*)GIocpManager->GetListenSocket(), sizeof(SOCKET)))
		{
			printf_s("[DEBUG] SO_UPDATE_ACCEPT_CONTEXT error: %d\n", GetLastError());
			return false;
		}*/

		int opt = 1;
		if (SOCKET_ERROR == setsockopt(m_socketClient, IPPROTO_TCP, TCP_NODELAY, (const char*)&opt, sizeof(int)))
		{
			printf_s("[DEBUG] TCP_NODELAY error: %d\n", GetLastError());
			return false;
		}

		opt = 0;
		if (SOCKET_ERROR == setsockopt(m_socketClient, SOL_SOCKET, SO_RCVBUF, (const char*)&opt, sizeof(int)))
		{
			printf_s("[DEBUG] SO_RCVBUF change error: %d\n", GetLastError());
			return false;
		}

		return true;
	}

	void UpdateActivity()
	{
		ULONGLONG now = GetTickCount64();
		mLastActivityTime.store(GetTickCount64(), std::memory_order_relaxed);
		// 클라이언트가 방금 활동했으므로 ping 기록도 리셋
		mLastPingTime.store(0, std::memory_order_relaxed);
	}

	ULONGLONG GetLastActivityTime() const
	{
		return mLastActivityTime.load(std::memory_order_relaxed);
	}

	void SetLastPingTime(ULONGLONG time)
	{
		mLastPingTime.store(time, std::memory_order_relaxed);
	}

	ULONGLONG GetLastPingTime() const
	{
		return mLastPingTime.load(std::memory_order_relaxed);
	}

	void DisconnectAsync(UINT32 expectedGeneration)
	{
		// 소켓을 끊기 직전 마지막 세대 검사 (Timeout Snipe 방지)
		if (mGeneration.load(std::memory_order_acquire) != expectedGeneration)
		{
			LOG_DEBUG("[Timeout] Snipe 방어 성공! (세대 변경됨)\n");
			return;
		}
		if (m_socketClient != INVALID_SOCKET)
		{
			shutdown(m_socketClient, SD_BOTH);
		}
	}

	// true 반환시 내가 닫는 스레드, false면 다른 스레드가 이미 닫음
	bool TryMarkDisconnected()
	{
		return mIsConnected.exchange(false, std::memory_order_acq_rel);
	}

	void AddRef() 
	{ 
		mRefCount.fetch_add(1, std::memory_order_relaxed);
	}

	// true면 내가 마지막 스레드 -> 세션 반납 책임
	bool ReleaseRef()
	{
		return mRefCount.fetch_sub(1, std::memory_order_acq_rel) == 1;
	}

	int GetRefCount() const
	{
		return mRefCount.load(std::memory_order_acquire);
	}

	void CloseAcceptSocket()
	{
		if (m_socketClient != INVALID_SOCKET)
		{
			closesocket(m_socketClient);
			m_socketClient = INVALID_SOCKET;
		}
	}

	//bool mAcceptPendingg = false;
private:
	UINT32			mIndex = 0;				// Client의 index
	SOCKET			m_socketClient;			// Client와 연결되는 소켓
	stOverlappedEx	m_stRecvOverlappedEx;	// RECV Overlapped I/O 작업을 위한 변수
	stOverlappedEx	m_stSendOverlappedEx;	// SEND Overlapped I/O 작업을 위한 변수
	stOverlappedEx	m_stAcceptOverlappedEx;	// Accept를 요청하고 IOCP 컴플리션에서 완료를 확인하기 위한 구조체
	stOverlappedEx mAcceptContext;
	std::mutex mSendLock;
	bool mIsSending = false;
	UINT64 mSendPos = 0; // SendBuffer의 쓰기위치 관리 변수
	HANDLE mIOCPHandle = INVALID_HANDLE_VALUE;
	ObjectPool<SendOverlappedEx>* mSendPool = nullptr;
	//bool mAcceptPending = false;

	char mRecvBuf[MAX_SOCKBUF];	// 수신용 버퍼
	char mSendBuf[MAX_SOCKBUF]; // 송신용 버퍼
	char mSendingBuf[MAX_SOCK_SENDBUF];
	//std::queue<stOverlappedEx*> mSendDataqueue;
	std::queue<SendOverlappedEx*> mSendDataqueue;
	//bool mIsConnected = false;			// Client의 접속 요청을 했는지 확인하는 변수
	std::atomic<bool>mIsConnected{ false };
	char mAcceptbuf[128];				// AcceptEx의 3번째 인자로 넘겨줄 버퍼
	UINT64 mLatestClosedTimeSec = 0;		// 마지막으로 소켓이 닫혔던 시간

	//UINT32 mGeneration = 0;
	std::atomic<UINT32> mGeneration{ 0 };
	std::atomic<int> mRefCount{ 0 };

	std::atomic<ULONGLONG> mLastActivityTime{ 0 };
	std::atomic<ULONGLONG> mLastPingTime{ 0 };


};