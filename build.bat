@echo off
setlocal enabledelayedexpansion

rem === CanoKey for STM32F407 - one-click build =====================
rem   usage: build.bat [HSE_HZ]
rem          build.bat            -> 8 MHz crystal (default)
rem          build.bat 25000000   -> 25 MHz crystal
rem   Override toolchain with env vars:
rem          CMAKE_EXE / NINJA_EXE / ARMGCC_DIR
rem ==================================================================

set "ROOT=%~dp0"
set "TOOLS=%ROOT%..\tools"

if "%~1"=="" (set "HSE=8000000") else (set "HSE=%~1")

if "%CMAKE_EXE%"=="" set "CMAKE_EXE=%TOOLS%\cmake\cmake-3.31.6-windows-x86_64\bin\cmake.exe"
if "%NINJA_EXE%"=="" set "NINJA_EXE=%TOOLS%\ninja\ninja.exe"
if "%ARMGCC_DIR%"=="" set "ARMGCC_DIR=%TOOLS%\gcc\gcc-arm-none-eabi-10.3-2021.10\bin"

if not exist "%CMAKE_EXE%" set "CMAKE_EXE=cmake"
if not exist "%NINJA_EXE%" set "NINJA_EXE=ninja"
if not exist "%ARMGCC_DIR%\arm-none-eabi-gcc.exe" if "%ARMGCC_DIR_FALLBACK%"=="" goto :nogcc

echo cmake : %CMAKE_EXE%
echo ninja : %NINJA_EXE%
echo gcc   : %ARMGCC_DIR%
echo HSE   : %HSE% Hz

if not exist "%ROOT%build" mkdir "%ROOT%build"
cd /d "%ROOT%build"
if errorlevel 1 goto :fail

"%CMAKE_EXE%" -G Ninja -DCMAKE_MAKE_PROGRAM="%NINJA_EXE%" -DCROSS_COMPILE="%ARMGCC_DIR%\arm-none-eabi-" -DCMAKE_BUILD_TYPE=Release -DHSE_VALUE=%HSE% ..
if errorlevel 1 goto :fail

"%NINJA_EXE%" canokey-f407.bin
if errorlevel 1 goto :fail

echo.
echo [OK] firmware: %ROOT%build\canokey-f407.bin
echo      flash   : st-flash --reset write canokey-f407.bin 0x08000000
exit /b 0

:nogcc
echo [X] arm-none-eabi-gcc not found. Set ARMGCC_DIR=<toolchain>\bin
goto :fail

:fail
echo [X] build failed
exit /b 1
