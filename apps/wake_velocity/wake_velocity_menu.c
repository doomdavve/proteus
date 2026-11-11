#include "wake_velocity/wake_velocity_menu.h"
#include "render/boat_ui.h"
#include "wake_velocity/wake_velocity_input.h"

#include <stddef.h>

enum { WIDTH = 320, HEIGHT = 200, ROW_HEIGHT = 16 };

typedef enum {
    ROW_RESUME,
    ROW_STEERING,
    ROW_CONTROLS,
#if !defined(__EMSCRIPTEN__)
    ROW_FULLSCREEN, // The browser owns the page's fullscreen.
#endif
    ROW_RETURN,
    ROW_RESET,
    ROW_LEAVE,
    ROWS
} Row;

static int row_count(const BoatMenu *menu)
{
    int count = menu->can_leave ? ROWS : ROW_LEAVE;
    return menu->return_label ? count : count - 1;
}

// Maps a visible position to its Row, skipping ROW_RETURN when it is not offered. ROW_LEAVE is
// last, so row_count already hides it when !can_leave.
static int row_at(const BoatMenu *menu, int position)
{
    return menu->return_label == NULL && position >= ROW_RETURN ? position + 1 : position;
}

static Rectangle row_bounds(const BoatMenu *menu, int row)
{
    int top = (HEIGHT - row_count(menu) * ROW_HEIGHT) / 2;
    return (Rectangle) { 60, (float)(top + row * ROW_HEIGHT), 200, ROW_HEIGHT - 2 };
}

void boat_menu_open(BoatMenu *menu)
{
    menu->open = true;
    menu->selected = 0;
    menu->time = 0;
}

BoatMenuChoice boat_menu_update(
    BoatMenu *menu, const BoatAppInput *input, Camera2D camera, float dt)
{
    menu->time += dt;
    int count = row_count(menu);
    int step = input->down - input->up;
    menu->selected = (menu->selected + step + count) % count;
    bool activate = input->activate;
    Vector2 pointer = input->pointer;
    bool pointer_pressed = input->pointer_pressed;
    Vector2 point = GetScreenToWorld2D(pointer, camera);
    Vector2 moved = input->pointer_delta;
    for (int i = 0; i < count; ++i)
        if (CheckCollisionPointRec(point, row_bounds(menu, i))) {
            if (moved.x || moved.y)
                menu->selected = i;
            if (pointer_pressed) {
                menu->selected = i;
                activate = true;
            }
        }
    int selected_row = row_at(menu, menu->selected);
#if !defined(__EMSCRIPTEN__)
    if (selected_row == ROW_FULLSCREEN && (input->left || input->right))
        activate = true;
#endif
    if (selected_row == ROW_STEERING && (activate || input->left || input->right))
        return BOAT_MENU_STEERING;
    if (!activate)
        return BOAT_MENU_NONE;
#if !defined(__EMSCRIPTEN__)
    if (selected_row == ROW_FULLSCREEN)
        return BOAT_MENU_FULLSCREEN;
#endif
    return selected_row == ROW_RESUME  ? BOAT_MENU_RESUME
        : selected_row == ROW_CONTROLS ? BOAT_MENU_CONTROLS
        : selected_row == ROW_RETURN   ? BOAT_MENU_RETURN
        : selected_row == ROW_RESET    ? BOAT_MENU_RESET
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
        "", "", "" }; // RETURN, RESET and LEAVE use the caller's labels.
    for (int i = 0; i < row_count(menu); ++i) {
        Rectangle row = row_bounds(menu, i);
        int choice = row_at(menu, i);
        bool active = i == menu->selected;
        if (active) {
            DrawRectangleRec(row, palette.selected);
            DrawRectangleLinesEx(row, 1, palette.edge);
            for (int j = 0; j < 3; ++j)
                DrawLine(66 + j, (int)row.y + 4 + j, 66 + j, (int)row.y + 9 - j, palette.accent);
        }
        Color ink = active ? palette.selected_text : palette.text;
        boat_ui_text(choice == ROW_LEAVE ? menu->leave_label
                : choice == ROW_RESET    ? menu->reset_label
                : choice == ROW_RETURN   ? menu->return_label
                                         : labels[choice],
            87, (int)row.y + 2, 10, ink);
#if !defined(__EMSCRIPTEN__)
        if (choice == ROW_FULLSCREEN)
            boat_ui_text(IsWindowState(FLAG_BORDERLESS_WINDOWED_MODE) ? "< ON >" : "< OFF >", 162,
                (int)row.y + 2, 10, ink);
#endif
        if (choice == ROW_STEERING)
            boat_ui_text(
                mouse_steering ? "< MOUSE >" : "< KEYBOARD >", 162, (int)row.y + 2, 10, ink);
    }
    int selected_row = row_at(menu, menu->selected);
    const char *hint = selected_row == ROW_RESET
        ? (menu->reset_hint ? menu->reset_hint : "ALSO: HOLD BACKSPACE FOR ONE SECOND")
        : selected_row == ROW_RETURN   ? "RELEASE THE HOOK AND UNLASH FIRST"
        : selected_row == ROW_STEERING ? "LEFT / RIGHT OR CLICK; M IN GAME"
        : networked && menu->room_hint ? menu->room_hint
        : networked                    ? "THE ROOM KEEPS RUNNING; YOUR BOAT IDLES"
                                       : "P / ESC: RESUME";
    int x = (WIDTH - boat_ui_width(hint, 10)) / 2, y = top + height + 3;
    DrawRectangle(x - 3, y - 1, boat_ui_width(hint, 10) + 6, 12, palette.shadow);
    boat_ui_text(hint, x, y + 1, 10, palette.hint);
    EndMode2D();
}
