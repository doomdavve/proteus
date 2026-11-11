#include "wake_velocity/wake_console_bridge.h"

#include "raylib.h"
#include <stdio.h>

void wake_console_bridge_sync(const WakeConsole *console) { (void)console; }

void wake_console_bridge_closed(void) { }

void wake_console_bridge_clipboard(WakeConsole *console, bool shortcut, bool shift)
{
    if (shortcut && IsKeyPressed(KEY_C)) {
        static char text[WAKE_CONSOLE_TRANSCRIPT];
        if (wake_console_transcript(console, shift, text, sizeof(text))) {
            SetClipboardText(text);
            snprintf(console->notice, sizeof(console->notice), "Copied %s",
                shift ? "scrollback" : "latest output");
        } else
            snprintf(console->notice, sizeof(console->notice), "Nothing to copy yet");
    }
    if (shortcut && IsKeyPressed(KEY_V))
        wake_console_paste(console, GetClipboardText());
}

void wake_console_bridge_complete(WakeConsole *console) { (void)console; }

bool wake_console_bridge_tab_key(void) { return true; }

bool wake_console_bridge_text_entry(void) { return true; }

void wake_console_bridge_state(bool open, bool available, bool focused)
{
    (void)open;
    (void)available;
    (void)focused;
}
