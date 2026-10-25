Wake Velocity - Windows x64

Extract the entire ZIP, then double-click launch.bat (or wake_velocity.exe) to open
the Wake Velocity title. Pick NAME, BOAT and STEERING under SETTINGS, then SINGLE PLAYER for the river, or
MULTIPLAYER to join a room found on the network (LAN) or type its HOST:PORT address.
Esc in a session opens a menu; BACK TO TITLE (or LEAVE ROOM) returns to the title.
Keep assets beside wake_velocity.exe.

To host a room for friends on the same network, choose MULTIPLAYER > HOST A ROOM,
pick the mode, place and water, then START ROOM, and allow wake_velocity.exe through
Windows Firewall when asked. You join it at once; CLOSE ROOM in the Esc menu ends it
for everyone. Friends see the room in the LAN row, or type this PC's local address with
port 24680 (for example 192.168.1.20:24680; run ipconfig to find it). Up to four boats.
For a room without playing in it, double-click host.bat instead; it passes options
through, for example: host.bat --waves
Hosting over the internet needs UDP port forwarding or a VPN; rooms have no
encryption or accounts.

Requires a GPU/driver supporting OpenGL 3.3. The build uses static raylib
and the static MSVC runtime.

A/D or Left/Right: steer (the wheel springs back to centre when released)
W/S, Up/Down or the scroll wheel (down: more): step thrust (4 forward / neutral / 2 reverse);
hold W/S to step every half second
M: switch between mouse steering (move to turn, stop to hold) and keyboard steering
R: reset boat
+/-: camera distance
PageUp/PageDown: camera height
C: reset camera
F3: toggle profiler overlay
Esc: menu (resume, steering, controls, back to title); single player pauses, rooms
  keep running with your boat idle. Esc on the title quits.

Explicit options skip the title. Eriksberg with 4x4 supersampling, from PowerShell:
  .\wake_velocity.exe --location eriksberg --gpu --gpu-aa

If GPU startup fails, launch from PowerShell to read the diagnostic output.
CPU rendering is also available:
  .\wake_velocity.exe --renderer cpu --location eriksberg

For the expanded river and bridge, use --location river. Add --waves for gentle
waves in any location. Run .\wake_velocity.exe --help for course and replay options.

When reporting issues, include build.txt, Windows version, GPU model, and
whether the problem also happens without --gpu-aa.

Map data (c) OpenStreetMap contributors, ODbL 1.0:
https://www.openstreetmap.org/copyright
Source details: assets/geometry/eriksberg/manifest.json
raylib, GLFW and ENet license notices are included alongside this file.
