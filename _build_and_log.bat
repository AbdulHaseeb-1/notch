@echo off
cd /d "%~dp0"
call run.bat > build-log.txt 2>&1
echo EXIT=%ERRORLEVEL% >> build-log.txt
