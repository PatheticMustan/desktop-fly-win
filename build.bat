@echo off
cd /d "%~dp0"
cmake -B build -S .
cmake --build build --config Release
if %ERRORLEVEL% EQU 0 (
    echo Built .\build\Release\DesktopFly.exe
)
