#ifndef WAKE_CONSOLE_BRIDGE_H
#define WAKE_CONSOLE_BRIDGE_H
// AR3 console bridge: what the live console needs from its platform. CMake selects
// wake_console_bridge.c (native: raylib clipboard shortcuts and the Tab key) or
// wake_console_bridge_web.c (the page's console panel: transcript copy, queued paste and Tab
// completion, clipboard notices). No call waits.

#include "wake_velocity/wake_console.h"
#include <stdbool.h>

// Publish the transcript for the page's copy and screen-reader lines (browser; native: nothing).
void wake_console_bridge_sync(const WakeConsole *console);
// The console closed: drop the page's queued Tab presses and paste (browser; native: nothing).
void wake_console_bridge_closed(void);
// Once per open frame, before typed text: native copies (Ctrl/Cmd+C, Shift for the scrollback)
// and pastes (Ctrl/Cmd+V) through raylib's clipboard; the browser takes the page's paste and
// clipboard notice.
void wake_console_bridge_clipboard(WakeConsole *console, bool shortcut, bool shift);
// After typed text: completions the page queued (browser; native: nothing).
void wake_console_bridge_complete(WakeConsole *console);
// Whether a Tab press in raylib's key queue completes (native); the page owns Tab in browsers.
bool wake_console_bridge_tab_key(void);
// Whether the console has a text-entry affordance: always natively; in browsers only with a
// physical keyboard (touch-only phones get no console).
bool wake_console_bridge_text_entry(void);
// Tell the page whether the console is open, available and focused (browser; native: nothing).
void wake_console_bridge_state(bool open, bool available, bool focused);
#endif
