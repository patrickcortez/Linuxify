@echo off
g++ -O2 -std=c++17 -mwindows -municode main.cpp -o fps-explorer.exe -lgdi32 -lshell32 -lole32 -lmsimg32
if %ERRORLEVEL% EQU 0 (
    echo Build successful: fps-explorer.exe
) else (
    echo Build failed.
)
