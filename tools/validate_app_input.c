#include "wake_velocity_input.h"
#include "wake_velocity_menu.h"

#include <stdio.h>
#include <stdlib.h>

// The pause menu's input handling: keyboard navigation, activation per row, pointer hover and
// press, and the menu clock, all driven by synthetic BoatAppInput values. No window, no raylib
// input reads, and no wall time.
#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            fprintf(stderr, "app input check failed at %d: %s\n", __LINE__, #condition);           \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

static const Camera2D CAMERA = { .zoom = 1 };

// RESUME, STEERING, CONTROLS, FULLSCREEN, RESET, LEAVE: six rows at y 52 + 16 * i, height 14.
static const float ROW_TOP = 52;
static const float ROW_STEP = 16;

static BoatMenu open_menu(void)
{
    BoatMenu menu = { 0 };
    boat_menu_open(&menu);
    menu.can_leave = true;
    menu.return_label = NULL;
    return menu;
}

static Vector2 row_center(int row) { return (Vector2) { 160, ROW_TOP + row * ROW_STEP + 7 }; }

static void check_zero_input(void)
{
    BoatMenu menu = open_menu();
    BoatAppInput input = { 0 };
    CHECK(boat_menu_update(&menu, &input, CAMERA, 0) == BOAT_MENU_NONE);
    CHECK(menu.selected == 0);
}

static void check_navigation(void)
{
    BoatMenu menu = open_menu();
    BoatAppInput input = { 0 };
    input.down = true;
    CHECK(boat_menu_update(&menu, &input, CAMERA, 0) == BOAT_MENU_NONE);
    CHECK(menu.selected == 1);

    menu = open_menu();
    input = (BoatAppInput) { 0 };
    input.up = true;
    CHECK(boat_menu_update(&menu, &input, CAMERA, 0) == BOAT_MENU_NONE);
    CHECK(menu.selected == 5);

    menu = open_menu();
    menu.selected = 5;
    input = (BoatAppInput) { 0 };
    input.down = true;
    CHECK(boat_menu_update(&menu, &input, CAMERA, 0) == BOAT_MENU_NONE);
    CHECK(menu.selected == 0);
}

static void check_activation(void)
{
    BoatMenu menu = open_menu();
    BoatAppInput input = { 0 };
    input.activate = true;
    CHECK(boat_menu_update(&menu, &input, CAMERA, 0) == BOAT_MENU_RESUME);

    static const BoatMenuChoice expected[] = { BOAT_MENU_RESUME, BOAT_MENU_CONTROLS,
        BOAT_MENU_FULLSCREEN, BOAT_MENU_RESET, BOAT_MENU_LEAVE };
    static const int rows[] = { 0, 2, 3, 4, 5 };
    for (int i = 0; i < 5; ++i) {
        menu = open_menu();
        menu.selected = rows[i];
        input = (BoatAppInput) { 0 };
        input.activate = true;
        CHECK(boat_menu_update(&menu, &input, CAMERA, 0) == expected[i]);
    }
}

static void check_steering(void)
{
    BoatMenu menu = open_menu();
    menu.selected = 1;
    BoatAppInput input = { 0 };
    input.left = true;
    CHECK(boat_menu_update(&menu, &input, CAMERA, 0) == BOAT_MENU_STEERING);

    menu = open_menu();
    menu.selected = 1;
    input = (BoatAppInput) { 0 };
    input.right = true;
    CHECK(boat_menu_update(&menu, &input, CAMERA, 0) == BOAT_MENU_STEERING);

    menu = open_menu();
    menu.selected = 1;
    input = (BoatAppInput) { 0 };
    input.activate = true;
    CHECK(boat_menu_update(&menu, &input, CAMERA, 0) == BOAT_MENU_STEERING);
}

static void check_fullscreen(void)
{
#if !defined(__EMSCRIPTEN__)
    BoatMenu menu = open_menu();
    menu.selected = 3;
    BoatAppInput input = { 0 };
    input.left = true;
    CHECK(boat_menu_update(&menu, &input, CAMERA, 0) == BOAT_MENU_FULLSCREEN);
#endif
}

static void check_pointer(void)
{
    BoatMenu menu = open_menu();
    menu.selected = 2;
    BoatAppInput input = { 0 };
    input.pointer = row_center(4);
    CHECK(boat_menu_update(&menu, &input, CAMERA, 0) == BOAT_MENU_NONE);
    CHECK(menu.selected == 2);

    menu = open_menu();
    input = (BoatAppInput) { 0 };
    input.pointer = row_center(4);
    input.pointer_delta = (Vector2) { 1, 0 };
    CHECK(boat_menu_update(&menu, &input, CAMERA, 0) == BOAT_MENU_NONE);
    CHECK(menu.selected == 4);

    menu = open_menu();
    input = (BoatAppInput) { 0 };
    input.pointer = row_center(4);
    input.pointer_pressed = true;
    CHECK(boat_menu_update(&menu, &input, CAMERA, 0) == BOAT_MENU_RESET);
    CHECK(menu.selected == 4);

    menu = open_menu();
    menu.selected = 2;
    input = (BoatAppInput) { 0 };
    input.pointer = (Vector2) { 5, 5 };
    input.pointer_pressed = true;
    CHECK(boat_menu_update(&menu, &input, CAMERA, 0) == BOAT_MENU_NONE);
    CHECK(menu.selected == 2);
}

static void check_clock(void)
{
    BoatMenu menu = open_menu();
    BoatAppInput input = { 0 };
    CHECK(boat_menu_update(&menu, &input, CAMERA, 0.25f) == BOAT_MENU_NONE);
    CHECK(boat_menu_update(&menu, &input, CAMERA, 0.25f) == BOAT_MENU_NONE);
    CHECK(menu.time == 0.5);
}

int main(void)
{
    check_zero_input();
    check_navigation();
    check_activation();
    check_steering();
    check_fullscreen();
    check_pointer();
    check_clock();
    printf("app input ok\n");
    return 0;
}
