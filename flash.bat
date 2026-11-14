@echo off
chcp 65001 >nul
rem flash.bat —— 双击即可调用 flash.ps1 烧录固件
echo === ESP32 姿态仪 一键烧录 ===
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0flash.ps1"
echo.
pause