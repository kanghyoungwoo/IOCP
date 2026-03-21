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
		//return m_socketClient != INVALID_SOCKET;
		return mIsConnected;
	}

	SOCKET GetSocket()
	{
		return m_socketClient;
	}

	UINT32 GetGeneration() const 
	{ 
		return mGeneration;
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
		mIsConnected = true;
		++mGeneration;

		// ���� ���� Ÿ�Ӿƿ� ������ ���� �ð� ����
		UpdateActivity();

		//Clear();
		if (BindIOCompletionPort(iocpHandle) == false)
		{
			return false;
		}
		return BindRecv();
	}

	void Closed(bool bIsForced = false)
	{
		// SO_LINGER�� ����ϸ� ������ close ���� �� ���۵��� ���� �����͸� ��� ó���� ������ ������
		struct linger stLinger = { 0,0 };	//SO_DONTLINGER�� ����

		// bIsForce�� true�� SO_LINGER, timeout = 0���� �����Ͽ� ���� �����Ŵ
		if (bIsForced == true)
		{
			stLinger.l_onoff = 1;
		}

		// socketClose������ ������ �ۼ����� ��� �ߴ�
		shutdown(m_socketClient, SD_BOTH);

		// ���� �ɼ��� ����
		setsockopt(m_socketClient, SOL_SOCKET, SO_LINGER, (char*)&stLinger, sizeof(stLinger));

		mIsConnected = false;

		mLatestClosedTimeSec = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now().time_since_epoch()).count();

		// ���� ������ ����
		closesocket(m_socketClient);

		m_socketClient = INVALID_SOCKET;
	}

	void Clear()
	{
		// �۽� ť ����
		std::lock_guard<std::mutex> gurad(mSendLock);

		while (!mSendDataqueue.empty())
		{
			//delete[] mSendDataqueue.front()->m_wsaBuf.buf;
			//delete mSendDataqueue.front();
			//mSendDataqueue.pop();
			mSendPool->Free(mSendDataqueue.front());	// Ǯ�� �ݳ�
			mSendDataqueue.pop();
		}
		
		mSendPos = 0;
		mIsSending = false;

		// ���� �ʱ�ȭ 
		ZeroMemory(mRecvBuf, sizeof(mRecvBuf));
		ZeroMemory(mSendBuf, sizeof(mSendBuf));

	}

	// WSASend Overlapped I/O �۾��� ����
	bool SendMsg(const UINT32 dataSize, char* pMsg)
	{
		// Ǯ���� SendOverlappedEx �ϳ��� ������ (�� �Ҵ� ����)
		auto pSendOvl = mSendPool->Alloc();
		if (pSendOvl == nullptr)
		{
			// ������
			mSendPool->IncrementAllocFail();
			LOG_ERROR_ONCE("SendPool 소진! dataSize=%d\n", dataSize);
			return false;
		}
		ZeroMemory(&pSendOvl->wsaOverlapped, sizeof(WSAOVERLAPPED));
		pSendOvl->wsaBuf.len = dataSize;
		pSendOvl->wsaBuf.buf = pSendOvl->buffer;	// ���� ���۸� ����Ŵ
		CopyMemory(pSendOvl->buffer, pMsg, dataSize);
		pSendOvl->operation = IOOperation::SEND;
		pSendOvl->generation = mGeneration;

		std::lock_guard<std::mutex> guard(mSendLock);
		mSendDataqueue.push(pSendOvl);

		//// overlapped ����ü ����
		//auto sendOverlappedEx = new stOverlappedEx;
		//ZeroMemory(sendOverlappedEx, sizeof(stOverlappedEx));
		//sendOverlappedEx->m_wsaBuf.len = dataSize;
		//sendOverlappedEx->m_wsaBuf.buf = new char[dataSize];
		//CopyMemory(sendOverlappedEx->m_wsaBuf.buf, pMsg, dataSize);
		//sendOverlappedEx->m_eOperation = IOOperation::SEND;

		//std::lock_guard<std::mutex> guard(mSendLock);
		//mSendDataqueue.push(sendOverlappedEx);
		
		// �����Ͱ� 1����� �տ� �����Ͱ� ������ �ٷ� wsasend
		if (mSendDataqueue.size() == 1)
		{
			SendIO();
		}

		// buffer�� �̿��� 1-send
		/*
		std::lock_guard<std::mutex>guard(mSendLock);
		if ((mSendPos + dataSize) > MAX_SOCK_SENDBUF)
		{
			mSendPos = 0;
		}
		auto pSendBuf = &mSendBuf[mSendPos];

		// ���� �޼��� ����
		CopyMemory(pSendBuf, pMsg, dataSize);
		mSendPos += dataSize;
		*/


		return true;
	}

	bool BindRecv()
	{
		DWORD dwFlag = 0;
		DWORD dwRecvNumBytes = 0;

		// Overlapped I/O�� ���� �� ������ ����
		m_stRecvOverlappedEx.m_wsaBuf.len = MAX_SOCKBUF;
		m_stRecvOverlappedEx.m_wsaBuf.buf = mRecvBuf;
		m_stRecvOverlappedEx.m_eOperation = IOOperation::RECV;
		m_stRecvOverlappedEx.generation = mGeneration;

		int nRet = WSARecv(m_socketClient,
			&(m_stRecvOverlappedEx.m_wsaBuf),
			1,
			&dwRecvNumBytes,
			&dwFlag,
			(LPWSAOVERLAPPED) & (m_stRecvOverlappedEx),
			NULL);

		// socket_error �� client socket�� ������������ ó��
		if (nRet == SOCKET_ERROR && (WSAGetLastError() != ERROR_IO_PENDING))
		{
			LOG_ERROR("WSARecv() 실패 : %d\n", WSAGetLastError());
			Closed(true); // ���� ���� ����
			return false;
		}
		LOG_DEBUG("bind recv 성공\n");
		return true;
	}

	// CompletionPort��ü�� ���ϰ� CompletionKey�� �����Ű�� ������ ��
	bool BindIOCompletionPort(HANDLE iocpHandle)
	{
		// socket�� pClientInfo�� CompletionPort��ü�� �����Ŵ
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
			LOG_ERROR_ONCE("WSASend() 실패 : %d\n", WSAGetLastError());
			return false;
		}

		// buffer����� 1-send
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

		// socket_error�� client socket�� ������ ������ ó��
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
		// buffer����� �̿��� 1-send
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
			SendIO();
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
		// Ŭ���̾�Ʈ�� ���� Ȱ�� ���̹Ƿ� ping ���� ��ϵ� ����
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

	void DisconnectAsync()
	{
		if (m_socketClient != INVALID_SOCKET)
		{
			shutdown(m_socketClient, SD_BOTH);
		}
	}

	//bool mAcceptPendingg = false;
private:
	UINT32			mIndex = 0;				// Client�� index
	SOCKET			m_socketClient;			// Client�� ����Ǵ� ����
	stOverlappedEx	m_stRecvOverlappedEx;	// RECV Overlapped I/O �۾��� ���� ����
	stOverlappedEx	m_stSendOverlappedEx;	// SEND Overlapped I/O �۾��� ���� ����
	stOverlappedEx	m_stAcceptOverlappedEx;	// Accept�� ��û�ϰ� IOCP ������Ʈ���� �ϷḦ Ȯ���ϱ� ���� ����ü
	stOverlappedEx mAcceptContext;
	std::mutex mSendLock;
	bool mIsSending = false;
	UINT64 mSendPos = 0; // SendBuffer�� ������ġ ���� ����
	HANDLE mIOCPHandle = INVALID_HANDLE_VALUE;
	ObjectPool<SendOverlappedEx>* mSendPool = nullptr;
	//bool mAcceptPending = false;
	
	char mRecvBuf[MAX_SOCKBUF];	// ������ ����
	char mSendBuf[MAX_SOCKBUF]; // ������ ����
	char mSendingBuf[MAX_SOCK_SENDBUF];
	//std::queue<stOverlappedEx*> mSendDataqueue;
	std::queue<SendOverlappedEx*> mSendDataqueue;
	bool mIsConnected = false;			// Client�� ���� ��û�� �ߴ��� Ȯ���ϴ� ����
	char mAcceptbuf[128];				// AcceptEx�� 3��° ���ڷ� �Ѱ��� ����
	UINT64 mLatestClosedTimeSec = 0;		// ���������� ������ ����� �ð�
	
	UINT32 mGeneration = 0;

	std::atomic<ULONGLONG> mLastActivityTime{ 0 };
	std::atomic<ULONGLONG> mLastPingTime{ 0 };
};