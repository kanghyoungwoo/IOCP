#pragma once
#include <winsock2.h>
#include <Ws2tcpip.h>


//#define MAX_SOCKBUF 1024	// 패킷 크기
const UINT32 MAX_SOCKBUF = 1024;
const UINT32 MAX_SOCK_SENDBUF = 4096;	// 소켓 버퍼 크기
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


