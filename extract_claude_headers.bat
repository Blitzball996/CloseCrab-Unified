@echo off
REM ============================================================
REM extract_claude_headers.bat
REM 从本地安装的 Claude Code 二进制中提取 anthropic-beta headers
REM 用法: 双击运行，或在命令行执行
REM ============================================================

setlocal enabledelayedexpansion

echo ============================================================
echo  Claude Code Beta Headers Extractor
echo ============================================================
echo.

REM --- 查找 Claude Code 安装路径 ---
set "CLAUDE_EXE="

REM 方法1: 通过 npm global
for /f "delims=" %%i in ('npm root -g 2^>nul') do set "NPM_ROOT=%%i"
if defined NPM_ROOT (
    if exist "%NPM_ROOT%\@anthropic-ai\claude-code\bin\claude.exe" (
        set "CLAUDE_EXE=%NPM_ROOT%\@anthropic-ai\claude-code\bin\claude.exe"
    )
)

REM 方法2: 通过 where 命令找到 claude
if not defined CLAUDE_EXE (
    for /f "delims=" %%i in ('where claude 2^>nul') do (
        if not defined CLAUDE_EXE set "CLAUDE_DIR=%%~dpi"
    )
    if defined CLAUDE_DIR (
        if exist "!CLAUDE_DIR!..\node_modules\@anthropic-ai\claude-code\bin\claude.exe" (
            set "CLAUDE_EXE=!CLAUDE_DIR!..\node_modules\@anthropic-ai\claude-code\bin\claude.exe"
        )
    )
)

REM 方法3: 常见路径
if not defined CLAUDE_EXE (
    if exist "C:\nvm4w\nodejs\node_modules\@anthropic-ai\claude-code\bin\claude.exe" (
        set "CLAUDE_EXE=C:\nvm4w\nodejs\node_modules\@anthropic-ai\claude-code\bin\claude.exe"
    )
)

if not defined CLAUDE_EXE (
    echo [ERROR] 找不到 Claude Code 安装路径
    echo 请确保已通过 npm install -g @anthropic-ai/claude-code 安装
    pause
    exit /b 1
)

echo [INFO] 找到 Claude Code: %CLAUDE_EXE%

REM --- 获取版本号 ---
for /f "tokens=2 delims=:" %%v in ('findstr /c:"\"version\"" "%CLAUDE_EXE%\..\..\package.json" 2^>nul') do (
    set "VERSION=%%v"
    set "VERSION=!VERSION: =!"
    set "VERSION=!VERSION:,=!"
    set "VERSION=!VERSION:"=!"
)
if defined VERSION (
    echo [INFO] 版本: %VERSION%
) else (
    echo [INFO] 版本: unknown
)
echo.

REM --- 使用 Git Bash 的 grep 提取 headers ---
REM 需要 Git for Windows (提供 grep)
set "GREP_CMD="
if exist "C:\Program Files\Git\usr\bin\grep.exe" (
    set "GREP_CMD=C:\Program Files\Git\usr\bin\grep.exe"
) else if exist "C:\Program Files (x86)\Git\usr\bin\grep.exe" (
    set "GREP_CMD=C:\Program Files (x86)\Git\usr\bin\grep.exe"
)

if not defined GREP_CMD (
    echo [ERROR] 需要 Git for Windows (提供 grep 工具)
    pause
    exit /b 1
)

REM --- 提取所有 beta header 格式的字符串 ---
set "OUTPUT_FILE=%~dp0claude_headers_latest.txt"

echo ============================================================  > "%OUTPUT_FILE%"
echo  Claude Code Beta Headers (auto-extracted)                   >> "%OUTPUT_FILE%"
echo  Extracted: %date% %time%                                    >> "%OUTPUT_FILE%"
if defined VERSION echo  Version: %VERSION%                       >> "%OUTPUT_FILE%"
echo ============================================================ >> "%OUTPUT_FILE%"
echo.                                                             >> "%OUTPUT_FILE%"

echo [INFO] 正在从二进制中提取 headers...
echo.

REM 使用 grep -oE 提取日期格式的 beta headers
"%GREP_CMD%" -oaE "[a-z]+-[a-z]+-[a-z]*-?20[0-9]{2}-[0-9]{2}-[0-9]{2}" "%CLAUDE_EXE%" 2>nul | sort -u > "%TEMP%\raw_headers.txt"

REM 也提取 claude-code-YYYYMMDD 格式
"%GREP_CMD%" -oaE "claude-code-20[0-9]{6}" "%CLAUDE_EXE%" 2>nul | sort -u >> "%TEMP%\raw_headers.txt"

REM 去重并排序
sort "%TEMP%\raw_headers.txt" | uniq > "%TEMP%\unique_headers.txt" 2>nul

echo --- Active Beta Headers ---                                  >> "%OUTPUT_FILE%"
echo.                                                             >> "%OUTPUT_FILE%"

set "COUNT=0"
for /f "usebackq delims=" %%h in ("%TEMP%\unique_headers.txt") do (
    set /a COUNT+=1
    echo   %%h
    echo %%h                                                      >> "%OUTPUT_FILE%"
)

echo.                                                             >> "%OUTPUT_FILE%"
echo --- Total: %COUNT% headers found ---                         >> "%OUTPUT_FILE%"
echo.                                                             >> "%OUTPUT_FILE%"

REM --- 提取 rP() 定义块 (内部key -> header值 映射) ---
echo.
echo --- Key Mapping (internal_key -^> header_value) ---           >> "%OUTPUT_FILE%"
echo.                                                             >> "%OUTPUT_FILE%"

REM 提取包含 rP("key","value") 的代码段
"%GREP_CMD%" -oaP "rP\(\"[a-z_]+\",\"[a-z0-9-]+\"" "%CLAUDE_EXE%" 2>nul | sort -u > "%TEMP%\key_map.txt"

if exist "%TEMP%\key_map.txt" (
    for /f "usebackq delims=" %%m in ("%TEMP%\key_map.txt") do (
        echo   %%m                                                >> "%OUTPUT_FILE%"
    )
)

echo.
echo ============================================================
echo [DONE] 结果已保存到: %OUTPUT_FILE%
echo ============================================================
echo.

REM --- 显示推荐的 anthropic-beta header 值 ---
echo.
echo === 推荐用于 API 调用的 anthropic-beta header ===
echo.
echo 基础 (必须):
echo   claude-code-20250219
echo.
echo 缓存相关:
echo   prompt-caching-scope-2026-01-05
echo   extended-cache-ttl-2025-04-11
echo.
echo Thinking:
echo   interleaved-thinking-2025-05-14
echo   redact-thinking-2026-02-12
echo   thinking-token-count-2026-05-13
echo.
echo 工具增强:
echo   advanced-tool-use-2025-11-20
echo   tool-search-tool-2025-10-19
echo.
echo 其他:
echo   context-management-2025-06-27
echo   structured-outputs-2025-12-15
echo   web-search-2025-03-05
echo   mcp-servers-2025-12-04
echo   mid-conversation-system-2026-04-07
echo.
echo === 已废弃 (不要使用) ===
echo   prompt-caching-2024-07-31        (被 prompt-caching-scope-2026-01-05 替代)
echo   token-efficient-tools-2025-02-19 (已内置到 Claude 4+ 模型)
echo   output-128k-2025-02-19           (已内置到 Claude 4+ 模型)
echo   token-efficient-tools-2026-03-28 (不存在的日期，无效)
echo.

pause
