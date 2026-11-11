#include "render/boat_screen.h"

#include <stdio.h>

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            fprintf(stderr, "Screen check failed at line %d: %s\n", __LINE__, #condition);         \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static bool same_rectangle(Rectangle a, float x, float y, float width, float height)
{
    return a.x == x && a.y == y && a.width == width && a.height == height;
}

int main(void)
{
    BoatResolution resolution = BOAT_RESOLUTION_HIGH;
    CHECK(boat_resolution_parse("low", &resolution) && resolution == BOAT_RESOLUTION_LOW);
    CHECK(boat_resolution_parse("high", &resolution) && resolution == BOAT_RESOLUTION_HIGH);
    CHECK(!boat_resolution_parse("ultra", &resolution) && resolution == BOAT_RESOLUTION_HIGH);
    CHECK(!boat_resolution_parse("", &resolution));
    for (int i = 0; i < BOAT_RESOLUTIONS; ++i) {
        BoatResolution parsed;
        CHECK(boat_resolution_parse(boat_resolution_name((BoatResolution)i), &parsed));
        CHECK(parsed == (BoatResolution)i);
    }
    CHECK(boat_resolution_window_scale(BOAT_RESOLUTION_LOW) == 3);
    CHECK(boat_resolution_window_scale(BOAT_RESOLUTION_HIGH) == 4);

    // Low keeps today's frame for every window: logical scene, small reflection, nearest.
    const int screens[][2] = { { 320, 200 }, { 960, 600 }, { 1280, 800 }, { 1000, 700 },
        { 1728, 1117 }, { 2560, 1600 }, { 100, 100 } };
    for (unsigned i = 0; i < sizeof(screens) / sizeof(screens[0]); ++i) {
        BoatScreenLayout low
            = boat_screen_layout(BOAT_RESOLUTION_LOW, 320, 200, screens[i][0], screens[i][1]);
        CHECK(low.scene_scale == 1 && low.scene_width == 320 && low.scene_height == 200);
        CHECK(low.reflection_width == 160 && low.reflection_height == 120);
        CHECK(!low.scene_filtered);
        // HUD placement and hit targets do not depend on the resolution.
        BoatScreenLayout high
            = boat_screen_layout(BOAT_RESOLUTION_HIGH, 320, 200, screens[i][0], screens[i][1]);
        CHECK(high.scale == low.scale);
        CHECK(same_rectangle(high.destination, low.destination.x, low.destination.y,
            low.destination.width, low.destination.height));
    }

    BoatScreenLayout layout = boat_screen_layout(BOAT_RESOLUTION_LOW, 320, 200, 1000, 700);
    CHECK(layout.scale == 3 && same_rectangle(layout.destination, 20, 50, 960, 600));
    layout = boat_screen_layout(BOAT_RESOLUTION_LOW, 320, 200, 100, 100);
    CHECK(layout.scale == 1 && same_rectangle(layout.destination, -110, -50, 320, 200));

    // The accepted look: a 1280x800 scene, 640x480 reflection, presented 1:1.
    layout = boat_screen_layout(BOAT_RESOLUTION_HIGH, 320, 200, 1280, 800);
    CHECK(layout.scale == 4 && layout.scene_scale == 4);
    CHECK(layout.scene_width == 1280 && layout.scene_height == 800);
    CHECK(layout.reflection_width == 640 && layout.reflection_height == 480);
    CHECK(!layout.scene_filtered);
    // A smaller window renders no more pixels than it shows.
    layout = boat_screen_layout(BOAT_RESOLUTION_HIGH, 320, 200, 960, 600);
    CHECK(layout.scene_scale == 3 && layout.scene_width == 960 && !layout.scene_filtered);
    CHECK(layout.reflection_width == 480 && layout.reflection_height == 360);
    layout = boat_screen_layout(BOAT_RESOLUTION_HIGH, 320, 200, 320, 200);
    CHECK(layout.scene_scale == 1 && layout.scene_width == 320 && !layout.scene_filtered);
    // A larger one keeps the high scale; whole multiples stay nearest, others filter.
    layout = boat_screen_layout(BOAT_RESOLUTION_HIGH, 320, 200, 1728, 1117);
    CHECK(layout.scale == 5 && layout.scene_scale == 4 && layout.scene_filtered);
    layout = boat_screen_layout(BOAT_RESOLUTION_HIGH, 320, 200, 2560, 1600);
    CHECK(layout.scale == 8 && layout.scene_scale == 4 && !layout.scene_filtered);
    // Browser mobile frames stay inside the target limit.
    layout = boat_screen_layout(BOAT_RESOLUTION_HIGH, 600, 200, 2400, 800);
    CHECK(layout.scale == 4 && layout.scene_scale == 3 && layout.scene_width == 1800);
    CHECK(layout.scene_filtered);
    layout = boat_screen_layout(BOAT_RESOLUTION_HIGH, 320, 960, 1280, 3840);
    CHECK(layout.scene_scale == 2 && layout.scene_height == 1920);
    for (int width = 200; width <= 960; width += 20)
        for (int height = 200; height <= 960; height += 20) {
            layout = boat_screen_layout(BOAT_RESOLUTION_HIGH, width, height, width * 4, height * 4);
            CHECK(layout.scene_scale >= 1 && layout.scene_scale <= BOAT_SCREEN_HIGH_SCALE);
            CHECK(layout.scene_width <= BOAT_SCREEN_MAX_TARGET);
            CHECK(layout.scene_height <= BOAT_SCREEN_MAX_TARGET);
        }
    puts("Boat screen layout checks passed.");
    return 0;
}
