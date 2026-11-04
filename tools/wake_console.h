#ifndef WAKE_CONSOLE_H
#define WAKE_CONSOLE_H
// PC (docs/PLUGIN_DEFAULT_PLAN.md): bounded console editing and scrollback. Commands live in
// wake_command.c; input and transcript helpers can be driven without a window.
#include "raylib.h"
#include <stdbool.h>
#include <stddef.h>

#define WAKE_CONSOLE_SCROLL 200
#define WAKE_CONSOLE_HISTORY 16
#define WAKE_CONSOLE_INPUT 480
#define WAKE_CONSOLE_LINE 192
#define WAKE_CONSOLE_TRANSCRIPT (WAKE_CONSOLE_SCROLL * WAKE_CONSOLE_LINE + 1)

typedef struct {
    bool open, welcomed, completing;
    char completion_prefix[WAKE_CONSOLE_INPUT];
    unsigned completion_index;
    char input[WAKE_CONSOLE_INPUT], draft[WAKE_CONSOLE_INPUT];
    size_t length, cursor;
    char history[WAKE_CONSOLE_HISTORY][WAKE_CONSOLE_INPUT];
    unsigned history_count;
    int history_at; // -1 while editing a fresh line.
    char scroll[WAKE_CONSOLE_SCROLL][WAKE_CONSOLE_LINE];
    bool monospace[WAKE_CONSOLE_SCROLL]; // Only the welcome drawing uses fixed cells.
    unsigned scroll_count, scroll_back, output_start, text_width, visible_rows;
    char notice[64];
} WakeConsole;

typedef struct {
    int panel, text_width, rows;
    size_t input_first;
    size_t input_count;
    int cursor_x, cursor_width;
} WakeConsoleView;

void wake_console_init(WakeConsole *console);
void wake_console_open(WakeConsole *console, const char *version);
bool wake_console_open_key(int key);
void wake_console_print(void *context, const char *line);
void wake_console_clear(void *context);
// Echo a submitted command and mark its output for Copy. Does not execute it.
void wake_console_begin_command(WakeConsole *console, const char *line);
// Copy logical lines, never display wrapping. ALL includes the entire retained scrollback.
size_t wake_console_transcript(const WakeConsole *console, bool all, char *out, size_t capacity);
// Paste one printable ASCII line atomically; multiline/oversize pastes leave input untouched.
bool wake_console_complete(WakeConsole *console, bool reverse);
bool wake_console_paste(WakeConsole *console, const char *text);
bool wake_console_input(WakeConsole *console, int key, int character, char *out, size_t capacity);
WakeConsoleView wake_console_view(const WakeConsole *console, int width, int height);
// Poll platform keys/clipboard. True when OUT receives a submitted line.
bool wake_console_update(WakeConsole *console, int width, int height, char *out, size_t capacity);
// Optional GPU frame supplies a soft backdrop; NULL keeps the translucent CPU panel.
void wake_console_draw(
    const WakeConsole *console, int width, int height, const Texture2D *background);
void wake_console_unload(void);
#endif
