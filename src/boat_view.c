#include "boat_view.h"
#include "boat_course_view.h"
#include "boat_map.h"
#include "boat_pose.h"
#include "boat_race.h"
#include "raymath.h"
#include "waterfront.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>

static const float CAMERA_BASE_FOV = 55 * DEG2RAD;
static const float CAMERA_SPEED_FOV = 7 * DEG2RAD;
static const float CAMERA_SPEED_START = 4.0f;
static const float CAMERA_SPEED_FULL = 18.0f;

void boat_player_pose(
    const BoatViewState *view, const BoatFlag *flag, WaterfrontPlayerState *player)
{
    const Boat *boat = &view->boat;
    player->skin = boat_skin_index(view->skin);
    double time = view->time;
    if (flag && flag->initialized) {
        player->flag = flag->surface;
    } else {
        BoatFlag rest = { 0 };
        boat_flag_reset(&rest, boat_pose_hull(boat), boat_pose_velocity(boat), time);
        player->flag = rest.surface;
    }
    BoatPoseParts parts = boat_pose_parts(boat);
    player->hull = parts.hull;
    player->cabin = parts.cabin;
    player->engine = parts.engine;
}

static void box(Renderer3D *renderer, Vector3 position, Vector3 scale, Color color)
{
    renderer3d_draw_box(renderer, (Transform3D) { .position = position, .scale = scale }, color);
}

static View3D boat_camera(const Boat *boat, const BoatPresentation *presentation,
    const BoatWorldDefinition *definition, BoatCamera camera_settings)
{
    // A small, bounded lens change accompanies forward travel. Derive it from recorded
    // pose/velocity so seeking a replay gives the same view without hidden camera history.
    // Ground speed matches scenery passing the camera; reverse/docking retain the base lens.
    Vector2 forward = { sinf(boat->heading), -cosf(boat->heading) };
    float speed = Vector2DotProduct(boat->velocity, forward);
    float travel = camera_settings.fixed_fov
        ? 0
        : Clamp((speed - CAMERA_SPEED_START) / (CAMERA_SPEED_FULL - CAMERA_SPEED_START), 0, 1);
    travel = travel * travel * (3 - 2 * travel);
    Vector3 boat_position = { -boat->position.x, 0, boat->position.y };
    Vector3 follow
        = { -sinf(presentation->camera_heading), 0, -cosf(presentation->camera_heading) };
    Vector3 eye = Vector3Subtract(boat_position, Vector3Scale(follow, camera_settings.distance));
    eye.y = camera_settings.height;
    Vector3 look_at = Vector3Add(boat_position, Vector3Scale(follow, 5));
    look_at.y = 0.6f;
    return (View3D) { .position = eye,
        .target = look_at,
        .up = { 0, 1, 0 },
        .vertical_fov = CAMERA_BASE_FOV + CAMERA_SPEED_FOV * travel,
        .near_plane = 0.3f,
        .far_plane = definition->far_plane };
}

static float water_particle_size(const BoatWaterParticle *water)
{
    if (water->spray)
        return water->size;
    float life = water->age / water->lifetime;
    if (water->wash)
        return (water->size + water->age * .9f) * (1 - .85f * life * life);
    return (water->size + water->age * .22f) * (1 - .45f * life * life);
}

static Color water_particle_color(
    const BoatWorldDefinition *definition, const BoatWaterParticle *water)
{
    float fade = 1 - water->age / water->lifetime;
    if (!water->spray)
        fade *= (.68f + .32f * water->variation) * fade;
    Color base = definition->particle_water;
    return (Color) { (unsigned char)(base.r + (226 - base.r) * fade),
        (unsigned char)(base.g + (239 - base.g) * fade),
        (unsigned char)(base.b + (244 - base.b) * fade), 255 };
}

static Transform3D water_particle_pose(
    double time, const BoatWorld *world, const BoatWaterParticle *water, float size)
{
    Transform3D pose = { .position = { -water->position.x, water->height, water->position.y },
        .scale = { size, water->spray ? size : .015f, size } };
    if (!water->spray && boat_world_displaced(world)) {
        Vector3 normal
            = boat_water_query_field(world->water, world->wake_field, water->position, time).normal;
        normal.x = -normal.x;
        pose.rotation = boat_pose_euler(
            QuaternionToMatrix(QuaternionFromVector3ToVector3((Vector3) { 0, 1, 0 }, normal)));
    }
    return pose;
}

// Shared authored current cues and scripted wake source for CPU/GPU views in all locations.
static void vessel_box(BoatCourseBoxDraw draw, void *context, Vector2 position, float heading,
    Vector3 local, Vector3 size, Color color)
{
    Vector3 p = Vector3Transform(local, MatrixRotateY(heading));
    p = Vector3Add(p, (Vector3) { -position.x, 0, position.y });
    draw(context, (Transform3D) { .position = p, .rotation = { 0, heading, 0 }, .scale = size }, color);
}

// Skill gates have tall endpoint markers and an open passage. Start/finish retain rows
// across the water: white start, teal cleared, yellow next, orange later, chequered finish.
static void race_visit(
    const BoatViewState *view, const BoatMap *map, BoatCourseBoxDraw draw, void *context)
{
    const BoatRaceCourse *course = boat_race_river_course();
    for (unsigned k = 0; k <= course->gate_count; ++k) {
        const BoatRaceGate *gate = k ? &course->gates[k - 1] : &course->start;
        Vector2 across = { -gate->direction.y, gate->direction.x };
        Color color = k == 0                ? RAYWHITE
            : k <= view->race.next_gate     ? (Color) { 86, 157, 150, 255 }
            : k == view->race.next_gate + 1 ? (Color) { 255, 216, 93, 255 }
                                            : ORANGE;
        bool finish = k == course->gate_count;
        if (k && !finish) {
            for (int side = -1; side <= 1; side += 2) {
                Vector2 p = Vector2Add(
                    gate->centre, Vector2Scale(across, (float)side * gate->half_width));
                if (Vector2Distance(p, view->boat.position) > 450)
                    continue;
                draw(context,
                    (Transform3D) { .position = { -p.x, .8f, p.y }, .scale = { 3, 2.2f, 3 } },
                    color);
                draw(context,
                    (Transform3D) { .position = { -p.x, 4.5f, p.y }, .scale = { .5f, 7, .5f } },
                    RAYWHITE);
                // Flags point into the opening, leaving the gate centre unobstructed.
                float inward = (float)side * 1.5f;
                draw(context,
                    (Transform3D) { .position
                        = { -p.x + across.x * inward, 7, p.y - across.y * inward },
                        .rotation = { 0, atan2f(across.y, across.x), 0 },
                        .scale = { 3, 1.8f, .25f } },
                    color);
            }
            continue;
        }
        for (int n = -(int)(gate->half_width / 12); n <= (int)(gate->half_width / 12); ++n) {
            Vector2 p = Vector2Add(gate->centre, Vector2Scale(across, 12.0f * (float)n));
            if (Vector2Distance(p, view->boat.position) > 450 || !boat_map_water(map, p, 1))
                continue;
            Color buoy = finish ? (n % 2 ? DARKGRAY : RAYWHITE) : color;
            draw(context,
                (Transform3D) { .position = { -p.x, .5f, p.y }, .scale = { 1.6f, 1.4f, 1.6f } },
                buoy);
            if (n % 4)
                continue;
            draw(context,
                (Transform3D) { .position = { -p.x, 2.8f, p.y }, .scale = { .3f, 4, .3f } },
                RAYWHITE);
            draw(context,
                (Transform3D) { .position = { -p.x - .8f, 4.2f, p.y }, .scale = { 1.6f, 1, .15f } },
                finish ? DARKGRAY : color);
        }
    }
}

static float storm_rain_hash(uint32_t seed)
{
    seed ^= seed >> 16;
    seed *= UINT32_C(0x7feb352d);
    seed ^= seed >> 15;
    seed *= UINT32_C(0x846ca68b);
    seed ^= seed >> 16;
    return (float)(seed & UINT32_C(0x00ffffff)) / 16777216.0f;
}

// Independent drop cycles break up the placement grid and stagger speeds, gaps and streak
// sizes. Reposition only at the top of a fall; seeking/replays need no particle state.
static void storm_rain_drop(const BoatViewState *view, BoatStorm storm, double clock,
    float top, float span, unsigned x, unsigned z, bool upper,
    BoatCourseBoxDraw draw, void *context)
{
    // Concentrate the low curtain around the viewer on a storm-relative 5 m lattice.
    // Hash actual cell coordinates so following the boat never relocates existing drops.
    int cell_x = (int)x, cell_z = (int)z;
    if (!upper) {
        cell_x += (int)floorf((view->boat.position.x - storm.centre.x) / 5) - 10;
        cell_z += (int)floorf((view->boat.position.y - storm.centre.y) / 5) - 10;
    }
    uint32_t seed = (uint32_t)cell_x * UINT32_C(0x9e3779b9)
        ^ (uint32_t)cell_z * UINT32_C(0x85ebca6b)
        ^ (upper ? UINT32_C(0xa511e9b3) : UINT32_C(0x63d83595));
    float speed = 18 + 22 * storm_rain_hash(seed + 1);
    double travel = clock * speed + storm_rain_hash(seed + 2) * span;
    double cycle = floor(travel / span);
    float fall = (float)(travel - cycle * span);
    uint32_t drop = seed ^ (uint32_t)(int64_t)cycle * UINT32_C(0x27d4eb2d);
    if (storm_rain_hash(drop + 3) < .20f)
        return;
    Vector2 p = { ((float)x - 11.5f + storm_rain_hash(drop + 4) - .5f) * storm.radius / 12,
        ((float)z - 11.5f + storm_rain_hash(drop + 5) - .5f) * storm.radius / 12 };
    if (!upper)
        p = (Vector2) { ((float)cell_x + storm_rain_hash(drop + 4)) * 5,
            ((float)cell_z + storm_rain_hash(drop + 5)) * 5 };
    if (Vector2Length(p) > storm.radius * .90f)
        return;
    float gust = sinf((float)fmod(clock * .7, 2 * PI) + p.x * .017f + p.y * .011f);
    p.x += 4 * gust;
    p = Vector2Add(storm.centre, p);
    if (Vector2Distance(view->boat.position, p) > 210)
        return;
    float slant = .10f + .22f * storm_rain_hash(drop + 6) + .08f * gust;
    // Fine exposure streaks rather than metre-scale solid bars. Ease their ends into
    // each fall so a new cycle does not pop a full-length streak into the cloud/water.
    float life = fall / span;
    float fade = Clamp(fminf(life, 1 - life) * 12, 0, 1);
    float width = (.035f + .045f * storm_rain_hash(drop + 7)) * fade;
    float length = ((upper ? 2.0f : .9f) + 2.2f * storm_rain_hash(drop + 8)) * fade;
    unsigned char brightness = (unsigned char)(185 + 35 * storm_rain_hash(drop + 9));
    if (fade < .05f)
        return;
    draw(context,
        (Transform3D) { .position = { -p.x, top - fall, p.y },
            .rotation = { .05f * gust, 0, -slant },
            .scale = { width, length, width } },
        (Color) {
            brightness, (unsigned char)(brightness + 17), (unsigned char)(brightness + 30), 255 });
}

// A travelling world-space squall: its cloud, rain curtain and bolts advertise the physical
// patch from outside it. All animation is a function of the shared race clock.
static void storm_visit(const BoatViewState *view, const BoatWorld *world,
    BoatCourseBoxDraw draw, void *context)
{
    BoatStorm storm = world->water.storm;
    storm.centre = boat_water_storm_centre(storm, view->time);
    if (storm.radius <= 0 || storm.height <= 0
        || Vector2Distance(view->boat.position, storm.centre) > storm.radius + 350)
        return;
    double clock = view->time - storm.started;
    bool flash = fmod(fmax(0, clock), 9.0) < .16 && clock > 1;
    const float cloud_height = 180;
    Color cloud = flash ? (Color) { 168, 182, 193, 255 } : (Color) { 48, 60, 73, 255 };
    for (int z = -1; z <= 1; ++z)
        for (int x = -1; x <= 1; ++x)
            draw(context,
                (Transform3D) { .position = { -storm.centre.x + (float)x * 52,
                    cloud_height + (float)((x + z + 4) % 3) * 6, storm.centre.y + (float)z * 52 },
                    .scale = { 68, 12, 68 } }, cloud);
    // Jittered, independently cycling drops near the boat, plus a sparse upper curtain.
    // Counts remain bounded below the shared scenery capacity even during lightning.
    for (unsigned z = 0; z < 24; ++z)
        for (unsigned x = 0; x < 24; ++x) {
            if (x < 20 && z < 20)
                storm_rain_drop(view, storm, clock, 32, 30, x, z, false, draw, context);
            if (x % 2 == 0 && z % 2 == 0)
                storm_rain_drop(view, storm, clock, cloud_height - 8, cloud_height - 44,
                    x, z, true, draw, context);
        }
    if (flash) {
        float x = -storm.centre.x - 42;
        float segment = (cloud_height - 4) / 10;
        for (int i = 0; i < 10; ++i)
            draw(context,
                (Transform3D) { .position = { x + (float)(i % 2) * 3,
                    cloud_height - 4 - ((float)i + .5f) * segment, storm.centre.y + 25 },
                    .rotation = { 0, 0, i % 2 ? -.18f : .18f },
                    .scale = { 1.4f, segment + 2, 1.4f } },
                (Color) { 235, 246, 255, 255 });
    }
}

static void environment_visit(
    const BoatViewState *view, const BoatWorld *world, BoatCourseBoxDraw draw, void *context)
{
    if (view->race.enabled && world->definition->mapped)
        race_visit(view, &world->resources->map, draw, context);
    storm_visit(view, world, draw, context);
    const Boat *boat = &view->boat;
    double time = view->time;
    if (Vector2LengthSqr(world->water.current) > 0) {
        for (int x = -24; x <= 24; x += 8)
            for (int z = -48; z <= 48; z += 16) {
                float phase = (float)fmod(time + (float)(x + z + 80) * .11f, 8);
                float fade = sinf(PI * phase / 8);
                float center_z = world->definition->mapped ? roundf(boat->position.y / 16) * 16 : 0;
                Vector2 p = { x + world->water.current_center, z + center_z };
                Vector2 flow = boat_water_current(world->water, p);
                if (Vector2LengthSqr(flow) < .01f || fade < .02f)
                    continue;
                p = Vector2Add(p, Vector2Scale(flow, phase));
                if (world->definition->mapped && !boat_map_water(&world->resources->map, p, .2f))
                    continue;
                float height
                    = boat_water_query_field(world->water, world->wake_field, p, time).height;
                draw(context,
                    (Transform3D) { .position = { -p.x, height + .025f, p.y },
                        .rotation = { 0, atan2f(-flow.x, flow.y), 0 },
                        .scale = { .12f * fade, .015f, 1.5f * fade } },
                    (Color) { 113, 171, 177, 255 });
            }
    }
    Vector2 source;
    if (boat_vessel_wake_position(world->water.vessel, time, &source)) {
        float heading = atan2f(world->water.vessel.direction.x, -world->water.vessel.direction.y);
        Color hull = { 41, 75, 99, 255 }, deck = { 221, 224, 209, 255 };
        vessel_box(draw, context, source, heading, (Vector3) { 0, .2f, 0 },
            (Vector3) { BOAT_VESSEL_BEAM, 2.8f, BOAT_VESSEL_LENGTH }, hull);
        vessel_box(draw, context, source, heading, (Vector3) { 0, 2.6f, 0 },
            (Vector3) { 9, 2, 28 }, deck);
        vessel_box(draw, context, source, heading, (Vector3) { 0, 4.2f, -7 },
            (Vector3) { 8, 1.2f, 12 }, deck);
        vessel_box(draw, context, source, heading, (Vector3) { 0, 4.4f, -13.05f },
            (Vector3) { 7, .6f, .1f }, hull);
        vessel_box(draw, context, source, heading, (Vector3) { 0, 5.3f, 8 },
            (Vector3) { 2, 3.5f, 3 }, (Color) { 202, 138, 49, 255 });
        for (int side = -1; side <= 1; side += 2)
            for (int z = -11; z <= 11; z += 3)
                vessel_box(draw, context, source, heading, (Vector3) { side * 4.55f, 2.8f, z },
                    (Vector3) { .1f, .8f, 1.6f }, hull);
    }
    if (boat_water_wake_source(world->water, time, &source)) {
        float height = boat_water_query_field(world->water, world->wake_field, source, time).height;
        draw(context,
            (Transform3D) {
                .position = { -source.x, height + .25f, source.y }, .scale = { 4.2f, .5f, 1.5f } },
            (Color) { 205, 151, 64, 255 });
        draw(context,
            (Transform3D) { .position = { -source.x + .5f, height + .8f, source.y },
                .scale = { 1.4f, .7f, 1.15f } },
            (Color) { 226, 222, 202, 255 });
    }
}

static void environment_draw(void *context, Transform3D transform, Color color)
{
    renderer3d_draw_box(context, transform, color);
}

static void basin_box(void *context, Transform3D transform, Color color)
{
    WaterfrontSceneFrame *frame = context;
    assert(frame->scenery_count < (int)(sizeof(frame->scenery) / sizeof(*frame->scenery)));
    int i = frame->scenery_count++;
    frame->scenery[i].transform = transform;
    frame->scenery[i].color = color;
}

static void basin_scenery(
    const BoatViewState *view, const BoatWorld *world, WaterfrontSceneFrame *frame)
{
    Vector2 half = world->definition->half_extent;
    frame->boating_basin = true;
    frame->lamp_count = 0;
    Color quay = { 148, 151, 137, 255 };
    for (int side = -1; side <= 1; side += 2) {
        basin_box(frame,
            (Transform3D) {
                .position = { side * (half.x + 2), 0, 0 }, .scale = { 4, 1.8f, 2 * half.y + 8 } },
            quay);
        basin_box(frame,
            (Transform3D) {
                .position = { 0, 0, side * (half.y + 2) }, .scale = { 2 * half.x, 1.8f, 4 } },
            quay);
        if (!view->course.enabled) {
            basin_box(frame,
                (Transform3D) { .position = { side * 12, .4f, 0 }, .scale = { .8f, 1, .8f } },
                (Color) { 232, 150, 65, 255 });
            basin_box(frame,
                (Transform3D) { .position = { side * 12, 1.1f, 0 }, .scale = { .25f, .5f, .25f } },
                (Color) { 248, 225, 175, 255 });
        }
    }
    if (!boat_world_displaced(world))
        for (int z = -(int)half.y + 4; z < half.y; z += 8)
            for (int x = -(int)half.x + 4; x < half.x; x += 8)
                basin_box(frame,
                    (Transform3D) {
                        .position = { x + (z % 4), .01f, z }, .scale = { 1.3f, .04f, .12f } },
                    (Color) { 62, 132, 142, 255 });
    boat_course_visit(&view->course, basin_box, frame);
    environment_visit(view, world, basin_box, frame);
}

// Live water particles of PRESENTATION as render pieces, at most CAPACITY.
static int pose_particles(const BoatPresentation *presentation, const BoatWorld *world, double time,
    WaterfrontPlayerParticle *out, int capacity)
{
    int count = 0;
    for (int i = 0; i < BOAT_WATER_PARTICLES && count < capacity; ++i) {
        const BoatWaterParticle *water = &presentation->water[i];
        if (water->lifetime <= 0
            || (world->definition->mapped
                && !boat_map_water(&world->resources->map, water->position, 0)))
            continue;
        float size = water_particle_size(water);
        out[count++] = (WaterfrontPlayerParticle) { .transform
            = water_particle_pose(time, world, water, size),
            .color = water_particle_color(world->definition, water),
            .spray = water->spray };
    }
    return count;
}

static void draw_skinned_boat(Renderer3D *renderer, const WaterfrontPlayerState *boat)
{
    renderer3d_draw_mesh(renderer, boat_skin_mesh(boat->skin, BOAT_SKIN_HULL), boat->hull);
    renderer3d_draw_mesh(renderer, boat_skin_mesh(boat->skin, BOAT_SKIN_COCKPIT), boat->cabin);
    for (int side = -1; side <= 1; side += 2)
        renderer3d_draw_mesh(renderer, boat_skin_mesh(boat->skin, BOAT_SKIN_ENGINE),
            boat_skin_outboard_pose(boat->hull, boat->engine, side));
    BoatFlagMesh flag_storage;
    Mesh3D flag_mesh = boat_flag_skin_mesh(&boat->flag, boat->skin, &flag_storage);
    renderer3d_draw_mesh(renderer, &flag_mesh, (Transform3D) { .scale = { 1, 1, 1 } });
}

void boat_scene_frame(const BoatViewState *view, const BoatPresentation *presentation,
    const BoatWorld *world, BoatCamera camera, WaterfrontSceneFrame *frame)
{
    const Boat *boat = &view->boat;
    double time = view->time;
    _Static_assert(BOAT_WATER_PARTICLES <= WATERFRONT_PLAYER_PARTICLES, "Player particle capacity");
    waterfront_scene_evaluate(time, world->water, frame);
    frame->camera = boat_camera(boat, presentation, world->definition, camera);
    frame->player_active = true;
    if (!world->definition->mapped)
        basin_scenery(view, world, frame);
    else
        environment_visit(view, world, basin_box, frame);
    frame->player_water = world->water;
    WaterfrontPlayerState *p = &frame->player;
    boat_player_pose(view, &presentation->flag, p);
    if (world->definition->mapped)
        waterfront_boating_gulls(
            time, p->hull.position, (Vector3) { -boat->velocity.x, 0, boat->velocity.y }, frame);
    p->particle_count
        = pose_particles(presentation, world, time, p->particles, WATERFRONT_PLAYER_PARTICLES);
}

static int pose_rivals(const BoatRivals *rivals, const BoatWorld *world, double time,
    WaterfrontRivalState out[WATERFRONT_RIVALS])
{
    int count = 0;
    for (unsigned i = 0; rivals && i < rivals->count && i < WATERFRONT_RIVALS; ++i) {
        const BoatRival *rival = &rivals->boats[i];
        static WaterfrontPlayerState pose; // Large; drawing is single-threaded.
        pose = (WaterfrontPlayerState) { 0 };
        boat_player_pose(
            &rival->view, rival->presentation ? &rival->presentation->flag : NULL, &pose);
        WaterfrontRivalState *r = &out[count++];
        *r = (WaterfrontRivalState) {
            .hull = pose.hull, .cabin = pose.cabin, .engine = pose.engine, .skin = pose.skin
        };
        r->flag = pose.flag;
        if (rival->presentation)
            r->particle_count = pose_particles(
                rival->presentation, world, time, r->particles, WATERFRONT_RIVAL_PARTICLES);
    }
    return count;
}

void boat_scene_rivals(
    const BoatRivals *rivals, const BoatWorld *world, double time, WaterfrontSceneFrame *frame)
{
    frame->rival_count = pose_rivals(rivals, world, time, frame->rivals);
}

void boat_cpu_view_destroy(BoatCpuView *cpu)
{
    boat_water_mesh_cache_destroy(&cpu->water_mesh);
    waterfront_scenery_destroy(&cpu->scenery);
}

bool boat_draw(Renderer3D *renderer, BoatCpuView *cpu, Color *pixels, int width, int height,
    const BoatViewState *view, const BoatPresentation *presentation, const BoatWorld *world,
    BoatCamera camera_settings)
{
    return boat_draw_with_ghost(renderer, cpu, pixels, width, height, view, presentation, world,
        camera_settings, NULL, NULL, 0);
}

bool boat_draw_with_ghost(Renderer3D *renderer, BoatCpuView *cpu, Color *pixels, int width,
    int height, const BoatViewState *view, const BoatPresentation *presentation,
    const BoatWorld *world, BoatCamera camera_settings, const BoatViewState *ghost,
    const BoatFlag *ghost_flag, float opacity)
{
    return boat_draw_scene(renderer, cpu, pixels, width, height, view, presentation, world,
        camera_settings, NULL, ghost, ghost_flag, opacity);
}

static bool water_visible(void *renderer, Vector3 min, Vector3 max)
{
    return renderer3d_box_visible(renderer, min, max);
}

bool boat_draw_scene(Renderer3D *renderer, BoatCpuView *cpu, Color *pixels, int width, int height,
    const BoatViewState *view, const BoatPresentation *presentation, const BoatWorld *world,
    BoatCamera camera_settings, const BoatRivals *rivals, const BoatViewState *ghost,
    const BoatFlag *ghost_flag, float opacity)
{
    const Boat *boat = &view->boat;
    double time = view->time;
    Vector2 half = world->definition->half_extent;
    for (int i = 0; i < width * height; ++i)
        pixels[i] = (Color) { 21, 36, 48, 255 };
    View3D camera = boat_camera(boat, presentation, world->definition, camera_settings);
    if (world->definition->mapped) {
        if (!waterfront_scenery_bind(&cpu->scenery, world->definition->map)
            || !waterfront_draw_boating(renderer, &cpu->scenery, pixels, width, height, camera,
                boat_world_displaced(world)))
            return false;
    } else {
        if (!renderer3d_begin(renderer, pixels, width, height, camera, (Vector3) { -1, 3, 2 }))
            return false;

        if (!boat_world_displaced(world)) {
            box(renderer, (Vector3) { 0, -0.6f, 0 }, (Vector3) { 2 * half.x, 1, 2 * half.y },
                (Color) { 36, 107, 125, 255 });
            // Static water marks provide a speed reference.
            for (int z = -(int)half.y + 4; z < half.y; z += 8)
                for (int x = -(int)half.x + 4; x < half.x; x += 8)
                    box(renderer, (Vector3) { x + (z % 4), -0.08f, z },
                        (Vector3) { 1.3f, 0.04f, 0.12f }, (Color) { 62, 132, 142, 255 });
        }
    }
    if (boat_world_displaced(world)) {
        const Mesh3D *surface = world->wake_field
            ? boat_water_surface_field(&cpu->water_mesh, world->water, world->wake_field, time)
            : boat_water_surface_visible(&cpu->water_mesh, world->definition->map, world->water,
                time, water_visible, renderer);
        if (!surface)
            return false;
        renderer3d_draw_emissive_mesh(renderer, surface, (Transform3D) { .scale = { 1, 1, 1 } });
    }
    if (world->definition->mapped) {
        WaterfrontTrafficFrame traffic;
        waterfront_traffic_evaluate(world->water, time, &traffic);
        waterfront_draw_traffic(renderer, &traffic);
        WaterfrontHarborFrame harbor;
        waterfront_harbor_evaluate(world->water, time, &harbor);
        waterfront_draw_harbor(renderer, &harbor);
        WaterfrontSceneFrame birds = { 0 };
        waterfront_boating_gulls(time,
            (Vector3) { -boat->position.x, boat->height, boat->position.y },
            (Vector3) { -boat->velocity.x, 0, boat->velocity.y }, &birds);
        for (int i = 0; i < birds.gull_count; ++i)
            for (int part = 0; part < WATERFRONT_GULL_PARTS; ++part)
                renderer3d_draw_emissive_mesh(
                    renderer, waterfront_gull_mesh(part), birds.gulls[i][part]);
    }
    for (int i = 0; i < BOAT_WATER_PARTICLES; ++i) {
        const BoatWaterParticle *p = &presentation->water[i];
        if (p->lifetime <= 0)
            continue;
        if (world->definition->mapped && !boat_map_water(&world->resources->map, p->position, 0))
            continue;
        renderer3d_draw_box(renderer, water_particle_pose(time, world, p, water_particle_size(p)),
            water_particle_color(world->definition, p));
    }
    static WaterfrontRivalState others[WATERFRONT_RIVALS]; // Drawing is single-threaded.
    int other_count = pose_rivals(rivals, world, time, others);
    for (int i = 0; i < other_count; ++i)
        for (int k = 0; k < others[i].particle_count; ++k)
            renderer3d_draw_box(
                renderer, others[i].particles[k].transform, others[i].particles[k].color);
    if (!world->definition->mapped) {
        Color quay = { 148, 151, 137, 255 };
        for (int side = -1; side <= 1; side += 2) {
            box(renderer, (Vector3) { side * (half.x + 2), 0, 0 },
                (Vector3) { 4, 1.8f, 2 * half.y + 8 }, quay);
            box(renderer, (Vector3) { 0, 0, side * (half.y + 2) },
                (Vector3) { 2 * half.x, 1.8f, 4 }, quay);
        }
        // Only the basin walls collide in this sketch; markers are visual turning references.
        for (int side = -1; !view->course.enabled && side <= 1; side += 2) {
            box(renderer, (Vector3) { side * 12, 0.4f, 0 }, (Vector3) { 0.8f, 1, 0.8f },
                (Color) { 232, 150, 65, 255 });
            box(renderer, (Vector3) { side * 12, 1.1f, 0 }, (Vector3) { 0.25f, 0.5f, 0.25f },
                (Color) { 248, 225, 175, 255 });
        }
    }
    environment_visit(view, world, environment_draw, renderer);
    boat_course_draw(&view->course, renderer);
    static WaterfrontPlayerState player; // Large; drawing is single-threaded.
    player = (WaterfrontPlayerState) { 0 };
    boat_player_pose(view, &presentation->flag, &player);
    draw_skinned_boat(renderer, &player);
    for (int i = 0; i < other_count; ++i) {
        const WaterfrontRivalState *r = &others[i];
        player.hull = r->hull;
        player.cabin = r->cabin;
        player.engine = r->engine;
        player.skin = r->skin;
        player.flag = r->flag;
        draw_skinned_boat(renderer, &player);
    }
    if (ghost && opacity > 0) {
        boat_player_pose(ghost, ghost_flag, &player);
        renderer->ghost_opacity = opacity;
        draw_skinned_boat(renderer, &player);
        renderer->ghost_opacity = 0;
    }
    return true;
}
