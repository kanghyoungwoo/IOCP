#pragma once
#include <winsock2.h>
#include <Ws2tcpip.h>
#include <mswsock.h>
#include <cstdint>
#include <cstdio>

#include <plog/Log.h>

const UINT32 MAX_SOCKBUF = 1024;
//const UINT32 MAX_SOCK_SENDBUF = 4096;
//const UINT32 MAX_SEND_QUEUE_DEPTH = 128;	// 클라이언트당 최대 Send 큐 깊이
//#define USE_AMAZON_AWS_DB // 주석처리 로컬모드

// ─────────────────────────────────────────────────────────────────────────
//  로깅 매크로 (plog 백엔드)
//  · 기존 printf 스타일 호출부를 그대로 유지
//  · 출력: 콘솔(레벨별 색상) + server.log(롤링 파일)
//  · 초기화: main() 에서 plog::init() 호출 필요
// ─────────────────────────────────────────────────────────────────────────

// plog/Log.h 가 LOG_DEBUG/INFO/ERROR 를 스트림 스타일로 먼저 정의함
// printf 스타일 래퍼로 교체하기 위해 undef
#undef LOG_VERBOSE
#undef LOG_DEBUG
#undef LOG_INFO
#undef LOG_WARNING
#undef LOG_ERROR
#undef LOG_FATAL

// 내부 헬퍼: printf 스타일 → 문자열, 마지막 개행 제거
#define _LOG_FMT(buf, fmt, ...) \
    do { \
        int _n = snprintf(buf, sizeof(buf), fmt, ##__VA_ARGS__); \
        if (_n > 0 && _n < (int)sizeof(buf) && buf[_n - 1] == '\n') \
            buf[_n - 1] = '\0'; \
    } while(0)

// DEBUG: _DEBUG 빌드에서만 출력
#ifdef _DEBUG
#define LOG_DEBUG(fmt, ...) \
    do { char _lb[2048]; _LOG_FMT(_lb, fmt, ##__VA_ARGS__); PLOG_DEBUG << _lb; } while(0)
#else
#define LOG_DEBUG(fmt, ...)
#endif

// INFO: 항상 출력 (일반 운영 메시지)
#define LOG_INFO(fmt, ...) \
    do { char _lb[2048]; _LOG_FMT(_lb, fmt, ##__VA_ARGS__); PLOG_INFO << _lb; } while(0)

// ERROR: 항상 출력
#define LOG_ERROR(fmt, ...) \
    do { char _lb[2048]; _LOG_FMT(_lb, fmt, ##__VA_ARGS__); PLOG_ERROR << _lb; } while(0)

// ERROR_ONCE: 최초 1회만 출력
#define LOG_ERROR_ONCE(fmt, ...) \
    do { \
        static bool _once = false; \
        if (!_once) { \
            _once = true; \
            char _lb[2048]; \
            _LOG_FMT(_lb, fmt, ##__VA_ARGS__); \
            PLOG_ERROR << _lb; \
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

