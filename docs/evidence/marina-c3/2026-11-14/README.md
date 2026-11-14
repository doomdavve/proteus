# M3 C3 integration evidence — 2026-11-14

Source: merged PR #203, `bb4d589`, plus the C3 browser regression and documentation.
Platform: Apple Silicon, macOS 26.6.2. Native Debug; Emscripten 6.0.12 MinSizeRel;
Playwright 1.64.1. All checks ran locally; no workflow dispatch or deployment.

Native configure/build and `ctest --preset full` passed 568/568 tests in 73.73 s,
with localhost sockets available and GPU CTests OFF. One raylib `stb_vorbis.c` pointer
comparison warning appeared; project sources emitted none. The full run includes native
staging, browser-backend staging model, route motion, room-definition finite/loop paths,
network codec/golden bytes and native route Apply/Undo.

The supported `web` target packaged both the game and isolated evaluator. Its Lua-isolation
check passed. Wasm plugin cases `free_scene_location`, `open_path_decode` and `scene_traffic`,
the Node authoring contract and the PSTB wire-version contract passed.

The complete suite passed on Chromium 156.0.8078.4 (SwiftShader), Firefox 157.0 and
WebKit 27.2. Chromium also passed tab freeze/resume and deadline-across-freeze checks.

Retained results: [native/Wasm summary and artifact hashes](verification.json),
[Chromium](chromium.json), [Firefox](firefox.json), [WebKit](webkit.json),
and the additional [installed Chrome 157.0.8103.52 run](chrome.json), which also passed.

Browser JSON files retain engine versions, completed checks, empty page-error lists and
owner evidence; verbose console logs and generated screenshots remain in the ignored
`build-web/c3-browser/` directory. Browser checks include the M3 route demo's real worker
launch, held dwell edit and Apply/Undo. They also cover the existing authoring lifecycle,
20 fresh validation workers, three restored sessions, imports and objective isolation.
The owner sequence is `[1,0,0]` → `[2,1,0]` → `[1,2,0]` (revision, epoch low, epoch high).

Commands from the worktree root (SDK/cache and local socket/browser access required):

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j 8
ctest --preset full
source /Users/davve/src/emsdk/emsdk_env.sh
emcmake cmake -S . -B build-web -DCMAKE_BUILD_TYPE=MinSizeRel
cmake --build build-web --target web validate_boat_plugin -j 8
node build-web/validate_boat_plugin.js --case free_scene_location
node build-web/validate_boat_plugin.js --case open_path_decode
node build-web/validate_boat_plugin.js --case scene_traffic
node tests/web/test_web_plugin_author.cjs
python3 tests/web/check_web_wire.py
python3 -m http.server 8033 --bind 127.0.0.1 --directory build-web/site
```

In another terminal, using the temporary Playwright environment:

```sh
/private/tmp/proteus-c3-browser-env/bin/python tests/web/check_web_plugin_author.py \
  --browser firefox --url http://127.0.0.1:8033/ --output build-web/c3-browser/firefox
/private/tmp/proteus-c3-browser-env/bin/python tests/web/check_web_plugin_author.py \
  --browser webkit --url http://127.0.0.1:8033/ --output build-web/c3-browser/webkit
/private/tmp/proteus-c3-browser-env/bin/python tests/web/check_web_plugin_author.py \
  --browser chromium --url http://127.0.0.1:8033/ --output build-web/c3-browser/chromium
```

Local setup reused the existing pinned raylib/ENet source caches. An initial sandboxed Wasm
link failed on the SDK cache lock; allowing SDK cache access resolved it. Initial Firefox and
WebKit checks exposed an immediate scenery assertion racing the asynchronous fetch; the final
check awaits readiness and both reruns passed.

These are automated lifecycle checks, not human acceptance, audio listening or a browser
frame-by-frame docking review. No new mixed WebRTC route-demo room session, Windows/Linux
run, physical Safari/mobile or hardware-browser matrix is claimed. C2b owns native visual
and motion evidence. C4 milestone closure remains open.

Additional installed-Chrome command (same SwiftShader launch flags):

```sh
/private/tmp/proteus-c3-browser-env/bin/python tests/web/check_web_plugin_author.py \
  --browser chromium --executable '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome' \
  --url http://127.0.0.1:8033/ --output build-web/c3-browser/chrome
```
