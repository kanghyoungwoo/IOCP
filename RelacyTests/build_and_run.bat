@echo off
:: ============================================================================
::  build_and_run.bat
::  Lock-Free 자료구조 5종 Relacy 형식 검증 (정상 + 음성 대조군)
::
::  실행:  build_and_run.bat
::
::  검증 대상 / 기대 결과
::   1. LockFreeStack  : FIXED=통과 / RACE_BUG=DATA RACE (poolNext non-atomic)
::   2. SPSCQueue      : OK=통과 / WEAKEN_PUB·WEAKEN_RECLAIM=DATA RACE
::   3. RingBuffer     : OK=통과 / WEAKEN_PUB·WEAKEN_RECLAIM=DATA RACE
::   4. MPSCQueue      : OK=통과 / WEAKEN_LINK=DATA RACE / VIOLATE_SC=DATA RACE
::   5. GlobalQueue    : OK=통과 / WEAKEN_PUB=DATA RACE / WRAP=통과(wrap 산술)
:: ============================================================================
setlocal EnableDelayedExpansion
set VCVARS="C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
set INC=%~dp0relacy_lib
set CL_FLAGS=/nologo /utf-8 /EHsc /std:c++20 /permissive /W0 /I"%INC%"

if not exist %VCVARS% (
    echo [ERROR] MSVC not found at %VCVARS%
    exit /b 1
)
call %VCVARS% >nul 2>&1

:: 인자: %1=소스 %2=출력exe %3=추가define(없으면 "")
goto :main

:build_run
set _SRC=%~dp0%~1
set _EXE=%~dp0%~2
set _DEF=%~3
echo.
echo --- build %~2  [%_DEF%] ---
cl %CL_FLAGS% %_DEF% "%_SRC%" /Fe"%_EXE%" /Fo"%_EXE%.obj" >nul 2>&1
if errorlevel 1 ( echo [FAIL] compile error & exit /b 1 )
"%_EXE%"
echo.
exit /b 0

:main
echo ========== 1. LockFreeStack ==========
call :build_run lockfreestack_race_test.cpp race_fixed.exe ""
call :build_run lockfreestack_race_test.cpp race_bug.exe   "/DRACE_BUG"

echo ========== 2. SPSCQueue ==========
call :build_run spsc_queue_test.cpp spsc_ok.exe           ""
call :build_run spsc_queue_test.cpp spsc_weak_pub.exe     "/DWEAKEN_PUB"
call :build_run spsc_queue_test.cpp spsc_weak_reclaim.exe "/DWEAKEN_RECLAIM"

echo ========== 3. RingBuffer ==========
call :build_run ringbuffer_test.cpp rb_ok.exe           ""
call :build_run ringbuffer_test.cpp rb_weak_pub.exe     "/DWEAKEN_PUB"
call :build_run ringbuffer_test.cpp rb_weak_reclaim.exe "/DWEAKEN_RECLAIM"

echo ========== 4. MPSCQueue ==========
call :build_run mpsc_queue_test.cpp mpsc_ok.exe         ""
call :build_run mpsc_queue_test.cpp mpsc_weak_link.exe  "/DWEAKEN_LINK"
call :build_run mpsc_queue_test.cpp mpsc_violate_sc.exe "/DVIOLATE_SC"

echo ========== 5. GlobalQueue (MPMC) ==========
call :build_run globalqueue_mpmc_test.cpp gq_ok.exe       ""
call :build_run globalqueue_mpmc_test.cpp gq_weak_pub.exe "/DWEAKEN_PUB"
call :build_run globalqueue_mpmc_test.cpp gq_wrap.exe     "/DWRAP"

echo ========== Done ==========
endlocal
