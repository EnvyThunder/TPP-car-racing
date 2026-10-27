@echo off
rem ===========================================================================
rem  Turbo8 racing: let friends on your Wi-Fi join a race YOU HOST.
rem
rem  Run this once on the HOST's PC: right-click it -> "Run as administrator".
rem  It allows the game's ports through Windows Firewall on every network type
rem  (home "Private" and "Public" Wi-Fi), and removes "block" rules Windows
rem  created for car_racing.exe if its network question was answered with
rem  Cancel - a block rule would beat any allow rule.
rem
rem    TCP 47800-47809  the race itself
rem    UDP 47801        "who has room CODE?" searches from your friends
rem
rem  To undo: run  allow_firewall.bat remove  (as administrator).
rem ===========================================================================
net session >nul 2>&1
if errorlevel 1 (
    echo.
    echo  Please right-click allow_firewall.bat and choose "Run as administrator".
    echo.
    pause
    exit /b 1
)

netsh advfirewall firewall delete rule name="Turbo8 racing - race" >nul 2>&1
netsh advfirewall firewall delete rule name="Turbo8 racing - find rooms" >nul 2>&1
if /i "%~1"=="remove" (
    echo Turbo8 firewall rules removed.
    pause
    exit /b 0
)

echo Removing old "block" rules for car_racing.exe (if any)...
powershell -NoProfile -Command "Get-NetFirewallApplicationFilter | Where-Object { $_.Program -like '*car_racing.exe' } | Get-NetFirewallRule | Where-Object { $_.Action -eq 'Block' } | Remove-NetFirewallRule; exit 0"

echo Allowing the game's ports...
netsh advfirewall firewall add rule name="Turbo8 racing - race" dir=in action=allow protocol=TCP localport=47800-47809 profile=any
netsh advfirewall firewall add rule name="Turbo8 racing - find rooms" dir=in action=allow protocol=UDP localport=47801 profile=any

echo.
echo  Done. Start the game, HOST A RACE and share the code.
echo  If friends still cannot join: the Wi-Fi blocks devices from seeing each
echo  other (common on college Wi-Fi) - use a phone hotspot instead.
echo.
pause
