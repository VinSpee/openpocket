@echo off
title OpenPocket - which firmware did this pedal have
REM Says which firmware family a pedal backup belongs to (the SHA-256 of its first 0x4000 bytes). It reads a file on
REM this computer only. Nothing is sent to the pedal, so it needs no administrator.
REM   fingerprint.bat              the newest backup in the OpenPocket-rescue folder of your user folder
REM   fingerprint.bat FILE         a backup (blackbox-...dump.bin) or a flash.bin
setlocal
cd /d "%~dp0"

set "PY="
where py >nul 2>&1 && set "PY=py -3"
if defined PY goto run
where python >nul 2>&1 && set "PY=python"
if defined PY goto run
echo.
echo Python 3 is not installed. Install it from https://www.python.org/downloads/
echo (tick "Add python.exe to PATH"), then run this again.
echo.
pause
exit /b 1

:run
%PY% tools\head_fingerprint.py %*
set "RC=%errorlevel%"
echo.
pause
exit /b %RC%
