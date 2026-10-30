#include "wake_velocity_menu.h"
#include "boat_ui.h"
#include "boat_web_touch.h"

enum { WIDTH = 320, HEIGHT = 200, ROW_HEIGHT = 16 };

typedef enum {
    ROW_RESUME,
    ROW_STEERING,
    ROW_CONTROLS,
#if !defined(__EMSCRIPTEN__)
    ROW_FULLSCREEN, // The browser owns the page's fullscreen.
#endif
    ROW_RESET,
    ROW_LEAVE,
    ROWS
} Row;

static int row_count(const BoatMenu *menu) { return menu->can_leave ? ROWS : ROW_LEAVE; }

static Rectangle row_bounds(const BoatMenu *menu, int row)
{
    int top = (HEIGHT - row_count(menu) * ROW_HEIGHT) / 2;
    return (Rectangle) { 60, (float)(top + row * ROW_HEIGHT), 200, ROW_HEIGHT - 2 };
}

void boat_menu_open(BoatMenu *menu)
{
    menu->open = true;
    menu->selected = ROW_RESUME;
    menu->time = 0;
}

BoatMenuChoice boat_menu_update(BoatMenu *menu, Camera2D camera, float dt)
{
    menu->time += dt;
    int count = row_count(menu);
    int step = (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S))
        - (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W));
    menu->selected = (menu->selected + step + count) % count;
    bool activate
        = IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER) || IsKeyPressed(KEY_SPACE);
    Vector2 pointer = GetMousePosition();
    bool pointer_pressed = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
#if defined(__EMSCRIPTEN__)
    int touch_click = boat_web_touch_click(&pointer.x);
    if (touch_click)
        pointer_pressed = touch_click > 0;
#endif
    Vector2 point = GetScreenToWorld2D(pointer, camera);
    Vector2 moved = GetMouseDelta();
    for (int i = 0; i < count; ++i)
        if (CheckCollisionPointRec(point, row_bounds(menu, i))) {
            if (moved.x || moved.y)
                menu->selected = i;
            if (pointer_pressed) {
                menu->selected = i;
                activate = true;
            }
        }
#if !defined(__EMSCRIPTEN__)
    if (menu->selected == ROW_FULLSCREEN
        && (IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_A)
            || IsKeyPressed(KEY_D)))
        activate = true;
#endif
    if (menu->selected == ROW_STEERING
        && (activate || IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_A)
            || IsKeyPressed(KEY_D)))
        return BOAT_MENU_STEERING;
    if (!activate)
        return BOAT_MENU_NONE;
#if !defined(__EMSCRIPTEN__)
    if (menu->selected == ROW_FULLSCREEN)
        return BOAT_MENU_FULLSCREEN;
#endif
    return menu->selected == ROW_RESUME  ? BOAT_MENU_RESUME
        : menu->selected == ROW_CONTROLS ? BOAT_MENU_CONTROLS
        : menu->selected == ROW_RESET    ? BOAT_MENU_RESET
                                         : BOAT_MENU_LEAVE;
}

void boat_menu_draw(const BoatMenu *menu, Camera2D camera, bool mouse_steering, bool networked)
{
    BoatUiPalette palette = boat_ui_palette();
    BeginMode2D(camera);
    Rectangle first = row_bounds(menu, 0), last = row_bounds(menu, row_count(menu) - 1);
    int top = (int)first.y - 19, height = (int)(last.y + last.height - first.y) + 24;
    DrawRectangle(52, top, 216, height, palette.panel);
    DrawRectangleLines(52, top, 216, height, palette.edge);
    const char *heading = networked ? "ROOM MENU" : "PAUSED";
    boat_ui_text(heading, (WIDTH - boat_ui_width(heading, 10)) / 2, top + 5, 10, palette.text);
    static const char *labels[ROWS] = { "RESUME", "STEERING", "CONTROLS",
#if !defined(__EMSCRIPTEN__)
        "FULLSCREEN",
#endif
        "", "" };
    for (int i = 0; i < row_count(menu); ++i) {
        Rectangle row = row_bounds(menu, i);
        bool active = i == menu->selected;
        if (active) {
            DrawRectangleRec(row, palette.selected);
            DrawRectangleLinesEx(row, 1, palette.edge);
            for (int j = 0; j < 3; ++j)
                DrawLine(66 + j, (int)row.y + 4 + j, 66 + j, (int)row.y + 9 - j, palette.accent);
        }
        Color ink = active ? palette.selected_text : palette.text;
        boat_ui_text(i == ROW_LEAVE ? menu->leave_label
                : i == ROW_RESET    ? menu->reset_label
                                    : labels[i],
            87, (int)row.y + 2, 10, ink);
#if !defined(__EMSCRIPTEN__)
        if (i == ROW_FULLSCREEN)
            boat_ui_text(IsWindowState(FLAG_BORDERLESS_WINDOWED_MODE) ? "< ON >" : "< OFF >", 162,
                (int)row.y + 2, 10, ink);
#endif
        if (i == ROW_STEERING)
            boat_ui_text(
                mouse_steering ? "< MOUSE >" : "< KEYBOARD >", 162, (int)row.y + 2, 10, ink);
    }
    const char *hint = menu->selected == ROW_RESET ? "ALSO: HOLD BACKSPACE FOR ONE SECOND"
        : menu->selected == ROW_STEERING           ? "LEFT / RIGHT OR CLICK; M IN GAME"
        : networked && menu->room_hint             ? menu->room_hint
        : networked                                ? "THE ROOM KEEPS RUNNING; YOUR BOAT IDLES"
                                                   : "P / ESC: RESUME";
    int x = (WIDTH - boat_ui_width(hint, 10)) / 2, y = top + height + 3;
    DrawRectangle(x - 3, y - 1, boat_ui_width(hint, 10) + 6, 12, palette.shadow);
    boat_ui_text(hint, x, y + 1, 10, palette.hint);
    EndMode2D();
}
