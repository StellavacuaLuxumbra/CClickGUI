@echo off
rem CClickGUI quick build (MinGW g++, no CMake - avoids mingw32-make unicode-path issue)
rem Usage: build.bat [path-to-g++]   e.g. build.bat D:\mingw64\bin\g++.exe

setlocal
set GXX=%1
if "%GXX%"=="" set GXX=g++

if not exist build mkdir build

%GXX% -std=c++17 -O2 -Wall -Iinclude ^
    src/Core.cpp src/Draw.cpp src/Anim.cpp src/Render.cpp src/Toasts.cpp ^
    src/Dialogs.cpp src/Input.cpp src/Window.cpp src/Hotkeys.cpp src/Config.cpp ^
    src/Effects.cpp ^
    examples/Demo.cpp ^
    -o build\cgu_demo.exe -lgdi32 -luser32 -lcomdlg32

if errorlevel 1 (
    echo build FAILED
    exit /b 1
)
echo build OK: build\cgu_demo.exe
endlocal
