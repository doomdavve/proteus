#include "wake_console.h"

#include "boat_ui.h"
#include "raylib.h"

#include <stdio.h>
#include <string.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
EM_JS(void, browser_console_line, (const char *line), {
    (Module.boatConsoleLines ||= []).push(UTF8ToString(line));
    if (Module.boatConsoleLines.length > 64)
        Module.boatConsoleLines.shift();
})
EM_JS(void, browser_console_clear, (void), { Module.boatConsoleLines = []; })
#endif

#define WAKE_CONSOLE_WORLD_1 161

void wake_console_init(WakeConsole *console)
{
    memset(console, 0, sizeof(*console));
    console->history_at = -1;
}

bool wake_console_open_key(int key)
{
    return key == KEY_GRAVE || key == KEY_F2 || key == WAKE_CONSOLE_WORLD_1;
}

void wake_console_print(void *context, const char *line)
{
    WakeConsole *console = context;
    if (!console || !line)
        return;
    size_t length = strlen(line);
    if (console->scroll_count == WAKE_CONSOLE_SCROLL) {
        memmove(console->scroll, console->scroll + 1,
            (WAKE_CONSOLE_SCROLL - 1) * sizeof(console->scroll[0]));
        --console->scroll_count;
    }
    if (length >= WAKE_CONSOLE_LINE)
        length = WAKE_CONSOLE_LINE - 1;
    memcpy(console->scroll[console->scroll_count], line, length);
    console->scroll[console->scroll_count][length] = 0;
    ++console->scroll_count;
    console->scroll_back = 0;
#ifdef __EMSCRIPTEN__
    browser_console_line(console->scroll[console->scroll_count - 1]);
#endif
}

void wake_console_clear(void *context)
{
    WakeConsole *console = context;
    if (console) {
        console->scroll_count = 0;
        console->scroll_back = 0;
    }
#ifdef __EMSCRIPTEN__
    browser_console_clear();
#endif
}

static void backspace(WakeConsole *console)
{
    while (console->length && ((unsigned char)console->input[console->length - 1] & 0xc0) == 0x80)
        --console->length;
    if (console->length)
        --console->length;
    console->input[console->length] = 0;
}

static void recall(WakeConsole *console, bool older)
{
    if (!console->history_count)
        return;
    if (older) {
        if (console->history_at < 0)
            console->history_at = (int)console->history_count - 1;
        else if (console->history_at > 0)
            --console->history_at;
    } else {
        if (console->history_at < 0)
            return;
        if (++console->history_at >= (int)console->history_count)
            console->history_at = -1;
    }
    if (console->history_at < 0) {
        console->input[0] = 0;
        console->length = 0;
    } else {
        snprintf(
            console->input, sizeof(console->input), "%s", console->history[console->history_at]);
        console->length = strlen(console->input);
    }
}

bool wake_console_input(WakeConsole *console, int key, int character, char *out, size_t capacity)
{
    if (!console->open || !out || !capacity)
        return false;
    if (key == KEY_ESCAPE) {
        console->open = false;
        return false;
    }
    if (key == KEY_ENTER || key == KEY_KP_ENTER) {
        snprintf(out, capacity, "%s", console->input);
        bool submitted = console->length > 0;
        if (submitted) {
            if (console->history_count == WAKE_CONSOLE_HISTORY) {
                memmove(console->history, console->history + 1,
                    (WAKE_CONSOLE_HISTORY - 1) * sizeof(console->history[0]));
                --console->history_count;
            }
            snprintf(console->history[console->history_count++], WAKE_CONSOLE_INPUT, "%s",
                console->input);
        }
        console->input[0] = 0;
        console->length = 0;
        console->history_at = -1;
        return submitted;
    }
    if (key == KEY_BACKSPACE) {
        backspace(console);
        return false;
    }
    if (key == KEY_UP || key == KEY_DOWN) {
        recall(console, key == KEY_UP);
        return false;
    }
    if (key == KEY_PAGE_UP || key == KEY_PAGE_DOWN) {
        if (key == KEY_PAGE_UP && console->scroll_back + 5 < console->scroll_count)
            console->scroll_back += 5;
        else if (key == KEY_PAGE_UP)
            console->scroll_back = console->scroll_count;
        else
            console->scroll_back = console->scroll_back > 5 ? console->scroll_back - 5 : 0;
        return false;
    }
    if (character >= 32 && character < 127 && console->length + 1 < sizeof(console->input)) {
        console->input[console->length++] = (char)character;
        console->input[console->length] = 0;
    }
    return false;
}

void wake_console_draw(const WakeConsole *console, int width, int height)
{
    if (!console->open || width < 64 || height < 32)
        return;
    const int font = 8, line_height = 11;
    int rows = height > 140 ? 8 : 4;
    int panel = (rows + 1) * line_height + 6;
    if (panel > height - 4)
        panel = height - 4;
    rows = (panel - 6) / line_height - 1;
    DrawRectangle(0, 0, width, panel, (Color) { 8, 15, 22, 235 });
    DrawRectangle(0, panel - 2, width, 2, (Color) { 90, 240, 255, 180 });
    unsigned first = console->scroll_count > console->scroll_back
        ? console->scroll_count - console->scroll_back
        : 0;
    unsigned start = first > (unsigned)rows ? first - (unsigned)rows : 0;
    int y = 3;
    for (unsigned i = start; i < first; ++i) {
        boat_ui_text(console->scroll[i], 4, y, font, (Color) { 218, 223, 202, 255 });
        y += line_height;
    }
    char prompt[WAKE_CONSOLE_INPUT + 3];
    snprintf(prompt, sizeof(prompt), "> %s", console->input);
    boat_ui_text(prompt, 4, panel - line_height - 1, font, RAYWHITE);
    int cursor = 4 + boat_ui_width(prompt, font) + 1;
    if (cursor < width - 2)
        DrawRectangle(
            cursor, panel - line_height, 1, line_height - 2, (Color) { 90, 240, 255, 255 });
}
