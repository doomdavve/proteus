#include "boat_sandbox_save.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// SV1b: byte format, header-only discovery, bounds and verdicts. No files, no wall time.
#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            fprintf(stderr, "sandbox save check failed at %d: %s\n", __LINE__, #condition);        \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

static uint64_t fnv(const unsigned char *bytes, size_t size)
{
    uint64_t hash = UINT64_C(14695981039346656037);
    for (size_t i = 0; i < size; ++i)
        hash = (hash ^ bytes[i]) * UINT64_C(1099511628211);
    return hash;
}

static void refresh_checksum(unsigned char *bytes, size_t size)
{
    uint64_t sum = fnv(bytes, size - 8);
    for (unsigned i = 0; i < 8; ++i)
        bytes[size - 8 + i] = (unsigned char)(sum >> (i * 8));
}

static const unsigned char STATE[]
    = { 'r', 2, 0, 1, 'a', 'i', 5, 0, 0, 0, 0, 0, 0, 0, 1, 'b', 't' };

static void sample(BoatSandboxSave *save)
{
    memset(save, 0, sizeof(*save));
    BoatSandboxSummary *s = &save->summary;
    snprintf(s->identity.package, sizeof(s->identity.package), "harbour-job");
    snprintf(s->identity.build, sizeof(s->identity.build), "test-build");
    s->identity.work_digest = 0x1122334455667788ull;
    s->identity.source_hash = 99;
    s->identity.state_version = 2;
    s->identity.physics_revision = 9;
    s->identity.tick_hz = 120;
    snprintf(s->name, sizeof(s->name), "Working Harbour");
    snprintf(s->location, sizeof(s->location), "outer");
    s->saved_at = 1793000000;
    s->tick = 24012;
    s->load = 2;
    s->built = 2;
    s->sites = 3;
    s->has_progress = true;
    snprintf(s->progress_label, sizeof(s->progress_label), "JETTY SUPPLIED");
    s->progress_value = 1;
    snprintf(save->site_ids[0], BOAT_PLUGIN_TEXT, "quay");
    snprintf(save->site_ids[1], BOAT_PLUGIN_TEXT, "jetty");
    snprintf(save->site_ids[2], BOAT_PLUGIN_TEXT, "landing");
    BoatCargoSnapshot *c = &save->cargo;
    c->crane_count = 3;
    c->active = 1;
    c->built[0] = c->built[1] = true;
    c->load_count = 1;
    c->loads[0] = (BoatCargoLoadSnapshot) { .load = 2,
        .source = 0,
        .delivered = false,
        .receivers = 2,
        .support = { .kind = BOAT_CARGO_SUPPORT_DECK, .deck_local = { 0.5f, 1.25f, -0.75f } } };
    c->moored = false;
    c->position = (Vector2) { 12.5f, -300.25f };
    c->heading = 1.5f;
    save->guide = 2;
    save->target = 3; // None.
    save->has_objective = true;
    BoatPluginObjective *o = &save->objective;
    o->state.size = sizeof(STATE);
    memcpy(o->state.bytes, STATE, sizeof(STATE));
    o->hud_count = 2;
    snprintf(o->hud[0].id, BOAT_PLUGIN_TEXT, "supplied");
    snprintf(o->hud[0].label, BOAT_PLUGIN_TEXT, "JETTY SUPPLIED");
    o->hud[0].value = 1;
    snprintf(o->hud[1].id, BOAT_PLUGIN_TEXT, "built");
    snprintf(o->hud[1].label, BOAT_PLUGIN_TEXT, "SITES BUILT");
    o->hud[1].value = -4;
    o->timers_enabled = true;
    o->work_enabled = true;
    o->timer_count = 2;
    snprintf(o->timers[0].id, BOAT_PLUGIN_TEXT, "alpha");
    o->timers[0].tick = 24120;
    snprintf(o->timers[1].id, BOAT_PLUGIN_TEXT, "beta");
    o->timers[1].tick = 24120;
    o->rng = 0xdeadbeefu;
    o->acknowledged = 7;
    o->tick = 24000;
    o->state_version = 2;
    o->generation = 3;
    o->initialized = true;
    save->event_count = 3;
    for (unsigned i = 0; i < 3; ++i) {
        save->events[i] = (BoatPluginEvent) { .tick = 24001 + i, .sequence = 8 + i, .load = 2 };
        snprintf(save->events[i].kind, sizeof(save->events[i].kind), "cargo.delivered");
        snprintf(save->events[i].id, sizeof(save->events[i].id), "landing");
    }
    save->generation = 3;
    save->sequence = 10;
}

static size_t encode(const BoatSandboxSave *save, unsigned char *out)
{
    size_t size = 0;
    CHECK(boat_sandbox_save_encode(save, out, BOAT_SANDBOX_SAVE_MAX, &size));
    CHECK(size <= BOAT_SANDBOX_SAVE_MAX);
    return size;
}

static void check_round_trip(void)
{
    static BoatSandboxSave save, back;
    static unsigned char a[BOAT_SANDBOX_SAVE_MAX], b[BOAT_SANDBOX_SAVE_MAX];
    sample(&save);
    size_t size = encode(&save, a);
    CHECK(boat_sandbox_save_decode(a, size, &back) == BOAT_SANDBOX_SAVE_OK);
    CHECK(!strcmp(back.site_ids[1], "jetty") && back.guide == 2 && back.target == 3);
    CHECK(back.cargo.active == 1 && back.cargo.loads[0].support.deck_local.y == 1.25f);
    CHECK(back.objective.timer_count == 2 && back.objective.hud[1].value == -4);
    CHECK(back.event_count == 3 && back.events[2].sequence == 10 && back.sequence == 10);
    CHECK(boat_plugin_objective_digest(&back.objective)
        == boat_plugin_objective_digest(&save.objective));
    CHECK(back.objective.command_count == 0 && back.objective.work_enabled);
    size_t size_b = encode(&back, b);
    CHECK(size == size_b && !memcmp(a, b, size));

    // A save with no objective and no events, and one built pad support, also round-trips.
    sample(&save);
    save.has_objective = false;
    save.event_count = 0;
    save.cargo.loads[0].support = (BoatCargoSupport) { .kind = BOAT_CARGO_SUPPORT_PAD, .pad = 2 };
    size = encode(&save, a);
    CHECK(boat_sandbox_save_decode(a, size, &back) == BOAT_SANDBOX_SAVE_OK);
    CHECK(back.cargo.loads[0].support.pad == 2 && !back.has_objective);
    CHECK(encode(&back, b) == size && !memcmp(a, b, size));
}

static void check_header(void)
{
    static BoatSandboxSave save;
    static unsigned char bytes[BOAT_SANDBOX_SAVE_MAX];
    sample(&save);
    size_t size = encode(&save, bytes);
    size_t header = bytes[6] | (size_t)bytes[7] << 8;
    CHECK(header > 16 && header <= BOAT_SANDBOX_SAVE_HEADER_MAX && header < size);
    BoatSandboxSummary summary;
    // Discovery reads the first 512 bytes only; the exact header is enough too.
    CHECK(boat_sandbox_save_peek(bytes, BOAT_SANDBOX_SAVE_HEADER_MAX, &summary)
        == BOAT_SANDBOX_SAVE_OK);
    CHECK(summary.tick == 24012 && summary.load == 2 && summary.built == 2 && summary.sites == 3);
    CHECK(!strcmp(summary.progress_label, "JETTY SUPPLIED") && summary.progress_value == 1);
    CHECK(summary.saved_at == 1793000000 && !strcmp(summary.name, "Working Harbour"));
    CHECK(!strcmp(summary.identity.package, "harbour-job") && summary.identity.tick_hz == 120);
    CHECK(boat_sandbox_save_peek(bytes, header, &summary) == BOAT_SANDBOX_SAVE_OK);
    CHECK(boat_sandbox_save_peek(bytes, header - 1, &summary) == BOAT_SANDBOX_SAVE_TRUNCATED);
    // A bad magic and a newer format are told apart; a flipped header byte fails its checksum.
    unsigned char copy[BOAT_SANDBOX_SAVE_HEADER_MAX];
    memcpy(copy, bytes, header);
    copy[0] = 'X';
    CHECK(boat_sandbox_save_peek(copy, header, &summary) == BOAT_SANDBOX_SAVE_MAGIC);
    memcpy(copy, bytes, header);
    copy[4] = BOAT_SANDBOX_SAVE_FORMAT + 1;
    CHECK(boat_sandbox_save_peek(copy, header, &summary) == BOAT_SANDBOX_SAVE_NEWER);
    memcpy(copy, bytes, header);
    copy[header - 1] ^= 1;
    CHECK(boat_sandbox_save_peek(copy, header, &summary) == BOAT_SANDBOX_SAVE_CHECKSUM);
}

static void check_corruption(void)
{
    static BoatSandboxSave save, out;
    static unsigned char bytes[BOAT_SANDBOX_SAVE_MAX], copy[BOAT_SANDBOX_SAVE_MAX];
    sample(&save);
    size_t size = encode(&save, bytes);
    size_t header = bytes[6] | (size_t)bytes[7] << 8;
    for (size_t n = 0; n < size; ++n)
        CHECK(boat_sandbox_save_decode(bytes, n, &out) != BOAT_SANDBOX_SAVE_OK);
    CHECK(boat_sandbox_save_decode(bytes, size, &out) == BOAT_SANDBOX_SAVE_OK);
    for (size_t i = 0; i < header; ++i) {
        memcpy(copy, bytes, size);
        copy[i] ^= 0x40;
        CHECK(boat_sandbox_save_decode(copy, size, &out) != BOAT_SANDBOX_SAVE_OK);
    }
    for (size_t i = header; i < size; i += 97) {
        memcpy(copy, bytes, size);
        copy[i] ^= 0x01;
        CHECK(boat_sandbox_save_decode(copy, size, &out) != BOAT_SANDBOX_SAVE_OK);
    }
    // Trailing bytes are refused.
    memcpy(copy, bytes, size);
    copy[size] = 0;
    CHECK(boat_sandbox_save_decode(copy, size + 1, &out) != BOAT_SANDBOX_SAVE_OK);

    // A flipped body that keeps a valid checksum is still checked semantically: rename the
    // active site's reference to an unknown id.
    memcpy(copy, bytes, size);
    unsigned found = 0;
    for (size_t i = header + 4; i + 5 <= size - 8; ++i)
        if (!memcmp(copy + i, "jetty", 5) && ++found == 2) {
            copy[i + 4] = 'z';
            break;
        }
    CHECK(found == 2);
    refresh_checksum(copy, size);
    CHECK(boat_sandbox_save_decode(copy, size, &out) == BOAT_SANDBOX_SAVE_MALFORMED);
}

static void check_bounds(void)
{
    static BoatSandboxSave save;
    static unsigned char bytes[BOAT_SANDBOX_SAVE_MAX];
    size_t size = 0;
    sample(&save);
    save.event_count = BOAT_PLUGIN_EVENTS;
    for (unsigned i = 3; i < BOAT_PLUGIN_EVENTS; ++i)
        save.events[i] = save.events[2], save.events[i].sequence = 8 + i;
    CHECK(boat_sandbox_save_encode(&save, bytes, BOAT_SANDBOX_SAVE_MAX, &size));
    CHECK(size <= BOAT_SANDBOX_SAVE_MAX);
    static BoatSandboxSave back;
    CHECK(boat_sandbox_save_decode(bytes, size, &back) == BOAT_SANDBOX_SAVE_OK);
    CHECK(back.event_count == BOAT_PLUGIN_EVENTS);
    CHECK(!boat_sandbox_save_encode(&save, bytes, size - 1, &size)); // Capacity.

    sample(&save);
    save.event_count = BOAT_PLUGIN_EVENTS + 1;
    CHECK(!boat_sandbox_save_encode(&save, bytes, sizeof(bytes), &size));
    sample(&save);
    save.objective.hud_count = BOAT_PLUGIN_HUD + 1;
    CHECK(!boat_sandbox_save_encode(&save, bytes, sizeof(bytes), &size));
    sample(&save);
    save.objective.timer_count = BOAT_PLUGIN_TIMERS + 1;
    CHECK(!boat_sandbox_save_encode(&save, bytes, sizeof(bytes), &size));
    sample(&save);
    save.objective.state.size = BOAT_PLUGIN_STATE_BYTES + 1;
    CHECK(!boat_sandbox_save_encode(&save, bytes, sizeof(bytes), &size));
    // The largest legal state still fits the format: a root array of 'i' tags is 3 + 9 * 256
    // bytes per table, so use a deep chain of short strings up to the 16 KiB limit.
    sample(&save);
    {
        unsigned n = 0;
        unsigned char *s = save.objective.state.bytes;
        s[n++] = 'a';
        s[n++] = 255;
        s[n++] = 0;
        for (unsigned i = 0; i < 255; ++i) {
            s[n++] = 's';
            s[n++] = 60;
            s[n++] = 0;
            memset(s + n, 'x', 60);
            n += 60;
        }
        CHECK(n < BOAT_PLUGIN_STATE_BYTES);
        save.objective.state.size = n;
    }
    CHECK(boat_sandbox_save_encode(&save, bytes, sizeof(bytes), &size));
    CHECK(boat_sandbox_save_decode(bytes, size, &back) == BOAT_SANDBOX_SAVE_OK);
    // Unknown or out-of-range site references and non-canonical objective state are refused.
    sample(&save);
    save.cargo.active = 3;
    CHECK(!boat_sandbox_save_encode(&save, bytes, sizeof(bytes), &size));
    sample(&save);
    save.cargo.loads[0].source = 4;
    CHECK(!boat_sandbox_save_encode(&save, bytes, sizeof(bytes), &size));
    sample(&save);
    save.cargo.position.x = NAN;
    CHECK(!boat_sandbox_save_encode(&save, bytes, sizeof(bytes), &size));
    sample(&save);
    save.cargo.built[2] = true; // Disagrees with the summary's built count.
    CHECK(!boat_sandbox_save_encode(&save, bytes, sizeof(bytes), &size));
    sample(&save);
    memcpy(save.objective.state.bytes,
        (const unsigned char[]) { 'r', 2, 0, 1, 'b', 't', 1, 'a', 'f' }, 9);
    save.objective.state.size = 9; // Keys out of order: not canonical.
    CHECK(!boat_sandbox_save_encode(&save, bytes, sizeof(bytes), &size));
}

static void check_verdicts(void)
{
    BoatSandboxSave save;
    sample(&save);
    BoatSandboxIdentity saved = save.summary.identity, now = saved;
    char reason[48];
    CHECK(boat_sandbox_save_check(&saved, &now, false, reason, sizeof(reason))
        == BOAT_SANDBOX_RESUME);
    now.source_hash = 1; // Behaviour or HUD text edits keep the save.
    snprintf(now.build, sizeof(now.build), "another-build");
    CHECK(boat_sandbox_save_check(&saved, &now, false, reason, sizeof(reason))
        == BOAT_SANDBOX_RESUME);
    now = saved;
    now.work_digest ^= 1;
    CHECK(boat_sandbox_save_check(&saved, &now, true, reason, sizeof(reason)) == BOAT_SANDBOX_REJECT
        && !strcmp(reason, "PACKAGE CHANGED"));
    now = saved;
    now.physics_revision++;
    CHECK(
        boat_sandbox_save_check(&saved, &now, false, reason, sizeof(reason)) == BOAT_SANDBOX_REJECT
        && !strcmp(reason, "PHYSICS CHANGED"));
    now = saved;
    now.tick_hz = 60;
    CHECK(boat_sandbox_save_check(&saved, &now, false, reason, sizeof(reason))
        == BOAT_SANDBOX_REJECT);
    now = saved;
    snprintf(now.package, sizeof(now.package), "other");
    CHECK(boat_sandbox_save_check(&saved, &now, false, reason, sizeof(reason))
        == BOAT_SANDBOX_REJECT);
    now = saved;
    now.state_version++;
    CHECK(
        boat_sandbox_save_check(&saved, &now, false, reason, sizeof(reason)) == BOAT_SANDBOX_REJECT
        && !strcmp(reason, "PROGRESS FORMAT CHANGED"));
    CHECK(
        boat_sandbox_save_check(&saved, &now, true, reason, sizeof(reason)) == BOAT_SANDBOX_MIGRATE
        && !reason[0]);
    CHECK(boat_sandbox_source_hash("a", 1) != boat_sandbox_source_hash("b", 1));
}

static void check_work_digest(void)
{
    BoatSceneDefinition scene = { 0 };
    scene.work_present = true;
    scene.work_count = 2;
    snprintf(scene.work[0].id, BOAT_SCENE_TEXT, "quay");
    scene.work[0].preset = BOAT_SCENE_WORK_SERVICE_PLATFORM;
    scene.work[0].x = 10;
    scene.work[0].z = 20;
    snprintf(scene.work[1].id, BOAT_SCENE_TEXT, "jetty");
    scene.work[1].preset = BOAT_SCENE_WORK_RECEIVING_PLATFORM;
    scene.work[1].x = 40;
    uint64_t base = boat_scene_work_digest(&scene);
    CHECK(base && base != boat_scene_work_digest(NULL));
    BoatSceneDefinition edit = scene;
    snprintf(edit.work_notice[BOAT_SCENE_NOTICE_DELIVERED], BOAT_SCENE_NOTICE_TEXT, "NICE ONE");
    CHECK(boat_scene_work_digest(&edit) == base); // Display text is outside the digest.
    CHECK(boat_scene_digest(&edit) != boat_scene_digest(&scene)); // But not the full one.
    edit = scene;
    edit.work[1].x += 0.5f;
    CHECK(boat_scene_work_digest(&edit) != base);
    edit = scene;
    edit.work[1].hidden = true;
    CHECK(boat_scene_work_digest(&edit) != base);
    edit = scene;
    edit.work[0].heading = 0.25f;
    CHECK(boat_scene_work_digest(&edit) != base);
    edit = scene;
    snprintf(edit.work[0].id, BOAT_SCENE_TEXT, "pier");
    CHECK(boat_scene_work_digest(&edit) != base);
    edit = scene;
    edit.work_boat = 1;
    CHECK(boat_scene_work_digest(&edit) != base);
}

int main(void)
{
    check_round_trip();
    check_header();
    check_corruption();
    check_bounds();
    check_verdicts();
    check_work_digest();
    puts("sandbox save ok");
    return 0;
}
