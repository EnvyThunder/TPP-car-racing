@echo off
setlocal
where qmake.exe >nul 2>nul
if %ERRORLEVEL% neq 0 goto :addpaths
goto :startrun

:addpaths
set "PATH=C:\Qt\6.11.1\mingw_64\bin;C:\Qt\Tools\mingw1310_64\bin;%PATH%"

:startrun
cd /d "%~dp0"
if exist "release\TopViewRacing.exe" (
    start "" "release\TopViewRacing.exe"
) else (
    echo [ERROR] Executable not found at release\TopViewRacing.exe!
    echo Please run build.bat first or build inside Qt Creator.
    pause
)
endlocal
