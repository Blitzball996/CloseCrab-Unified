@echo off
setlocal enabledelayedexpansion
chcp 65001 >/dev/null 2>&1
echo ========================================
echo   CloseCrab Knowledge Graph Builder
echo ========================================
echo.

:: Locate the codebase-memory binary (installed by setup-codebase-memory.ps1)
set "CBM=%USERPROFILE%\.crab\tools\codebase-memory-mcp.exe"
if not exist "!CBM!" (
    echo [ERROR] codebase-memory-mcp.exe not found at:
    echo   !CBM!
    echo.
    echo Run this first to install it:
    echo   pwsh -File scripts\setup-codebase-memory.ps1
    pause
    exit /b 1
)

:: Project path: from drag-and-drop arg, or prompt
if "%~1" neq "" (
    set "PROJECT_PATH=%~1"
    echo Detected dragged path: !PROJECT_PATH!
) else (
    set /p "PROJECT_PATH=Enter project path (or drag a folder onto this bat): "
)

if "!PROJECT_PATH!"=="" (
    echo [ERROR] Path cannot be empty
    pause
    exit /b 1
)
if not exist "!PROJECT_PATH!" (
    echo [ERROR] Path does not exist: !PROJECT_PATH!
    pause
    exit /b 1
)

:: Convert backslashes to forward slashes for the JSON arg
set "JSON_PATH=!PROJECT_PATH:\=/!"

echo.
echo [1/2] Indexing project: !PROJECT_PATH!
echo (10-120s depending on project size; Unreal/large repos: add a .cbmignore)
echo.

"!CBM!" cli index_repository "{\"repo_path\":\"!JSON_PATH!\"}"
if !errorlevel! neq 0 (
    echo.
    echo [ERROR] Indexing failed
    pause
    exit /b 1
)

echo.
echo [2/2] Done. Starting graph visualization...
echo Browser opens http://localhost:9749  (Ctrl+C to stop)
echo.
start http://localhost:9749
"!CBM!" --ui=true --port=9749

pause
