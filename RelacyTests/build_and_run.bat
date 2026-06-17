@echo off
:: build_and_run.bat
:: LockFreeStack poolNext Data Race — Relacy 재현 & 검증
::
:: 실행:  build_and_run.bat
:: 결과:
::   race_bug.exe  → DATA RACE 탐지 (poolNext non-atomic)
::   race_fixed.exe → 에러 없음    (poolNext atomic)

setlocal
set VCVARS="C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
set INC=%~dp0relacy_lib
set SRC=%~dp0lockfreestack_race_test.cpp

if not exist %VCVARS% (
    echo [ERROR] MSVC not found at %VCVARS%
    exit /b 1
)

:: ── 1b: 버그 버전 (RACE_BUG) ──────────────────────────────────────────────
echo.
echo === [1b] Build: RACE_BUG version ===
call %VCVARS% >nul 2>&1
cl /EHsc /std:c++20 /permissive /W0 ^
   /I"%INC%" /DRACE_BUG ^
   "%SRC%" ^
   /Fe"%~dp0race_bug.exe" ^
   /Fo"%~dp0race_bug.obj" ^
   >nul 2>&1

if errorlevel 1 (
    echo [FAIL] Compile error
    exit /b 1
)
echo [OK] Compiled.

echo.
echo === [1b] Run: RACE_BUG version (expect DATA RACE) ===
"%~dp0race_bug.exe"
echo.

:: ── 2b: 수정 버전 ──────────────────────────────────────────────────────────
echo === [2b] Build: FIXED version ===
cl /EHsc /std:c++20 /permissive /W0 ^
   /I"%INC%" ^
   "%SRC%" ^
   /Fe"%~dp0race_fixed.exe" ^
   /Fo"%~dp0race_fixed.obj" ^
   >nul 2>&1

if errorlevel 1 (
    echo [FAIL] Compile error
    exit /b 1
)
echo [OK] Compiled.

echo.
echo === [2b] Run: FIXED version (expect success) ===
"%~dp0race_fixed.exe"
echo.

echo === Done ===
endlocal
