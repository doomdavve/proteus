#ifndef WAKE_CONSOLE_H
#define WAKE_CONSOLE_H
// PC (docs/PLUGIN_DEFAULT_PLAN.md): the Quake-style live console's input, history and bounded
// scrollback. Logic and drawing are separate so headless tests can drive every key. The console
// only collects a line; tools/wake_command.c decides what it means.
#include <stdbool.h>
#include <stddef.h>

#define WAKE_CONSOLE_SCROLL 200
#define WAKE_CONSOLE_HISTORY 16
#define WAKE_CONSOLE_INPUT 480
#define WAKE_CONSOLE_LINE 192

typedef struct {
    bool open;
    char input[WAKE_CONSOLE_INPUT];
    size_t length;
    char history[WAKE_CONSOLE_HISTORY][WAKE_CONSOLE_INPUT];
    unsigned history_count;
    int history_at; // -1 while editing a fresh line.
    char scroll[WAKE_CONSOLE_SCROLL][WAKE_CONSOLE_LINE];
    unsigned scroll_count, scroll_back;
} WakeConsole;

void wake_console_init(WakeConsole *console);
// The opening keys: grave, GLFW's world-1 key (161, where macOS ISO keyboards may report section)
// and F2. None is used by the game.
bool wake_console_open_key(int key);
// APPEND through this boundary: bounded scrollback and no line breaks.
void wake_console_print(void *context, const char *line);
void wake_console_clear(void *context);
// One frame's keys. CHARACTER is a codepoint from GetCharPressed, or 0. True when OUT receives a
// submitted line; the caller runs it through wake_command_submit.
bool wake_console_input(WakeConsole *console, int key, int character, char *out, size_t capacity);
// Draw inside the low-resolution frame: a drop-down panel with the last lines and the prompt.
void wake_console_draw(const WakeConsole *console, int width, int height);
#endif
