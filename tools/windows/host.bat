@echo off
rem Opens a multiplayer room on every network interface (UDP port 24680 unless --port is given),
rem answering LAN searches on UDP port 24681.
rem Extra options are passed through, for example: host.bat --waves
cd /d "%~dp0"
echo Friends on this network find the room under MULTIPLAYER (LAN row) on the title.
echo If it does not show up, they can type this machine's address, for example 192.168.1.20:24680.
echo Close this window or press Ctrl+C to end the room.
wake_velocity.exe --serve --bind 0.0.0.0 %*
if errorlevel 1 pause
