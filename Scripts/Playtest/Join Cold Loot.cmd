@echo off
if not exist "%~dp0ColdLoot.exe" (
    echo Put this launcher beside ColdLoot.exe in the extracted playtest build.
    pause
    exit /b 1
)
pushd "%~dp0"
powershell.exe -NoLogo -NoProfile -Command "$address = (Read-Host 'Enter the host IPv4 address').Trim(); $ip = $null; if ($address -notmatch '^\d{1,3}(\.\d{1,3}){3}$' -or -not [System.Net.IPAddress]::TryParse($address, [ref]$ip)) { Write-Host 'Enter the four numbers of the host IPv4 address, separated by dots.'; exit 1 }; Start-Process -FilePath (Join-Path (Get-Location) 'ColdLoot.exe') -ArgumentList ($ip.ToString() + ':7777')"
if errorlevel 1 pause
popd
