Wake Velocity - Windows x64

Extract the entire ZIP, then double-click launch.bat (or wake_velocity.exe) to open
the Wake Velocity title. Pick NAME, BOAT and STEERING under SETTINGS, then SINGLE PLAYER for the river, or
MULTIPLAYER to join a published SERVER, a room found on the network (LAN), or a HOST:PORT address.
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

Explicit options skip the title. The outer river with 4x4 supersampling, from PowerShell:
  .\wake_velocity.exe --location outer --gpu --gpu-aa

If GPU startup fails, launch from PowerShell to read the diagnostic output.
CPU rendering is also available:
  .\wake_velocity.exe --renderer cpu --location outer

Use --location basin for the handling basin. Add --waves for gentle
waves in either location. Run .\wake_velocity.exe --help for course and replay options.

Lua voyage plugins: copy a folder from assets\plugins somewhere writable and run
  .\wake_velocity.exe --plugin C:\path\to\copy
Saving its files reloads the voyage. The authoring folder holds the plugin authoring tools
(Python 3.10 or newer, not included); see authoring\README.txt.

Performance reports (graphics development): double-click collect-performance.bat to run the
controlled ordinary river and four-boat stress benchmark, three repeats each. A few minutes
later it opens a folder in Documents\Wake Velocity\performance and writes a ZIP beside it;
copy that ZIP back to the Mac. Pass "full" on the command line for the complete matrix
(flat/waves x AA off/4x4), or "check" to verify the fixture without a GPU context. For a hitch
during normal play, press F4: the recent 30 seconds are written to the same report folder, and
the path appears briefly on screen. F4 works with the overlay hidden and does not reset the boat.
Run .\wake_velocity.exe --help for the matching --performance-report and
--no-performance-capture options.

Choosing the GPU (PCs with integrated and dedicated graphics):
Windows may select integrated graphics even when a GeForce/Radeon dedicated GPU is installed.
Open Settings > System > Display > Graphics, add the desktop app from this extracted package,
then choose Options > High performance (check the GPU name), save, and restart the app.
Set wake_velocity.exe for gameplay AND boat_water_review.exe for collect-performance.bat;
setting launch.bat or collect-performance.bat does not select the benchmark executable's GPU.
If you extract a new release to another folder, check the preference for the new EXE paths.

Verify the result in a new report: benchmark JSON files contain gl_renderer; F4 report ZIPs
contain manifest.json with system.gl_renderer. It should name the intended GPU. Keep reports
from different GPUs separate, and include the actual renderer when reporting performance.
A GPU listed in Task Manager alone does not establish which GPU rendered this application.

When reporting issues, include build.txt, Windows version, GPU model, and
whether the problem also happens without --gpu-aa.

Map data (c) OpenStreetMap contributors, ODbL 1.0:
https://www.openstreetmap.org/copyright
Source details: assets/geometry/outer/manifest.json and assets/geometry/river/manifest.json
raylib, GLFW and ENet license notices are included alongside this file.
