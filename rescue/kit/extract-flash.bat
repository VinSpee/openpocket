@echo off
title OpenPocket - take flash.bin out of a firmware package
REM Takes flash.bin out of an official BlackBox package (.fwsc), for RESCUE.bat --image. It reads and writes files on
REM this computer only. Nothing is sent to the pedal, so it needs no administrator.
REM   extract-flash.bat firmware\BlackBox_023.fwsc        writes BlackBox_023-flash.bin in this folder
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
%PY% tools\extract_flash.py %*
set "RC=%errorlevel%"
echo.
pause
exit /b %RC%
