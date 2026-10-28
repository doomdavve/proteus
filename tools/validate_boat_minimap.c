/* Solo minimap: mapped locations bake a chart and the basin has none; both layers fit their
 * screen areas; every voyage shelter and the destination project onto water pixels inside the
 * frame while the inland ground does not; composing marks the boat, destination and exposed
 * water without touching the trim; and the N cycle runs corner, chart, hidden.
 *
 * Review: --screenshot FILE [--mode corner|chart] [--location river|outer]
 * [--background SCENE.png] [--at X,Z,HEADING] [--shelter N] [--scale N] writes one composition,
 * over a 320x200 scene capture when given and nearest-scaled N times, without opening a window. */
#include "boat_minimap.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(test)                                                                                \
    do {                                                                                           \
        if (!(test)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #test);                             \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

#define WIDTH 320
#define HEIGHT 200
#define INSTRUMENTS_TOP (HEIGHT - 46)

static Color pixel(const Image *image, Vector2 p)
{
    return ((const Color *)image->data)[(int)p.y * image->width + (int)p.x];
}

static bool same(Color a, Color b) { return a.r == b.r && a.g == b.g && a.b == b.b; }

// Water pixels are bluer than they are green; land, shore and trim are not.
static bool watery(Color c) { return c.b > c.g; }

static bool contains(Rectangle outer, Rectangle inner)
{
    return inner.x >= outer.x && inner.y >= outer.y
        && inner.x + inner.width <= outer.x + outer.width
        && inner.y + inner.height <= outer.y + outer.height;
}

static int review(int argc, char **argv)
{
    const char *screenshot = NULL, *background = NULL;
    BoatMinimapMode mode = BOAT_MINIMAP_CORNER;
    int scale = 1;
    const BoatWorldDefinition *location = boat_location(BOAT_LOCATION_OUTER);
    const BoatVoyageRoute *route = location->voyage;
    Vector2 at = route->shelters[0].position;
    float heading = route->shelters[0].heading;
    BoatVoyage voyage;
    boat_voyage_start(&voyage, route, 0);
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--screenshot") && i + 1 < argc)
            screenshot = argv[++i];
        else if (!strcmp(argv[i], "--background") && i + 1 < argc)
            background = argv[++i];
        else if (!strcmp(argv[i], "--mode") && i + 1 < argc) {
            ++i;
            mode = !strcmp(argv[i], "chart") ? BOAT_MINIMAP_CHART : BOAT_MINIMAP_CORNER;
        } else if (!strcmp(argv[i], "--location") && i + 1 < argc) {
            location = boat_location_find(argv[++i]);
            if (!location) {
                fprintf(stderr, "Unknown location: %s\n", argv[i]);
                return 1;
            }
        } else if (!strcmp(argv[i], "--at") && i + 1 < argc) {
            if (sscanf(argv[++i], "%f,%f,%f", &at.x, &at.y, &heading) != 3) {
                fprintf(stderr, "--at takes X,Z,HEADING\n");
                return 1;
            }
        } else if (!strcmp(argv[i], "--scale") && i + 1 < argc)
            scale = atoi(argv[++i]) > 1 ? atoi(argv[i]) : 1;
        else if (!strcmp(argv[i], "--shelter") && i + 1 < argc)
            voyage.shelter = (unsigned)atoi(argv[++i]) % route->shelter_count;
        else {
            fprintf(stderr, "Unknown argument: %s\n", argv[i]);
            return 1;
        }
    }
    BoatMinimap minimap;
    if (!boat_minimap_init(&minimap, location)) {
        fprintf(stderr, "No minimap for %s\n", location->name);
        return 1;
    }
    const Image *frame
        = boat_minimap_compose(&minimap, mode, location->voyage ? &voyage : NULL, at, heading);
    Image scene = background ? LoadImage(background)
                             : GenImageColor(WIDTH, HEIGHT, (Color) { 14, 23, 32, 255 });
    ImageFormat(&scene, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    Rectangle bounds = boat_minimap_bounds(&minimap, mode, scene.width, scene.height);
    ImageDraw(&scene, *frame, (Rectangle) { 0, 0, bounds.width, bounds.height }, bounds, WHITE);
    ImageResizeNN(&scene, scene.width * scale, scene.height * scale);
    bool ok = ExportImage(scene, screenshot);
    UnloadImage(scene);
    boat_minimap_destroy(&minimap);
    return ok ? 0 : 1;
}

int main(int argc, char **argv)
{
    SetTraceLogLevel(LOG_WARNING);
    if (argc > 1)
        return review(argc, argv);

    BoatMinimap minimap;
    CHECK(!boat_minimap_init(&minimap, boat_location(BOAT_LOCATION_BASIN)));
    CHECK(!boat_minimap_ready(&minimap));
    CHECK(!boat_minimap_compose(&minimap, BOAT_MINIMAP_CORNER, NULL, (Vector2) { 0 }, 0));
    boat_minimap_destroy(&minimap);

    CHECK(boat_minimap_next(BOAT_MINIMAP_HIDDEN) == BOAT_MINIMAP_CORNER);
    CHECK(boat_minimap_next(BOAT_MINIMAP_CORNER) == BOAT_MINIMAP_CHART);
    CHECK(boat_minimap_next(BOAT_MINIMAP_CHART) == BOAT_MINIMAP_HIDDEN);

    const BoatLocationId mapped[] = { BOAT_LOCATION_RIVER, BOAT_LOCATION_OUTER };
    for (int m = 0; m < 2; ++m) {
        const BoatWorldDefinition *location = boat_location(mapped[m]);
        CHECK(boat_minimap_init(&minimap, location));
        CHECK(boat_minimap_ready(&minimap));
        Rectangle scene = { 0, 0, WIDTH, HEIGHT };
        Rectangle corner = boat_minimap_bounds(&minimap, BOAT_MINIMAP_CORNER, WIDTH, HEIGHT);
        Rectangle chart = boat_minimap_bounds(&minimap, BOAT_MINIMAP_CHART, WIDTH, HEIGHT);
        CHECK(contains(scene, corner) && contains(scene, chart));
        CHECK(corner.x + corner.width > WIDTH * .7f && corner.width <= 96);
        CHECK(chart.y + chart.height <= INSTRUMENTS_TOP && chart.height > corner.height * 1.5f);
        Rectangle hidden = boat_minimap_bounds(&minimap, BOAT_MINIMAP_HIDDEN, WIDTH, HEIGHT);
        CHECK(hidden.width == 0 && hidden.height == 0);
        CHECK(!boat_minimap_compose(&minimap, BOAT_MINIMAP_HIDDEN, NULL, location->spawn, 0));

        for (int mode = BOAT_MINIMAP_CORNER; mode <= BOAT_MINIMAP_CHART; ++mode) {
            const BoatMinimapLayer *layer = &minimap.layers[mode];
            // The spawn lies in water; inland Hisingen, north of the crane, does not.
            Vector2 spawn = boat_minimap_project(&minimap, mode, location->spawn);
            CHECK(watery(pixel(&layer->base, (Vector2) { spawn.x - 2, spawn.y - 2 })));
            Vector2 inland = boat_minimap_project(&minimap, mode, (Vector2) { 0, 700 });
            CHECK(!watery(pixel(&layer->base, (Vector2) { inland.x - 2, inland.y - 2 })));
            if (location->voyage) {
                const BoatVoyageRoute *route = location->voyage;
                for (unsigned i = 0; i < route->shelter_count; ++i) {
                    Vector2 p = boat_minimap_project(&minimap, mode, route->shelters[i].position);
                    CHECK(p.x >= 2 && p.y >= 2 && p.x < layer->frame.width - 2
                        && p.y < layer->frame.height - 2);
                    CHECK(watery(pixel(&layer->base, (Vector2) { p.x - 2, p.y - 2 })));
                }
            }
        }

        BoatVoyage voyage = { 0 };
        if (location->voyage)
            boat_voyage_start(&voyage, location->voyage, 0);
        for (int mode = BOAT_MINIMAP_CORNER; mode <= BOAT_MINIMAP_CHART; ++mode) {
            Image trim = ImageCopy(minimap.layers[mode].frame);
            const Image *frame = boat_minimap_compose(&minimap, mode, &voyage, location->spawn, 0);
            CHECK(frame == &minimap.layers[mode].frame);
            // Marks never cover the trim.
            for (int y = 0; y < frame->height; ++y)
                for (int x = 0; x < frame->width; ++x)
                    if (x < 2 || y < 2 || x >= frame->width - 2 || y >= frame->height - 2)
                        CHECK(same(pixel(frame, (Vector2) { (float)x, (float)y }),
                            pixel(&trim, (Vector2) { (float)x, (float)y })));
            UnloadImage(trim);
            // The boat is white; heading 0 points south, down the frame.
            Vector2 boat = boat_minimap_project(&minimap, mode, location->spawn);
            CHECK(same(pixel(frame, boat), WHITE));
            CHECK(same(pixel(frame, (Vector2) { boat.x, boat.y + 2 }), WHITE));
            if (location->voyage) {
                const BoatVoyageRoute *route = location->voyage;
                Vector2 goal = boat_minimap_project(&minimap, mode, route->arrival_centre);
                CHECK(same(pixel(frame, goal), ((Color) { 233, 180, 104, 255 })));
            }
        }
        boat_minimap_destroy(&minimap);
        CHECK(!boat_minimap_ready(&minimap));
    }
    printf("boat_minimap=ok\n");
    return 0;
}
