@echo off
REM Launch the magiceyes engine + SDL2 viewer (native Windows, no WSL).
REM Usage: run_win.bat <gp2x-binary> [scale]
REM Run this FROM the directory that holds the GP2X binary and its Data\ folder.
setlocal
set "BINDIR=%~dp0..\..\bin"
if "%~1"=="" ( echo usage: run_win.bat ^<gp2x-binary^> [scale] & exit /b 1 )
start "magiceyes-engine" "%BINDIR%\me_unicorn.exe" "%~1"
timeout /t 1 /nobreak >nul
"%BINDIR%\viewer.exe" %2
endlocal
