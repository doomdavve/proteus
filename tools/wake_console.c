#include "wake_console.h"

#include "boat_ui.h"
#include "raylib.h"
#include "wake_command.h"
#include <limits.h>

#include <stdio.h>
#include <string.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
EM_JS(void, browser_console_sync, (const char *latest, const char *transcript), {
    Module.boatConsoleCopy = UTF8ToString(latest);
    Module.boatConsoleTranscript = UTF8ToString(transcript);
    Module.boatConsoleLines = Module.boatConsoleTranscript.split('\n').filter(Boolean).slice(-64);
})
EM_JS(int, browser_console_paste, (char *text, int capacity), {
    const paste = Module.boatConsolePaste;
    if (paste == undefined)
        return 0;
    delete Module.boatConsolePaste;
    if (lengthBytesUTF8(paste) >= capacity)
        return -1;
    stringToUTF8(paste, text, capacity);
    return 1;
})
EM_JS(void, browser_console_notice, (char *text, int capacity), {
    if (!Module.boatConsoleClipboardNotice)
        return;
    stringToUTF8(Module.boatConsoleClipboardNotice, text, capacity);
    delete Module.boatConsoleClipboardNotice;
})
#endif

// Use the small face's natural advances for prose; only the ASCII drawing uses six-pixel cells.
// Nine-pixel rows leave room for descenders and a visible block cursor.
enum { LOGO_CELL = 6, LINE_HEIGHT = 9, PAD = 4 };

static int glyph_width(char c)
{
    char glyph[] = { c, 0 };
    return c == ' ' ? 3 : boat_ui_compact_width(glyph);
}

static int text_width(const char *text, size_t count)
{
    int width = 0;
    for (size_t i = 0; i < count && text[i]; ++i)
        width += glyph_width(text[i]);
    return width;
}

static size_t fit_text(const char *text, int width, bool monospace)
{
    size_t count = 0;
    while (text[count]) {
        int advance = monospace ? LOGO_CELL : glyph_width(text[count]);
        if (advance > width)
            break;
        width -= advance;
        ++count;
    }
    return count;
}

static size_t wrap_text(const char *text, int width, bool monospace)
{
    size_t count = fit_text(text, width, monospace);
    if (!monospace && text[count])
        for (size_t i = count; i > 0; --i)
            if (text[i - 1] == ' ')
                return i;
    return count ? count : 1;
}

static unsigned wrapped_rows(const WakeConsole *console, int width)
{
    unsigned rows = 0;
    for (unsigned i = 0; i < console->scroll_count; ++i) {
        const char *text = console->scroll[i];
        do {
            size_t count = *text ? wrap_text(text, width, console->monospace[i]) : 0;
            ++rows;
            text += count;
        } while (*text);
    }
    return rows;
}

size_t wake_console_transcript(const WakeConsole *console, bool all, char *out, size_t capacity)
{
    if (!capacity)
        return 0;
    size_t at = 0;
    for (unsigned i = all ? 0 : console->output_start; i < console->scroll_count; ++i) {
        size_t length = strlen(console->scroll[i]);
        if (length + 1 >= capacity - at)
            break; // Never copy a partial logical line.
        memcpy(out + at, console->scroll[i], length);
        at += length;
        out[at++] = '\n';
    }
    out[at] = 0;
    return at;
}

static void sync_browser(const WakeConsole *console)
{
#ifdef __EMSCRIPTEN__
    static char latest[WAKE_CONSOLE_TRANSCRIPT], transcript[WAKE_CONSOLE_TRANSCRIPT];
    wake_console_transcript(console, false, latest, sizeof(latest));
    wake_console_transcript(console, true, transcript, sizeof(transcript));
    browser_console_sync(latest, transcript);
#else
    (void)console;
#endif
}

void wake_console_init(WakeConsole *console)
{
    memset(console, 0, sizeof(*console));
    console->history_at = -1;
    console->text_width = 312;
    console->visible_rows = 15;
}

void wake_console_open(WakeConsole *console, const char *version)
{
    console->open = true;
    if (console->welcomed)
        return;
    console->welcomed = true;
    wake_console_print(console, "             v  ~.     v");
    wake_console_print(console, "    v           /|");
    wake_console_print(console, "               / |       v");
    wake_console_print(console, "        v     /__|__");
    wake_console_print(console, "            \\--------/");
    wake_console_print(console, "~~~~~~~~~~~~~`~~~~~~'~~~~~~~~~~~~~~~~~~");
    for (unsigned i = console->scroll_count - 6; i < console->scroll_count; ++i)
        console->monospace[i] = true;
    char title[WAKE_CONSOLE_LINE];
    snprintf(title, sizeof(title), "Wake Velocity %s", version);
    wake_console_print(console, title);
    wake_console_print(console, "/help: commands   /status: session");
    wake_console_print(console, "Lua: edit def   = expression: inspect");
    wake_console_print(console, "The world keeps running. Helm neutral.");
    wake_console_print(console, "Ctrl/Cmd+C copy; +Shift: all output");
    wake_console_print(console, "Ctrl/Cmd+V paste  Up/Down history");
}

bool wake_console_open_key(int key) { return key == KEY_GRAVE || key == KEY_F2 || key == 161; }

void wake_console_print(void *context, const char *line)
{
    WakeConsole *console = context;
    if (!console || !line)
        return;
    if (console->scroll_count == WAKE_CONSOLE_SCROLL) {
        memmove(console->scroll, console->scroll + 1,
            (WAKE_CONSOLE_SCROLL - 1) * sizeof(console->scroll[0]));
        memmove(console->monospace, console->monospace + 1,
            (WAKE_CONSOLE_SCROLL - 1) * sizeof(console->monospace[0]));
        --console->scroll_count;
        if (console->output_start)
            --console->output_start;
    }
    // Keep one printable ASCII line. The bitmap font and editor have the same character set.
    console->monospace[console->scroll_count] = false;
    size_t at = 0;
    for (; *line && at < WAKE_CONSOLE_LINE - 1; ++line)
        console->scroll[console->scroll_count][at++] = *line >= 32 && *line < 127 ? *line : ' ';
    console->scroll[console->scroll_count++][at] = 0;
    if (console->scroll_back) {
        unsigned rows = wrapped_rows(console, (int)console->text_width);
        const char *text = console->scroll[console->scroll_count - 1];
        unsigned added = 0;
        do {
            size_t count = *text ? wrap_text(text, (int)console->text_width, false) : 0;
            ++added;
            text += count;
        } while (*text);
        console->scroll_back += added;
        unsigned maximum = rows > console->visible_rows ? rows - console->visible_rows : 0;
        if (console->scroll_back > maximum)
            console->scroll_back = maximum;
    }
    sync_browser(console);
}

void wake_console_clear(void *context)
{
    WakeConsole *console = context;
    if (!console)
        return;
    console->scroll_count = console->scroll_back = console->output_start = 0;
    sync_browser(console);
}

void wake_console_begin_command(WakeConsole *console, const char *line)
{
    console->scroll_back = 0;
    console->notice[0] = 0;
    // Echo in bounded chunks so even a full 479-byte command remains available to Copy.
    unsigned start = console->scroll_count;
    do {
        char echo[WAKE_CONSOLE_LINE];
        size_t length = strlen(line);
        if (length > sizeof(echo) - 3)
            length = sizeof(echo) - 3;
        snprintf(echo, sizeof(echo), "> %.*s", (int)length, line);
        bool full = console->scroll_count == WAKE_CONSOLE_SCROLL;
        wake_console_print(console, echo);
        if (full && start)
            --start;
        line += length;
    } while (*line);
    console->output_start = start;
    sync_browser(console);
}

static void changed(WakeConsole *console)
{
    console->history_at = -1;
    console->completing = false;
    console->notice[0] = 0;
}

bool wake_console_paste(WakeConsole *console, const char *text)
{
    if (!text)
        return false;
    size_t length = strlen(text);
    if (length >= sizeof(console->input) - console->length) {
        snprintf(console->notice, sizeof(console->notice), "Paste too long (max %d)",
            WAKE_CONSOLE_INPUT - 1);
        return false;
    }
    for (size_t i = 0; i < length; ++i)
        if ((unsigned char)text[i] < 32 || (unsigned char)text[i] >= 127) {
            snprintf(
                console->notice, sizeof(console->notice), "Paste one ASCII line; Enter runs it");
            return false;
        }
    memmove(console->input + console->cursor + length, console->input + console->cursor,
        console->length - console->cursor + 1);
    memcpy(console->input + console->cursor, text, length);
    console->cursor += length;
    console->length += length;
    changed(console);
    return true;
}

bool wake_console_complete(WakeConsole *console, bool reverse)
{
    if (!console->open)
        return false;
    size_t start = 0;
    while (console->input[start] == ' ')
        ++start;
    if (console->input[start] != '/' || console->cursor <= start)
        return false;
    size_t end = start + 1;
    while (console->input[end] && console->input[end] != ' ')
        ++end;
    if (console->cursor > end)
        return false; // Lua and command arguments belong to the user.
    char prefix[WAKE_CONSOLE_INPUT];
    if (console->completing)
        snprintf(prefix, sizeof(prefix), "%s", console->completion_prefix);
    else {
        size_t length = console->cursor - start - 1;
        memcpy(prefix, console->input + start + 1, length);
        prefix[length] = 0;
    }
    const char *matches[16];
    unsigned count = wake_command_completions(prefix, matches, 16);
    if (!count) {
        snprintf(console->notice, sizeof(console->notice), "No command matches /%.35s", prefix);
        return false;
    }
    unsigned index = reverse ? count - 1 : 0;
    size_t length = strlen(matches[index]);
    if (console->completing) {
        if (console->completion_index != UINT_MAX)
            index = reverse ? (console->completion_index + count - 1) % count
                            : (console->completion_index + 1) % count;
        length = strlen(matches[index]);
    } else if (!reverse && count > 1) {
        size_t common = strlen(matches[0]);
        for (unsigned i = 1; i < count; ++i) {
            size_t j = 0;
            while (j < common && matches[0][j] == matches[i][j])
                ++j;
            common = j;
        }
        if (common > strlen(prefix)) {
            length = common;
            index = UINT_MAX; // The next Tab starts cycling full matches.
        }
    }
    size_t old_length = end - start - 1;
    size_t next_length = console->length - old_length + length;
    if (next_length >= sizeof(console->input)) {
        snprintf(console->notice, sizeof(console->notice), "Completion does not fit");
        return false;
    }
    memmove(console->input + start + 1 + length, console->input + end, console->length - end + 1);
    memcpy(console->input + start + 1, matches[index == UINT_MAX ? 0 : index], length);
    console->length = next_length;
    console->cursor = start + 1 + length;
    changed(console);
    console->completing = true;
    console->completion_index = index;
    snprintf(console->completion_prefix, sizeof(console->completion_prefix), "%s", prefix);
    if (count > 1)
        snprintf(console->notice, sizeof(console->notice), "%u matches; Tab next / Shift+Tab back",
            count);
    return true;
}

static void recall(WakeConsole *console, bool older)
{
    if (!console->history_count)
        return;
    if (older) {
        if (console->history_at < 0) {
            snprintf(console->draft, sizeof(console->draft), "%s", console->input);
            console->history_at = (int)console->history_count - 1;
        } else if (console->history_at > 0)
            --console->history_at;
    } else {
        if (console->history_at < 0)
            return;
        if (++console->history_at >= (int)console->history_count)
            console->history_at = -1;
    }
    snprintf(console->input, sizeof(console->input), "%s",
        console->history_at < 0 ? console->draft : console->history[console->history_at]);
    console->cursor = console->length = strlen(console->input);
}

bool wake_console_input(WakeConsole *console, int key, int character, char *out, size_t capacity)
{
    if (!console->open || !out || !capacity)
        return false;
    if (key == KEY_TAB) {
        wake_console_complete(console, false);
        return false;
    }
    if (key)
        console->completing = false;
    if (key == KEY_ESCAPE) {
        console->open = false;
        return false;
    }
    if (key == KEY_ENTER || key == KEY_KP_ENTER) {
        if (console->length >= capacity)
            return false;
        memcpy(out, console->input, console->length + 1);
        bool submitted = console->length > 0;
        if (submitted
            && (!console->history_count
                || strcmp(console->history[console->history_count - 1], console->input))) {
            if (console->history_count == WAKE_CONSOLE_HISTORY) {
                memmove(console->history, console->history + 1,
                    (WAKE_CONSOLE_HISTORY - 1) * sizeof(console->history[0]));
                --console->history_count;
            }
            snprintf(console->history[console->history_count++], WAKE_CONSOLE_INPUT, "%s",
                console->input);
        }
        console->input[0] = console->draft[0] = 0;
        console->length = console->cursor = 0;
        console->history_at = -1;
        return submitted;
    }
    if (key == KEY_LEFT && console->cursor)
        --console->cursor;
    else if (key == KEY_RIGHT && console->cursor < console->length)
        ++console->cursor;
    else if (key == KEY_HOME)
        console->cursor = 0;
    else if (key == KEY_END)
        console->cursor = console->length;
    else if ((key == KEY_BACKSPACE && console->cursor)
        || (key == KEY_DELETE && console->cursor < console->length)) {
        size_t at = key == KEY_BACKSPACE ? --console->cursor : console->cursor;
        memmove(console->input + at, console->input + at + 1, console->length - at);
        --console->length;
        changed(console);
    } else if (key == KEY_UP || key == KEY_DOWN)
        recall(console, key == KEY_UP);
    else if (key == KEY_PAGE_UP || key == KEY_PAGE_DOWN) {
        unsigned rows = wrapped_rows(console, (int)console->text_width);
        unsigned maximum = rows > console->visible_rows ? rows - console->visible_rows : 0;
        unsigned step = console->visible_rows > 1 ? console->visible_rows - 1 : 1;
        if (key == KEY_PAGE_UP) {
            console->scroll_back += step;
            if (console->scroll_back > maximum)
                console->scroll_back = maximum;
        } else
            console->scroll_back = console->scroll_back > step ? console->scroll_back - step : 0;
    }
    if (character >= 32 && character < 127) {
        char text[] = { (char)character, 0 };
        wake_console_paste(console, text);
    }
    return false;
}

WakeConsoleView wake_console_view(const WakeConsole *console, int width, int height)
{
    WakeConsoleView view = { 0 };
    if (width < 64 || height < 64)
        return view;
    view.text_width = width - PAD * 2;
    view.panel = height * 7 / 8;
    view.rows = (view.panel - PAD * 2) / LINE_HEIGHT - 3;
    if (view.rows < 1)
        view.rows = 1;
    view.panel = PAD * 2 + (view.rows + 3) * LINE_HEIGHT;
    view.cursor_width = console->cursor < console->length
        ? glyph_width(console->input[console->cursor])
        : glyph_width('0');
    int available = view.text_width - text_width("> ", 2);
    int before_cursor = text_width(console->input, console->cursor);
    while (before_cursor + view.cursor_width > available)
        before_cursor -= glyph_width(console->input[view.input_first++]);
    view.input_count = fit_text(console->input + view.input_first, available, false);
    view.cursor_x = PAD + text_width("> ", 2) + before_cursor;

    return view;
}

static bool pressed(int key) { return IsKeyPressed(key) || IsKeyPressedRepeat(key); }

bool wake_console_update(WakeConsole *console, int width, int height, char *out, size_t capacity)
{
    WakeConsoleView view = wake_console_view(console, width, height);
    unsigned width_pixels = view.text_width > 0 ? (unsigned)view.text_width : 1;
    if (width_pixels != console->text_width)
        console->scroll_back = 0;
    console->text_width = width_pixels;
    console->visible_rows = view.rows > 0 ? (unsigned)view.rows : 1;
    bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    bool shortcut = (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)
                        || IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER))
        && !IsKeyDown(KEY_LEFT_ALT) && !IsKeyDown(KEY_RIGHT_ALT);
    if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_F2)
        || (!shift && (IsKeyPressed(KEY_GRAVE) || IsKeyPressed(161)))) {
        console->open = false;
        while (GetCharPressed()) { }
        return false;
    }
#ifndef __EMSCRIPTEN__
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
#else
    char text[WAKE_CONSOLE_INPUT];
    int paste = browser_console_paste(text, sizeof(text));
    if (paste > 0)
        wake_console_paste(console, text);
    else if (paste < 0)
        snprintf(console->notice, sizeof(console->notice), "Paste too long (max %d)",
            WAKE_CONSOLE_INPUT - 1);
    browser_console_notice(console->notice, sizeof(console->notice));
#endif
    int character;
    while ((character = GetCharPressed()))
        if (!shortcut)
            wake_console_input(console, 0, character, out, capacity);
    if (IsKeyPressed(KEY_TAB))
        wake_console_complete(console, shift);
    const int keys[] = { KEY_ENTER, KEY_KP_ENTER, KEY_BACKSPACE, KEY_DELETE, KEY_LEFT, KEY_RIGHT,
        KEY_HOME, KEY_END, KEY_UP, KEY_DOWN, KEY_PAGE_UP, KEY_PAGE_DOWN };
    for (unsigned i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i)
        if ((keys[i] == KEY_ENTER || keys[i] == KEY_KP_ENTER) ? IsKeyPressed(keys[i])
                                                              : pressed(keys[i]))
            if (wake_console_input(console, keys[i], 0, out, capacity))
                return true;
    float wheel = GetMouseWheelMove();
    if (wheel != 0)
        wake_console_input(console, wheel > 0 ? KEY_PAGE_UP : KEY_PAGE_DOWN, 0, out, capacity);
    return false;
}

static void draw_text(const char *text, size_t count, int x, int y, bool monospace, Color color)
{
    for (size_t i = 0; i < count && text[i]; ++i) {
        char glyph[] = { text[i], 0 };
        int natural = boat_ui_compact_width(glyph);
        int advance = monospace ? LOGO_CELL : glyph_width(text[i]);
        int center = x + (monospace ? LOGO_CELL / 2 : natural / 2);
        if (!boat_ui_compact(glyph, center, y, color))
            boat_ui_text(glyph, x, y, 8, color);
        x += advance;
    }
}

static void draw_label(const char *text, int width, int y, Color color)
{
    draw_text(text, fit_text(text, width, false), PAD, y, false, color);
}

static Shader backdrop_shader;
static bool backdrop_attempted;
static int backdrop_texel = -1;

void wake_console_unload(void)
{
    if (backdrop_shader.id)
        UnloadShader(backdrop_shader);
    backdrop_shader = (Shader) { 0 };
    backdrop_attempted = false;
    backdrop_texel = -1;
}

static void draw_backdrop(Texture2D background, int width, int height, int panel)
{
    if (!backdrop_attempted) {
        backdrop_attempted = true;
        // Use the existing low-resolution scene texture: nine taps only under the panel, with
        // no extra render target. Text is drawn afterwards and remains crisp. No asset to ship.
        static const char fragment[] =
#ifdef __EMSCRIPTEN__
            "#version 300 es\nprecision mediump float;\n"
#else
            "#version 330\n"
#endif
            "in vec2 fragTexCoord;\n"
            "uniform sampler2D texture0;\n"
            "uniform vec2 texelSize;\n"
            "out vec4 finalColor;\n"
            "void main() {\n"
            "  vec4 sum = vec4(0.0);\n"
            "  for (int y = -1; y <= 1; ++y) {\n"
            "    for (int x = -1; x <= 1; ++x) {\n"
            "      float weight = float((2 - abs(x)) * (2 - abs(y)));\n"
            "      vec2 uv = clamp(fragTexCoord + vec2(float(x), float(y)) * texelSize * 1.5,\n"
            "                      texelSize * 0.5, vec2(1.0) - texelSize * 0.5);\n"
            "      sum += texture(texture0, uv) * weight;\n"
            "    }\n"
            "  }\n"
            "  finalColor = sum / 16.0;\n"
            "}\n";
        backdrop_shader = LoadShaderFromMemory(NULL, fragment);
        backdrop_texel
            = IsShaderValid(backdrop_shader) ? GetShaderLocation(backdrop_shader, "texelSize") : -1;
    }
    if (backdrop_texel < 0)
        return; // Shader refusal leaves the regular translucent overlay available.
    float texel[] = { 1.0f / width, 1.0f / height };
    SetShaderValue(backdrop_shader, backdrop_texel, texel, SHADER_UNIFORM_VEC2);
    BeginShaderMode(backdrop_shader);
    DrawTexturePro(background, (Rectangle) { 0, height - panel, width, -panel },
        (Rectangle) { 0, 0, width, panel }, (Vector2) { 0 }, 0, WHITE);
    EndShaderMode();
}

void wake_console_draw(
    const WakeConsole *console, int width, int height, const Texture2D *background)
{
    WakeConsoleView view = wake_console_view(console, width, height);
    if (!console->open || !view.text_width)
        return;
    Color accent = { 90, 240, 255, 255 }, ink = { 218, 223, 202, 255 };
    if (background && background->id)
        draw_backdrop(*background, width, height, view.panel);
    DrawRectangle(0, 0, width, view.panel, (Color) { 8, 15, 22, 195 });
    DrawRectangle(0, view.panel - 2, width, 2, accent);
    unsigned total = wrapped_rows(console, view.text_width);
    unsigned maximum = total > (unsigned)view.rows ? total - (unsigned)view.rows : 0;
    unsigned back = console->scroll_back < maximum ? console->scroll_back : maximum;
    unsigned end = total - back;
    unsigned start = end > (unsigned)view.rows ? end - (unsigned)view.rows : 0;
    draw_label(back ? "SCROLLBACK  PgDn: latest" : "CONSOLE  /help", view.text_width, PAD, accent);
    unsigned row = 0;
    for (unsigned i = 0; i < console->scroll_count; ++i) {
        const char *text = console->scroll[i];
        do {
            size_t count = *text ? wrap_text(text, view.text_width, console->monospace[i]) : 0;
            if (row >= start && row < end)
                draw_text(text, count, PAD, PAD + LINE_HEIGHT * (1 + (int)(row - start)),
                    console->monospace[i], ink);
            ++row;
            text += count;
        } while (*text);
    }
    int y = PAD + (view.rows + 1) * LINE_HEIGHT;
    draw_text(view.input_first ? "< " : "> ", 2, PAD, y, false, accent);
    draw_text(console->input + view.input_first, view.input_count, PAD + text_width("> ", 2), y,
        false, RAYWHITE);
    DrawRectangle(view.cursor_x, y - 1, view.cursor_width, 9, accent);
    if (console->cursor < console->length)
        draw_text(console->input + console->cursor, 1, view.cursor_x, y, false,
            (Color) { 8, 15, 22, 255 });
    draw_label(console->notice[0] ? console->notice : "Esc close  PgUp/PgDn scroll",
        view.text_width, y + LINE_HEIGHT, ink);
}
