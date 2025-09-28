#pragma once
#pragma comment(lib, "mswsock.lib")

#include <MSWSock.h>
#include "Define.h"
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

	void Init(const UINT32 index, HANDLE iocpHandle)
	{
		mIndex = index;
		mIOCPHandle = iocpHandle;
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

		//Clear();
		if (BindIOCompletionPort(iocpHandle) == false)
		{
			return false;
		}
		return BindRecv();
	}

	void Closed(bool bIsForced = false)
	{
		// SO_LINGER를 사용하면 소켓을 close 했을 때 전송되지 않은 데이터를 어떻게 처리할 것인지 조정함
		struct linger stLinger = { 0,0 };	//SO_DONTLINGER로 설정

		// bIsForce가 true면 SO_LINGER, timeout = 0으로 설정하여 강제 종료시킴
		if (bIsForced == true)
		{
			stLinger.l_onoff = 1;
		}

		// socketClose소켓의 데이터 송수신을 모두 중단
		shutdown(m_socketClient, SD_BOTH);

		// 소켓 옵션을 설정
		setsockopt(m_socketClient, SOL_SOCKET, SO_LINGER, (char*)&stLinger, sizeof(stLinger));

		mIsConnected = false;

		mLatestClosedTimeSec = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now().time_since_epoch()).count();

		// 소켓 연결을 종료
		closesocket(m_socketClient);

		m_socketClient = INVALID_SOCKET;
	}

	void Clear()
	{
		// 송신 큐 정리
		std::lock_guard<std::mutex> gurad(mSendLock);

		while (!mSendDataqueue.empty())
		{
			delete[] mSendDataqueue.front()->m_wsaBuf.buf;
			delete mSendDataqueue.front();
			mSendDataqueue.pop();
		}
		
		mSendPos = 0;
		mIsSending = false;

		// 버퍼 초기화 
		ZeroMemory(mRecvBuf, sizeof(mRecvBuf));
		ZeroMemory(mSendBuf, sizeof(mSendBuf));

	}

	// WSASend Overlapped I/O 작업을 시작
	bool SendMsg(const UINT32 dataSize, char* pMsg)
	{
		// overlapped 구조체 생성
		auto sendOverlappedEx = new stOverlappedEx;
		ZeroMemory(sendOverlappedEx, sizeof(stOverlappedEx));
		sendOverlappedEx->m_wsaBuf.len = dataSize;
		sendOverlappedEx->m_wsaBuf.buf = new char[dataSize];
		CopyMemory(sendOverlappedEx->m_wsaBuf.buf, pMsg, dataSize);
		sendOverlappedEx->m_eOperation = IOOperation::SEND;

		std::lock_guard<std::mutex> guard(mSendLock);
		mSendDataqueue.push(sendOverlappedEx);
		// 데이터가 1개라면 앞에 데이터가 없으니 바로 wsasend
		if (mSendDataqueue.size() == 1)
		{
			SendIO();
		}

		// buffer를 이용한 1-send
		/*
		std::lock_guard<std::mutex>guard(mSendLock);
		if ((mSendPos + dataSize) > MAX_SOCK_SENDBUF)
		{
			mSendPos = 0;
		}
		auto pSendBuf = &mSendBuf[mSendPos];

		// 전송 메세지 복사
		CopyMemory(pSendBuf, pMsg, dataSize);
		mSendPos += dataSize;
		*/


		return true;
	}

	bool BindRecv()
	{
		DWORD dwFlag = 0;
		DWORD dwRecvNumBytes = 0;

		// Overlapped I/O를 위해 각 정보를 세팅
		m_stRecvOverlappedEx.m_wsaBuf.len = MAX_SOCKBUF;
		m_stRecvOverlappedEx.m_wsaBuf.buf = mRecvBuf;
		m_stRecvOverlappedEx.m_eOperation = IOOperation::RECV;

		int nRet = WSARecv(m_socketClient,
			&(m_stRecvOverlappedEx.m_wsaBuf),
			1,
			&dwRecvNumBytes,
			&dwFlag,
			(LPWSAOVERLAPPED) & (m_stRecvOverlappedEx),
			NULL);

		// socket_error 면 client socket이 끊어진것으로 처리
		if (nRet == SOCKET_ERROR && (WSAGetLastError() != ERROR_IO_PENDING))
		{
			printf("[ERROR] WSARecv() 실패 : %d\n", WSAGetLastError());
			Closed(true); // 연결 강제 해제
			return false;
		}
		printf("bind recv 성공\n");
		return true;
	}

	// CompletionPort객체와 소켓과 CompletionKey를 연결시키는 역할을 함
	bool BindIOCompletionPort(HANDLE iocpHandle)
	{
		// socket과 pClientInfo를 CompletionPort객체와 연결시킴
		auto hIOCP = CreateIoCompletionPort((HANDLE)GetSocket()
			, iocpHandle
			, (ULONG_PTR)(this), 0);

		if (hIOCP == INVALID_HANDLE_VALUE)
		{
			printf("[ERROR] CreateIoCompletionPort() 실패 : %d\n", GetLastError());
			return false;
		}
		printf("BindIOCompletionport 성공!\n");

		return true;
	}

	bool SendIO()
	{
		auto sendOverlappedEx = mSendDataqueue.front();
		DWORD dwRecvNumBytes = 0;
		int nRet = WSASend(
			m_socketClient,
			&(sendOverlappedEx->m_wsaBuf),
			1,
			&dwRecvNumBytes,
			0,
			(LPWSAOVERLAPPED)sendOverlappedEx,
			NULL
		);

		if (nRet == SOCKET_ERROR && (WSAGetLastError() != ERROR_IO_PENDING))
		{
			printf("[ERROR] WSASend() 실패 : %d\n", WSAGetLastError());
			return false;
		}

		// buffer방식의 1-send
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

		// socket_error면 client socket이 끊어진 것으로 처리
		if (nRet == SOCKET_ERROR && (WSAGetLastError() != ERROR_IO_PENDING))
		{
			printf("[ERROR] WSASend() 실패 : %d\n", WSAGetLastError());
			return false;
		}
		mSendPos = 0;
		*/

		return true;
	}

	void SendComplete(const UINT32 dataSize_)
	{
		// buffer방식을 이용한 1-send
		/*
		mIsSending = false;
		*/
		printf("[송신 완료] bytes : %d\n", dataSize_);
		std::lock_guard<std::mutex> guard(mSendLock);
		delete[] mSendDataqueue.front()->m_wsaBuf.buf;
		delete mSendDataqueue.front();
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
			printf("[ERROR] SO_UPDATE_ACCEPT_CONTEXT 실패 : %d \n",WSAGetLastError());
			return false;
		}


		printf("AcceptCompletion : SessionIndex(%d)\n", mIndex);

		if (OnConnect(mIOCPHandle, m_socketClient) == false)
		{
			return false;
		}
		SOCKADDR_IN stClientAddr;
		int nAddrLen = sizeof(SOCKADDR_IN);
		char clientIP[32] = { 0 };
		inet_ntop(AF_INET, &(stClientAddr.sin_addr), clientIP, 32 - 1);
		printf("Client IP : %s, SOCKET(%d)\n", clientIP, (int)m_socketClient);

		//mAcceptPendingg = false;

		return true;
	}

	bool PostAccept(SOCKET listenSock, const UINT64 curTimeSec)
	{
		printf("PostAccept Client Index : %d\n", GetIndex());

		mLatestClosedTimeSec = UINT32_MAX;
		m_socketClient = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_IP, NULL, 0, WSA_FLAG_OVERLAPPED);
		if (m_socketClient == INVALID_SOCKET)
		{
			printf("Client Socket Error : %d \n", GetLastError());
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

			{printf("[ERROR] AcceptEx 실패! 에러 코드: %d\n", GetLastError());

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
			printf("Client Socket Error : %d \n", GetLastError());
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
				printf("[ERROR] AcceptEx 실패! 에러 코드: %d\n", GetLastError());

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

	//bool mAcceptPendingg = false;
private:
	UINT32			mIndex = 0;				// Client의 index
	SOCKET			m_socketClient;			// Client와 연결되는 소켓
	stOverlappedEx	m_stRecvOverlappedEx;	// RECV Overlapped I/O 작업을 위한 변수
	stOverlappedEx	m_stSendOverlappedEx;	// SEND Overlapped I/O 작업을 위한 변수
	stOverlappedEx	m_stAcceptOverlappedEx;	// Accept를 요청하고 IOCP 오브젝트에서 완료를 확인하기 위한 구조체
	stOverlappedEx mAcceptContext;
	std::mutex mSendLock;
	bool mIsSending = false;
	UINT64 mSendPos = 0; // SendBuffer의 시작위치 지정 변수
	HANDLE mIOCPHandle = INVALID_HANDLE_VALUE;

	//bool mAcceptPending = false;
	
	char mRecvBuf[MAX_SOCKBUF];	// 데이터 버퍼
	char mSendBuf[MAX_SOCKBUF]; // 데이터 버퍼
	char mSendingBuf[MAX_SOCK_SENDBUF];
	std::queue<stOverlappedEx*> mSendDataqueue;
	bool mIsConnected = false;			// Client가 접속 요청을 했는지 확인하는 변수
	char mAcceptbuf[128];				// AcceptEx의 3번째 인자로 넘겨줄 버퍼
	UINT64 mLatestClosedTimeSec = 0;		// 마지막으로 연결이 종료된 시간
	
};