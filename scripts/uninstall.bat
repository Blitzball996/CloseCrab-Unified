@echo off
chcp 65001 > nul
setlocal enabledelayedexpansion
echo ============================================
echo  CloseCrab Uninstall (portable / ZIP)
echo ============================================
echo.
echo This removes CloseCrab files from THIS folder:
echo   %~dp0
echo.
echo  Installed-version users: uninstall from
echo  "Settings ^> Apps" or the Start Menu uninstaller instead.
echo.

set /p CONFIRM="Delete CloseCrab from this folder? (y/N): "
if /i not "%CONFIRM%"=="y" (
    echo Cancelled.
    pause
    exit /b 0
)

set /p PURGE="Also delete your data/config (data\, config\, logs)? (y/N): "

echo.
echo Removing program files...
del /q "%~dp0closecrab.exe" 2>nul
del /q "%~dp0closecrab-unified.exe" 2>nul
del /q "%~dp0*.dll" 2>nul
del /q "%~dp0closecrab.log" 2>nul
del /q "%~dp0trace.log" 2>nul
del /q "%~dp0crash.log" 2>nul

if /i "%PURGE%"=="y" (
    echo Removing data and config...
    rmdir /s /q "%~dp0data" 2>nul
    rmdir /s /q "%~dp0logs" 2>nul
    rmdir /s /q "%~dp0config" 2>nul
    echo Data and config removed.
) else (
    echo Kept your data and config ^(data\, config\^).
)

echo.
echo ============================================
echo  CloseCrab uninstalled from this folder.
echo ============================================
echo (You can now delete this folder, including uninstall.bat.)
pause
endlocal
