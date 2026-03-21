#pragma once
#include <Windows.h>
#include <DbgHelp.h>
#include <cstdio>
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

		// �������� ���� ����
		HANDLE hFile = CreateFileA(dumpFileName, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

		if (hFile != INVALID_HANDLE_VALUE)
		{
			MINIDUMP_EXCEPTION_INFORMATION dumpInfo;
			dumpInfo.ThreadId = GetCurrentThreadId();
			dumpInfo.ExceptionPointers = pExceptionInfo; // ũ���� �� ������ �޸�/�������� ����
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
	}
}