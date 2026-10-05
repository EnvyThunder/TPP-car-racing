@echo off
setlocal
echo ============================================================
echo   BUILDING TOP-VIEW RACING SIMULATION (NATIVE QT CPU RASTER)
echo ============================================================

where qmake.exe >nul 2>nul
if %ERRORLEVEL% neq 0 goto :addpaths
goto :startbuild

:addpaths
set "PATH=C:\Qt\6.11.1\mingw_64\bin;C:\Qt\Tools\mingw1310_64\bin;%PATH%"

:startbuild
cd /d "%~dp0"

echo [1/3] Running qmake...
qmake.exe cg_car_racing_project.pro -spec win32-g++
if %ERRORLEVEL% neq 0 (
    echo [ERROR] qmake failed!
    exit /b %ERRORLEVEL%
)

echo [2/3] Compiling C++ source files...
mingw32-make.exe -f Makefile.Release -j4
if %ERRORLEVEL% neq 0 (
    echo [ERROR] Compilation failed!
    exit /b %ERRORLEVEL%
)

echo [3/3] Deploying Qt libraries...
windeployqt.exe --release --no-translations "release\TopViewRacing.exe"

echo ============================================================
echo   BUILD SUCCESSFUL! Run with: .\run.bat
echo ============================================================
endlocal
