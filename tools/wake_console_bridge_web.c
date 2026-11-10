#include "wake_console_bridge.h"

#include <emscripten.h>
#include <stdio.h>

EM_JS(void, wake_js_console_sync, (const char *latest, const char *transcript), {
    Module.boatConsoleCopy = UTF8ToString(latest);
    Module.boatConsoleTranscript = UTF8ToString(transcript);
    Module.boatConsoleLines = Module.boatConsoleTranscript.split('\n').filter(Boolean).slice(-64);
})
EM_JS(int, wake_js_console_paste, (char *text, int capacity), {
    const paste = Module.boatConsolePaste;
    if (paste == undefined)
        return 0;
    delete Module.boatConsolePaste;
    if (lengthBytesUTF8(paste) >= capacity)
        return -1;
    stringToUTF8(paste, text, capacity);
    return 1;
})
EM_JS(void, wake_js_console_reset_input, (void), {
    Module.boatConsoleTabs = [];
    delete Module.boatConsolePaste;
})
EM_JS(int, wake_js_console_tab, (void), {
    return Module.boatConsoleTabs && Module.boatConsoleTabs.length ? Module.boatConsoleTabs.shift()
                                                                   : 0;
})
EM_JS(void, wake_js_console_notice, (char *text, int capacity), {
    if (!Module.boatConsoleClipboardNotice)
        return;
    stringToUTF8(Module.boatConsoleClipboardNotice, text, capacity);
    delete Module.boatConsoleClipboardNotice;
})
// clang-format would split JavaScript's === into == =.
// clang-format off
EM_JS(int, wake_js_physical_keyboard, (void), {
    return (window.matchMedia && window.matchMedia('(pointer: fine)').matches)
        || (navigator.maxTouchPoints || 0) === 0;
})
// clang-format on
EM_JS(void, wake_js_console_state, (bool open, bool available, bool focused), {
    Module.boatConsoleOpen = !!open;
    Module.boatConsoleAvailable = !!available;
    Module.boatConsoleFocused = !!focused;
})

void wake_console_bridge_sync(const WakeConsole *console)
{
    static char latest[WAKE_CONSOLE_TRANSCRIPT], transcript[WAKE_CONSOLE_TRANSCRIPT];
    wake_console_transcript(console, false, latest, sizeof(latest));
    wake_console_transcript(console, true, transcript, sizeof(transcript));
    wake_js_console_sync(latest, transcript);
}

void wake_console_bridge_closed(void) { wake_js_console_reset_input(); }

void wake_console_bridge_clipboard(WakeConsole *console, bool shortcut, bool shift)
{
    (void)shortcut;
    (void)shift;
    char text[WAKE_CONSOLE_INPUT];
    int paste = wake_js_console_paste(text, sizeof(text));
    if (paste > 0)
        wake_console_paste(console, text);
    else if (paste < 0)
        snprintf(console->notice, sizeof(console->notice), "Paste too long (max %d)",
            WAKE_CONSOLE_INPUT - 1);
    wake_js_console_notice(console->notice, sizeof(console->notice));
}

void wake_console_bridge_complete(WakeConsole *console)
{
    int tab;
    while ((tab = wake_js_console_tab()))
        wake_console_complete(console, tab < 0);
}

bool wake_console_bridge_tab_key(void) { return false; }

bool wake_console_bridge_text_entry(void) { return wake_js_physical_keyboard() != 0; }

void wake_console_bridge_state(bool open, bool available, bool focused)
{
    wake_js_console_state(open, available, focused);
}
