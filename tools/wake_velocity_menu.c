#include "wake_velocity_menu.h"

#include <math.h>

enum { WIDTH = 320, HEIGHT = 200, ROW_HEIGHT = 16 };

typedef enum { ROW_RESUME, ROW_STEERING, ROW_CONTROLS, ROW_RESET, ROW_LEAVE, ROWS } Row;

static Color rgb(unsigned char r, unsigned char g, unsigned char b)
{
    return (Color) { r, g, b, 255 };
}

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
    Vector2 point = GetScreenToWorld2D(GetMousePosition(), camera);
    Vector2 moved = GetMouseDelta();
    for (int i = 0; i < count; ++i)
        if (CheckCollisionPointRec(point, row_bounds(menu, i))) {
            if (moved.x || moved.y)
                menu->selected = i;
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                menu->selected = i;
                activate = true;
            }
        }
    if (menu->selected == ROW_STEERING
        && (activate || IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_A)
            || IsKeyPressed(KEY_D)))
        return BOAT_MENU_STEERING;
    if (!activate)
        return BOAT_MENU_NONE;
    return menu->selected == ROW_RESUME  ? BOAT_MENU_RESUME
        : menu->selected == ROW_CONTROLS ? BOAT_MENU_CONTROLS
        : menu->selected == ROW_RESET    ? BOAT_MENU_RESET
                                         : BOAT_MENU_LEAVE;
}

void boat_menu_draw(const BoatMenu *menu, Camera2D camera, bool mouse_steering, bool networked)
{
    BeginMode2D(camera);
    Rectangle first = row_bounds(menu, 0), last = row_bounds(menu, row_count(menu) - 1);
    int top = (int)first.y - 19, height = (int)(last.y + last.height - first.y) + 24;
    DrawRectangle(52, top, 216, height, rgb(13, 45, 67));
    DrawRectangleLines(52, top, 216, height, rgb(140, 210, 199));
    const char *heading = networked ? "ROOM MENU" : "PAUSED";
    DrawText(heading, (WIDTH - MeasureText(heading, 10)) / 2, top + 5, 10, rgb(255, 239, 177));
    static const char *labels[ROWS] = { "RESUME", "STEERING", "CONTROLS", "", "" };
    for (int i = 0; i < row_count(menu); ++i) {
        Rectangle row = row_bounds(menu, i);
        bool active = i == menu->selected;
        if (active) {
            DrawRectangleRec(row, rgb(241, 184, 99));
            DrawRectangleLinesEx(row, 1, rgb(255, 232, 168));
            int tick = (int)round(sin(menu->time * 4.0));
            DrawText("(*)", 65, (int)row.y + 2 + tick, 10, rgb(33, 55, 66));
        }
        Color ink = active ? rgb(29, 52, 68) : rgb(229, 241, 220);
        DrawText(i == ROW_LEAVE  ? menu->leave_label
                : i == ROW_RESET ? menu->reset_label
                                 : labels[i],
            87, (int)row.y + 2, 10, ink);
        if (i == ROW_STEERING)
            DrawText(mouse_steering ? "< MOUSE >" : "< KEYBOARD >", 162, (int)row.y + 2, 10, ink);
    }
    const char *hint = menu->selected == ROW_RESET ? "ALSO: HOLD BACKSPACE FOR ONE SECOND"
        : menu->selected == ROW_STEERING           ? "LEFT / RIGHT OR CLICK; M IN GAME"
        : networked && menu->room_hint             ? menu->room_hint
        : networked                                ? "THE ROOM KEEPS RUNNING; YOUR BOAT IDLES"
                                                   : "ESC: RESUME";
    int x = (WIDTH - MeasureText(hint, 10)) / 2, y = top + height + 3;
    DrawRectangle(x - 3, y - 1, MeasureText(hint, 10) + 6, 12, (Color) { 10, 38, 57, 200 });
    DrawText(hint, x, y + 1, 10, rgb(255, 196, 150));
    EndMode2D();
}
