#pragma once
#include <winsock2.h>
#include <Ws2tcpip.h>
#include <mswsock.h>

//#define MAX_SOCKBUF 1024
const UINT32 MAX_SOCKBUF = 1024;
const UINT32 MAX_SOCK_SENDBUF = 4096;	
const UINT64 RE_USE_SESSION_WAIT_TIMESEC = 3; 
#define MAX_WORKERTHREAD 4
//#define USE_AMAZON_AWS_DB // 주석처리 로컬모드

// Release 빌드 시 디버그 출력 제거
#ifdef _DEBUG
#define LOG_DEBUG(fmt, ...) printf(fmt, ##__VA_ARGS__)
#else
#define LOG_DEBUG(fmt, ...)
#endif

// 에러 메시지는 항상 출력
#define LOG_ERROR(fmt, ...) printf("[ERROR] " fmt, ##__VA_ARGS__)

// 반복 에러는 최초 1회만 출력
#define LOG_ERROR_ONCE(fmt, ...) \
    do { \
        static bool _once = false; \
        if (!_once) { \
            _once = true; \
            printf("[ERROR] " fmt, ##__VA_ARGS__); \
        } \
    } while(0)

enum class IOOperation
{
	RECV,
	SEND,
	ACCEPT
};

//WSAOVERLAPPED 
typedef struct _stOverlappedEx
{
	WSAOVERLAPPED	m_wsaOverlapped;		// Overlapped I/O
	//SOCKET			m_socketClient;		// Client
	WSABUF			m_wsaBuf;				// Overlapped I/O
	IOOperation		m_eOperation;			// 
	UINT32			clientSessionIndex = 0;	//추가
	UINT32			generation = 0;
}stOverlappedEx;

// Send 전용 통합 구조체: Overlapped + 데이터 버퍼를 하나로 합침
// 메모리 풀에서 이 단위로 할당/반납하므로 힙 할당이 발생하지 않는다.
struct SendOverlappedEx
{
	WSAOVERLAPPED	wsaOverlapped;
	WSABUF			wsaBuf;
	IOOperation		operation;
	UINT32			sessionIndex = 0;
	UINT32			generation = 0;			// 추가
	char			buffer[MAX_SOCKBUF];	// 고정 크기 내장 버퍼
};


