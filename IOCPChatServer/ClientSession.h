#pragma once
#pragma comment(lib, "mswsock.lib")

#include "Define.h"
#include <MSWSock.h>
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

	//char* SendBuff()
	//{
	//	return mSendBuf;
	//}

	//UINT64 GetLatestClosedTimeSec()
	//{
	//	return mLatestClosedTimeSec;
	//}


	bool OnConnect(HANDLE iocpHandle, SOCKET socket);

	void Closed(bool bIsForced = false);

	void Clear();

	// WSASend Overlapped I/O 작업을 수행
	bool SendMsg(const UINT32 dataSize, char* pMsg);

	bool BindRecv();

	// CompletionPort 객체와 소켓과 CompletionKey를 연결시키는 연결용 함수
	bool BindIOCompletionPort(HANDLE iocpHandle);

	bool SendIO();

	void SendComplete(const UINT32 dataSize_);

	bool AcceptCompletion(SOCKET listenSock_);

	//bool PostAccept(SOCKET listenSock, const UINT64 curTimeSec);

	bool PostImmediateAccept(SOCKET listenSock);

	bool SetSockOption();

	void UpdateActivity()
	{
		ULONGLONG now = GetTickCount64();
		mLastActivityTime.store(now, std::memory_order_relaxed);
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

	void DisconnectAsync(UINT32 expectedGeneration);

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
	//stOverlappedEx	m_stSendOverlappedEx;	// SEND Overlapped I/O 작업을 위한 변수 // -> send가 풀 기반으로 변경됨
	//stOverlappedEx	m_stAcceptOverlappedEx;	// Accept를 요청하고 IOCP 컴플리션에서 완료를 확인하기 위한 구조체, mAcceptContext가 대체
	stOverlappedEx mAcceptContext;
	std::mutex mSendLock;
	bool mIsSending = false;
	UINT64 mSendPos = 0; // SendBuffer의 쓰기위치 관리 변수
	HANDLE mIOCPHandle = INVALID_HANDLE_VALUE;
	ObjectPool<SendOverlappedEx>* mSendPool = nullptr;
	//bool mAcceptPending = false;

	char mRecvBuf[MAX_SOCKBUF];	// 수신용 버퍼
	//char mSendBuf[MAX_SOCKBUF]; // 송신용 버퍼 // 풀 기반 전송으로 대체
	//char mSendingBuf[MAX_SOCK_SENDBUF];	// 1-send 방식 미사용
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