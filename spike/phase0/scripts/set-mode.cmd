@REM Edited by: OpenAI-Codex_Responsible: Source_Code
@echo off
if /I "%~1"=="off" goto write
if /I "%~1"=="steer" goto write
if /I "%~1"=="throttle" goto write
if /I "%~1"=="brake" goto write
if /I "%~1"=="buttons" goto write
echo Usage: set-mode.cmd ^<off^|steer^|throttle^|brake^|buttons^>
exit /b 1
:write
if not exist "%LOCALAPPDATA%\ETS2AP_Spike" mkdir "%LOCALAPPDATA%\ETS2AP_Spike" || exit /b 1
>"%LOCALAPPDATA%\ETS2AP_Spike\mode.txt" echo %~1 %RANDOM%
exit /b 0
