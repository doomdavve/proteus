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

typedef struct {
    BoatWorldResources resources;
    BoatWorld world;
    Boat boat;
    BoatSceneWorkSite sites[BOAT_SCENE_WORK_SITES]; // The package's, or the basin reference.
    unsigned site_count;
    BoatCargoSandbox sandbox;
    uint64_t tick;
    bool crane_view, paused;
    bool braking;
    int aim; // 1 pad, 2/3 port/starboard. Motors, never a pose teleport.
    // The site helm guidance points to: after departure the job's guided site, else the next
    // one; N cycles.
    unsigned target;
    unsigned guide; // S1d: the job's work.guide site, or site_count for none.
    const char *notice;
    // A formatted notice; raylib's TextFormat buffers are reused within the next frame.
    char notice_text[64];
    // S1c: the package's job. Native cargo events queue here until an update commits them.
    const BoatPluginRevision *revision; // NULL: no job (the basin reference or no objective).
    BoatPluginObjective objective;
    BoatPluginEvent events[BOAT_PLUGIN_EVENTS];
    unsigned event_count, generation;
    int64_t sequence;
    // Why the last update's batch waits: a build the boat or crate occupies, or a supply whose
    // delivered crate is not set down yet. NULL when the job is not waiting.
    const char *waiting;
} Experiment;

typedef struct {
    Mesh mesh;
    Material material;
} GpuMesh;

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

static bool water_visible(void *context, Vector3 lo, Vector3 hi)
{
    return renderer3d_box_visible(context, lo, hi);
}

static bool harbour_cpu(
    HarbourView *h, Renderer3D *cpu, Color *pixels, View3D camera, const Experiment *e)
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

static bool harbour_gpu_water(HarbourView *h, const Experiment *e)
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

static const BoatCargoCrane *active(const Experiment *e)
{
    return &e->sandbox.cranes[e->sandbox.active];
}

// The next visible site after FROM, for helm guidance; hidden sites are not there yet.
static unsigned next_site(const Experiment *e, unsigned from)
{
    unsigned n = e->sandbox.crane_count;
    for (unsigned i = 1; i <= n; ++i)
        if (!e->sandbox.cranes[(from + i) % n].hidden)
            return (from + i) % n;
    return from;
}

// KIND's template: the package's own notice, else the native default. Each {site} token becomes
// the site's name as a sign: capitals with underscores as spaces. The notice owns its text.
static void notice_text(
    const Experiment *e, BoatSceneNotice kind, unsigned site, char *out, size_t capacity)
{
    static const char *const defaults[BOAT_SCENE_NOTICES]
        = { "Crate delivered. It stays counted, even if moved again.", "{site} built.",
              "Supplies used. Next crate waits on the {site} pad.",
              "Crate returned to the {site} pad." };
    const char *format
        = e->world.scene && e->world.scene->work_present && e->world.scene->work_notice[kind][0]
        ? e->world.scene->work_notice[kind]
        : defaults[kind];
    char name[sizeof(e->sites[site].id)];
    snprintf(name, sizeof(name), "%s", e->sites[site].id);
    for (char *c = name; *c; ++c)
        *c = *c == '_' ? ' ' : (char)toupper((unsigned char)*c);
    size_t n = 0;
    for (const char *p = format; *p && n + 1 < capacity;) {
        if (!strncmp(p, "{site}", 6)) {
            for (const char *q = name; *q && n + 1 < capacity; ++q)
                out[n++] = *q;
            p += 6;
        } else
            out[n++] = *p++;
    }
    out[n] = 0;
}

static void notice(Experiment *e, BoatSceneNotice kind, unsigned site)
{
    notice_text(e, kind, site, e->notice_text, sizeof(e->notice_text));
    e->notice = e->notice_text;
}

static void job_suspend(Experiment *e, const char *message)
{
    e->objective.quarantined = true;
    fprintf(stderr, "Cargo job suspended: %s\n", message);
}

// A fresh job for a fresh session: new generation, no queued events, init at tick zero.
static void job_reset(Experiment *e)
{
    e->event_count = 0;
    e->sequence = 0;
    e->waiting = NULL;
    e->guide = e->site_count;
    e->objective = (BoatPluginObjective) {
        .generation = ++e->generation, .rng = 12345, .work_enabled = true
    };
    if (!e->revision)
        return;
    e->objective.state_version = e->revision->free.manifest.state_version;
    BoatPluginDiagnostic d = { 0 };
    if (boat_plugin_objective_step(&e->objective, e->revision->source, e->revision->source_size,
            BOAT_PLUGIN_OBJECTIVE_INIT, 0, NULL, 0, NULL, &d)
        != BOAT_PLUGIN_OK)
        job_suspend(e, d.message);
}

// Move the native cargo events into the job's unacknowledged queue (and raise the delivery
// notice). Saving does the same first, so a key action's event is never left in the core.
static void job_queue_events(Experiment *e)
{
    static const char *const kinds[]
        = { "cargo.secured", "cargo.delivered", "cargo.lost", "cargo.reset" };
    BoatCargoEvent native[BOAT_CARGO_EVENTS];
    unsigned count = boat_cargo_take_events(&e->sandbox, native, BOAT_CARGO_EVENTS);
    for (unsigned i = 0; i < count; ++i) {
        if (native[i].kind == BOAT_CARGO_EVENT_DELIVERED)
            notice(e, BOAT_SCENE_NOTICE_DELIVERED, native[i].site);
        if (!e->revision || e->objective.quarantined)
            continue;
        if (e->event_count == BOAT_PLUGIN_EVENTS) {
            job_suspend(e, "event queue full");
            break;
        }
        BoatPluginEvent *event = &e->events[e->event_count++];
        *event = (BoatPluginEvent) {
            .tick = (int64_t)e->tick, .sequence = ++e->sequence, .load = native[i].load
        };
        snprintf(event->kind, sizeof(event->kind), "%s", kinds[native[i].kind]);
        snprintf(event->id, sizeof(event->id), "%s", e->sites[native[i].site].id);
    }
}

// After each tick: queue the native cargo events, and at each 12-tick boundary run the update.
// Its work.enable intents are checked against the live boat and crate before the batch commits;
// a refusal keeps the previous checkpoint and its unacknowledged events for the next boundary.
static void job_tick(Experiment *e)
{
    job_queue_events(e);
    if (!e->revision || e->objective.quarantined || e->tick % 12)
        return;
    BoatPluginObjective next = e->objective;
    BoatPluginDiagnostic d = { 0 };
    if (boat_plugin_objective_step(&next, e->revision->source, e->revision->source_size,
            BOAT_PLUGIN_OBJECTIVE_UPDATE, (int64_t)e->tick, e->events, e->event_count, NULL, &d)
        != BOAT_PLUGIN_OK) {
        job_suspend(e, d.message);
        return;
    }
    // Check every intent against the live sandbox first; apply them only if all can happen.
    bool build[BOAT_SCENE_WORK_SITES] = { false };
    unsigned supply = e->site_count, guide = e->site_count;
    for (unsigned i = 0; i < next.command_count; ++i) {
        const char *kind = next.commands[i].kind;
        bool enable = !strcmp(kind, "work.enable"), supplying = !strcmp(kind, "cargo.supply");
        if (!enable && !supplying && strcmp(kind, "work.guide"))
            continue;
        unsigned site = 0;
        while (site < e->site_count && strcmp(e->sites[site].id, next.commands[i].id))
            ++site;
        if (site == e->site_count) {
            job_suspend(e, "a work command names no work site");
            return;
        }
        if (supplying)
            supply = site;
        else if (!enable)
            guide = site;
        else if (e->sandbox.cranes[site].hidden) { // Building a visible site is idempotent.
            if (boat_cargo_site_occupied(&e->sandbox, site, &e->boat)) {
                e->waiting = "BUILD WAITING: MOVE CLEAR";
                return;
            }
            build[site] = true;
        }
    }
    if (guide < e->site_count && e->sandbox.cranes[guide].hidden && !build[guide]) {
        job_suspend(e, "work.guide names a site that is not built");
        return;
    }
    if (supply < e->site_count) {
        if (e->sites[supply].preset != BOAT_SCENE_WORK_SERVICE_PLATFORM
            || (e->sandbox.cranes[supply].hidden && !build[supply])) {
            job_suspend(e, "cargo.supply names no built service platform");
            return;
        }
        if (!boat_cargo_supply_ready(&e->sandbox)) {
            e->waiting = "SUPPLY WAITING: SET CRATE DOWN";
            return;
        }
    }
    for (unsigned i = 0; i < e->site_count; ++i)
        if (build[i] && boat_cargo_site_enable(&e->sandbox, i, &e->boat))
            notice(e, BOAT_SCENE_NOTICE_BUILT, i);
    if (supply < e->site_count && boat_cargo_supply(&e->sandbox, supply))
        notice(e, BOAT_SCENE_NOTICE_SUPPLIED, supply);
    if (guide < e->site_count)
        e->guide = e->target = guide;
    e->waiting = NULL;
    e->objective = next;
    e->event_count = 0; // The update acknowledged every queued event.
}

// The world, cargo and controls of a fresh session, without the job.
static void session_reset(Experiment *e)
{
    // The reset must keep the package's scene: its notices and workboat drive this session.
    const BoatSceneDefinition *scene = e->world.scene;
    boat_world_init(&e->world, &e->resources, boat_water_gentle());
    e->world.scene = scene;
    e->world.water.upstream = e->resources.definition->upstream;
    e->world.water.player_wake_enabled = true;
    boat_cargo_reset_sites_with(
        &e->sandbox, e->world.scene ? e->world.scene->work_boat : 0, e->sites, e->site_count);
    e->boat = (Boat) { .position = boat_cargo_berth(active(e)), .heading = active(e)->heading };
    e->target = next_site(e, e->sandbox.active);
    e->tick = 0;
    e->crane_view = true;
    e->aim = 0;
    e->paused = false;
    e->braking = false;
    e->notice = "F: catch crate. 2 / 3: lift and aim at deck.";
}

static void reset(Experiment *e)
{
    session_reset(e);
    job_reset(e);
}

// SV1c: the session as a save record. The package's identity compares the work digest of its
// scene, the manifest state_version and the physics revision; the source hash and build string
// are for messages only.
static void experiment_identity(const Experiment *e, BoatSandboxIdentity *id)
{
    const BoatPluginManifest *m = &e->revision->free.manifest;
    *id = (BoatSandboxIdentity) { .work_digest = boat_scene_work_digest(&e->revision->free.scene),
        .source_hash = boat_sandbox_source_hash(e->revision->source, e->revision->source_size),
        .state_version = m->state_version,
        .physics_revision = BOAT_PHYSICS_REVISION,
        .tick_hz = BOAT_TICK_HZ };
    snprintf(id->package, sizeof(id->package), "%s", m->id);
    snprintf(id->build, sizeof(id->build), "cargo_sandbox");
}

// Between ticks only. A key action's core events move into the job's queue first; nothing
// else is re-derived. Jobless sessions (the basin reference) have nothing to save.
static bool experiment_save(Experiment *e, int64_t saved_at, BoatSandboxSave *save)
{
    if (!e->revision)
        return false;
    job_queue_events(e);
    memset(save, 0, sizeof(*save));
    boat_cargo_snapshot(&e->sandbox, &e->boat, &save->cargo);
    BoatSandboxSummary *sum = &save->summary;
    experiment_identity(e, &sum->identity);
    const BoatPluginManifest *m = &e->revision->free.manifest;
    snprintf(sum->name, sizeof(sum->name), "%s", m->name);
    snprintf(sum->location, sizeof(sum->location), "%s", m->location);
    sum->saved_at = saved_at;
    sum->tick = e->tick;
    sum->load = e->sandbox.cargo.load;
    sum->sites = e->site_count;
    for (unsigned i = 0; i < e->site_count; ++i) {
        snprintf(save->site_ids[i], sizeof(save->site_ids[i]), "%s", e->sites[i].id);
        sum->built += !e->sandbox.cranes[i].hidden;
    }
    if (e->objective.hud_count) {
        sum->has_progress = true;
        snprintf(sum->progress_label, sizeof(sum->progress_label), "%s", e->objective.hud[0].label);
        sum->progress_value = e->objective.hud[0].value;
    }
    save->guide = e->guide;
    save->target = e->target;
    save->has_objective = true;
    save->objective = e->objective;
    memset(save->objective.commands, 0, sizeof(save->objective.commands));
    save->objective.command_count = 0; // Inspection only; not part of the checkpoint.
    memcpy(save->events, e->events, e->event_count * sizeof(e->events[0]));
    save->event_count = e->event_count;
    save->generation = e->generation;
    save->sequence = e->sequence;
    return true;
}

// Replace the session with SAVE, or return false leaving it untouched when the record does not
// belong to this package's sites or the core refuses it. Resume never queues events and never
// re-runs the job's init. The boat resumes where the core puts it (at rest), the crane at rest.
static bool experiment_resume(Experiment *e, const BoatSandboxSave *save)
{
    if (!e->revision || !save->has_objective || save->summary.sites != e->site_count
        || save->guide > e->site_count || save->target >= e->site_count)
        return false;
    for (unsigned i = 0; i < e->site_count; ++i)
        if (strcmp(save->site_ids[i], e->sites[i].id))
            return false;
    BoatCargoSandbox sandbox;
    Boat boat;
    if (!boat_cargo_restore(&sandbox, e->revision->free.scene.work_boat, e->sites, e->site_count,
            &save->cargo, &boat))
        return false;
    session_reset(e);
    e->sandbox = sandbox;
    e->boat = boat;
    e->tick = save->summary.tick;
    e->crane_view = e->sandbox.moored;
    e->guide = save->guide;
    e->target = save->target;
    e->notice = "Resumed the saved session.";
    e->waiting = NULL;
    e->objective = save->objective;
    memset(e->events, 0, sizeof(e->events));
    memcpy(e->events, save->events, save->event_count * sizeof(e->events[0]));
    e->event_count = save->event_count;
    e->sequence = save->sequence;
    e->generation = save->objective.generation;
    return true;
}

static Vector3 local_target(int aim)
{
    return (Vector3) { aim == 2 ? .65f : -.65f, BOAT_CARGO_DECK_Y, .65f };
}

static void tick(Experiment *e, float slew, float reach, float hoist, BoatInput input)
{
    BoatCargoSandbox *s = &e->sandbox;
    if (e->crane_view && s->moored) {
        if (slew || reach || hoist)
            e->aim = 0;
        BoatCargoCrane *crane = &s->cranes[s->active];
        if (e->aim) {
            Vector3 target = e->aim == 1 ? boat_cargo_site_point(crane, (Vector3) { -5, 1, 0 })
                                         : boat_pose_attachment(&e->boat, local_target(e->aim));
            Vector3 local = boat_cargo_site_local(crane, target);
            float dx = local.x + 4;
            float dz = local.z - 3;
            float angle = atan2f(dx, dz);
            float radius = hypotf(dx, dz);
            // Lift clear before slewing; the final lowering is the player's action.
            if (crane->rope > .9f)
                hoist = 1;
            else {
                slew = Clamp(remainderf(angle - crane->angle, 2 * PI) * 3, -1, 1);
                reach = Clamp((radius - crane->reach) * 2, -1, 1);
            }
        }
        boat_cargo_motor(crane, slew, reach, hoist);
    }
    Boat before = e->boat;
    bool assisting = s->mooring;
    BoatHandling handling = { .thrust = s->boat->thrust, .hull = &s->boat->hull };
    boat_step_handling(&e->boat, &e->world, boat_tick_seconds(++e->tick),
        s->moored || s->mooring ? (BoatInput) { 0 } : input, &handling);
    if (assisting) {
        // Keep dynamic wave pose, then let the surveyed assist own planar motion exactly once.
        e->boat.position = before.position;
        e->boat.heading = before.heading;
        e->boat.velocity = before.velocity;
        e->boat.yaw_velocity = before.yaw_velocity;
    }
    boat_cargo_load_effect(s, &before, &e->boat);
    if (e->braking && !s->moored)
        boat_cargo_brake(&e->boat);
    boat_cargo_moor_step(s, &e->boat);
    if (s->moored) {
        // Assisted berth constrains only horizontal movement; wave heave/roll remain live.
        e->boat.position = boat_cargo_berth(active(e));
        e->boat.velocity = (Vector2) { 0 };
        e->boat.heading = active(e)->heading;
        e->boat.yaw_velocity = 0;
        e->boat.throttle = e->boat.engine_response = 0;
    } else {
        // Conservative circle against each solid test quay; this fixture is not scene editing.
        for (unsigned i = 0; i < s->crane_count; ++i) {
            if (s->cranes[i].hidden)
                continue;
            Vector3 local = boat_cargo_site_local(
                &s->cranes[i], (Vector3) { -e->boat.position.x, 0, e->boat.position.y });
            float nearest_x = Clamp(local.x, -6.5f, -3.5f), nearest_z = Clamp(local.z, -2, 4);
            Vector2 away = { local.x - nearest_x, local.z - nearest_z };
            if (Vector2Length(away) < 2.65f) {
                e->boat.position = before.position;
                e->boat.velocity = Vector2Scale(before.velocity, -.12f);
                break;
            }
        }
    }
    boat_cargo_step(s, &e->boat, &e->world, boat_tick_seconds(e->tick));
    job_tick(e);
}

static void box(BoatModel *m, Vector3 p, Vector3 size, Color color)
{
    boat_model_box(m, p, size, color);
}

static void boat_box(BoatModel *m, const Boat *boat, Vector3 p, Vector3 size, Color color)
{
    size_t first = m->mesh.triangle_count * 3;
    boat_model_box(m, p, size, color);
    for (size_t i = first; i < m->mesh.triangle_count * 3; ++i)
        m->vertices[i] = boat_pose_attachment(boat, m->vertices[i]);
}

// The scene mesh is built relative to the first site's berth, which scene() adds back last.
static Vector3 site_vertex(const Experiment *e, const BoatCargoCrane *c, Vector3 local)
{
    return Vector3Add(boat_cargo_site_direction(c, local),
        Vector3Subtract(c->origin, e->sandbox.cranes[0].origin));
}

static void to_site(const Experiment *e, BoatModel *m, size_t first, const BoatCargoCrane *c)
{
    for (size_t i = first; i < m->mesh.triangle_count * 3; ++i)
        m->vertices[i] = site_vertex(e, c, m->vertices[i]);
}

static Vector3 berth_point(const Experiment *e, const BoatCargoCrane *c, float x, float z)
{
    Vector3 p = boat_cargo_site_point(c, (Vector3) { x, 0, z });
    p.y = boat_water_query_borrowed(
              &e->world.water, (Vector2) { -p.x, p.z }, boat_tick_seconds(e->tick))
              .height
        + .1f;
    return Vector3Subtract(p, e->sandbox.cranes[0].origin);
}

static void berth_line(const Experiment *e, const BoatCargoCrane *c, BoatModel *m, float ax,
    float az, float bx, float bz, Color color)
{
    boat_model_beam(m, berth_point(e, c, ax, az), berth_point(e, c, bx, bz), .065f, color);
}

// A site's platform and crane. A receiving platform's pad is marked as the delivery surface.
static void site_parts(const Experiment *e, BoatModel *m, unsigned index)
{
    const BoatCargoSandbox *s = &e->sandbox;
    const BoatCargoCrane *crane = &s->cranes[index];
    size_t first = m->mesh.triangle_count * 3;
    bool receiving = crane->preset == BOAT_SCENE_WORK_RECEIVING_PLATFORM;
    box(m, (Vector3) { -5, 0, 1 }, (Vector3) { 3, 2, 6 }, (Color) { 126, 134, 132, 255 });
    box(m, (Vector3) { -5, 1.01f, 0 }, (Vector3) { 2, .03f, 2.8f },
        receiving ? (Color) { 220, 207, 144, 255 } : teal);
    for (int side = -1; receiving && side <= 1; side += 2) {
        box(m, (Vector3) { -5 + side * .95f, 1.03f, 0 }, (Vector3) { .1f, .03f, 2.8f }, yellow);
        box(m, (Vector3) { -5, 1.03f, side * 1.35f }, (Vector3) { 2, .03f, .1f }, yellow);
    }
    for (int i = 0; i < 4; ++i)
        box(m, (Vector3) { -3.48f, .85f, -1.5f + i * 1.5f }, (Vector3) { .07f, .25f, .6f }, yellow);
    box(m, (Vector3) { -4, 1.3f, 3 }, (Vector3) { .9f, .6f, .9f }, ink);
    box(m, (Vector3) { -4, 3.3f, 3 }, (Vector3) { .35f, 4.4f, .35f }, yellow);
    Vector3 hook = index == s->active
        ? boat_cargo_hook(s)
        : Vector3Subtract(boat_cargo_tip(crane), (Vector3) { 0, crane->rope, 0 });
    Vector3 tip = boat_cargo_site_local(crane, boat_cargo_tip(crane));
    hook = boat_cargo_site_local(crane, hook);
    boat_model_beam(m, (Vector3) { -4, 5.5f, 3 }, tip, .22f, yellow);
    boat_model_beam(m, (Vector3) { -4, 4.6f, 3 }, tip, .07f, ink);
    boat_model_beam(m, tip, hook, .035f, ink);
    box(m, hook, (Vector3) { .16f, .23f, .16f }, yellow);
    to_site(e, m, first, crane);
}

// One evaluated mesh for both renderers, including rope, cargo and support markers.
static size_t scene(const Experiment *e, BoatModel *m)
{
    const BoatCargoSandbox *s = &e->sandbox;
    Vector3 origin = s->cranes[0].origin;
    BoatCargo cargo = s->cargo;
    cargo.position = Vector3Subtract(cargo.position, origin);
    const BoatCargo *c = &cargo;
    boat_model_begin(m);
    for (unsigned i = 0; i < s->crane_count; ++i)
        if (!s->cranes[i].hidden)
            site_parts(e, m, i);

    Boat local_boat = e->boat;
    local_boat.position = Vector2Subtract(local_boat.position, boat_cargo_berth(&s->cranes[0]));
    const Boat *b = &local_boat;
    boat_box(m, b, (Vector3) { 0, .18f, 0 }, (Vector3) { 2.6f, .85f, 4.6f }, teal);
    boat_box(m, b, (Vector3) { 0, .69f, 0 }, (Vector3) { 2.6f, .12f, 3.8f },
        (Color) { 176, 151, 106, 255 });
    boat_box(m, b, (Vector3) { 0, 1.05f, -1.95f }, (Vector3) { 1.1f, .8f, .45f }, ink);
    boat_box(m, b, (Vector3) { 0, .45f, 2.4f }, (Vector3) { .5f, .7f, .5f }, ink);
    for (int side = -1; side <= 1; side += 2) {
        boat_box(m, b, (Vector3) { side * 1.32f, .35f, -.9f }, (Vector3) { .14f, .45f, .6f }, ink);
        boat_box(m, b, (Vector3) { side * 1.32f, .35f, 1.1f }, (Vector3) { .14f, .45f, .6f }, ink);
        boat_box(m, b, local_target(side < 0 ? 2 : 3), (Vector3) { .85f, .025f, .85f },
            (Color) { 220, 207, 144, 255 });
    }
    if (s->moored) {
        boat_model_beam(m, site_vertex(e, active(e), (Vector3) { -3.4f, 1, -1 }),
            boat_pose_attachment(b, (Vector3) { -1.25f, .8f, -1 }), .04f, yellow);
        boat_model_beam(m, site_vertex(e, active(e), (Vector3) { -3.4f, 1, 1.5f }),
            boat_pose_attachment(b, (Vector3) { -1.25f, .8f, 1.5f }), .04f, yellow);
    }
    size_t receivers = m->mesh.triangle_count;
    size_t start = receivers * 3;
    box(m, (Vector3) { 0 }, (Vector3) { .8f, .8f, .8f },
        c->state == BOAT_CARGO_SECURED ? green : (Color) { 196, 111, 59, 255 });
    box(m, (Vector3) { 0, -.32f, 0 }, (Vector3) { .92f, .12f, .92f }, (Color) { 116, 78, 50, 255 });
    box(m, (Vector3) { 0, .05f, -.405f }, (Vector3) { .18f, .45f, .025f }, yellow);
    Matrix rotation = MatrixRotateXYZ(c->rotation);
    for (size_t i = start; i < m->mesh.triangle_count * 3; ++i)
        m->vertices[i] = Vector3Add(c->position, Vector3Transform(m->vertices[i], rotation));
    if (c->state == BOAT_CARGO_SECURED) {
        for (int side = -1; side <= 1; side += 2)
            boat_model_beam(m, boat_pose_attachment(b, (Vector3) { side * 1.2f, .78f, .65f }),
                Vector3Add(c->position, (Vector3) { 0, .43f, 0 }), .04f, ink);
    }
    BoatCargoMoorStatus berth = boat_cargo_moor_status(s, &e->boat);
    for (unsigned i = 0; i < s->crane_count; ++i) {
        const BoatCargoCrane *crane = &s->cranes[i];
        if (crane->hidden)
            continue;
        Color berth_color = berth.site == i && berth.reason == BOAT_CARGO_MOOR_READY
            ? green
            : (Color) { 178, 220, 220, 255 };
        // The outline marks the actual final hull pose; the bow arrow faces the site heading.
        berth_line(e, crane, m, -1.45f, -2.45f, 1.45f, -2.45f, berth_color);
        berth_line(e, crane, m, -1.45f, 2.45f, 1.45f, 2.45f, berth_color);
        berth_line(e, crane, m, -1.45f, -2.45f, -1.45f, 2.45f, berth_color);
        berth_line(e, crane, m, 1.45f, -2.45f, 1.45f, 2.45f, berth_color);
        berth_line(e, crane, m, 0, -3.2f, -.5f, -2.6f, berth_color);
        berth_line(e, crane, m, 0, -3.2f, .5f, -2.6f, berth_color);
        berth_line(e, crane, m, 0, -3.2f, 0, -2.2f, berth_color);
        // Turning references and a berth approach, away from the work surface.
        size_t first = m->mesh.triangle_count * 3;
        for (int side = -1; side <= 1; side += 2) {
            box(m, (Vector3) { side * 11, .35f, -18 }, (Vector3) { .5f, .8f, .5f }, yellow);
        }
        to_site(e, m, first, crane);
    }
    for (size_t i = 0; i < m->mesh.triangle_count * 3; ++i)
        m->vertices[i] = Vector3Add(m->vertices[i], origin);
    return receivers;
}

static bool site_camera_clear(const Experiment *e, Vector3 p)
{
    for (unsigned i = 0; i < e->sandbox.crane_count; ++i) {
        const BoatCargoCrane *crane = &e->sandbox.cranes[i];
        if (crane->hidden)
            continue;
        Vector3 q = boat_cargo_site_local(crane, p);
        if (q.x > -7 && q.x < -3 && q.z > -2.5f && q.z < 4.5f && q.y < 1.5f)
            return false;
        if (q.x > -4.6f && q.x < -3.4f && q.z > 2.4f && q.z < 3.6f && q.y < 6)
            return false;
        Vector3 a = boat_cargo_site_point(crane, (Vector3) { -4, 5.5f, 3 });
        Vector3 b = boat_cargo_tip(crane), axis = Vector3Subtract(b, a);
        float t
            = Clamp(Vector3DotProduct(Vector3Subtract(p, a), axis) / Vector3LengthSqr(axis), 0, 1);
        if (Vector3Distance(p, Vector3Add(a, Vector3Scale(axis, t))) <= .6f)
            return false;
    }
    return true;
}

static View3D camera(const Experiment *e, const WaterfrontScenery *scenery)
{
    Vector3 centre = boat_cargo_site_point(active(e), (Vector3) { -2, 2.2f, 1 });
    Vector3 eye = boat_cargo_site_point(active(e), (Vector3) { 8.5f, 9, 12 });
    if (!e->crane_view) {
        centre = boat_pose_attachment(&e->boat, (Vector3) { 0, 1, 0 });
        eye = Vector3Add(
            centre, (Vector3) { sinf(e->boat.heading) * 13, 9, cosf(e->boat.heading) * 13 });
    }
    if (e->world.definition->mapped) {
        Vector3 safe = centre;
        for (int i = 1; i <= 80; ++i) {
            Vector3 p = Vector3Lerp(centre, eye, i / 80.0f);
            if (!site_camera_clear(e, p) || !waterfront_scenery_clear(scenery, p, .45f)
                || !waterfront_harbor_camera_clear_scene(e->world.scene, p, .45f)
                || !waterfront_traffic_camera_clear_scene(
                    e->world.scene, p, boat_tick_seconds(e->tick), .45f))
                break;
            safe = p;
        }
        eye = Vector3Distance(safe, centre) > .05f ? safe
                                                   : Vector3Add(centre, (Vector3) { 0, .6f, 0 });
    }
    return (View3D) { eye, centre, { 0, 1, 0 }, 55 * DEG2RAD, .1f,
        e->world.definition->mapped ? 4500 : 240 };
}

static void water_mesh(const Experiment *e, BoatModel *m)
{
    // A small finite basin patch. Water queries share the physical ambient/player wakes.
    boat_model_begin(m);
    float cx = -e->boat.position.x, cz = e->boat.position.y;
    for (int z = -10; z < 10; ++z)
        for (int x = -10; x < 10; ++x) {
            Vector3 p[4] = { { cx + x * 5, 0, cz + z * 5 }, { cx + x * 5, 0, cz + (z + 1) * 5 },
                { cx + (x + 1) * 5, 0, cz + (z + 1) * 5 }, { cx + (x + 1) * 5, 0, cz + z * 5 } };
            for (int i = 0; i < 4; ++i)
                p[i].y = boat_water_query_borrowed(
                    &e->world.water, (Vector2) { -p[i].x, p[i].z }, boat_tick_seconds(e->tick))
                             .height;
            boat_model_quad(m, p[0], p[1], p[2], p[3],
                (x + z) % 2 ? (Color) { 45, 107, 122, 255 } : (Color) { 48, 113, 127, 255 });
        }
}

static bool gpu_init(GpuMesh *g)
{
    g->mesh = (Mesh) { .vertexCount = MAX_VERTICES, .triangleCount = MAX_VERTICES / 3 };
    g->mesh.vertices = MemAlloc(MAX_VERTICES * 3 * sizeof(float));
    g->mesh.colors = MemAlloc(MAX_VERTICES * 4);
    if (!g->mesh.vertices || !g->mesh.colors) {
        MemFree(g->mesh.vertices);
        MemFree(g->mesh.colors);
        *g = (GpuMesh) { 0 };
        return false;
    }
    memset(g->mesh.vertices, 0, MAX_VERTICES * 3 * sizeof(float));
    memset(g->mesh.colors, 255, MAX_VERTICES * 4);
    UploadMesh(&g->mesh, true);
    g->material = LoadMaterialDefault();
    return g->mesh.vaoId != 0;
}

static void gpu_draw(GpuMesh *g, const BoatModel *m, bool lit)
{
    Vector3 light = Vector3Normalize((Vector3) { -1, 3, 2 });
    for (size_t t = 0; t < m->mesh.triangle_count; ++t) {
        Triangle3D triangle = m->triangles[t];
        Vector3 a = m->vertices[triangle.vertices[0]], b = m->vertices[triangle.vertices[1]],
                c = m->vertices[triangle.vertices[2]];
        Vector3 normal
            = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(b, a), Vector3Subtract(c, a)));
        float shade = lit ? .3f + .7f * fmaxf(0, Vector3DotProduct(normal, light)) : 1;
        for (int i = 0; i < 3; ++i) {
            size_t v = t * 3 + i;
            Vector3 p = m->vertices[triangle.vertices[i]];
            memcpy(g->mesh.vertices + v * 3, &p, sizeof(p));
            g->mesh.colors[v * 4] = (unsigned char)(triangle.color.r * shade);
            g->mesh.colors[v * 4 + 1] = (unsigned char)(triangle.color.g * shade);
            g->mesh.colors[v * 4 + 2] = (unsigned char)(triangle.color.b * shade);
            g->mesh.colors[v * 4 + 3] = 255;
        }
    }
    g->mesh.triangleCount = (int)m->mesh.triangle_count;
    g->mesh.vertexCount = g->mesh.triangleCount * 3;
    UpdateMeshBuffer(g->mesh, 0, g->mesh.vertices, g->mesh.triangleCount * 9 * sizeof(float), 0);
    UpdateMeshBuffer(g->mesh, 3, g->mesh.colors, g->mesh.triangleCount * 12, 0);
    DrawMesh(g->mesh, g->material, MatrixIdentity());
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
        gpu_draw(gpu, &triangle, false);
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

static void hook_action(Experiment *e)
{
    e->notice = boat_cargo_hook_toggle(&e->sandbox)
        ? "Hook changed. Lower to a surface before releasing."
        : "Move hook near the crate's top. Unlash first; moor to operate.";
    e->aim = 0;
}

static void lash_action(Experiment *e)
{
    if (e->sandbox.mooring) {
        e->notice = "Wait for mooring to finish, or G to cancel, before unlashing.";
        return;
    }
    e->notice = boat_cargo_lash_toggle(&e->sandbox, &e->boat)
        ? "Lashing changed. Green crate + straps = secured."
        : "Detach hook, settle fully inside deck, stop boat, then lash.";
}

static void moor_action(Experiment *e)
{
    bool moored = e->sandbox.moored;
    bool changed = boat_cargo_moor_toggle(&e->sandbox, &e->boat);
    if (changed && e->sandbox.mooring)
        e->target = e->sandbox.active;
    else if (changed && moored) // Departure: guide toward the job's site, else the next one.
        e->target = e->guide < e->site_count && e->guide != e->sandbox.active
            ? e->guide
            : next_site(e, e->sandbox.active);
    e->notice = changed
        ? (e->sandbox.moored           ? "Moored. TAB: crane controls."
                  : e->sandbox.mooring ? "Mooring assist active. G cancels."
                                       : "Ready to drive. TAB: helm controls.")
        : (e->sandbox.moored
                  ? "Release hook and lash loose deck cargo before departure."
                  : "Approach the berth outline; SPACE brakes; G starts mooring assist.");
    e->aim = 0;
}

static void recover_action(Experiment *e)
{
    if (boat_cargo_recover(&e->sandbox)) {
        notice(e, BOAT_SCENE_NOTICE_RETURNED, e->sandbox.source);
    } else
        e->notice = "Release the hook and unlash before returning the crate.";
    e->aim = 0;
}

static void view_action(Experiment *e)
{
    if (e->crane_view || e->sandbox.moored) {
        e->crane_view = !e->crane_view;
        e->aim = 0;
        e->boat.throttle = 0;
        e->notice = e->crane_view ? "Crane controls active: A/D slew, W/S reach, Q/E hoist."
                                  : "Helm controls active: arrows drive, hold SPACE to brake.";
    } else
        e->notice = "Moor at a work berth to operate its crane.";
}

// A screen marker over the target berth, so a distant site can be found from the helm.
static void target_marker(const Experiment *e, View3D view)
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
static void job_ui(const Experiment *e)
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

static void ui(Experiment *e, View3D view, Vector2 mouse, float *slew, float *reach, float *hoist)
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
        hook_action(e);
    if (button((Rectangle) { 82, 236, 70, 22 }, "L Lash", mouse, !e->paused, false))
        lash_action(e);
    if (button((Rectangle) { 156, 236, 70, 22 },
            e->sandbox.mooring      ? "G Cancel"
                : e->sandbox.moored ? "G Unmoor"
                                    : "G Moor",
            mouse, !e->paused, false))
        moor_action(e);
    if (button((Rectangle) { 230, 236, 85, 22 }, e->crane_view ? "TAB Helm" : "TAB Crane", mouse,
            !e->paused && (e->crane_view || e->sandbox.moored), false))
        view_action(e);
    if (button((Rectangle) { 319, 236, 72, 22 }, "R Reset", mouse, true, false))
        reset(e);
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

// Lift the crate on the moored site's pad to the deck's AIM target (2 left, 3 right), release
// and lash it, through the real hook, aim, release and lash paths.
static bool load_and_lash(Experiment *e, int aim)
{
    if (!boat_cargo_hook_toggle(&e->sandbox))
        return false;
    e->aim = aim;
    for (int i = 0; i < 2000; ++i)
        tick(e, 0, 0, 0, (BoatInput) { 0 });
    e->aim = 0;
    for (int i = 0; i < 900 && !e->sandbox.cargo.deck_contact; ++i)
        tick(e, 0, 0, -.5f, (BoatInput) { 0 });
    if (!e->sandbox.cargo.deck_contact || !boat_cargo_hook_toggle(&e->sandbox))
        return false;
    for (int i = 0; i < 600 && e->sandbox.cargo.settled < .5f; ++i)
        tick(e, 0, 0, 0, (BoatInput) { 0 });
    return boat_cargo_lash_toggle(&e->sandbox, &e->boat);
}

// Pose the unmoored boat at SITE's approach (in place of a crossing) and moor there by the real
// G path. With APPROACH_ONLY, stop once the berth reads ready.
static bool moor_at(Experiment *e, unsigned site, bool approach_only)
{
    const BoatCargoCrane *crane = &e->sandbox.cranes[site];
    e->crane_view = false;
    Vector3 approach = boat_cargo_site_point(crane, (Vector3) { 1.8f, 0, 4.3f });
    e->boat.position = (Vector2) { -approach.x, approach.z };
    e->boat.heading = crane->heading + .5f;
    e->boat.velocity = (Vector2) { 0 };
    boat_cargo_step(&e->sandbox, &e->boat, &e->world, boat_tick_seconds(e->tick));
    BoatCargoMoorStatus status = boat_cargo_moor_status(&e->sandbox, &e->boat);
    if (status.site != site || status.reason != BOAT_CARGO_MOOR_READY)
        return false;
    if (approach_only)
        return true;
    moor_action(e);
    for (int i = 0; i < 900 && !e->sandbox.moored; ++i)
        tick(e, 0, 0, 0, (BoatInput) { 0 });
    if (!e->sandbox.moored || e->sandbox.active != site)
        return false;
    view_action(e);
    return true;
}

// SV1c fixture switch: stop unload_at on the tick the core delivery is queued but the job has
// not yet counted it.
static bool stop_at_pending_delivery;

// Carry the lashed crate (lashed on the left target) to RECEIVER, moor and unload it onto that
// site's pad with its crane, until the native delivery commits there. The job may use the
// delivered crate up meanwhile, so the commit is watched tick by tick.
static bool unload_at(Experiment *e, unsigned receiver, bool approach_only)
{
    if (!moor_at(e, receiver, approach_only))
        return false;
    if (approach_only)
        return true;
    if (!boat_cargo_lash_toggle(&e->sandbox, &e->boat))
        return false;
    e->aim = 2;
    for (int i = 0; i < 2000; ++i)
        tick(e, 0, 0, 0, (BoatInput) { 0 });
    e->aim = 0;
    Vector3 top = { 0, BOAT_CARGO_HALF, 0 };
    for (int i = 0; i < 900
        && Vector3Distance(boat_cargo_hook(&e->sandbox), Vector3Add(e->sandbox.cargo.position, top))
            > .6f;
        ++i)
        tick(e, 0, 0, -.5f, (BoatInput) { 0 });
    if (!boat_cargo_hook_toggle(&e->sandbox))
        return false;
    e->aim = 1;
    for (int i = 0; i < 2000; ++i)
        tick(e, 0, 0, 0, (BoatInput) { 0 });
    e->aim = 0;
    for (int i = 0; i < 900 && e->sandbox.cargo.position.y > 1.41f; ++i)
        tick(e, 0, 0, -.5f, (BoatInput) { 0 });
    if (!boat_cargo_hook_toggle(&e->sandbox))
        return false;
    unsigned load = e->sandbox.cargo.load;
    bool delivered = false;
    for (int i = 0; i < 240; ++i) {
        tick(e, 0, 0, i < 120 ? 1 : 0, (BoatInput) { 0 });
        delivered |= e->sandbox.cargo.load == load && e->sandbox.cargo.state == BOAT_CARGO_PAD
            && e->sandbox.cargo.pad == receiver && e->sandbox.cargo.receivers & 1u << receiver;
        if (stop_at_pending_delivery && delivered && e->event_count)
            return true;
    }
    return delivered;
}

// The committed value of the job's HUD row LABEL, or -1 when there is none.
static int64_t hud_value(const Experiment *e, const char *label)
{
    for (unsigned i = 0; i < e->objective.hud_count; ++i)
        if (!strcmp(e->objective.hud[i].label, label))
            return e->objective.hud[i].value;
    return -1;
}

// The review fixtures follow the bundled outer job by the package's own site ids: the landing
// receiver, the quay (a service platform the job builds) and the jetty receiver. SITE_COUNT
// means the package has no such site.
static unsigned find_site(const Experiment *e, const char *id)
{
    for (unsigned i = 0; i < e->site_count; ++i)
        if (!strcmp(e->sites[i].id, id))
            return i;
    return e->site_count;
}

static bool stage(Experiment *e, const char *name)
{
    if (!strcmp(name, "origin"))
        return true;
    if (!strcmp(name, "swing")) {
        e->notice = "Swing fixture. F: release, R: recover crate and boat.";
        boat_cargo_hook_toggle(&e->sandbox);
        for (int i = 0; i < 360; ++i)
            tick(e, .7f, .5f, 1, (BoatInput) { 0 });
    } else if (!strcmp(name, "shadow-bow") || !strcmp(name, "shadow-water")) {
        for (int i = 0; i < 420; ++i)
            tick(e, 0, 0, 0, (BoatInput) { 0 });
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
            tick(e, 0, 0, 0, (BoatInput) { 0 });
        e->aim = 0;
        float gap = !strcmp(name, "shadow-high") ? 1.2f : .2f;
        BoatCargoShadow shadow = { 0 };
        for (int i = 0; i < 900; ++i) {
            shadow = boat_cargo_shadow(&e->sandbox, &e->boat, &e->world, boat_tick_seconds(e->tick),
                BOAT_CARGO_SHADOW_DECK);
            if (shadow.count && shadow.clearance <= gap)
                break;
            tick(e, 0, 0, -.5f, (BoatInput) { 0 });
        }
        return shadow.count && shadow.clearance > .05f && shadow.clearance <= gap;
    } else if (!strcmp(name, "berth-approach") || !strcmp(name, "mooring")
        || !strcmp(name, "remoor")) {
        // Explicit returning-boat review pose; subsequent capture uses the real G/tick path.
        if (!stage(e, "departure"))
            return false; // Carry a crate obtained through the actual lift/lash path.
        e->crane_view = false;
        Vector3 approach = boat_cargo_site_point(active(e), (Vector3) { 1.8f, 0, 4.3f });
        e->boat.position = (Vector2) { -approach.x, approach.z };
        e->boat.heading = active(e)->heading + .5f;
        e->boat.velocity = (Vector2) { .2f, -.8f };
        e->boat.throttle = .25f;
        e->boat.engine_response = .2f;
        boat_cargo_step(&e->sandbox, &e->boat, &e->world, boat_tick_seconds(e->tick));
        if (!strcmp(name, "berth-approach"))
            return boat_cargo_moor_status(&e->sandbox, &e->boat).reason == BOAT_CARGO_MOOR_READY;
        moor_action(e);
        if (!e->sandbox.mooring)
            return false;
        int ticks = !strcmp(name, "mooring") ? 120 : 900;
        for (int i = 0; i < ticks && !e->sandbox.moored; ++i)
            tick(e, 0, 0, 0, (BoatInput) { 0 });
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
            tick(e, 0, 0, 0, (BoatInput) { 0 });
        return e->target != e->sandbox.active;
    } else if (!strcmp(name, "redelivered") || !strcmp(name, "job-reset")) {
        // S1c/S1d exactly-once review: after the counted delivery the job used the crate up and
        // supplied load 2 at the quay. Setting load 2 on the landing's pad (a pose in place of a
        // crossing) commits a native delivery there, which the job ignores: it counts load 1 at
        // the landing once and wants load 2 at the jetty. R then restarts everything together.
        if (!stage(e, "delivered") || !e->revision)
            return false;
        unsigned landing = find_site(e, "landing");
        if (landing == e->site_count)
            return false;
        e->sandbox.cargo.state = BOAT_CARGO_FLOATING;
        e->sandbox.cargo.position
            = boat_cargo_site_point(&e->sandbox.cranes[landing], (Vector3) { -5, 1.6f, 0 });
        for (int i = 0; i < 600; ++i)
            tick(e, 0, 0, 0, (BoatInput) { 0 });
        const BoatCargo *c = &e->sandbox.cargo;
        BoatPluginObjective o = e->objective;
        bool once = c->state == BOAT_CARGO_PAD && c->pad == landing && c->load == 2
            && c->receivers == 1u << landing && !o.quarantined && o.acknowledged == e->sequence
            && hud_value(e, "LANDING BUILT") == 1 && hud_value(e, "SUPPLIES TO JETTY / 1") == 0;
        if (!strcmp(name, "redelivered"))
            return once;
        reset(e);
        for (int i = 0; i < 24; ++i)
            tick(e, 0, 0, 0, (BoatInput) { 0 });
        bool hidden = true;
        for (unsigned i = 0; i < e->site_count; ++i)
            hidden &= !e->sites[i].hidden || e->sandbox.cranes[i].hidden;
        return once && hidden && !c->delivered && c->load == 1 && e->guide == e->site_count
            && e->objective.hud_count == 1 && hud_value(e, "CRATES TO LANDING / 1") == 0;
    } else if (!strcmp(name, "delivery-pending")) {
        // SV1c resume fixture: the core has delivered the crate but the job has not counted it
        // yet, so the delivery event is still queued and unacknowledged.
        unsigned landing = find_site(e, "landing");
        if (!e->revision || landing == e->site_count || !stage(e, "departure"))
            return false;
        stop_at_pending_delivery = true;
        bool ok = unload_at(e, landing, false);
        stop_at_pending_delivery = false;
        return ok && e->sandbox.cargo.delivered && e->event_count == 1
            && hud_value(e, "LANDING BUILT") == -1;
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
        unsigned landing = find_site(e, "landing"), quay = find_site(e, "landing_quay");
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
            tick(e, 0, 0, 0, (BoatInput) { 0 });
        BoatPluginObjective waiting = e->objective;
        if (!e->sandbox.cargo.delivered || !e->waiting || !e->sandbox.cranes[quay].hidden
            || waiting.tick > (int64_t)e->tick - 12 || waiting.hud[0].value != 0
            || e->event_count != 1)
            return false;
        e->boat.position = boat_cargo_berth(&e->sandbox.cranes[landing]);
        e->boat.velocity = (Vector2) { 0 };
        for (int i = 0; i < 24; ++i)
            tick(e, 0, 0, 0, (BoatInput) { 0 });
        e->notice = "Build-blocked review fixture. R: restart from the first site.";
        return !e->waiting && !e->sandbox.cranes[quay].hidden && hud_value(e, "LANDING BUILT") == 1
            && !e->event_count && e->sandbox.source == quay;
    } else if (!strcmp(name, "landing-built")) {
        // S1c review: after the delivery, look back from the helm at the landing and the quay
        // the job built beside it.
        if (!stage(e, "delivered") || !e->revision)
            return false;
        unsigned built = find_site(e, "landing_quay");
        if (built == e->site_count || !boat_cargo_moor_toggle(&e->sandbox, &e->boat))
            return false;
        const BoatCargoCrane *landing = active(e), *quay = &e->sandbox.cranes[built];
        e->crane_view = false;
        Vector3 between = Vector3Lerp(boat_cargo_site_point(landing, (Vector3) { -2, 0, 1 }),
            boat_cargo_site_point(quay, (Vector3) { -2, 0, 1 }), .5f);
        Vector3 out = boat_cargo_site_point(landing, (Vector3) { 0, 0, -15 });
        Vector3 at = Vector3Add(between, Vector3Subtract(out, landing->origin));
        e->boat.position = (Vector2) { -at.x, at.z };
        e->boat.heading = landing->heading + PI;
        e->boat.velocity = (Vector2) { 0 };
        for (int i = 0; i < 60; ++i)
            tick(e, 0, 0, 0, (BoatInput) { 0 });
        e->notice = "Landing built review fixture. R: restart from the first site.";
        return !quay->hidden;
    } else if (!strcmp(name, "receiver-approach") || !strcmp(name, "delivered")) {
        // S1b review: carry the lashed crate to the first receiving site (an explicit pose in
        // place of the crossing), then moor and unload with that site's crane by the real path.
        unsigned receiver = find_site(e, "landing");
        if (receiver == e->site_count || !stage(e, "departure"))
            return false;
        if (!unload_at(e, receiver, !strcmp(name, "receiver-approach")))
            return false;
        if (!strcmp(name, "receiver-approach"))
            return true;
        // S1c/S1d: the package's job counts the delivery once, builds its hidden quay and jetty
        // and uses the crate up for load 2 on the quay's pad. The notice must be owned text,
        // not a reused raylib TextFormat buffer.
        for (int i = 0; i < 8; ++i) // Cycle every TextFormat buffer, as one HUD frame does.
            TextFormat("%d", i);
        unsigned quay = find_site(e, "landing_quay");
        if (e->revision && quay == e->site_count)
            return false;
        char supplied[64] = "";
        if (quay < e->site_count)
            notice_text(e, BOAT_SCENE_NOTICE_SUPPLIED, quay, supplied, sizeof(supplied));
        if (e->revision
            && (strcmp(e->notice, supplied) || e->sandbox.cargo.load != 2
                || e->sandbox.cargo.state != BOAT_CARGO_PAD || e->sandbox.cargo.pad != quay
                || e->sandbox.source != quay || e->guide != find_site(e, "jetty")))
            return false;
        e->notice = "Delivered review fixture. R: restart from the first site.";
        for (unsigned i = 0; e->revision && i < e->site_count; ++i)
            if (e->sites[i].hidden && e->sandbox.cranes[i].hidden)
                return false;
        return !e->revision
            || (!e->objective.quarantined && !e->waiting && e->objective.hud_count == 2
                && hud_value(e, "LANDING BUILT") == 1
                && hud_value(e, "SUPPLIES TO JETTY / 1") == 0);
    } else if (!strcmp(name, "quay-pickup") || !strcmp(name, "jetty-approach")
        || !strcmp(name, "jetty-delivered")) {
        // S1d's second outing: from the landing, moor at the built quay beside it by the real G
        // path, where load 2 waits; lift and lash it, depart toward the job's guided jetty, then
        // moor and unload there (explicit poses in place of the two short drives).
        unsigned quay = find_site(e, "landing_quay");
        unsigned jetty = find_site(e, "jetty");
        if (!e->revision || quay == e->site_count || jetty == e->site_count
            || !stage(e, "delivered") || !boat_cargo_moor_toggle(&e->sandbox, &e->boat)
            || !moor_at(e, quay, false))
            return false;
        if (!strcmp(name, "quay-pickup")) {
            e->notice = "Quay pickup review fixture. R: restart from the first site.";
            return e->sandbox.cargo.state == BOAT_CARGO_PAD && e->sandbox.cargo.pad == quay
                && e->sandbox.cargo.load == 2 && e->guide == jetty;
        }
        if (!load_and_lash(e, 2))
            return false;
        moor_action(e); // G: departure guidance follows the job's work.guide.
        if (e->sandbox.moored || e->target != jetty)
            return false;
        if (!unload_at(e, jetty, !strcmp(name, "jetty-approach")))
            return false;
        if (!strcmp(name, "jetty-approach"))
            return true;
        for (int i = 0; i < 24; ++i)
            tick(e, 0, 0, 0, (BoatInput) { 0 });
        e->notice = "Jetty delivered review fixture. R: restart from the first site.";
        return !e->objective.quarantined && !e->waiting && e->sandbox.cargo.load == 2
            && e->sandbox.cargo.pad == jetty && hud_value(e, "LANDING BUILT") == 1
            && hud_value(e, "JETTY SUPPLIED") == 1;
    } else if (!strcmp(name, "loaded-left") || !strcmp(name, "loaded-right")
        || !strcmp(name, "departure")) {
        // Exercise the real application assist, wave-posed deck, release and departure.
        if (!load_and_lash(e, !strcmp(name, "loaded-right") ? 3 : 2)
            || !boat_cargo_moor_toggle(&e->sandbox, &e->boat))
            return false;
        if (!strcmp(name, "departure"))
            return true; // The user's handoff: unmoored, still operating crane controls.
        e->crane_view = false;
        e->notice = "Loaded review fixture. R: restart the hands-on lifting trial.";
        for (int i = 0; i < 600; ++i)
            tick(e, 0, 0, 0,
                (BoatInput) {
                    .throttle_rate = i < 30 ? .4f : 0, .steering_rate = i > 360 ? .35f : 0 });
    }
    return true;
}

// SV1c resume check. The staged session is saved, then resumed twice, once from the record
// itself and once from its encoded and decoded bytes. Each copy then runs the same scripted
// 1200 ticks, and every tick's cargo, boat, events, objective and HUD must hash equal.
enum { RESUME_TICKS = 1200 };

static void hash_bytes(uint64_t *h, const void *bytes, size_t size)
{
    const unsigned char *p = bytes;
    for (size_t i = 0; i < size; ++i)
        *h = (*h ^ p[i]) * UINT64_C(1099511628211);
}
#define HASH(h, value) hash_bytes(h, &(value), sizeof(value))

static uint64_t session_hash(const Experiment *e)
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
        hash_bytes(&h, e->events[i].kind, strlen(e->events[i].kind) + 1);
        hash_bytes(&h, e->events[i].id, strlen(e->events[i].id) + 1);
    }
    uint64_t objective = boat_plugin_objective_digest(&e->objective);
    HASH(&h, objective);
    HASH(&h, e->objective.quarantined);
    return h;
}

// The same scripted actions and inputs for every copy, a function of the tick count alone.
static void resume_script(Experiment *e, unsigned i)
{
    if (i == 5 || i == 900)
        hook_action(e);
    if (i == 300)
        lash_action(e);
    if (i == 700)
        moor_action(e);
    float slew = ((int)(i / 150) % 3 - 1) * .5f, reach = (int)(i / 200) % 2 ? .4f : -.4f;
    float hoist = (int)(i / 90) % 2 ? -.3f : .5f;
    tick(e, slew, reach, hoist,
        (BoatInput) { .throttle_rate = i < 30 ? .4f : 0, .steering_rate = i > 360 ? .1f : 0 });
}

#define RESUME_CHECK(condition)                                                                    \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            fprintf(stderr, "Resume check failed at %d: %s\n", __LINE__, #condition);              \
            return false;                                                                          \
        }                                                                                          \
    } while (0)

static bool resume_check(Experiment *e, const char *scenario)
{
    static BoatSandboxSave record, decoded;
    static unsigned char bytes[BOAT_SANDBOX_SAVE_MAX], again[BOAT_SANDBOX_SAVE_MAX];
    static uint64_t trace[2][RESUME_TICKS];
    size_t size = 0, size_again = 0;
    RESUME_CHECK(experiment_save(e, 0, &record));
    RESUME_CHECK(boat_sandbox_save_encode(&record, bytes, sizeof(bytes), &size));
    RESUME_CHECK(boat_sandbox_save_decode(bytes, size, &decoded) == BOAT_SANDBOX_SAVE_OK);
    RESUME_CHECK(boat_sandbox_save_encode(&decoded, again, sizeof(again), &size_again)
        && size == size_again && !memcmp(bytes, again, size));
    BoatSandboxIdentity now;
    experiment_identity(e, &now);
    char reason[48];
    RESUME_CHECK(
        boat_sandbox_save_check(&decoded.summary.identity, &now, false, reason, sizeof(reason))
        == BOAT_SANDBOX_RESUME);
    bool is_swing = !strcmp(scenario, "swing"), is_capsized = !strcmp(scenario, "capsized");
    bool lashed = !strcmp(scenario, "departure") || !strcmp(scenario, "crossing");
    bool pending = !strcmp(scenario, "delivery-pending");
    for (unsigned pass = 0; pass < 2; ++pass) {
        RESUME_CHECK(experiment_resume(e, pass ? &decoded : &record));
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
            RESUME_CHECK(
                e->event_count == 1 && c->delivered && hud_value(e, "LANDING BUILT") == -1);
        for (unsigned i = 0; i < RESUME_TICKS; ++i) {
            resume_script(e, i);
            trace[pass][i] = session_hash(e);
        }
        RESUME_CHECK(!e->objective.quarantined);
        // Exactly one counted delivery and one built quay, whichever side of the update the
        // save fell on.
        unsigned quay = find_site(e, "landing_quay");
        if (pending || !strcmp(scenario, "delivered") || !strcmp(scenario, "landing-built")
            || !strcmp(scenario, "quay-pickup") || !strcmp(scenario, "jetty-delivered")) {
            RESUME_CHECK(quay < e->site_count && !e->sandbox.cranes[quay].hidden);
            RESUME_CHECK(hud_value(e, "LANDING BUILT") == 1);
        }
        if (!strcmp(scenario, "jetty-delivered"))
            RESUME_CHECK(hud_value(e, "JETTY SUPPLIED") == 1);
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

// SV1d: the session file and its automatic saving. Nothing here is a player action: the
// harness saves at milestones, every 30 s of play, and when pausing, losing focus or quitting.
typedef struct {
    char path[1100];
    bool enabled;
    bool autosaving; // The live window loop writes; staged runs only write on --save-now.
    bool failed;
    int mark; // Presentation frames left to show SAVED.
    unsigned writes; // Successful automatic writes, for the autosave check.
    BoatAutosave autosave;
} SaveFile;

static bool stale_digest; // Test fixture: write a save whose work digest no longer matches.

static bool save_write(Experiment *e, SaveFile *file)
{
    static BoatSandboxSave record;
    static unsigned char bytes[BOAT_SANDBOX_SAVE_MAX];
    size_t size = 0;
    if (!experiment_save(e, (int64_t)time(NULL), &record))
        return false;
    if (stale_digest)
        record.summary.identity.work_digest ^= 1;
    return boat_sandbox_save_encode(&record, bytes, sizeof(bytes), &size)
        && boat_file_write_atomic(file->path, bytes, size);
}

// Try one file: true when it decoded, belongs to this package and the core resumed it.
// REASON gets the first problem otherwise.
static bool save_try(Experiment *e, const char *path, const char **reason)
{
    static BoatSandboxSave record;
    static unsigned char bytes[BOAT_SANDBOX_SAVE_MAX];
    size_t size = 0;
    if (!boat_file_read(path, bytes, sizeof(bytes), &size)) {
        *reason = "DAMAGED";
        return false;
    }
    BoatSandboxSaveStatus status = boat_sandbox_save_decode(bytes, size, &record);
    if (status != BOAT_SANDBOX_SAVE_OK) {
        *reason = status == BOAT_SANDBOX_SAVE_NEWER ? "NEWER FORMAT" : "DAMAGED";
        return false;
    }
    BoatSandboxIdentity now;
    static char text[48];
    experiment_identity(e, &now);
    if (boat_sandbox_save_check(&record.summary.identity, &now, false, text, sizeof(text))
        != BOAT_SANDBOX_RESUME) {
        *reason = text;
        return false;
    }
    if (!experiment_resume(e, &record)) {
        *reason = "PACKAGE CHANGED";
        return false;
    }
    printf("Resumed save %s at tick %llu (load %u, %u of %u sites built)\n", path,
        (unsigned long long)record.summary.tick, record.summary.load, record.summary.built,
        record.summary.sites);
    return true;
}

// Resume from the file, else from its backup. A file that cannot be used is set aside as
// NAME.rejected (never deleted), with its backup, and the session starts fresh.
static int save_load(Experiment *e, const SaveFile *file)
{
    char backup[1100];
    snprintf(backup, sizeof(backup), "%s.bak", file->path);
    const char *reason = "DAMAGED";
    bool had_file = boat_file_exists(file->path), had_backup = boat_file_exists(backup);
    if (had_file && save_try(e, file->path, &reason))
        return 1;
    if (had_backup && save_try(e, backup, &reason)) {
        printf("Save %s was not usable (%s); resumed its backup\n", file->path, reason);
        return 1;
    }
    int result = 0;
    if (had_file && boat_file_set_aside(file->path, ".rejected")) {
        printf("Save %s set aside as %s.rejected: %s\n", file->path, file->path, reason);
        result = 2;
    }
    if (had_backup && boat_file_set_aside(backup, ".rejected")) {
        printf("Backup %s set aside as %s.rejected\n", backup, backup);
        result = 2;
    }
    return result;
}

// What a frame changes, for the autosave policy.
typedef struct {
    Vector2 position;
    float heading;
    uint64_t progress; // The committed job state, HUD and timers, not its tick.
    unsigned built, load, active;
    bool moored, delivered, secured, on_pad;
    unsigned char receivers;
} Watch;

static Watch watch(const Experiment *e)
{
    Watch w = { .position = e->boat.position,
        .heading = e->boat.heading,
        .progress = UINT64_C(14695981039346656037),
        .load = e->sandbox.cargo.load,
        .active = e->sandbox.active,
        .moored = e->sandbox.moored,
        .delivered = e->sandbox.cargo.delivered,
        .receivers = e->sandbox.cargo.receivers,
        .secured = e->sandbox.cargo.state == BOAT_CARGO_SECURED,
        .on_pad = e->sandbox.cargo.state == BOAT_CARGO_PAD };
    for (unsigned i = 0; i < e->sandbox.crane_count; ++i)
        w.built |= (unsigned)!e->sandbox.cranes[i].hidden << i;
    const BoatPluginObjective *o = &e->objective;
    hash_bytes(&w.progress, o->state.bytes, o->state.size);
    for (unsigned i = 0; i < o->hud_count; ++i) {
        hash_bytes(&w.progress, o->hud[i].label, strlen(o->hud[i].label) + 1);
        hash_bytes(&w.progress, &o->hud[i].value, sizeof(o->hud[i].value));
    }
    hash_bytes(&w.progress, &o->quarantined, sizeof(o->quarantined));
    return w;
}

// Dirty: the boat or the job moved on. Milestone: something the player would hate to repeat.
static void watch_note(SaveFile *file, uint64_t tick, const Watch *before, const Watch *now)
{
    bool pose = before->position.x != now->position.x || before->position.y != now->position.y
        || before->heading != now->heading;
    bool milestone = before->progress != now->progress || before->built != now->built
        || before->load != now->load || before->active != now->active
        || before->moored != now->moored || before->delivered != now->delivered
        || before->receivers != now->receivers || before->secured != now->secured
        || before->on_pad != now->on_pad;
    boat_autosave_note(&file->autosave, tick, pose || milestone, milestone);
}

typedef struct {
    Watch last;
    bool was_paused, was_focused;
} FrameWatch;

// Once per frame, after its tick loop and outside every tick. LEAVING (pausing, losing focus)
// writes when dirty; a milestone writes now; otherwise the period decides. The SAVED mark shows
// only for milestone and leaving writes.
static void autosave_frame(
    Experiment *e, SaveFile *file, FrameWatch *frame, bool paused, bool focused)
{
    Watch now = watch(e);
    watch_note(file, e->tick, &frame->last, &now);
    frame->last = now;
    bool leaving = (paused && !frame->was_paused) || (frame->was_focused && !focused);
    frame->was_paused = paused;
    frame->was_focused = focused;
    if (!boat_autosave_due(&file->autosave, e->tick, leaving))
        return;
    bool show = file->autosave.milestone || leaving;
    bool ok = save_write(e, file);
    boat_autosave_written(&file->autosave, e->tick, ok);
    file->failed = !ok;
    file->writes += ok;
    if (ok && show)
        file->mark = 90; // 1.5 s of presentation at 60 frames.
    if (!ok)
        fprintf(stderr, "Could not write save %s\n", file->path);
}

// The window loop's automatic saving driven on a virtual clock: 2 ticks per frame, no window
// and no wall time. The staged session drifts, lashes at one frame, pauses at another and
// quits; the writes must be one periodic, one milestone, one on pausing and one on quitting.
static bool autosave_check(Experiment *e, SaveFile *file)
{
    enum { FRAMES = 2100, LASH = 1900, PAUSE = 2000 };
    FrameWatch frame = { .last = watch(e), .was_focused = true };
    file->autosaving = true;
    unsigned count_at[FRAMES] = { 0 };
    bool paused = false;
    for (unsigned f = 0; f < FRAMES; ++f) {
        paused = f == PAUSE || f == PAUSE + 1;
        if (!paused) {
            if (f == LASH)
                lash_action(e);
            // A gentle circle at a steady throttle keeps the boat's pose changing.
            BoatInput input = { .throttle_rate = f < 8 ? .4f : 0, .steering_rate = .3f };
            tick(e, 0, 0, 0, input);
            tick(e, 0, 0, 0, input);
        }
        autosave_frame(e, file, &frame, paused, true);
        count_at[f] = file->writes;
    }
    RESUME_CHECK(count_at[LASH - 1] == 1); // The period (3600 ticks of drift).
    RESUME_CHECK(count_at[LASH] == 2 && file->mark > 0); // The lash milestone.
    RESUME_CHECK(count_at[PAUSE - 1] == 2 && count_at[PAUSE] == 3); // Pausing.
    RESUME_CHECK(count_at[PAUSE + 1] == 3 && count_at[FRAMES - 1] == 3);
    RESUME_CHECK(boat_autosave_due(&file->autosave, e->tick, true)); // Dirty again: quitting.
    RESUME_CHECK(save_write(e, file));
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
static bool scene_check(Experiment *e, BoatPluginRevision *revision)
{
    if (e->world.scene != &revision->free.scene) {
        fprintf(stderr, "Scene check: the session does not use the package's scene\n");
        return false;
    }
    char *notice = revision->free.scene.work_notice[BOAT_SCENE_NOTICE_BUILT];
    snprintf(notice, sizeof(revision->free.scene.work_notice[BOAT_SCENE_NOTICE_BUILT]),
        "CUSTOM {site} DONE");
    reset(e);
    if (e->world.scene != &revision->free.scene) {
        fprintf(stderr, "Scene check: a reset dropped the package's scene\n");
        return false;
    }
    char text[64], expected[64], name[sizeof(e->sites[0].id)];
    notice_text(e, BOAT_SCENE_NOTICE_BUILT, 0, text, sizeof(text));
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
         autosave = false, scene_check_flag = false;
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
    Experiment e = { 0 };
    SetTraceLogLevel(LOG_WARNING);
    if (!boat_world_resources_init(&e.resources, location))
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
        boat_world_resources_destroy(&e.resources);
        return 1;
    }
    e.world.scene = &revision.free.scene;
    e.revision = revision.free.manifest.objective ? &revision : NULL;
    e.site_count = revision.free.scene.work_count;
    memcpy(e.sites, revision.free.scene.work, sizeof(e.sites));
    reset(&e);
    if (scene_check_flag) {
        bool ok = scene_check(&e, &revision);
        boat_world_resources_destroy(&e.resources);
        return ok ? 0 : 1;
    }
    // Which file, if any. Only plain interactive play uses the game's data directory; every
    // staged, scripted or capture run uses a save only when told which one.
    static SaveFile save;
    bool plain = !screenshot && !shadow_review && !survey && !resume && !demo && !frames
        && !strcmp(scenario, "origin");
    if (e.revision && (save_arg || data_dir || plain)) {
        char directory[1000] = "";
        if (save_arg)
            snprintf(save.path, sizeof(save.path), "%s", save_arg);
        else if ((data_dir && snprintf(directory, sizeof(directory), "%s", data_dir) > 0)
            || boat_file_data_directory(directory, sizeof(directory)))
            snprintf(save.path, sizeof(save.path), "%s/cargo-%s.sav", directory,
                e.revision->free.manifest.id);
        save.enabled = save.path[0] != 0;
        if (save.enabled && directory[0])
            boat_file_make_directory(directory);
    }
    if (expect && !save.enabled) {
        fprintf(stderr, "--expect needs a save file (--save or --data-dir).\n");
        boat_world_resources_destroy(&e.resources);
        return 2;
    }
    int loaded = 0;
    if (save.enabled && !fresh && !strcmp(scenario, "origin") && !shadow_review && !survey
        && !resume && !autosave)
        loaded = save_load(&e, &save);
    if (expect) {
        bool ok = (!strcmp(expect, "resumed") && loaded == 1)
            || (!strcmp(expect, "rejected") && loaded == 2);
        if (!ok) {
            fprintf(
                stderr, "--expect %s: the save did not match (load result %d)\n", expect, loaded);
            boat_world_resources_destroy(&e.resources);
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
            clear = boat_cargo_site_clear(
                &e.resources.map, e.world.scene, e.world.water, &e.sites[i]);
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
            e.boat.position = boat_cargo_berth(active(&e));
            e.boat.heading = active(&e)->heading;
            View3D view = camera(&e, &harbour.scenery);
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
        boat_world_resources_destroy(&e.resources);
        return clear ? 0 : 1;
    }
    if (!stage(&e, scenario)) {
        fprintf(stderr, "Cargo review scenario failed: %s\n", scenario);
        boat_world_resources_destroy(&e.resources);
        return 1;
    }
    if (autosave) {
        bool ok = save.enabled && autosave_check(&e, &save);
        waterfront_scenery_destroy(&harbour.scenery);
        boat_scenery_unload();
        boat_world_resources_destroy(&e.resources);
        return ok ? 0 : 1;
    }
    if (save_now) {
        bool ok = save.enabled && save_write(&e, &save);
        printf("%s %s at tick %llu\n", ok ? "Wrote save" : "Could not write save", save.path,
            (unsigned long long)e.tick);
        waterfront_scenery_destroy(&harbour.scenery);
        boat_scenery_unload();
        boat_world_resources_destroy(&e.resources);
        return ok ? 0 : 1;
    }
    if (resume) {
        bool ok = resume_check(&e, scenario);
        waterfront_scenery_destroy(&harbour.scenery);
        boat_scenery_unload();
        boat_world_resources_destroy(&e.resources);
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
        if (gpu && !gpu_init(&mesh))
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
    FrameWatch frame_watch = { .last = watch(&e), .was_focused = true };
    int review_missing = 0, review_min = WIDTH * HEIGHT;
    while (headless || !WindowShouldClose()) {
        if (shadow_review) {
            int sample = frame % 32, surface = frame / 32;
            if (!sample)
                reset(&e);
            for (int i = 0; i < 30; ++i)
                tick(&e, 0, 0, 0, (BoatInput) { 0 });
            e.sandbox.cargo.state = BOAT_CARGO_HANGING;
            e.sandbox.cargo.deck_contact = false;
            Vector3 p = shadow_review_positions[surface];
            e.sandbox.cargo.position = boat_cargo_site_point(active(&e), p);
            e.sandbox.cargo.rotation = (Vector3) { 0 };
        } else if (!headless && !screenshot) {
            if (IsKeyPressed(KEY_R)) {
                reset(&e);
                boat_autosave_note(&save.autosave, e.tick, true, true); // A new session.
            }
            if (IsKeyPressed(KEY_P))
                e.paused = !e.paused;
            if (!e.paused) {
                if (IsKeyPressed(KEY_F))
                    hook_action(&e);
                if (IsKeyPressed(KEY_L))
                    lash_action(&e);
                if (IsKeyPressed(KEY_G))
                    moor_action(&e);
                if (IsKeyPressed(KEY_TAB))
                    view_action(&e);
                if (e.crane_view && e.sandbox.moored)
                    for (int i = 1; i <= 3; ++i)
                        if (IsKeyPressed(KEY_ONE + i - 1))
                            e.aim = i;
                if (IsKeyDown(KEY_SPACE))
                    e.boat.throttle = 0;
                if (IsKeyPressed(KEY_N) && !e.sandbox.moored && !e.sandbox.mooring)
                    e.target = next_site(&e, e.target);
                if (IsKeyPressed(KEY_C))
                    recover_action(&e);
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
                tick(&e, slew, reach, hoist, input);
                accumulator -= BOAT_TICK_SECONDS;
            }
            if (save.autosaving)
                autosave_frame(&e, &save, &frame_watch, e.paused, IsWindowFocused());
        }
        size_t receiver_count = scene(&e, &solids);
        Mesh3D receivers = solids.mesh;
        receivers.triangle_count = receiver_count;
        boat_model_begin(&body_decal);
        if (!boat_cargo_decal(&body_decal, &receivers, e.sandbox.cargo.position, NULL, 0,
                (Vector2) { 0 }, shadow_review ? (Color) { 255, 0, 0, 255 } : (Color) { 0 })) {
            result = 1;
            break;
        }
        if (!location->mapped)
            water_mesh(&e, &water);
        View3D view = camera(&e, &harbour.scenery);
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
                gpu_draw(&mesh, &water, false);
            gpu_draw(&mesh, &solids, true);
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
            gpu_draw(&mesh, &water_decal, false);
            gpu_draw(&mesh, &body_decal, true);
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
    if (save.autosaving && boat_autosave_due(&save.autosave, e.tick, true)) {
        bool ok = save_write(&e, &save);
        boat_autosave_written(&save.autosave, e.tick, ok);
        printf("%s %s at tick %llu\n", ok ? "Saved" : "Could not save", save.path,
            (unsigned long long)e.tick);
    } else if (save.autosaving)
        printf("Save %s is current at tick %llu\n", save.path, (unsigned long long)e.tick);
    if (shadow_review) {
        printf("shadow review: %s, %d/256 missing, minimum %d visible pixels\n",
            gpu ? "GPU" : "CPU", review_missing, review_min);
        result = review_missing ? 1 : 0;
    }
    renderer3d_destroy(&cpu);
    waterfront_scenery_destroy(&harbour.scenery);
    waterfront_harbor_models_destroy(&harbour.harbor);
    boat_water_mesh_cache_destroy(&harbour.water);
    boat_world_resources_destroy(&e.resources);
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
