@echo off
rem flash.bat - double-click helper that calls flash.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0flash.ps1"
echo.
pause
