@echo off
REM Build + run noch
setlocal
set QTDIR=C:\Qt\6.12.0\mingw_64
set PATH=%PATH%;C:\Qt\Tools\mingw1310_64\bin;C:\Qt\Tools\CMake_64\bin;C:\Qt\Tools\Ninja;%QTDIR%\bin

cd /d "%~dp0"

REM A running noch.exe locks the binary, so stop it before rebuilding.
taskkill /im noch.exe /f >nul 2>&1

if not exist build\build.ninja (
    echo [1/3] Configuring...
    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release || goto :err
)

echo [2/3] Building...
cmake --build build || goto :err

REM Copy the Qt runtime next to noch.exe once, so it also starts outside this
REM script (for example from "Launch at startup").
if not exist build\.deployed-v3 (
    echo [3/3] Deploying Qt runtime...
    windeployqt --qmldir qml --no-translations --no-opengl-sw --no-system-d3d-compiler build\noch.exe >nul || goto :err
    echo ok> build\.deployed-v3
)

echo.
echo Launching noch...
start "" build\noch.exe
exit /b 0

:err
echo.
echo BUILD FAILED - see the errors above.
exit /b 1
