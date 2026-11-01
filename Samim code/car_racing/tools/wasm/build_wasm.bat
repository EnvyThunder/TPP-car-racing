@echo off
rem The browser version (docs/browser/README.md): Qt 6.11.1 for WebAssembly + Emscripten 4.0.7.
rem   tools\wasm\build_wasm.bat            builds into C:\Qt\tpp-wasm (outside OneDrive: 16 MB of output)
rem   python -m http.server 8099 --directory C:\Qt\tpp-wasm   then open http://localhost:8099/car_racing.html
rem (emsdk_env.bat sees Git Bash's SHELL and prints bash commands, so the paths are set here)
set SHELL=
set EMSDK=C:/Qt/emsdk
set EMSDK_NODE=C:/Qt/emsdk/node/24.19.0_64bit/node.exe
set EMSDK_PYTHON=C:/Qt/emsdk/python/3.13.3_64bit/python.exe
set EM_CONFIG=C:\Qt\emsdk\.emscripten
set PATH=C:\Qt\emsdk;C:\Qt\emsdk\upstream\emscripten;C:\Qt\emsdk\node\24.19.0_64bit;C:\Qt\emsdk\python\3.13.3_64bit;C:\Qt\Tools\mingw1310_64\bin;%PATH%
set SRC=%~dp0..\..
set OUT=C:\Qt\tpp-wasm
if not exist %OUT% mkdir %OUT%
cd /d %OUT%
call C:\Qt\6.11.1\wasm_singlethread\bin\qmake.bat "%SRC%\car_racing.pro" CONFIG+=release
if errorlevel 1 exit /b 1
mingw32-make -j8 %*
if errorlevel 1 exit /b 1
rem our own page (loading screen, saved files in IndexedDB) as the site's index.html
copy /y "%SRC%\tools\wasm\index.html" "%OUT%\index.html" >nul
