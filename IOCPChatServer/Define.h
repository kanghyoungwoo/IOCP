#pragma once
#include <winsock2.h>
#include <Ws2tcpip.h>


#define MAX_SOCKBUF 1024	// 패킷 크기
#define MAX_WORKERTHREAD 4	// 쓰레드 풀에 넣을 쓰레드 수

enum class IOOperation
{
	RECV,
	SEND
};

//WSAOVERLAPPED 구조체를 확장 시켜 필요한 정보를 더 넣음
typedef struct _stOverlappedEx
{
	WSAOVERLAPPED	m_wsaOverlapped;		// Overlapped I/O 구조체
	SOCKET			m_socketClient;			// Client 소켓
	WSABUF			m_wsaBuf;				// Overlapped I/O작업 버퍼
	IOOperation		m_eOperation;			// 작업 동작 종류
}stOverlappedEx;


// 클라이언트 정보를 담기 위한 구조체
typedef struct _stClientInfo
{
	int mIndex = 0;							// Client의 index
	SOCKET			m_socketClient;			// Client와 연결되는 소켓
	stOverlappedEx	m_stRecvOverlappedEx;	// RECV Overlapped I/O 작업을 위한 변수
	stOverlappedEx	m_stSendOverlappedEx;	// SEND Overlapped I/O 작업을 위한 변수

	char mRecvBuf[MAX_SOCKBUF];	// 데이터 버퍼
	char mSendBuf[MAX_SOCKBUF]; // 데이터 버퍼
	
	_stClientInfo()
	{
		ZeroMemory(&m_stRecvOverlappedEx, sizeof(_stOverlappedEx));
		ZeroMemory(&m_stSendOverlappedEx, sizeof(_stOverlappedEx));
		m_socketClient = INVALID_SOCKET;
	}
}stClientInfo;