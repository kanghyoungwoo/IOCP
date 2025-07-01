#pragma once
#include <winsock2.h>
#include <Ws2tcpip.h>


#define MAX_SOCKBUF 1024	// ��Ŷ ũ��
#define MAX_WORKERTHREAD 4	// ������ Ǯ�� ���� ������ ��

enum class IOOperation
{
	RECV,
	SEND
};

//WSAOVERLAPPED ����ü�� Ȯ�� ���� �ʿ��� ������ �� ����
typedef struct _stOverlappedEx
{
	WSAOVERLAPPED	m_wsaOverlapped;		// Overlapped I/O ����ü
	SOCKET			m_socketClient;			// Client ����
	WSABUF			m_wsaBuf;				// Overlapped I/O�۾� ����
	IOOperation		m_eOperation;			// �۾� ���� ����
}stOverlappedEx;


// Ŭ���̾�Ʈ ������ ��� ���� ����ü
typedef struct _stClientInfo
{
	int mIndex = 0;							// Client�� index
	SOCKET			m_socketClient;			// Client�� ����Ǵ� ����
	stOverlappedEx	m_stRecvOverlappedEx;	// RECV Overlapped I/O �۾��� ���� ����
	stOverlappedEx	m_stSendOverlappedEx;	// SEND Overlapped I/O �۾��� ���� ����

	char mRecvBuf[MAX_SOCKBUF];	// ������ ����
	char mSendBuf[MAX_SOCKBUF]; // ������ ����
	
	_stClientInfo()
	{
		ZeroMemory(&m_stRecvOverlappedEx, sizeof(_stOverlappedEx));
		ZeroMemory(&m_stSendOverlappedEx, sizeof(_stOverlappedEx));
		m_socketClient = INVALID_SOCKET;
	}
}stClientInfo;