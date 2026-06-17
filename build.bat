@echo off
chcp 65001 > nul
setlocal enabledelayedexpansion

REM ============================================================
REM  CloseCrab-Unified - One-click build script
REM  Builds the Release configuration via CMake + Visual Studio.
REM ============================================================

REM --- Resolve paths relative to this script ---
set "PROJECT_DIR=%~dp0"
set "BUILD_DIR=%PROJECT_DIR%build"
set "CONFIG=Release"
set "EXE_NAME=closecrab.exe"

echo ========================================
echo  CloseCrab-Unified - Build (%CONFIG%)
echo ========================================
echo Project: %PROJECT_DIR%
echo.

REM --- Allow optional arg: build.bat Debug ---
if /I "%~1"=="Debug"   set "CONFIG=Debug"
if /I "%~1"=="Release" set "CONFIG=Release"

REM --- Check CMake is available ---
where cmake >nul 2>nul
if errorlevel 1 (
    echo [ERROR] cmake not found in PATH.
    echo         Install CMake or add it to PATH, then retry.
    goto :fail
)

REM --- Warn if closecrab.exe is running (it locks the output exe -> LNK1104) ---
tasklist /FI "IMAGENAME eq %EXE_NAME%" /NH 2>nul | find /I "%EXE_NAME%" >nul
if not errorlevel 1 (
    echo [WARN] %EXE_NAME% is currently running and will lock the output file.
    echo        The linker cannot overwrite a running exe ^(LNK1104^).
    choice /C YN /M "Close the running %EXE_NAME% now"
    if errorlevel 2 (
        echo Skipping. Build will likely fail at link stage if the exe stays open.
    ) else (
        echo Closing %EXE_NAME%...
        taskkill /F /IM %EXE_NAME% >nul 2>nul
        REM Give the OS a moment to release the file handle.
        ping -n 2 127.0.0.1 >nul
    )
    echo.
)

REM --- Configure if the build dir has no cache yet ---
if not exist "%BUILD_DIR%\CMakeCache.txt" (
    echo [1/2] Configuring project ^(first run^)...
    cmake -S "%PROJECT_DIR%" -B "%BUILD_DIR%" -G "Visual Studio 17 2022" -A x64
    if errorlevel 1 (
        echo [ERROR] CMake configure failed.
        goto :fail
    )
) else (
    echo [1/2] Build cache found, skipping configure.
)

echo.
echo [2/2] Building target closecrab-unified ^(%CONFIG%^)...
cmake --build "%BUILD_DIR%" --config %CONFIG% --target closecrab-unified -- /m
if errorlevel 1 (
    echo.
    echo [ERROR] Build failed. See messages above.
    goto :fail
)

REM --- Report result ---
set "OUT_EXE=%BUILD_DIR%\%CONFIG%\%EXE_NAME%"
echo.
echo ========================================
if exist "%OUT_EXE%" (
    echo  BUILD SUCCEEDED
    echo  Output: %OUT_EXE%
    for %%F in ("%OUT_EXE%") do echo  Size:   %%~zF bytes  ^| Modified: %%~tF
) else (
    echo  Build reported success but exe not found:
    echo  %OUT_EXE%
)
echo ========================================
goto :done

:fail
echo.
echo Build did not complete successfully.
pause
exit /b 1

:done
echo.
pause
exit /b 0
