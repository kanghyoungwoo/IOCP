#pragma once

#include "Define.h"
#include <stdio.h>
#include <mutex>
#include <queue>

//// Ŭ���̾�Ʈ ������ ��� ���� ����ü
//typedef struct _stClientInfo
//{
//	int mIndex = 0;							// Client�� index
//	SOCKET			m_socketClient;			// Client�� ����Ǵ� ����
//	stOverlappedEx	m_stRecvOverlappedEx;	// RECV Overlapped I/O �۾��� ���� ����
//	stOverlappedEx	m_stSendOverlappedEx;	// SEND Overlapped I/O �۾��� ���� ����
//
//	char mRecvBuf[MAX_SOCKBUF];	// ������ ����
//	char mSendBuf[MAX_SOCKBUF]; // ������ ����
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

		// ���� ������ ����
		closesocket(m_socketClient);

		m_socketClient = INVALID_SOCKET;
	}

	void Clear()
	{
		mSendPos = 0;
		mIsSending = false;
	}

	// WSASend Overlapped I/O �۾��� ����
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
		DWORD dwRecvNumByter = 0;

		// Overlapped I/O�� ���� �� ������ ����
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

		// socket_error �� client socket�� ������������ ó��
		if (nRet == SOCKET_ERROR && (WSAGetLastError() != ERROR_IO_PENDING))
		{
			printf("[ERROR] WSARecv() ���� : %d\n", WSAGetLastError());
			return false;
		}
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
			printf("[ERROR] CreateIoCompletionPort() ���� : %d\n", GetLastError());
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
			printf("[ERROR] WSASend() ���� : %d\n", WSAGetLastError());
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
			printf("[ERROR] WSASend() ���� : %d\n", WSAGetLastError());
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
		printf("[�۽� �Ϸ�] bytes : %d\n", dataSize_);
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
	UINT32 mIndex = 0;							// Client�� index
	SOCKET			m_socketClient;			// Client�� ����Ǵ� ����
	stOverlappedEx	m_stRecvOverlappedEx;	// RECV Overlapped I/O �۾��� ���� ����
	stOverlappedEx	m_stSendOverlappedEx;	// SEND Overlapped I/O �۾��� ���� ����
	std::mutex mSendLock;
	bool mIsSending = false;
	UINT64 mSendPos = 0; // SendBuffer�� ������ġ ���� ����
	
	char mRecvBuf[MAX_SOCKBUF];	// ������ ����
	char mSendBuf[MAX_SOCKBUF]; // ������ ����
	char mSendingBuf[MAX_SOCK_SENDBUF];
	std::queue<stOverlappedEx*> mSendDataqueue;
};