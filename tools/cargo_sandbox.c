// Native cargo handling toy. Its work sites and job come from a bundled package per location
// (S1a-S1c); no progression, files or network state enter the simulation.
#include "boat_assets.h"
#include "boat_cargo.h"
#include "boat_cargo_view.h"
#include "boat_file.h"
#include "boat_model.h"
#include "boat_plugin_catalog.h"
#include "boat_plugin_state.h"
#include "boat_sandbox_save.h"
#include "boat_scenery.h"
#include "boat_water_mesh.h"
#include "boat_work_save.h"
#include "boat_work_session.h"
#include "raymath.h"
#include "renderer3d.h"
#include "renderer_gpu.h"
#include "rlgl.h"
#include "waterfront.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum { WIDTH = 480, HEIGHT = 300, MAX_VERTICES = BOAT_MODEL_MAX_TRIANGLES * 3 };
static const Color yellow = { 241, 183, 57, 255 }, teal = { 45, 116, 127, 255 };
static const Color ink = { 20, 31, 42, 255 }, green = { 99, 216, 156, 255 };
static bool shadow_review;
static const Vector3 shadow_review_positions[]
    = { { .65f, 2.7f, .65f }, { .65f, 2.7f, -2.35f }, { 2.8f, 2.7f, .65f }, { -5, 2.7f, 0 },
          { .65f, 1.4f, .65f }, { .65f, 1.4f, -2.35f }, { 2.8f, 2.7f, -2.2f }, { 3.5f, 2.7f, 1 } };
static const int shadow_review_surfaces[] = { 0, 0, 1, 0, 0, 0, 1, 1 };
enum { SHADOW_REVIEW_FRAMES = 256 };
static int shadow_pixel_count(const Color *pixels, int frame)
{
    int count = 0, surface = shadow_review_surfaces[frame / 32];
    for (int i = 0; i < WIDTH * HEIGHT; ++i) {
        Color p = pixels[i];
        count += surface == 0 ? p.r > 120 && p.g < 8 && p.b < 8
            : surface == 1    ? p.r > 120 && p.g < 8 && p.b > 120
                              : p.r < 8 && p.g < 8 && p.b > 120;
    }
    return count;
}

typedef BoatCargoViewGpu GpuMesh;

typedef struct {
    WaterfrontScenery scenery;
    BoatWaterMeshCache water;
    WaterfrontHarborModels harbor;
    WaterfrontGpuScene gpu;
    WaterfrontSceneFrame frame;
    Mesh patch;
    Vector2 patch_centre;
    int patch_capacity;
} HarbourView;

static BoatCargoView view_of(const BoatWorkSession *e)
{
    return (BoatCargoView) { .sandbox = &e->sandbox,
        .boat = &e->boat,
        .world = &e->world,
        .tick = e->tick,
        .crane_view = e->crane_view };
}

static bool water_visible(void *context, Vector3 lo, Vector3 hi)
{
    return renderer3d_box_visible(context, lo, hi);
}

static bool harbour_cpu(
    HarbourView *h, Renderer3D *cpu, Color *pixels, View3D camera, const BoatWorkSession *e)
{
    WaterfrontLighting light = waterfront_lighting(WATERFRONT_DAYLIGHT_DAY);
    if (!waterfront_draw_boating(cpu, &h->scenery, pixels, WIDTH, HEIGHT, camera, true,
            (WaterfrontSquall) { 0 }, &light))
        return false;
    const Mesh3D *water = boat_water_surface_visible(&h->water, e->world.definition->map,
        e->world.water, boat_tick_seconds(e->tick), water_visible, cpu);
    if (!water)
        return false;
    renderer3d_draw_emissive_mesh(cpu, water, (Transform3D) { .scale = { 1, 1, 1 } });
    waterfront_draw_traffic(cpu, &h->frame.traffic);
    return waterfront_draw_harbor(cpu, &h->harbor, &h->frame.harbor);
}

static bool harbour_gpu_water(HarbourView *h, const BoatWorkSession *e)
{
    Vector2 centre
        = { roundf(-e->boat.position.x / 12) * 12, roundf(e->boat.position.y / 12) * 12 };
    if (h->patch.vaoId && Vector2Equals(centre, h->patch_centre))
        return true;
    const Mesh3D *source = boat_water_patch(
        &h->water, e->world.definition->map, (Vector2) { -e->boat.position.x, e->boat.position.y });
    if (!source)
        return false;
    int count = (int)source->triangle_count * 3;
    if (!h->patch.vaoId || count > h->patch_capacity) {
        if (h->patch.vaoId)
            UnloadMesh(h->patch);
        h->patch_capacity = count;
        h->patch = (Mesh) { .vertexCount = count, .triangleCount = count / 3 };
        h->patch.vertices = MemAlloc((unsigned int)count * 3 * sizeof(float));
        if (!h->patch.vertices)
            return false;
        memset(h->patch.vertices, 0, (size_t)count * 3 * sizeof(float));
        UploadMesh(&h->patch, true);
    }
    for (int i = 0; i < count; ++i) {
        Vector3 v = source->vertices[source->triangles[i / 3].vertices[i % 3]];
        memcpy(h->patch.vertices + 3 * i, &v, sizeof(v));
    }
    h->patch.vertexCount = count;
    h->patch.triangleCount = count / 3;
    UpdateMeshBuffer(h->patch, 0, h->patch.vertices, count * 3 * sizeof(float), 0);
    h->patch_centre = centre;
    h->gpu.near_water = h->patch; // Borrowed by the scene, exactly as in BoatGpu.
    return h->patch.vaoId != 0;
}

static bool shadow_stream_check(GpuMesh *gpu, RenderTexture2D target)
{
    static BoatModel triangle;
    boat_model_begin(&triangle);
    boat_model_triangle(
        &triangle, (Vector3) { -1, -1, 0 }, (Vector3) { 1, -1, 0 }, (Vector3) { 0, 1, 0 }, RED);
    Camera3D camera = { { 0, 0, 4 }, { 0 }, { 0, 1, 0 }, 55, CAMERA_PERSPECTIVE };
    int counts[2];
    for (int pass = 0; pass < 2; ++pass) {
        if (pass)
            boat_model_begin(&triangle);
        BeginTextureMode(target);
        ClearBackground(BLACK);
        BeginMode3D(camera);
        boat_cargo_view_gpu_draw(gpu, &triangle.mesh, false);
        EndMode3D();
        EndTextureMode();
        Image capture = LoadImageFromTexture(target.texture);
        Color *pixels = LoadImageColors(capture);
        counts[pass] = 0;
        for (int i = 0; i < WIDTH * HEIGHT; ++i)
            counts[pass] += pixels[i].r > 120 && pixels[i].g < 80 && pixels[i].b < 80;
        UnloadImageColors(pixels);
        UnloadImage(capture);
    }
    printf("shadow stream: populated=%d pixels; empty=%d pixels\n", counts[0], counts[1]);
    return counts[0] > 20 && counts[1] == 0;
}

static bool button(Rectangle r, const char *label, Vector2 mouse, bool enabled, bool held)
{
    bool over = enabled && CheckCollisionPointRec(mouse, r);
    DrawRectangleRec(r, over ? teal : (Color) { 42, 58, 69, 255 });
    DrawText(label, (int)r.x + 5, (int)r.y + 6, 10, enabled ? RAYWHITE : GRAY);
    return over
        && (held ? IsMouseButtonDown(MOUSE_BUTTON_LEFT) : IsMouseButtonPressed(MOUSE_BUTTON_LEFT));
}

// A screen marker over the target berth, so a distant site can be found from the helm.
static void target_marker(const BoatWorkSession *e, View3D view)
{
    Vector3 berth = boat_cargo_site_point(&e->sandbox.cranes[e->target], (Vector3) { 0, 4, 0 });
    Vector3 forward = Vector3Subtract(view.target, view.position);
    if (Vector3DotProduct(Vector3Subtract(berth, view.position), forward) <= 0)
        return;
    Camera3D camera
        = { view.position, view.target, view.up, view.vertical_fov * RAD2DEG, CAMERA_PERSPECTIVE };
    Vector2 p = GetWorldToScreenEx(berth, camera, WIDTH, HEIGHT);
    p.x = WIDTH - p.x; // Every view uses the game's mirrored presentation (starboard right).
    // The helm camera looks down at the boat, so a distant berth is usually above the view:
    // pin the marker to the view's edge and point it outward.
    bool above = p.y < 72;
    p.x = Clamp(p.x, 12, WIDTH - 12);
    p.y = Clamp(p.y, 72, 210);
    if (above)
        DrawTriangle((Vector2) { p.x, p.y - 10 }, (Vector2) { p.x - 6, p.y },
            (Vector2) { p.x + 6, p.y }, yellow);
    else
        DrawTriangle((Vector2) { p.x - 5, p.y - 8 }, (Vector2) { p.x, p.y },
            (Vector2) { p.x + 5, p.y - 8 }, yellow);
    Vector2 to = Vector2Subtract(boat_cargo_berth(&e->sandbox.cranes[e->target]), e->boat.position);
    const char *label
        = TextFormat("%s %.0f m", TextToUpper(e->sites[e->target].id), Vector2Length(to));
    int width = MeasureText(label, 10);
    DrawText(label, (int)Clamp(p.x - width / 2.0f, 4, WIDTH - 4 - width),
        above ? (int)p.y + 3 : (int)p.y - 20, 10, yellow);
}

// The job's committed HUD rows, right-aligned under the status bar.
static void job_ui(const BoatWorkSession *e)
{
    if (!e->revision)
        return;
    int y = 61;
    for (unsigned i = 0; i <= e->objective.hud_count; ++i) {
        const BoatPluginHud *row = &e->objective.hud[i];
        const char *text = i < e->objective.hud_count
            ? TextFormat("%s: %lld", row->label, (long long)row->value)
            : e->objective.quarantined ? "JOB SUSPENDED"
                                       : e->waiting;
        if (!text)
            break;
        int width = MeasureText(text, 10);
        DrawRectangle(WIDTH - width - 14, y - 3, width + 10, 15, ink);
        DrawText(text, WIDTH - width - 9, y, 10,
            i < e->objective.hud_count ? yellow : (Color) { 232, 106, 84, 255 });
        y += 16;
    }
}

static void ui(
    BoatWorkSession *e, View3D view, Vector2 mouse, float *slew, float *reach, float *hoist)
{
    const BoatCargo *c = &e->sandbox.cargo;
    BoatCargoMoorStatus berth = boat_cargo_moor_status(&e->sandbox, &e->boat);
    const char *states[] = { "ON PAD", "ON HOOK", "LOOSE ON DECK", "LASHED", "FREE / FLOATING" };
    const char *state = states[c->state];
    if (c->state == BOAT_CARGO_PAD
        && e->sandbox.cranes[c->pad].preset == BOAT_SCENE_WORK_RECEIVING_PLATFORM)
        state = "ON RECEIVING PAD";
    DrawRectangle(0, 0, WIDTH, 57, ink);
    DrawText(TextFormat("%s / CARGO TOY", TextToUpper(e->sites[berth.site].id)), 9, 6, 10, yellow);
    const char *throttle = fabsf(e->boat.throttle) < .005f
        ? "THROTTLE IDLE"
        : TextFormat("THROTTLE %s %.0f%%", e->boat.throttle > 0 ? "FWD" : "REV",
              fabsf(e->boat.throttle) * 100);
    DrawText(throttle, 280, 6, 10, RAYWHITE);
    DrawRectangle(394, 8, 78, 5, (Color) { 55, 70, 77, 255 });
    float thrust = e->boat.throttle * 38;
    DrawRectangle(
        (int)(433 + fminf(thrust, 0)), 8, (int)fabsf(thrust), 5, thrust >= 0 ? green : yellow);
    DrawLine(433, 6, 433, 15, RAYWHITE);
    DrawText(TextFormat("%s  |  %s  |  %s  |  %.1f m/s",
                 e->crane_view ? "CRANE CONTROLS" : "HELM CONTROLS",
                 e->sandbox.moored        ? "MOORED"
                     : e->sandbox.mooring ? "MOORING"
                                          : "UNMOORED",
                 state, Vector2Length(e->boat.velocity)),
        9, 21, 10, RAYWHITE);
    if (e->crane_view) {
        BoatCargoShadow deck = boat_cargo_shadow(
            &e->sandbox, &e->boat, &e->world, boat_tick_seconds(e->tick), BOAT_CARGO_SHADOW_DECK);
        if (c->state == BOAT_CARGO_HANGING && deck.count)
            DrawText(TextFormat("DECK GAP %.2f m / E LOWERS / Q RAISES", fmaxf(0, deck.clearance)),
                9, 40, 10, yellow);
        else
            DrawText(
                "DARK FOOTPRINT = LANDING SURFACE / TAB SWITCHES CONTROLS", 9, 40, 10, RAYWHITE);
    } else if (berth.site != e->target && !e->sandbox.moored && !e->sandbox.mooring) {
        // Away from the target, steer toward it; its own berth takes over on arrival.
        Vector2 to
            = Vector2Subtract(boat_cargo_berth(&e->sandbox.cranes[e->target]), e->boat.position);
        float turn = remainderf(atan2f(to.x, -to.y) - e->boat.heading, 2 * PI);
        DrawText(TextFormat("TO %s %.0f m / %s %.0f deg / N: NEXT SITE",
                     TextToUpper(e->sites[e->target].id), Vector2Length(to),
                     turn < 0 ? "LEFT" : "RIGHT", fabsf(turn) * RAD2DEG),
            9, 40, 10, yellow);
        target_marker(e, view);
    } else {
        Vector2 to
            = Vector2Subtract(boat_cargo_berth(&e->sandbox.cranes[berth.site]), e->boat.position);
        float turn = berth.reason == BOAT_CARGO_MOOR_DISTANCE
            ? remainderf(atan2f(to.x, -to.y) - e->boat.heading, 2 * PI)
            : berth.heading_error;
        DrawText(
            TextFormat("BERTH %.1f m / %s %.0f deg / ENGINE %+03.0f%%", berth.distance,
                turn < 0 ? "LEFT" : "RIGHT", fabsf(turn) * RAD2DEG, e->boat.engine_response * 100),
            9, 40, 10, berth.reason == BOAT_CARGO_MOOR_READY ? green : RAYWHITE);
        if (!e->sandbox.moored && !e->sandbox.mooring && berth.distance > 12)
            target_marker(e, view);
    }
    DrawRectangle(0, 218, WIDTH, 82, ink);
    const char *prompt = e->notice;
    if (e->sandbox.mooring)
        prompt = "MOORING ASSIST: stopping and aligning. G: cancel.";
    else if (!e->crane_view && e->sandbox.moored)
        prompt = "MOORED. TAB: crane controls. G: unmoor and drive.";
    else if (e->braking)
        prompt = "BRAKING: throttle idle, slowing to a stop. G when BERTH READY.";
    else if (e->crane_view && !e->sandbox.moored)
        prompt = "Ready to drive. TAB switches to HELM CONTROLS; arrows drive.";
    else if (!e->crane_view && !e->sandbox.moored && berth.reason == BOAT_CARGO_MOOR_READY)
        prompt = "BERTH READY. G: assisted mooring. SPACE: brake.";
    else if (!e->crane_view && !e->sandbox.moored && berth.distance < 12)
        prompt = berth.reason == BOAT_CARGO_MOOR_FAST
            ? "Hold SPACE to brake below 1.5 m/s, then G: assisted mooring."
            : berth.reason == BOAT_CARGO_MOOR_HEADING
            ? "Turn toward the berth's bow arrow; G when BERTH READY."
            : "Approach the boat outline slowly; SPACE brakes; G when ready.";
    else if (c->state == BOAT_CARGO_SECURED && e->sandbox.moored)
        prompt = "Cargo secured. G: unmoor, TAB: helm controls, arrows: drive.";
    else if (c->state == BOAT_CARGO_HANGING && c->deck_contact)
        prompt = "Resting on deck. F: release hook, wait briefly, L: lash.";
    else if (c->state == BOAT_CARGO_LOOSE && c->settled >= .5f)
        prompt = "Settled. L: lash, G: unmoor, TAB: helm. Arrows to drive.";
    else if (e->aim)
        prompt = "Aim assist lifting / slewing. E: lower slowly onto the deck.";
    else if (c->state == BOAT_CARGO_FLOATING && c->wet)
        prompt = "Crate in the water. C: return it to its source pad.";
    DrawText(prompt, 8, 221, 10, RAYWHITE);
    if (button((Rectangle) { 8, 236, 70, 22 }, "F Hook", mouse, !e->paused, false))
        boat_work_hook_action(e);
    if (button((Rectangle) { 82, 236, 70, 22 }, "L Lash", mouse, !e->paused, false))
        boat_work_lash_action(e);
    if (button((Rectangle) { 156, 236, 70, 22 },
            e->sandbox.mooring      ? "G Cancel"
                : e->sandbox.moored ? "G Unmoor"
                                    : "G Moor",
            mouse, !e->paused, false))
        boat_work_moor_action(e);
    if (button((Rectangle) { 230, 236, 85, 22 }, e->crane_view ? "TAB Helm" : "TAB Crane", mouse,
            !e->paused && (e->crane_view || e->sandbox.moored), false))
        boat_work_view_action(e);
    if (button((Rectangle) { 319, 236, 72, 22 }, "R Reset", mouse, true, false))
        boat_work_session_reset(e);
    if (button((Rectangle) { 395, 236, 77, 22 }, e->paused ? "P Resume" : "P Pause", mouse, true,
            false))
        e->paused = !e->paused;
    if (e->crane_view) {
        bool enabled = !e->paused && e->sandbox.moored;
        const char *labels[] = { "A <", "D >", "W Out", "S In", "Q Up", "E Down" };
        float *rates[] = { slew, slew, reach, reach, hoist, hoist };
        const float signs[] = { -1, 1, 1, -1, 1, -1 };
        for (int i = 0; i < 6; ++i)
            if (button((Rectangle) { 8 + i * 52, 263, 48, 22 }, labels[i], mouse, enabled, true))
                *rates[i] = signs[i];
        for (int i = 1; i <= 3; ++i)
            if (button((Rectangle) { 324 + (i - 1) * 50, 263, 46, 22 },
                    i == 1       ? "1 Pad"
                        : i == 2 ? "2 Left"
                                 : "3 Rgt",
                    mouse, enabled, false))
                e->aim = i;
    } else
        DrawText("UP/DOWN: throttle  LEFT/RIGHT: steer  SPACE: brake  N: next site", 8, 269, 10,
            RAYWHITE);
    job_ui(e);
    if (e->paused)
        DrawText("PAUSED", 205, 100, 20, yellow);
}

static bool stage(BoatWorkSession *e, const char *name)
{
    if (!strcmp(name, "origin"))
        return true;
    if (!strcmp(name, "swing")) {
        e->notice = "Swing fixture. F: release, R: recover crate and boat.";
        boat_cargo_hook_toggle(&e->sandbox);
        for (int i = 0; i < 360; ++i)
            boat_work_session_tick(e, .7f, .5f, 1, (BoatInput) { 0 });
    } else if (!strcmp(name, "shadow-bow") || !strcmp(name, "shadow-water")) {
        for (int i = 0; i < 420; ++i)
            boat_work_session_tick(e, 0, 0, 0, (BoatInput) { 0 });
        Vector3 p
            = !strcmp(name, "shadow-bow") ? shadow_review_positions[5] : shadow_review_positions[6];
        BoatCargoCrane *crane = &e->sandbox.cranes[e->sandbox.active];
        e->sandbox.cargo.position = boat_cargo_site_point(crane, p);
        e->sandbox.cargo.state = BOAT_CARGO_HANGING;
        e->sandbox.cargo.rotation = (Vector3) { 0 };
        e->sandbox.cargo.deck_contact = false;
        crane->angle = atan2f(p.x + 4, p.z - 3);
        crane->reach = hypotf(p.x + 4, p.z - 3);
        crane->rope = 5.5f - p.y - BOAT_CARGO_HALF;
    } else if (!strcmp(name, "shadow-high") || !strcmp(name, "shadow-low")) {
        if (!boat_cargo_hook_toggle(&e->sandbox))
            return false;
        e->aim = 2;
        for (int i = 0; i < 2000; ++i)
            boat_work_session_tick(e, 0, 0, 0, (BoatInput) { 0 });
        e->aim = 0;
        float gap = !strcmp(name, "shadow-high") ? 1.2f : .2f;
        BoatCargoShadow shadow = { 0 };
        for (int i = 0; i < 900; ++i) {
            shadow = boat_cargo_shadow(&e->sandbox, &e->boat, &e->world, boat_tick_seconds(e->tick),
                BOAT_CARGO_SHADOW_DECK);
            if (shadow.count && shadow.clearance <= gap)
                break;
            boat_work_session_tick(e, 0, 0, -.5f, (BoatInput) { 0 });
        }
        return shadow.count && shadow.clearance > .05f && shadow.clearance <= gap;
    } else if (!strcmp(name, "berth-approach") || !strcmp(name, "mooring")
        || !strcmp(name, "remoor")) {
        // Explicit returning-boat review pose; subsequent capture uses the real G/tick path.
        if (!stage(e, "departure"))
            return false; // Carry a crate obtained through the actual lift/lash path.
        e->crane_view = false;
        Vector3 approach = boat_cargo_site_point(boat_work_active(e), (Vector3) { 1.8f, 0, 4.3f });
        e->boat.position = (Vector2) { -approach.x, approach.z };
        e->boat.heading = boat_work_active(e)->heading + .5f;
        e->boat.velocity = (Vector2) { .2f, -.8f };
        e->boat.throttle = .25f;
        e->boat.engine_response = .2f;
        boat_cargo_step(&e->sandbox, &e->boat, &e->world, boat_tick_seconds(e->tick));
        if (!strcmp(name, "berth-approach"))
            return boat_cargo_moor_status(&e->sandbox, &e->boat).reason == BOAT_CARGO_MOOR_READY;
        boat_work_moor_action(e);
        if (!e->sandbox.mooring)
            return false;
        int ticks = !strcmp(name, "mooring") ? 120 : 900;
        for (int i = 0; i < ticks && !e->sandbox.moored; ++i)
            boat_work_session_tick(e, 0, 0, 0, (BoatInput) { 0 });
        return !strcmp(name, "mooring") ? e->sandbox.mooring
                                        : e->sandbox.moored && !e->sandbox.mooring
                && e->sandbox.cargo.state == BOAT_CARGO_SECURED;
    } else if (!strcmp(name, "crossing")) {
        // Helm review a third of the way out, slightly off the line to the guided next site.
        if (!stage(e, "departure"))
            return false;
        e->crane_view = false;
        Vector2 from = e->boat.position;
        Vector2 to = boat_cargo_berth(&e->sandbox.cranes[e->target]);
        e->boat.position = Vector2Lerp(from, to, .3f);
        Vector2 ahead = Vector2Subtract(to, e->boat.position);
        e->boat.heading = atan2f(ahead.x, -ahead.y) + .25f;
        for (int i = 0; i < 120; ++i)
            boat_work_session_tick(e, 0, 0, 0, (BoatInput) { 0 });
        return e->target != e->sandbox.active;
    } else if (!strcmp(name, "redelivered") || !strcmp(name, "job-reset")) {
        // S1c/S1d exactly-once review: after the counted delivery the job used the crate up and
        // supplied load 2 at the quay. Setting load 2 on the landing's pad (a pose in place of a
        // crossing) commits a native delivery there, which the job ignores: it counts load 1 at
        // the landing once and wants load 2 at the jetty. R then restarts everything together.
        if (!stage(e, "delivered") || !e->revision)
            return false;
        unsigned landing = boat_work_find_site(e, "landing");
        if (landing == e->site_count)
            return false;
        e->sandbox.cargo.state = BOAT_CARGO_FLOATING;
        e->sandbox.cargo.position
            = boat_cargo_site_point(&e->sandbox.cranes[landing], (Vector3) { -5, 1.6f, 0 });
        for (int i = 0; i < 600; ++i)
            boat_work_session_tick(e, 0, 0, 0, (BoatInput) { 0 });
        const BoatCargo *c = &e->sandbox.cargo;
        BoatPluginObjective o = e->objective;
        bool once = c->state == BOAT_CARGO_PAD && c->pad == landing && c->load == 2
            && c->receivers == 1u << landing && !o.quarantined && o.acknowledged == e->sequence
            && boat_work_hud_value(e, "LANDING BUILT") == 1
            && boat_work_hud_value(e, "SUPPLIES TO JETTY / 1") == 0;
        if (!strcmp(name, "redelivered"))
            return once;
        boat_work_session_reset(e);
        for (int i = 0; i < 24; ++i)
            boat_work_session_tick(e, 0, 0, 0, (BoatInput) { 0 });
        bool hidden = true;
        for (unsigned i = 0; i < e->site_count; ++i)
            hidden &= !e->sites[i].hidden || e->sandbox.cranes[i].hidden;
        return once && hidden && !c->delivered && c->load == 1 && e->guide == e->site_count
            && e->objective.hud_count == 1 && boat_work_hud_value(e, "CRATES TO LANDING / 1") == 0;
    } else if (!strcmp(name, "delivery-pending")) {
        // SV1c resume fixture: the core has delivered the crate but the job has not counted it
        // yet, so the delivery event is still queued and unacknowledged.
        unsigned landing = boat_work_find_site(e, "landing");
        if (!e->revision || landing == e->site_count || !stage(e, "departure"))
            return false;
        bool ok = boat_work_script_unload_at(e, landing, false, true);
        return ok && e->sandbox.cargo.delivered && e->event_count == 1
            && boat_work_hud_value(e, "LANDING BUILT") == -1;
    } else if (!strcmp(name, "capsized")) {
        // SV1c resume fixture: the crossing, then the boat capsizes with the crate lashed on.
        if (!stage(e, "crossing"))
            return false;
        e->boat.motion_state = BOAT_CAPSIZED;
        return e->sandbox.cargo.state == BOAT_CARGO_SECURED;
    } else if (!strcmp(name, "build-blocked")) {
        // S1c: a build the boat would sit inside waits, keeping the job's last checkpoint and
        // its delivery event, and happens at the first update after the boat moves clear. The
        // crate is posed onto the landing's pad; the delivery itself is the native commit.
        if (!e->revision)
            return false;
        unsigned landing = boat_work_find_site(e, "landing"),
                 quay = boat_work_find_site(e, "landing_quay");
        if (landing == e->site_count || quay == e->site_count
            || !boat_cargo_moor_toggle(&e->sandbox, &e->boat))
            return false;
        e->crane_view = false;
        e->boat.position = boat_cargo_berth(&e->sandbox.cranes[quay]);
        e->boat.heading = e->sandbox.cranes[quay].heading;
        e->sandbox.cargo.state = BOAT_CARGO_FLOATING;
        e->sandbox.cargo.position
            = boat_cargo_site_point(&e->sandbox.cranes[landing], (Vector3) { -5, 1.6f, 0 });
        for (int i = 0; i < 240; ++i)
            boat_work_session_tick(e, 0, 0, 0, (BoatInput) { 0 });
        BoatPluginObjective waiting = e->objective;
        if (!e->sandbox.cargo.delivered || !e->waiting || !e->sandbox.cranes[quay].hidden
            || waiting.tick > (int64_t)e->tick - 12 || waiting.hud[0].value != 0
            || e->event_count != 1)
            return false;
        e->boat.position = boat_cargo_berth(&e->sandbox.cranes[landing]);
        e->boat.velocity = (Vector2) { 0 };
        for (int i = 0; i < 24; ++i)
            boat_work_session_tick(e, 0, 0, 0, (BoatInput) { 0 });
        e->notice = "Build-blocked review fixture. R: restart from the first site.";
        return !e->waiting && !e->sandbox.cranes[quay].hidden
            && boat_work_hud_value(e, "LANDING BUILT") == 1 && !e->event_count
            && e->sandbox.source == quay;
    } else if (!strcmp(name, "landing-built")) {
        // S1c review: after the delivery, look back from the helm at the landing and the quay
        // the job built beside it.
        if (!stage(e, "delivered") || !e->revision)
            return false;
        unsigned built = boat_work_find_site(e, "landing_quay");
        if (built == e->site_count || !boat_cargo_moor_toggle(&e->sandbox, &e->boat))
            return false;
        const BoatCargoCrane *landing = boat_work_active(e), *quay = &e->sandbox.cranes[built];
        e->crane_view = false;
        Vector3 between = Vector3Lerp(boat_cargo_site_point(landing, (Vector3) { -2, 0, 1 }),
            boat_cargo_site_point(quay, (Vector3) { -2, 0, 1 }), .5f);
        Vector3 out = boat_cargo_site_point(landing, (Vector3) { 0, 0, -15 });
        Vector3 at = Vector3Add(between, Vector3Subtract(out, landing->origin));
        e->boat.position = (Vector2) { -at.x, at.z };
        e->boat.heading = landing->heading + PI;
        e->boat.velocity = (Vector2) { 0 };
        for (int i = 0; i < 60; ++i)
            boat_work_session_tick(e, 0, 0, 0, (BoatInput) { 0 });
        e->notice = "Landing built review fixture. R: restart from the first site.";
        return !quay->hidden;
    } else if (!strcmp(name, "receiver-approach") || !strcmp(name, "delivered")) {
        // S1b review: carry the lashed crate to the first receiving site (an explicit pose in
        // place of the crossing), then moor and unload with that site's crane by the real path.
        unsigned receiver = boat_work_find_site(e, "landing");
        if (receiver == e->site_count || !stage(e, "departure"))
            return false;
        if (!boat_work_script_unload_at(e, receiver, !strcmp(name, "receiver-approach"), false))
            return false;
        if (!strcmp(name, "receiver-approach"))
            return true;
        // S1c/S1d: the package's job counts the delivery once, builds its hidden quay and jetty
        // and uses the crate up for load 2 on the quay's pad. The notice must be owned text,
        // not a reused raylib TextFormat buffer.
        for (int i = 0; i < 8; ++i) // Cycle every TextFormat buffer, as one HUD frame does.
            TextFormat("%d", i);
        unsigned quay = boat_work_find_site(e, "landing_quay");
        if (e->revision && quay == e->site_count)
            return false;
        char supplied[64] = "";
        if (quay < e->site_count)
            boat_work_notice_text(e, BOAT_SCENE_NOTICE_SUPPLIED, quay, supplied, sizeof(supplied));
        if (e->revision
            && (strcmp(e->notice, supplied) || e->sandbox.cargo.load != 2
                || e->sandbox.cargo.state != BOAT_CARGO_PAD || e->sandbox.cargo.pad != quay
                || e->sandbox.source != quay || e->guide != boat_work_find_site(e, "jetty")))
            return false;
        e->notice = "Delivered review fixture. R: restart from the first site.";
        for (unsigned i = 0; e->revision && i < e->site_count; ++i)
            if (e->sites[i].hidden && e->sandbox.cranes[i].hidden)
                return false;
        return !e->revision
            || (!e->objective.quarantined && !e->waiting && e->objective.hud_count == 2
                && boat_work_hud_value(e, "LANDING BUILT") == 1
                && boat_work_hud_value(e, "SUPPLIES TO JETTY / 1") == 0);
    } else if (!strcmp(name, "quay-pickup") || !strcmp(name, "jetty-approach")
        || !strcmp(name, "jetty-delivered")) {
        // S1d's second outing: from the landing, moor at the built quay beside it by the real G
        // path, where load 2 waits; lift and lash it, depart toward the job's guided jetty, then
        // moor and unload there (explicit poses in place of the two short drives).
        unsigned quay = boat_work_find_site(e, "landing_quay");
        unsigned jetty = boat_work_find_site(e, "jetty");
        if (!e->revision || quay == e->site_count || jetty == e->site_count
            || !stage(e, "delivered") || !boat_cargo_moor_toggle(&e->sandbox, &e->boat)
            || !boat_work_script_moor_at(e, quay, false))
            return false;
        if (!strcmp(name, "quay-pickup")) {
            e->notice = "Quay pickup review fixture. R: restart from the first site.";
            return e->sandbox.cargo.state == BOAT_CARGO_PAD && e->sandbox.cargo.pad == quay
                && e->sandbox.cargo.load == 2 && e->guide == jetty;
        }
        if (!boat_work_script_load_and_lash(e, 2))
            return false;
        boat_work_moor_action(e); // G: departure guidance follows the job's work.guide.
        if (e->sandbox.moored || e->target != jetty)
            return false;
        if (!boat_work_script_unload_at(e, jetty, !strcmp(name, "jetty-approach"), false))
            return false;
        if (!strcmp(name, "jetty-approach"))
            return true;
        for (int i = 0; i < 24; ++i)
            boat_work_session_tick(e, 0, 0, 0, (BoatInput) { 0 });
        e->notice = "Jetty delivered review fixture. R: restart from the first site.";
        return !e->objective.quarantined && !e->waiting && e->sandbox.cargo.load == 2
            && e->sandbox.cargo.pad == jetty && boat_work_hud_value(e, "LANDING BUILT") == 1
            && boat_work_hud_value(e, "JETTY SUPPLIED") == 1;
    } else if (!strcmp(name, "loaded-left") || !strcmp(name, "loaded-right")
        || !strcmp(name, "departure")) {
        // Exercise the real application assist, wave-posed deck, release and departure.
        if (!boat_work_script_load_and_lash(e, !strcmp(name, "loaded-right") ? 3 : 2)
            || !boat_cargo_moor_toggle(&e->sandbox, &e->boat))
            return false;
        if (!strcmp(name, "departure"))
            return true; // The user's handoff: unmoored, still operating crane controls.
        e->crane_view = false;
        e->notice = "Loaded review fixture. R: restart the hands-on lifting trial.";
        for (int i = 0; i < 600; ++i)
            boat_work_session_tick(e, 0, 0, 0,
                (BoatInput) {
                    .throttle_rate = i < 30 ? .4f : 0, .steering_rate = i > 360 ? .35f : 0 });
    }
    return true;
}

// SV1c resume check. The staged session is saved, then resumed twice, once from the record
// itself and once from its encoded and decoded bytes. Each copy then runs the same scripted
// 1200 ticks, and every tick's cargo, boat, events, objective and HUD must hash equal.
enum { RESUME_TICKS = 1200 };

#define HASH(h, value) boat_work_hash_bytes(h, &(value), sizeof(value))

static uint64_t session_hash(const BoatWorkSession *e)
{
    uint64_t h = UINT64_C(14695981039346656037);
    const BoatCargo *c = &e->sandbox.cargo;
    HASH(&h, e->tick);
    HASH(&h, c->state);
    HASH(&h, c->load);
    HASH(&h, c->delivered);
    HASH(&h, c->receivers);
    HASH(&h, c->pad);
    HASH(&h, c->position);
    HASH(&h, c->rotation);
    HASH(&h, e->boat.position);
    HASH(&h, e->boat.heading);
    HASH(&h, e->sandbox.moored);
    HASH(&h, e->sandbox.mooring);
    HASH(&h, e->sandbox.active);
    HASH(&h, e->sandbox.source);
    for (unsigned i = 0; i < e->sandbox.crane_count; ++i)
        HASH(&h, e->sandbox.cranes[i].hidden);
    HASH(&h, e->guide);
    HASH(&h, e->target);
    HASH(&h, e->sequence);
    HASH(&h, e->event_count);
    for (unsigned i = 0; i < e->event_count; ++i) {
        HASH(&h, e->events[i].tick);
        HASH(&h, e->events[i].sequence);
        HASH(&h, e->events[i].load);
        boat_work_hash_bytes(&h, e->events[i].kind, strlen(e->events[i].kind) + 1);
        boat_work_hash_bytes(&h, e->events[i].id, strlen(e->events[i].id) + 1);
    }
    uint64_t objective = boat_plugin_objective_digest(&e->objective);
    HASH(&h, objective);
    HASH(&h, e->objective.quarantined);
    return h;
}

// The same scripted actions and inputs for every copy, a function of the tick count alone.
static void resume_script(BoatWorkSession *e, unsigned i)
{
    if (i == 5 || i == 900)
        boat_work_hook_action(e);
    if (i == 300)
        boat_work_lash_action(e);
    if (i == 700)
        boat_work_moor_action(e);
    float slew = ((int)(i / 150) % 3 - 1) * .5f, reach = (int)(i / 200) % 2 ? .4f : -.4f;
    float hoist = (int)(i / 90) % 2 ? -.3f : .5f;
    boat_work_session_tick(e, slew, reach, hoist,
        (BoatInput) { .throttle_rate = i < 30 ? .4f : 0, .steering_rate = i > 360 ? .1f : 0 });
}

#define RESUME_CHECK(condition)                                                                    \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            fprintf(stderr, "Resume check failed at %d: %s\n", __LINE__, #condition);              \
            return false;                                                                          \
        }                                                                                          \
    } while (0)

static bool resume_check(BoatWorkSession *e, const char *scenario)
{
    static BoatSandboxSave record, decoded;
    static unsigned char bytes[BOAT_SANDBOX_SAVE_MAX], again[BOAT_SANDBOX_SAVE_MAX];
    static uint64_t trace[2][RESUME_TICKS];
    size_t size = 0, size_again = 0;
    RESUME_CHECK(boat_work_session_save(e, 0, &record));
    RESUME_CHECK(boat_sandbox_save_encode(&record, bytes, sizeof(bytes), &size));
    RESUME_CHECK(boat_sandbox_save_decode(bytes, size, &decoded) == BOAT_SANDBOX_SAVE_OK);
    RESUME_CHECK(boat_sandbox_save_encode(&decoded, again, sizeof(again), &size_again)
        && size == size_again && !memcmp(bytes, again, size));
    BoatSandboxIdentity now;
    boat_work_session_identity(e, &now);
    char reason[48];
    RESUME_CHECK(
        boat_sandbox_save_check(&decoded.summary.identity, &now, false, reason, sizeof(reason))
        == BOAT_SANDBOX_RESUME);
    bool is_swing = !strcmp(scenario, "swing"), is_capsized = !strcmp(scenario, "capsized");
    bool lashed = !strcmp(scenario, "departure") || !strcmp(scenario, "crossing");
    bool pending = !strcmp(scenario, "delivery-pending");
    for (unsigned pass = 0; pass < 2; ++pass) {
        RESUME_CHECK(boat_work_session_resume(e, pass ? &decoded : &record));
        const BoatCargo *c = &e->sandbox.cargo;
        const BoatCargoSnapshot *snap = &record.cargo;
        RESUME_CHECK(e->tick == record.summary.tick && c->load == snap->loads[0].load);
        RESUME_CHECK(
            c->delivered == snap->loads[0].delivered && c->receivers == snap->loads[0].receivers);
        RESUME_CHECK(c->state == BOAT_CARGO_PAD || c->state == BOAT_CARGO_SECURED);
        RESUME_CHECK(e->sandbox.moored == snap->moored && !e->sandbox.mooring);
        for (unsigned i = 0; i < e->site_count; ++i)
            RESUME_CHECK(!e->sandbox.cranes[i].hidden == snap->built[i]);
        RESUME_CHECK(boat_plugin_objective_digest(&e->objective)
            == boat_plugin_objective_digest(&record.objective));
        RESUME_CHECK(e->event_count == record.event_count && e->sequence == record.sequence);
        RESUME_CHECK(e->generation == e->objective.generation);
        if (is_swing)
            RESUME_CHECK(c->state == BOAT_CARGO_PAD && e->sandbox.moored); // Hook released.
        if (lashed)
            RESUME_CHECK(c->state == BOAT_CARGO_SECURED);
        if (!strcmp(scenario, "crossing"))
            RESUME_CHECK(!e->sandbox.moored);
        if (is_capsized)
            RESUME_CHECK(e->sandbox.moored && c->state == BOAT_CARGO_PAD
                && e->boat.motion_state != BOAT_CAPSIZED);
        if (pending)
            RESUME_CHECK(e->event_count == 1 && c->delivered
                && boat_work_hud_value(e, "LANDING BUILT") == -1);
        for (unsigned i = 0; i < RESUME_TICKS; ++i) {
            resume_script(e, i);
            trace[pass][i] = session_hash(e);
        }
        RESUME_CHECK(!e->objective.quarantined);
        // Exactly one counted delivery and one built quay, whichever side of the update the
        // save fell on.
        unsigned quay = boat_work_find_site(e, "landing_quay");
        if (pending || !strcmp(scenario, "delivered") || !strcmp(scenario, "landing-built")
            || !strcmp(scenario, "quay-pickup") || !strcmp(scenario, "jetty-delivered")) {
            RESUME_CHECK(quay < e->site_count && !e->sandbox.cranes[quay].hidden);
            RESUME_CHECK(boat_work_hud_value(e, "LANDING BUILT") == 1);
        }
        if (!strcmp(scenario, "jetty-delivered"))
            RESUME_CHECK(boat_work_hud_value(e, "JETTY SUPPLIED") == 1);
    }
    for (unsigned i = 0; i < RESUME_TICKS; ++i)
        if (trace[0][i] != trace[1][i]) {
            fprintf(stderr, "Resume check: the copies diverged at tick %u\n", i);
            return false;
        }
    printf("resume check %s: saved %zu bytes at tick %llu; %d ticks equal\n", scenario, size,
        (unsigned long long)record.summary.tick, RESUME_TICKS);
    return true;
}

// The window loop's automatic saving driven on a virtual clock: 2 ticks per frame, no window
// and no wall time. The staged session drifts, lashes at one frame, pauses at another and
// quits; the writes must be one periodic, one milestone, one on pausing and one on quitting.
static bool autosave_check(BoatWorkSession *e, BoatWorkSave *file)
{
    enum { FRAMES = 2100, LASH = 1900, PAUSE = 2000 };
    BoatWorkFrameWatch frame = { .last = boat_work_watch(e), .was_focused = true };
    file->autosaving = true;
    unsigned count_at[FRAMES] = { 0 };
    bool paused = false;
    for (unsigned f = 0; f < FRAMES; ++f) {
        paused = f == PAUSE || f == PAUSE + 1;
        if (!paused) {
            if (f == LASH)
                boat_work_lash_action(e);
            // A gentle circle at a steady throttle keeps the boat's pose changing.
            BoatInput input = { .throttle_rate = f < 8 ? .4f : 0, .steering_rate = .3f };
            boat_work_session_tick(e, 0, 0, 0, input);
            boat_work_session_tick(e, 0, 0, 0, input);
        }
        boat_work_save_frame(e, file, &frame, paused, true, 0);
        count_at[f] = file->writes;
    }
    RESUME_CHECK(count_at[LASH - 1] == 1); // The period (3600 ticks of drift).
    RESUME_CHECK(count_at[LASH] == 2 && file->mark > 0); // The lash milestone.
    RESUME_CHECK(count_at[PAUSE - 1] == 2 && count_at[PAUSE] == 3); // Pausing.
    RESUME_CHECK(count_at[PAUSE + 1] == 3 && count_at[FRAMES - 1] == 3);
    RESUME_CHECK(boat_autosave_due(&file->autosave, e->tick, true)); // Dirty again: quitting.
    RESUME_CHECK(boat_work_save_write(e, file, 0));
    boat_autosave_written(&file->autosave, e->tick, true);
    RESUME_CHECK(!boat_autosave_due(&file->autosave, e->tick, true));
    char backup[1100];
    snprintf(backup, sizeof(backup), "%s.bak", file->path);
    RESUME_CHECK(boat_file_exists(file->path) && boat_file_exists(backup));
    printf("autosave check: writes at the period, the lash, the pause and quit; saved tick %llu\n",
        (unsigned long long)e->tick);
    return true;
}

// The flag's fixture: a session reset keeps the package's scene, so its custom notice and its
// workboat survive the reset instead of falling back to the native defaults.
static bool scene_check(BoatWorkSession *e, BoatPluginRevision *revision)
{
    if (e->world.scene != &revision->free.scene) {
        fprintf(stderr, "Scene check: the session does not use the package's scene\n");
        return false;
    }
    char *notice = revision->free.scene.work_notice[BOAT_SCENE_NOTICE_BUILT];
    snprintf(notice, sizeof(revision->free.scene.work_notice[BOAT_SCENE_NOTICE_BUILT]),
        "CUSTOM {site} DONE");
    boat_work_session_reset(e);
    if (e->world.scene != &revision->free.scene) {
        fprintf(stderr, "Scene check: a reset dropped the package's scene\n");
        return false;
    }
    char text[64], expected[64], name[sizeof(e->sites[0].id)];
    boat_work_notice_text(e, BOAT_SCENE_NOTICE_BUILT, 0, text, sizeof(text));
    snprintf(name, sizeof(name), "%s", e->sites[0].id);
    for (char *c = name; *c; ++c)
        *c = *c == '_' ? ' ' : (char)toupper((unsigned char)*c);
    snprintf(expected, sizeof(expected), "CUSTOM %s DONE", name);
    if (strcmp(text, expected)) {
        fprintf(stderr, "Scene check: notice is \"%s\", expected \"%s\"\n", text, expected);
        return false;
    }
    if (e->sandbox.boat != boat_cargo_workboat(revision->free.scene.work_boat)) {
        fprintf(stderr, "Scene check: the workboat is not the package's\n");
        return false;
    }
    printf("Scene check: a reset kept the package's scene, notice and workboat\n");
    return true;
}

int main(int argc, char **argv)
{
    bool gpu = true, demo = false, survey = false, resume = false, fresh = false, save_now = false,
         autosave = false, scene_check_flag = false, stale_digest = false;
    const BoatWorldDefinition *location = boat_location(BOAT_LOCATION_OUTER);
    int frames = 0;
    const char *screenshot = NULL, *scenario = "origin", *package = NULL, *save_arg = NULL,
               *data_dir = NULL, *expect = NULL;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--location") && i + 1 < argc) {
            location = boat_location_find(argv[++i]);
            if (!location)
                return 2;
        } else if (!strcmp(argv[i], "--package") && i + 1 < argc)
            package = argv[++i];
        else if (!strcmp(argv[i], "--survey"))
            survey = true;
        else if (!strcmp(argv[i], "--resume-check"))
            resume = true;
        else if (!strcmp(argv[i], "--save") && i + 1 < argc)
            save_arg = argv[++i];
        else if (!strcmp(argv[i], "--data-dir") && i + 1 < argc)
            data_dir = argv[++i];
        else if (!strcmp(argv[i], "--new"))
            fresh = true;
        else if (!strcmp(argv[i], "--save-now"))
            save_now = true;
        else if (!strcmp(argv[i], "--autosave-check"))
            autosave = true;
        else if (!strcmp(argv[i], "--scene-check"))
            scene_check_flag = true;
        else if (!strcmp(argv[i], "--expect") && i + 1 < argc) {
            expect = argv[++i];
            if (strcmp(expect, "resumed") && strcmp(expect, "rejected"))
                return 2;
        } else if (!strcmp(argv[i], "--stale-digest"))
            stale_digest = true;
        else if (!strcmp(argv[i], "--shadow-review"))
            shadow_review = true;
        else if (!strcmp(argv[i], "--renderer") && i + 1 < argc) {
            const char *renderer = argv[++i];
            if (strcmp(renderer, "cpu") && strcmp(renderer, "gpu"))
                return 2;
            gpu = !strcmp(renderer, "gpu");
        } else if (!strcmp(argv[i], "--screenshot") && i + 1 < argc)
            screenshot = argv[++i];
        else if (!strcmp(argv[i], "--scenario") && i + 1 < argc)
            scenario = argv[++i];
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc) {
            char *end;
            long n = strtol(argv[++i], &end, 10);
            if (*end || n < 1 || n > 100000)
                return 2;
            frames = (int)n;
        } else if (!strcmp(argv[i], "--demo"))
            demo = true;
        else {
            fprintf(stderr,
                "Usage: cargo_sandbox [--location outer|basin] [--package ID] [--survey]\n"
                "  --package: bundled free package with scene.work sites (default per "
                "location)\n"
                "  [--renderer cpu|gpu] [--demo] [--frames N]\n"
                "  [--shadow-review] (virtual wave-cycle pixel/stream checks)\n"
                "  [--screenshot FILE] [--scenario "
                "origin|swing|departure|loaded-left|loaded-right|shadow-high|shadow-low|shadow-bow|"
                "shadow-water|"
                "berth-approach|mooring|remoor|crossing|receiver-approach|delivered|landing-built|"
                "\n"
                "redelivered|job-reset|build-blocked|quay-pickup|jetty-approach|jetty-delivered|\n"
                "delivery-pending|capsized]\n"
                "  [--resume-check] (with --scenario: save, resume twice, compare 1200 ticks)\n"
                "  Saving is automatic (milestones, every 30 s of play, pause, focus loss, quit) "
                "and "
                "resumes\n"
                "  at startup; only interactive play uses the game's data directory.\n"
                "  [--save FILE | --data-dir DIR] pick the save file (cargo-<package>.sav in "
                "DIR);\n"
                "  --scenario, --screenshot, --survey, --demo and --frames use a save only with "
                "one "
                "of these.\n"
                "  [--new] start fresh, replacing the save at the first write (kept as FILE.bak)\n"
                "  [--save-now] (test fixture, with --scenario and a save file) write once and "
                "exit;\n"
                "  [--stale-digest] makes that file look written by a changed package\n"
                "  [--autosave-check] (test fixture, with --scenario and a save file) drives the "
                "window\n"
                "  loop's automatic saving on a virtual clock and checks when it writes\n"
                "  [--scene-check] (test fixture) checks that a session reset keeps the package's "
                "scene\n"
                "  [--expect resumed|rejected] (test fixture, with a save file) fails unless the "
                "save\n"
                "  resumed or was set aside as .rejected\n"
                "F hook, Q/E hoist, A/D slew, W/S reach, 1/2/3 aim assist, L lash,\n"
                "G moor/cancel, TAB crane/helm, arrows drive, hold SPACE to brake, N next site,\n"
                "C return an unhooked, unlashed crate to its source pad, P pause, R reset.\n");
            return !strcmp(argv[i], "--help") ? 0 : 2;
        }
    }
    if (strcmp(scenario, "origin") && strcmp(scenario, "swing") && strcmp(scenario, "departure")
        && strcmp(scenario, "loaded-left") && strcmp(scenario, "loaded-right")
        && strcmp(scenario, "shadow-high") && strcmp(scenario, "shadow-low")
        && strcmp(scenario, "shadow-bow") && strcmp(scenario, "shadow-water")
        && strcmp(scenario, "berth-approach") && strcmp(scenario, "mooring")
        && strcmp(scenario, "remoor") && strcmp(scenario, "crossing")
        && strcmp(scenario, "receiver-approach") && strcmp(scenario, "delivered")
        && strcmp(scenario, "landing-built") && strcmp(scenario, "redelivered")
        && strcmp(scenario, "job-reset") && strcmp(scenario, "build-blocked")
        && strcmp(scenario, "quay-pickup") && strcmp(scenario, "jetty-approach")
        && strcmp(scenario, "jetty-delivered") && strcmp(scenario, "delivery-pending")
        && strcmp(scenario, "capsized"))
        return 2;
    if (resume && (screenshot || frames || demo || survey || shadow_review))
        return 2;
    if (scene_check_flag
        && (screenshot || frames || demo || survey || resume || shadow_review || save_now
            || autosave))
        return 2;
    if (expect && (!(save_arg || data_dir) || strcmp(scenario, "origin")))
        return 2;
    if ((save_now || autosave)
        && (!(save_arg || data_dir) || screenshot || frames || demo || survey || resume
            || (save_now && autosave)))
        return 2;
    if (shadow_review && (frames || demo || survey || strcmp(scenario, "origin"))) {
        fprintf(stderr, "--shadow-review uses its own fixed virtual poses; omit other modes.\n");
        return 2;
    }
    if (screenshot && (frames || demo)) {
        fprintf(stderr, "Use --scenario for a capture; --demo/--frames need a live window.\n");
        return 2;
    }
    static BoatWorldResources resources;
    BoatWorkSession e = { .resources = &resources };
    SetTraceLogLevel(LOG_WARNING);
    if (!boat_world_resources_init(&resources, location))
        return 1;
    // Every location reads its bundled package; the basin's is its reference fixture.
    static BoatPluginRevision revision;
    const char *work_name = package ? package : location->name;
    const BoatPluginCatalogEntry *entry = package
        ? boat_plugin_catalog_find(package)
        : boat_plugin_catalog_work_default(location->name);
    BoatPluginDiagnostic d = { 0 };
    if (!entry || boat_plugin_kind_parse(entry->kind) != BOAT_PLUGIN_KIND_FREE
        || strcmp(entry->location, location->name)
        || !boat_plugin_catalog_decode(entry, &revision, &d) || !revision.free.has_scene
        || !revision.free.scene.work_present) {
        fprintf(stderr, "No bundled free package with scene.work sites for %s: %s\n",
            location->name, work_name);
        boat_world_resources_destroy(&resources);
        return 1;
    }
    e.world.scene = &revision.free.scene;
    e.revision = revision.free.manifest.objective ? &revision : NULL;
    e.site_count = revision.free.scene.work_count;
    memcpy(e.sites, revision.free.scene.work, sizeof(e.sites));
    boat_work_session_reset(&e);
    if (scene_check_flag) {
        bool ok = scene_check(&e, &revision);
        boat_world_resources_destroy(&resources);
        return ok ? 0 : 1;
    }
    // Which file, if any. Only plain interactive play uses the game's data directory; every
    // staged, scripted or capture run uses a save only when told which one.
    static BoatWorkSave save;
    bool plain = !screenshot && !shadow_review && !survey && !resume && !demo && !frames
        && !strcmp(scenario, "origin");
    if (e.revision && (save_arg || data_dir || plain))
        boat_work_save_path(&save, save_arg, data_dir, e.revision->free.manifest.id);
    save.stale_digest = stale_digest;
    if (expect && !save.enabled) {
        fprintf(stderr, "--expect needs a save file (--save or --data-dir).\n");
        boat_world_resources_destroy(&resources);
        return 2;
    }
    int loaded = 0;
    if (save.enabled && !fresh && !strcmp(scenario, "origin") && !shadow_review && !survey
        && !resume && !autosave)
        loaded = boat_work_save_load(&e, &save);
    if (expect) {
        bool ok = (!strcmp(expect, "resumed") && loaded == 1)
            || (!strcmp(expect, "rejected") && loaded == 2);
        if (!ok) {
            fprintf(
                stderr, "--expect %s: the save did not match (load result %d)\n", expect, loaded);
            boat_world_resources_destroy(&resources);
            return 1;
        }
    }
    static HarbourView harbour;
    if (location->mapped) {
        char assets[4096];
        bool clear = boat_asset_path(assets, sizeof(assets), "")
            && boat_scenery_load_location(location, assets)
            && waterfront_scenery_bind(&harbour.scenery, location->map);
        for (unsigned i = 0; clear && i < e.site_count; ++i)
            clear
                = boat_cargo_site_clear(&resources.map, e.world.scene, e.world.water, &e.sites[i]);
        if (!clear) {
            fprintf(stderr, "Cargo work sites need their scenery pack and clear mapped water.\n");
            return 1;
        }
    }
    if (survey) {
        bool clear = true;
        for (unsigned i = 0; i < e.site_count; ++i) {
            // Each site's moored crane view, as after mooring there.
            e.sandbox.active = i;
            e.boat.position = boat_cargo_berth(boat_work_active(&e));
            e.boat.heading = boat_work_active(&e)->heading;
            BoatCargoView view_state = view_of(&e);
            View3D view = boat_cargo_view_camera(&view_state, &harbour.scenery);
            bool camera_clear = !location->mapped
                || waterfront_scenery_clear(&harbour.scenery, view.position, .45f);
            printf("cargo site: %s %s (%s%s) at %.0f,%.0f heading %.2f; water/berth/traffic "
                   "survey clear; camera %s\n",
                location->name, e.sites[i].id,
                boat_scene_work_preset_name((BoatSceneWorkPreset)e.sites[i].preset),
                e.sites[i].hidden ? ", hidden until built" : "", e.sites[i].x, e.sites[i].z,
                e.sites[i].heading, camera_clear ? "clear" : "blocked");
            clear &= camera_clear;
        }
        waterfront_scenery_destroy(&harbour.scenery);
        boat_scenery_unload();
        boat_world_resources_destroy(&resources);
        return clear ? 0 : 1;
    }
    if (!stage(&e, scenario)) {
        fprintf(stderr, "Cargo review scenario failed: %s\n", scenario);
        boat_world_resources_destroy(&resources);
        return 1;
    }
    if (autosave) {
        bool ok = save.enabled && autosave_check(&e, &save);
        waterfront_scenery_destroy(&harbour.scenery);
        boat_scenery_unload();
        boat_world_resources_destroy(&resources);
        return ok ? 0 : 1;
    }
    if (save_now) {
        bool ok = save.enabled && boat_work_save_write(&e, &save, (int64_t)time(NULL));
        printf("%s %s at tick %llu\n", ok ? "Wrote save" : "Could not write save", save.path,
            (unsigned long long)e.tick);
        waterfront_scenery_destroy(&harbour.scenery);
        boat_scenery_unload();
        boat_world_resources_destroy(&resources);
        return ok ? 0 : 1;
    }
    if (resume) {
        bool ok = resume_check(&e, scenario);
        waterfront_scenery_destroy(&harbour.scenery);
        boat_scenery_unload();
        boat_world_resources_destroy(&resources);
        return ok ? 0 : 1;
    }
    static BoatModel solids, water, body_decal, water_decal;
    static Color pixels[WIDTH * HEIGHT];
    Renderer3D cpu = { 0 };
    Image image = { pixels, WIDTH, HEIGHT, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
    SetTraceLogLevel(LOG_WARNING);
    bool headless = (screenshot || shadow_review) && !gpu;
    RenderTexture2D target = { 0 }, world_target = { 0 };
    Texture2D texture = { 0 };
    GpuMesh mesh = { 0 };
    if (!headless) {
        SetConfigFlags(screenshot || shadow_review ? FLAG_WINDOW_HIDDEN
                                                   : FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
        InitWindow(WIDTH * 2, HEIGHT * 2, "Wake Velocity - cargo handling experiment");
        if (!IsWindowReady())
            return 1;
        SetWindowMinSize(WIDTH, HEIGHT);
        SetTargetFPS(60);
        target = LoadRenderTexture(WIDTH, HEIGHT);
        if (gpu && !boat_cargo_view_gpu_init(&mesh))
            return 1;
        if (gpu && location->mapped) {
            char shader[4096];
            world_target = LoadRenderTexture(WIDTH, HEIGHT);
            if (!world_target.id
                || !boat_asset_path(shader, sizeof(shader), "shaders/waterfront_city.fs")
                || !waterfront_gpu_scene_init(&harbour.gpu, shader)
                || !waterfront_gpu_scene_bind(&harbour.gpu, &harbour.scenery))
                return 1;
        }
        if (!gpu)
            texture = LoadTextureFromImage(image);
    }
    if (shadow_review && gpu && !shadow_stream_check(&mesh, target))
        return 1;
    int result = 0, frame = 0;
    double accumulator = 0;
    float mouse_slew = 0, mouse_reach = 0, mouse_hoist = 0;
    save.autosaving = save.enabled && !headless && !screenshot && !shadow_review;
    BoatWorkFrameWatch frame_watch = { .last = boat_work_watch(&e), .was_focused = true };
    int review_missing = 0, review_min = WIDTH * HEIGHT;
    while (headless || !WindowShouldClose()) {
        if (shadow_review) {
            int sample = frame % 32, surface = frame / 32;
            if (!sample)
                boat_work_session_reset(&e);
            for (int i = 0; i < 30; ++i)
                boat_work_session_tick(&e, 0, 0, 0, (BoatInput) { 0 });
            e.sandbox.cargo.state = BOAT_CARGO_HANGING;
            e.sandbox.cargo.deck_contact = false;
            Vector3 p = shadow_review_positions[surface];
            e.sandbox.cargo.position = boat_cargo_site_point(boat_work_active(&e), p);
            e.sandbox.cargo.rotation = (Vector3) { 0 };
        } else if (!headless && !screenshot) {
            if (IsKeyPressed(KEY_R)) {
                boat_work_session_reset(&e);
                boat_autosave_note(&save.autosave, e.tick, true, true); // A new session.
            }
            if (IsKeyPressed(KEY_P))
                e.paused = !e.paused;
            if (!e.paused) {
                if (IsKeyPressed(KEY_F))
                    boat_work_hook_action(&e);
                if (IsKeyPressed(KEY_L))
                    boat_work_lash_action(&e);
                if (IsKeyPressed(KEY_G))
                    boat_work_moor_action(&e);
                if (IsKeyPressed(KEY_TAB))
                    boat_work_view_action(&e);
                if (e.crane_view && e.sandbox.moored)
                    for (int i = 1; i <= 3; ++i)
                        if (IsKeyPressed(KEY_ONE + i - 1))
                            e.aim = i;
                if (IsKeyDown(KEY_SPACE))
                    e.boat.throttle = 0;
                if (IsKeyPressed(KEY_N) && !e.sandbox.moored && !e.sandbox.mooring)
                    e.target = boat_work_next_site(&e, e.target);
                if (IsKeyPressed(KEY_C))
                    boat_work_recover_action(&e);
            }
            e.braking = !e.paused && !e.crane_view && !e.sandbox.moored && !e.sandbox.mooring
                && IsKeyDown(KEY_SPACE);
            accumulator = e.paused ? 0 : accumulator + (demo ? 1.0 / 60 : fmin(GetFrameTime(), .1));
            float slew = mouse_slew, reach = mouse_reach, hoist = mouse_hoist;
            if (IsKeyDown(KEY_A))
                slew -= 1;
            if (IsKeyDown(KEY_D))
                slew += 1;
            if (IsKeyDown(KEY_W))
                reach += 1;
            if (IsKeyDown(KEY_S))
                reach -= 1;
            if (IsKeyDown(KEY_Q))
                hoist += 1;
            if (IsKeyDown(KEY_E))
                hoist -= 1;
            BoatInput input = { 0 };
            if (!e.crane_view) {
                input.throttle_rate = IsKeyDown(KEY_UP) - IsKeyDown(KEY_DOWN);
                input.steering_rate = IsKeyDown(KEY_RIGHT) - IsKeyDown(KEY_LEFT);
                if (e.braking)
                    input.throttle_rate = 0;
            }
            if (demo && e.tick == 0)
                boat_cargo_hook_toggle(&e.sandbox);
            while (accumulator >= BOAT_TICK_SECONDS) {
                if (demo) {
                    slew = sinf((float)e.tick * .003f) * .6f;
                    reach = sinf((float)e.tick * .002f) * .4f;
                    hoist = e.tick < 240 ? 1 : 0;
                }
                boat_work_session_tick(&e, slew, reach, hoist, input);
                accumulator -= BOAT_TICK_SECONDS;
            }
            if (save.autosaving)
                boat_work_save_frame(
                    &e, &save, &frame_watch, e.paused, IsWindowFocused(), (int64_t)time(NULL));
        }
        BoatCargoView view_state = view_of(&e);
        size_t receiver_count = boat_cargo_view_scene(&view_state, &solids);
        Mesh3D receivers = solids.mesh;
        receivers.triangle_count = receiver_count;
        boat_model_begin(&body_decal);
        if (!boat_cargo_decal(&body_decal, &receivers, e.sandbox.cargo.position, NULL, 0,
                (Vector2) { 0 }, shadow_review ? (Color) { 255, 0, 0, 255 } : (Color) { 0 })) {
            result = 1;
            break;
        }
        if (!location->mapped)
            boat_cargo_view_water(&view_state, &water);
        View3D view = boat_cargo_view_camera(&view_state, &harbour.scenery);
        if (!gpu)
            view.far_plane = 240; // Bound the reference rasterizer's displaced-water work.
        if (location->mapped) {
            waterfront_scene_evaluate_ground_scene(e.world.scene, boat_tick_seconds(e.tick),
                e.world.water, waterfront_kit_ground(location->map), &harbour.frame);
            harbour.frame.camera = view;
            harbour.frame.time = boat_tick_seconds(e.tick);
            harbour.frame.player_active = true;
            harbour.frame.hide_player = true;
            harbour.frame.player_water = e.world.water;
            harbour.frame.player.hull = boat_pose_hull(&e.boat);
        }
        if (!gpu) {
            for (int i = 0; i < WIDTH * HEIGHT; ++i)
                pixels[i] = (Color) { 145, 173, 185, 255 };
            bool ready = location->mapped
                ? harbour_cpu(&harbour, &cpu, pixels, view, &e)
                : renderer3d_begin(&cpu, pixels, WIDTH, HEIGHT, view, (Vector3) { -1, 3, 2 });
            if (!ready) {
                result = 1;
                break;
            }
            Transform3D identity = { .scale = { 1, 1, 1 } };
            if (!location->mapped)
                renderer3d_draw_emissive_mesh(&cpu, &water.mesh, identity);
            cpu.light_direction = Vector3Normalize((Vector3) { -1, 3, 2 });
            renderer3d_draw_mesh(&cpu, &solids.mesh, identity);
            boat_model_begin(&water_decal);
            const Mesh3D *water_receiver
                = location->mapped ? &harbour.water.surface.mesh : &water.mesh;
            if (!boat_cargo_decal(&water_decal, water_receiver, e.sandbox.cargo.position, NULL, 0,
                    (Vector2) { 0 },
                    shadow_review ? (Color) { 255, 0, 255, 255 } : (Color) { 0 })) {
                result = 1;
                break;
            }
            renderer3d_draw_emissive_mesh(&cpu, &water_decal.mesh, identity);
            renderer3d_draw_mesh(&cpu, &body_decal.mesh, identity);
            if (headless) {
                if (!shadow_review) {
                    result = ExportImage(image, screenshot) ? 0 : 1;
                    break;
                }
            } else {
                UpdateTexture(texture, pixels);
            }
        }
        if (shadow_review && headless) {
            int surface = frame / 32;
            int count = shadow_pixel_count(pixels, frame);
            if (count < review_min)
                review_min = count;
            if (!count) {
                printf("shadow missing: case=%d sample=%d time=%.2f\n", surface, frame % 32,
                    boat_tick_seconds(e.tick));
                if (!review_missing && screenshot)
                    ExportImage(image, screenshot);
                ++review_missing;
            }
            if (++frame == SHADOW_REVIEW_FRAMES)
                break;
            continue;
        }
        int scale = (int)fminf(GetScreenWidth() / (float)WIDTH, GetScreenHeight() / (float)HEIGHT);
        if (scale < 1)
            scale = 1;
        Vector2 offset = { (GetScreenWidth() - WIDTH * scale) / 2.0f,
            (GetScreenHeight() - HEIGHT * scale) / 2.0f };
        Vector2 mouse = Vector2Scale(Vector2Subtract(GetMousePosition(), offset), 1.0f / scale);
        BeginTextureMode(world_target.id ? world_target : target);
        ClearBackground((Color) { 145, 173, 185, 255 });
        if (gpu) {
            if (location->mapped
                && (!harbour_gpu_water(&harbour, &e)
                    || !waterfront_gpu_scene_draw(&harbour.gpu, &harbour.frame, WIDTH, HEIGHT))) {
                EndTextureMode();
                result = 1;
                break;
            }
            Camera3D ray = { view.position, view.target, view.up, view.vertical_fov * RAD2DEG,
                CAMERA_PERSPECTIVE };
            BeginMode3D(ray);
            // Match the game's mirrored presentation basis, so starboard is screen-right
            // from behind the helm. The reflection reverses winding; depth still hides backs.
            Matrix projection = MatrixPerspective(
                view.vertical_fov, WIDTH / (float)HEIGHT, view.near_plane, view.far_plane);
            if (!location->mapped)
                projection.m0 = -projection.m0;
            rlSetMatrixProjection(projection);
            rlDisableBackfaceCulling();
            if (!location->mapped)
                boat_cargo_view_gpu_draw(&mesh, &water.mesh, false);
            boat_cargo_view_gpu_draw(&mesh, &solids.mesh, true);
            boat_model_begin(&water_decal);
            Vector2 focus = { -e.boat.position.x, e.boat.position.y };
            const Mesh3D *water_receiver = &water.mesh;
            const BoatWater *displace = NULL;
            Mesh3D far_water;
            if (location->mapped) {
                Vector2 cargo = { e.sandbox.cargo.position.x, e.sandbox.cargo.position.z };
                if (Vector2Distance(cargo, focus) < BOAT_WATER_PATCH_RADIUS) {
                    water_receiver = &harbour.water.patches[0].mesh;
                    displace = &e.world.water;
                } else {
                    far_water = *location->map->mesh;
                    far_water.triangle_count = boat_map_leading_water(location->map->mesh);
                    water_receiver = &far_water;
                }
            }
            if (!boat_cargo_decal(&water_decal, water_receiver, e.sandbox.cargo.position, displace,
                    boat_tick_seconds(e.tick), focus,
                    shadow_review ? (Color) { 255, 0, 255, 255 } : (Color) { 0 })) {
                EndMode3D();
                EndTextureMode();
                result = 1;
                break;
            }
            boat_cargo_view_gpu_draw(&mesh, &water_decal.mesh, false);
            boat_cargo_view_gpu_draw(&mesh, &body_decal.mesh, true);
            rlEnableBackfaceCulling();
            EndMode3D();
        } else
            DrawTexture(texture, 0, 0, WHITE);
        if (world_target.id) {
            EndTextureMode();
            BeginTextureMode(target);
            ClearBackground(BLACK);
            DrawTexturePro(world_target.texture, (Rectangle) { 0, 0, -WIDTH, -HEIGHT },
                (Rectangle) { 0, 0, WIDTH, HEIGHT }, (Vector2) { 0 }, 0, WHITE);
        }
        mouse_slew = mouse_reach = mouse_hoist = 0;
        ui(&e, view, screenshot ? (Vector2) { -1, -1 } : mouse, &mouse_slew, &mouse_reach,
            &mouse_hoist);
        if (save.failed || save.mark > 0) {
            const char *text = save.failed ? "SAVE FAILED" : "SAVED";
            int width = MeasureText(text, 10);
            DrawRectangle(WIDTH - width - 14, 199, width + 10, 15, ink);
            DrawText(text, WIDTH - width - 9, 202, 10,
                save.failed ? (Color) { 232, 106, 84, 255 } : green);
            if (save.mark > 0)
                --save.mark;
        }
        EndTextureMode();
        if (shadow_review) {
            Image capture = LoadImageFromTexture(target.texture);
            ImageFlipVertical(&capture);
            Color *review_pixels = LoadImageColors(capture);
            int surface = frame / 32;
            int count = shadow_pixel_count(review_pixels, frame);
            if (count < review_min)
                review_min = count;
            if (!count) {
                printf("shadow missing: case=%d sample=%d time=%.2f\n", surface, frame % 32,
                    boat_tick_seconds(e.tick));
                if (!review_missing && screenshot)
                    ExportImage(capture, screenshot);
                ++review_missing;
            }
            UnloadImageColors(review_pixels);
            UnloadImage(capture);
            if (++frame == SHADOW_REVIEW_FRAMES)
                break;
            continue;
        } else if (screenshot) {
            Image capture = LoadImageFromTexture(target.texture);
            ImageFlipVertical(&capture);
            result = ExportImage(capture, screenshot) ? 0 : 1;
            UnloadImage(capture);
            break;
        }
        BeginDrawing();
        ClearBackground(BLACK);
        DrawTexturePro(target.texture, (Rectangle) { 0, 0, WIDTH, -HEIGHT },
            (Rectangle) { offset.x, offset.y, WIDTH * scale, HEIGHT * scale }, (Vector2) { 0 }, 0,
            WHITE);
        EndDrawing();
        if (frames && ++frame >= frames)
            break;
    }
    if (save.autosaving)
        boat_work_save_leave(&e, &save, (int64_t)time(NULL));
    if (shadow_review) {
        printf("shadow review: %s, %d/256 missing, minimum %d visible pixels\n",
            gpu ? "GPU" : "CPU", review_missing, review_min);
        result = review_missing ? 1 : 0;
    }
    renderer3d_destroy(&cpu);
    waterfront_scenery_destroy(&harbour.scenery);
    waterfront_harbor_models_destroy(&harbour.harbor);
    boat_water_mesh_cache_destroy(&harbour.water);
    boat_world_resources_destroy(&resources);
    if (!headless) {
        if (gpu) {
            if (location->mapped)
                waterfront_gpu_scene_destroy(&harbour.gpu);
            if (harbour.patch.vaoId)
                UnloadMesh(harbour.patch);
            UnloadMesh(mesh.mesh);
            UnloadMaterial(mesh.material);
        } else
            UnloadTexture(texture);
        UnloadRenderTexture(target);
        if (world_target.id)
            UnloadRenderTexture(world_target);
        CloseWindow();
    }
    boat_scenery_unload();
    return result;
}
