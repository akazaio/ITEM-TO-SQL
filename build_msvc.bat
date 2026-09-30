@echo off
setlocal
cd /d "%~dp0"
if not exist build mkdir build
cmake -S . -B build -A x64
if errorlevel 1 exit /b 1
cmake --build build --config Release --parallel
if errorlevel 1 exit /b 1
echo.
echo Built: build\Release\TBLtoSQLStudio.exe
endlocal
