#pragma once
#include <winsock2.h>
#include <Ws2tcpip.h>
#include <mswsock.h>
#include <cstdint>
const UINT32 MAX_SOCKBUF = 1024;
//const UINT32 MAX_SOCK_SENDBUF = 4096;
//const UINT32 MAX_SEND_QUEUE_DEPTH = 128;	// 클라이언트당 최대 Send 큐 깊이
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
	ACCEPT,
	ZOMBIE_CLEANUP
};

// 1. 공통 헤더
struct OverlappedBase
{
	WSAOVERLAPPED   wsaOverlapped;          // offset 0 - 반드시 첫 번째
	WSABUF          wsaBuf;
	IOOperation     operation;
	UINT32          clientSessionIndex = 0;
	UINT32          generation = 0;
};

// 2. RECV / ACCEPT / ZOMBIE_CLEANUP 용,  공통 헤더만 포함
struct stOverlappedEx
{
    OverlappedBase  base;                   // 첫 번째 멤버
};

// 3. SEND 전용 - 공통 헤더 + 전용 필드
struct SendOverlappedEx
{
    OverlappedBase  base;                   // 첫 번째 멤버 (offset 0)
    char            buffer[MAX_SOCKBUF];
    uint32_t        poolNext = UINT32_MAX;
};

