/* Multiplayer M4 fleet rendering review: four fleet boats in formation in the basin, drawn from
 * slot 0's follow camera with slots 1-3 as rivals in their slot countries (Sweden, Finland,
 * Denmark, Norway), through the same boat_draw_scene / boat_gpu_draw_scene calls as
 * wake_velocity --connect. --check is the display-free CTest gate: every rival changes the CPU
 * image, a repeated render is identical and drawing leaves the fleet and presentations
 * untouched. */
#include "boat_fleet.h"
#include "boat_gpu.h"
#include "boat_rearview.h"
#include "boat_test_scenery.h"
#include "boat_test_world.h"
#include "raymath.h"

#include <math.h>
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

typedef struct {
    BoatFleet fleet;
    BoatPresentation presentations[BOAT_FLEET_SLOTS];
} Formation;

// Slot 0 trails three boats heading north, in calm water or waves, for SECONDS.
static void formation(Formation *f, bool waves, float seconds)
{
    const BoatWorldDefinition *basin = boat_location(BOAT_LOCATION_BASIN);
    boat_fleet_init(&f->fleet, test_world(BOAT_LOCATION_BASIN),
        boat_world_test_water(basin, waves, false, false), BOAT_OBJECT_LIFE_TICKS,
        BOAT_OBJECT_DEFAULT_HEIGHT);
    static const Vector2 start[BOAT_FLEET_SLOTS] = { { 0, 50 }, { -5, 40 }, { 5, 36 }, { 0, 29 } };
    for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i) {
        boat_fleet_place(&f->fleet, i, (Boat) { .position = start[i], .throttle = .55f });
        boat_presentation_reset(&f->presentations[i]);
    }
    BoatInput inputs[BOAT_FLEET_SLOTS] = { 0 };
    for (int tick = 0; tick < (int)roundf(seconds * BOAT_TICK_HZ); ++tick) {
        boat_fleet_step(&f->fleet, inputs);
        BoatWorld world = boat_fleet_world(&f->fleet);
        for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i)
            boat_presentation_step(&f->presentations[i], &f->fleet.state.slots[i].boat, &world,
                boat_fleet_time(&f->fleet));
    }
}

static void views(const Formation *f, unsigned skip, BoatViewState *own, BoatRivals *rivals)
{
    double time = boat_fleet_time(&f->fleet);
    *own = (BoatViewState) { .boat = f->fleet.state.slots[0].boat, .time = time };
    *rivals = (BoatRivals) { 0 };
    for (unsigned i = 1; i < BOAT_FLEET_SLOTS; ++i) {
        if (i == skip)
            continue;
        BoatRival *r = &rivals->boats[rivals->count++];
        *r = (BoatRival) { .view = { .boat = f->fleet.state.slots[i].boat, .time = time },
            .presentation = &f->presentations[i] };
        r->view.skin = i;
    }
}

static bool draw_cpu(
    Renderer3D *renderer, BoatCpuView *cpu_view, Color *pixels, const Formation *f, unsigned skip)
{
    BoatViewState own;
    BoatRivals rivals;
    views(f, skip, &own, &rivals);
    BoatWorld world = boat_fleet_world(&f->fleet);
    return boat_draw_scene(renderer, cpu_view, pixels, 320, 200, &own, &f->presentations[0], &world,
        BOAT_CAMERA_DEFAULT, &rivals, NULL, NULL, 0);
}

// Probe the rendered hull triangles, including the hull pose's scale and mesh normalization.
// A hand-authored deck height here would repeat the attachment's original scale mistake.
static float hull_surface(const Mesh3D *mesh, Transform3D hull, float x, float z)
{
    float top = -INFINITY;
    for (size_t i = 0; i < mesh->triangle_count; ++i) {
        const Triangle3D *triangle = &mesh->triangles[i];
        Vector3 a = boat_model_point(hull, mesh->vertices[triangle->vertices[0]]);
        Vector3 b = boat_model_point(hull, mesh->vertices[triangle->vertices[1]]);
        Vector3 c = boat_model_point(hull, mesh->vertices[triangle->vertices[2]]);
        float area = (b.z - c.z) * (a.x - c.x) + (c.x - b.x) * (a.z - c.z);
        if (fabsf(area) < 1e-8f)
            continue;
        float u = ((b.z - c.z) * (x - c.x) + (c.x - b.x) * (z - c.z)) / area;
        float v = ((c.z - a.z) * (x - c.x) + (a.x - c.x) * (z - c.z)) / area;
        if (u >= -1e-5f && v >= -1e-5f && u + v <= 1 + 1e-5f)
            top = fmaxf(top, u * a.y + v * b.y + (1 - u - v) * c.y);
    }
    return top;
}

static int cannon_attachment(Formation *f)
{
    boat_combat_enable(&f->fleet);
    BoatWorld world = boat_fleet_world(&f->fleet);
    BoatViewState view = { 0 };
    BoatPresentation presentation = { 0 };
    static WaterfrontSceneFrame frame;
    // Combat room slots use the Nordic speedboats, not solo-only appearances.
    for (unsigned skin = 0; skin < BOAT_SKIN_COUNTRIES; ++skin) {
        view.skin = skin;
        boat_scene_frame(&view, &presentation, &world, BOAT_CAMERA_DEFAULT, &frame);
        // The scene appends the cannon's foot, body, face and opening after its environment.
        CHECK(frame.scenery_count >= 4);
        Transform3D foot = frame.scenery[frame.scenery_count - 4].transform;
        Transform3D body = frame.scenery[frame.scenery_count - 3].transform;
        const Mesh3D *hull = boat_skin_mesh(skin, BOAT_SKIN_HULL);
        for (int x = -1; x <= 1; x += 2)
            for (int z = -1; z <= 1; z += 2) {
                float deck = hull_surface(hull, frame.player.hull,
                    foot.position.x + (float)x * foot.scale.x * .5f,
                    foot.position.z + (float)z * foot.scale.z * .5f);
                CHECK(isfinite(deck)); // Every corner is inside the rendered hull outline.
                CHECK(deck > foot.position.y - foot.scale.y * .5f);
                CHECK(deck < foot.position.y + foot.scale.y * .5f); // Foot penetrates the deck.
            }
        CHECK(body.position.y - body.scale.y * .5f < foot.position.y + foot.scale.y * .5f);
        CHECK(body.position.y + body.scale.y * .5f > foot.position.y - foot.scale.y * .5f);
    }
    puts("cannon attachment: every mounting-foot corner intersects each room-slot hull");
    return 0;
}

static int rearview(Formation *f)
{
    enum { W = BOAT_REARVIEW_WIDTH, H = BOAT_REARVIEW_HEIGHT };
    static Color baseline[W * H], front[W * H], behind[W * H];
    Renderer3D renderer = { 0 };
    BoatCpuView cpu = { 0 };
    BoatWorld world = boat_fleet_world(&f->fleet);
    uint64_t hash = boat_fleet_hash(&f->fleet.state);
    BoatCamera rear = { .rear_view = true };
    BoatPresentation presentation = { .camera_heading = 1.2f };
    for (unsigned turn = 0; turn < 3; ++turn) {
        float heading = (float)turn * 1.1f;
        BoatViewState own = { .boat = { .heading = heading }, .time = 3 };
        Vector2 forward = { sinf(heading), -cosf(heading) };
        Vector2 starboard = { cosf(heading), sinf(heading) };
        BoatRivals rivals = { .count = 1 };
        rivals.boats[0].view = own;
        rivals.boats[0].view.skin = BOAT_SKIN_DENMARK;
        CHECK(boat_draw_scene(&renderer, &cpu, baseline, W, H, &own, &presentation, &world, rear,
            NULL, NULL, NULL, 0));
        rivals.boats[0].view.boat.position
            = Vector2Add(Vector2Scale(forward, 16), Vector2Scale(starboard, 4));
        CHECK(boat_draw_scene(&renderer, &cpu, front, W, H, &own, &presentation, &world, rear,
            &rivals, NULL, NULL, 0));
        CHECK(memcmp(baseline, front, sizeof(front)) == 0); // A boat ahead is outside the mirror.
        rivals.boats[0].view.boat.position
            = Vector2Add(Vector2Scale(forward, -16), Vector2Scale(starboard, 4));
        CHECK(boat_draw_scene(&renderer, &cpu, behind, W, H, &own, &presentation, &world, rear,
            &rivals, NULL, NULL, 0));
        unsigned changed = 0, mirrored_x = 0;
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x)
                if (memcmp(&behind[y * W + x], &baseline[y * W + x], sizeof(Color))) {
                    ++changed;
                    mirrored_x += (unsigned)(W - 1 - x);
                }
        CHECK(changed > 8);
        CHECK(mirrored_x > changed * W / 2); // Starboard pursuer appears on the mirrored right.
        CHECK(boat_fleet_hash(&f->fleet.state) == hash);
    }
    boat_cpu_view_destroy(&cpu);
    renderer3d_destroy(&renderer);
    puts("rearview: aft boats visible, forward boats excluded, starboard orientation and read-only "
         "state");
    return 0;
}

static int check(void)
{
    static Formation f, copy;
    static Color all[320 * 200], again[320 * 200], without[320 * 200];
    formation(&f, true, 3);
    copy = f;
    Renderer3D renderer = { 0 };
    BoatCpuView cpu_view = { 0 };
    CHECK(draw_cpu(&renderer, &cpu_view, all, &f, 0));
    CHECK(draw_cpu(&renderer, &cpu_view, again, &f, 0));
    CHECK(memcmp(all, again, sizeof(all)) == 0);
    CHECK(boat_fleet_hash(&f.fleet.state) == boat_fleet_hash(&copy.fleet.state));
    CHECK(memcmp(f.presentations, copy.presentations, sizeof(f.presentations)) == 0);
    unsigned changed[BOAT_FLEET_SLOTS] = { 0 };
    for (unsigned skip = 1; skip < BOAT_FLEET_SLOTS; ++skip) {
        CHECK(draw_cpu(&renderer, &cpu_view, without, &f, skip));
        for (int p = 0; p < 320 * 200; ++p)
            changed[skip] += memcmp(&all[p], &without[p], sizeof(Color)) != 0;
        CHECK(changed[skip] > 40);
    }
    CHECK(cannon_attachment(&f) == 0);
    CHECK(rearview(&f) == 0);
    boat_cpu_view_destroy(&cpu_view);
    renderer3d_destroy(&renderer);
    printf("fleet render: three rivals each drew %u/%u/%u pixels; repeat identical; fleet and "
           "presentations unchanged\nboat_fleet_render=ok\n",
        changed[1], changed[2], changed[3]);
    return 0;
}

int main(int argc, char **argv)
{
    test_scenery_load();
    const char *screenshot = NULL;
    bool gpu = false, waves = false;
    float time = 3;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--check") == 0)
            return check();
        if (strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc)
            screenshot = argv[++i];
        else if (strcmp(argv[i], "--time") == 0 && i + 1 < argc)
            time = strtof(argv[++i], NULL);
        else if (strcmp(argv[i], "--waves") == 0)
            waves = true;
        else if (strcmp(argv[i], "--renderer") == 0 && i + 1 < argc
            && (strcmp(argv[i + 1], "cpu") == 0 || strcmp(argv[i + 1], "gpu") == 0))
            gpu = strcmp(argv[++i], "gpu") == 0;
        else
            screenshot = NULL, i = argc;
    }
    if (!screenshot || !isfinite(time) || time < 0 || time > 8) {
        fprintf(stderr,
            "Usage: boat_fleet_review --check | --screenshot FILE [--renderer cpu|gpu] "
            "[--time 0..8] [--waves]\n");
        return 2;
    }
    SetTraceLogLevel(LOG_WARNING);
    static Formation f;
    formation(&f, waves, time);
    BoatViewState own;
    BoatRivals rivals;
    views(&f, 0, &own, &rivals);
    BoatWorld world = boat_fleet_world(&f.fleet);
    if (!gpu) {
        static Color pixels[320 * 200];
        Renderer3D renderer = { 0 };
        BoatCpuView cpu_view = { 0 };
        bool drawn = boat_draw_scene(&renderer, &cpu_view, pixels, 320, 200, &own,
            &f.presentations[0], &world, BOAT_CAMERA_DEFAULT, &rivals, NULL, NULL, 0);
        boat_cpu_view_destroy(&cpu_view);
        renderer3d_destroy(&renderer);
        return drawn
                && ExportImage(
                    (Image) { pixels, 320, 200, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 }, screenshot)
            ? 0
            : 1;
    }
    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    InitWindow(320, 200, "Fleet review");
    BoatGpu graphics = { 0 };
    if (!IsWindowReady() || !boat_gpu_init(&graphics, false))
        return 1;
    boat_gpu_draw_scene(
        &graphics, &own, &f.presentations[0], &world, BOAT_CAMERA_DEFAULT, &rivals, NULL, NULL, 0);
    Image image = LoadImageFromTexture(graphics.target.texture);
    ImageFlipVertical(&image);
    ImageFlipHorizontal(&image); // Match the CPU camera's mirrored basis.
    bool saved = !graphics.failed && ExportImage(image, screenshot);
    UnloadImage(image);
    boat_gpu_destroy(&graphics);
    CloseWindow();
    test_worlds_close();
    return saved ? 0 : 1;
}
