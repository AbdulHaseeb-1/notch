@echo off
REM Build and keep a log under .artifacts/ (ignored by git).
setlocal
cd /d "%~dp0.."
if not exist .artifacts mkdir .artifacts
call run.bat > .artifacts\build-log.txt 2>&1
echo EXIT=%ERRORLEVEL% >> .artifacts\build-log.txt
