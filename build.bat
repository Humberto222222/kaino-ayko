@echo off
REM =============================================
REM  AYKO Operations Platform - Build Script
REM  Hackathon Inovahack 2026 - Equipe Kaino
REM =============================================
echo.
echo =============================================
echo    AYKO Operations Platform - Backend Build
echo =============================================
echo.

REM Set MSYS2 compiler paths
set MSYS2_PATH=C:\msys64\ucrt64\bin
set "PATH=%MSYS2_PATH%;%PATH%"

echo [1/3] Compiling backend with g++...
g++ -std=c++17 -DASIO_STANDALONE -DCROW_STATIC ^
    -I"%MSYS2_PATH%\..\include" ^
    -I"include" ^
    src\main.cpp ^
    -lsqlite3 -lws2_32 -lmswsock ^
    -o ayko_server.exe

if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Compilation failed!
    pause
    exit /b 1
)

echo [2/3] Backend compiled successfully!
echo [3/3] Starting server...
echo.
echo =============================================
echo   Server:     http://localhost:8080
echo   Admin:      http://localhost:8080/admin
echo   Cliente:    http://localhost:8080/client
echo   Pressione Ctrl+C para parar
echo =============================================
echo.

ayko_server.exe

pause
