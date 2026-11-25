@echo off
cd /d "%~dp0"
python flash.py %*
if errorlevel 1 (echo. & echo Something went wrong - see the messages above.)
pause
