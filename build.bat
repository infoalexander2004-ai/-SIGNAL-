@echo off
rem Сборка игры СИГНАЛ (signal.exe) из signal.cpp
set GXX=C:\Users\8523~1\AppData\Local\MICROS~1\WinGet\Packages\BRECHT~1.SOU\mingw64\bin\g++.exe
"%GXX%" -O2 -Wall -finput-charset=utf-8 -std=c++17 -mwindows -static -static-libgcc -static-libstdc++ -o signal.exe signal.cpp -lgdi32 -lwinmm -lmsimg32
if errorlevel 1 (
    echo ОШИБКА СБОРКИ
    pause
    exit /b 1
)
echo ГОТОВО: signal.exe
pause
