@echo off
REM Build + run noch.
REM Uses the CMake presets in CMakePresets.json, which locate Qt through the
REM QTDIR environment variable -- no toolchain paths are hardcoded here.
REM Set QTDIR once, e.g.  setx QTDIR C:\Qt\6.9.3\msvc2022_64
REM (Ninja and a compiler must be on PATH, as with any CMake build.)
setlocal
if defined QTDIR set PATH=%QTDIR%\bin;%PATH%

cd /d "%~dp0"

REM A running noch.exe locks the binary, so stop it before rebuilding.
taskkill /im noch.exe /f >nul 2>&1

if not exist build\build.ninja (
    echo [1/3] Configuring...
    cmake --preset release || goto :err
)

echo [2/3] Building...
cmake --build --preset release || goto :err

REM Deploy the Qt runtime next to noch.exe so it also starts outside this
REM script (for example from "Launch at startup"). Run it every time: a new
REM QML import (for example QtQuick.Dialogs) needs its module copied, and a
REM one-off marker would leave the app failing to start with "module ... is
REM not installed". windeployqt only updates what changed.
echo [3/3] Deploying Qt runtime...
if defined QTDIR (set WDQ=%QTDIR%\bin\windeployqt.exe) else (set WDQ=windeployqt)
"%WDQ%" --qmldir qml --no-translations --no-opengl-sw --no-system-d3d-compiler build\noch.exe >nul || goto :err

echo.
echo Launching noch...
start "" build\noch.exe
exit /b 0

:err
echo.
echo BUILD FAILED - see the errors above.
exit /b 1
