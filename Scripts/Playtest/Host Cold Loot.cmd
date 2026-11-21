@echo off
if not exist "%~dp0ColdLoot.exe" (
    echo Put this launcher beside ColdLoot.exe in the extracted playtest build.
    pause
    exit /b 1
)
start "" /d "%~dp0." "%~dp0ColdLoot.exe" "/Game/Maps/IcebergNetwork?listen" -port=7777
