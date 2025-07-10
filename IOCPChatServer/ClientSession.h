#pragma once

#include "Define.h"
#include <stdio.h>
#include <mutex>
#include <queue>

//// 클라이언트 정보를 담기 위한 구조체
//typedef struct _stClientInfo
//{
//	int mIndex = 0;							// Client의 index
//	SOCKET			m_socketClient;			// Client와 연결되는 소켓
//	stOverlappedEx	m_stRecvOverlappedEx;	// RECV Overlapped I/O 작업을 위한 변수
//	stOverlappedEx	m_stSendOverlappedEx;	// SEND Overlapped I/O 작업을 위한 변수
//
//	char mRecvBuf[MAX_SOCKBUF];	// 데이터 버퍼
//	char mSendBuf[MAX_SOCKBUF]; // 데이터 버퍼
//
//	_stClientInfo()
//	{
//		ZeroMemory(&m_stRecvOverlappedEx, sizeof(_stOverlappedEx));
//		ZeroMemory(&m_stSendOverlappedEx, sizeof(_stOverlappedEx));
//		m_socketClient = INVALID_SOCKET;
//	}
//}stClientInfo;

class ClientSession
{
public:
	ClientSession()
	{
		ZeroMemory(&m_stRecvOverlappedEx, sizeof(_stOverlappedEx));
		ZeroMemory(&m_stSendOverlappedEx, sizeof(_stOverlappedEx));
		m_socketClient = INVALID_SOCKET;
	}

	void Init(const UINT32 index)
	{
		mIndex = index;
	}

	UINT32 GetIndex()
	{
		return mIndex;
	}

	bool IsConnected()
	{
		return m_socketClient != INVALID_SOCKET;
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

	bool OnConnect(HANDLE iocpHandle, SOCKET socket)
	{
		m_socketClient = socket;
		if (BindIOCompletionPort(iocpHandle) == false)
		{
			return false;
		}
		return BindRecv();
	}

	void Closed(bool bIsForced)
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

		// 소켓 연결을 종료
		closesocket(m_socketClient);

		m_socketClient = INVALID_SOCKET;
	}

	void Clear()
	{
		mSendPos = 0;
		mIsSending = false;
	}

	// WSASend Overlapped I/O 작업을 시작
	bool SendMsg(const UINT32 dataSize, char* pMsg)
	{
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
		DWORD dwRecvNumByter = 0;

		// Overlapped I/O를 위해 각 정보를 세팅
		m_stRecvOverlappedEx.m_wsaBuf.len = MAX_SOCKBUF;
		m_stRecvOverlappedEx.m_wsaBuf.buf = mRecvBuf;
		m_stRecvOverlappedEx.m_eOperation = IOOperation::RECV;

		int nRet = WSARecv(m_socketClient,
			&(m_stRecvOverlappedEx.m_wsaBuf),
			1,
			&dwRecvNumByter,
			&dwFlag,
			(LPWSAOVERLAPPED) & (m_stRecvOverlappedEx),
			NULL);

		// socket_error 면 client socket이 끊어진것으로 처리
		if (nRet == SOCKET_ERROR && (WSAGetLastError() != ERROR_IO_PENDING))
		{
			printf("[ERROR] WSARecv() 실패 : %d\n", WSAGetLastError());
			return false;
		}
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

private:
	UINT32 mIndex = 0;							// Client의 index
	SOCKET			m_socketClient;			// Client와 연결되는 소켓
	stOverlappedEx	m_stRecvOverlappedEx;	// RECV Overlapped I/O 작업을 위한 변수
	stOverlappedEx	m_stSendOverlappedEx;	// SEND Overlapped I/O 작업을 위한 변수
	std::mutex mSendLock;
	bool mIsSending = false;
	UINT64 mSendPos = 0; // SendBuffer의 시작위치 지정 변수
	
	char mRecvBuf[MAX_SOCKBUF];	// 데이터 버퍼
	char mSendBuf[MAX_SOCKBUF]; // 데이터 버퍼
	char mSendingBuf[MAX_SOCK_SENDBUF];
	std::queue<stOverlappedEx*> mSendDataqueue;
};