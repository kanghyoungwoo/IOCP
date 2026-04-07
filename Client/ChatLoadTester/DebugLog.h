#pragma once
#include <cstdio>

#ifdef _DEBUG
#define DEBUG_LOG(fmt, ...) printf(fmt, ##__VA_ARGS__)
#else
#define DEBUG_LOG(fmt, ...) ((void)0)
#endif
