#pragma once
// ================================================================
// Logger.h - 조건부 컴파일을 이용한 로깅 시스템
// ================================================================

#include <cstdio>

// 디버그 모드(_DEBUG 정의)에서만 출력되는 로그
#ifdef _DEBUG
    #define LOG_DEBUG(fmt, ...) printf("[디버그] " fmt "\n", ##__VA_ARGS__)
#else
    #define LOG_DEBUG(fmt, ...) 
#endif

// 항상 출력되는 로그 (중요 정보 및 에러)
#define LOG_INFO(fmt, ...)  printf("[정보] " fmt "\n", ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) printf("[에러] " fmt "\n", ##__VA_ARGS__)

// 통계 및 UI 전용 출력 (줄바꿈 없음)
#define LOG_UI(fmt, ...)    printf(fmt, ##__VA_ARGS__)
