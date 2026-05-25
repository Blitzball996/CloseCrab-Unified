@echo off
title CloseCrab Launcher
color 0A

echo.
echo   ╔══════════════════════════════════════╗
echo   ║        CloseCrab Launcher            ║
echo   ║   AI Coding Assistant Control Panel  ║
echo   ╚══════════════════════════════════════╝
echo.

:: Check Node.js
where node >nul 2>&1
if %errorlevel% neq 0 (
    echo   [ERROR] Node.js not found!
    echo   Please install from: https://nodejs.org
    echo.
    pause
    exit /b 1
)

:: Find CloseCrab-Web
set "WEB_DIR=%~dp0..\CloseCrab-Web"
if not exist "%WEB_DIR%\bin\cli.js" (
    set "WEB_DIR=%~dp0CloseCrab-Web"
)
if not exist "%WEB_DIR%\bin\cli.js" (
    echo   [ERROR] CloseCrab-Web not found!
    echo   Expected at: %WEB_DIR%
    pause
    exit /b 1
)

:: Start CloseCrab-Web server
echo   Starting CloseCrab-Web server...
cd /d "%WEB_DIR%"
start /b node bin/cli.js --port 3000

:: Wait for server to be ready
timeout /t 2 /nobreak >nul

:: Open browser to Dashboard
echo   Opening Dashboard in browser...
start http://localhost:3000

echo.
echo   CloseCrab is running!
echo   Dashboard: http://localhost:3000
echo.
echo   Press any key to stop all services and exit.
echo.
pause >nul

:: Cleanup: kill node processes we started
taskkill /f /im node.exe >nul 2>&1
echo   Services stopped. Goodbye!
timeout /t 2 /nobreak >nul
