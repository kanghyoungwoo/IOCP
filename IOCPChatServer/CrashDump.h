#pragma once
#include <Windows.h>
#include <DbgHelp.h>
#include <cstdio>
#include <csignal>
#include <exception>
#include "Define.h"

#pragma comment(lib, "Dbghelp.lib")

namespace CrashDump
{
	inline LONG WINAPI UnhandledExceptionHandler(EXCEPTION_POINTERS* pExceptionInfo)
	{
		SYSTEMTIME st;
		GetLocalTime(&st);

		char dumpFileName[MAX_PATH];
		sprintf_s(dumpFileName, "CrashDump_%04d%02d%02d_%02d%02d%02d.dmp", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

		// 덤프파일 생성 시작
		HANDLE hFile = CreateFileA(dumpFileName, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

		if (hFile != INVALID_HANDLE_VALUE)
		{
			MINIDUMP_EXCEPTION_INFORMATION dumpInfo;
			dumpInfo.ThreadId = GetCurrentThreadId();
			dumpInfo.ExceptionPointers = pExceptionInfo; // 크래시 시 당시의 메모리/레지스터 정보
			dumpInfo.ClientPointers = FALSE;

			MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(),
				hFile, MiniDumpNormal, &dumpInfo, NULL, NULL);

			CloseHandle(hFile);
			LOG_ERROR("[CRASH] Dump file created: %s\n", dumpFileName);
		}

		return EXCEPTION_EXECUTE_HANDLER;
	}

	inline void Init()
	{
		SetUnhandledExceptionFilter(UnhandledExceptionHandler);

		// std::terminate (미처리 C++ 예외) → SEH로 전환
		std::set_terminate([]() {
			RaiseException(0xE0000001, EXCEPTION_NONCONTINUABLE, 0, nullptr);
		});

		// abort() → SEH로 전환
		_set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
		signal(SIGABRT, [](int) {
			RaiseException(0xE0000002, EXCEPTION_NONCONTINUABLE, 0, nullptr);
		});
	}
}
