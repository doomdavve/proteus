#include "plugins/boat_plugin_state.h"
#include "session/boat_sandbox_save.h"

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

static void write_double(unsigned char *at, double value)
{
    uint64_t bits;
    memcpy(&bits, &value, sizeof(bits));
    for (unsigned i = 0; i < 8; ++i)
        at[i] = (unsigned char)(bits >> (i * 8));
}

static double read_double(const unsigned char *at)
{
    uint64_t bits = 0;
    for (unsigned i = 0; i < 8; ++i)
        bits |= (uint64_t)at[i] << (i * 8);
    double value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static size_t body_length(const unsigned char *bytes)
{
    size_t header = bytes[6] | (size_t)bytes[7] << 8;
    size_t body = 0;
    for (unsigned i = 0; i < 4; ++i)
        body |= (size_t)bytes[header + i] << (i * 8);
    return body;
}

static void set_body_length(unsigned char *bytes, size_t body)
{
    size_t header = bytes[6] | (size_t)bytes[7] << 8;
    for (unsigned i = 0; i < 4; ++i)
        bytes[header + i] = (unsigned char)(body >> (i * 8));
}

// Remove COUNT bytes from the end of the body and keep the record internally consistent, so the
// decoder reaches the missing tail rather than the length check. SRC and DST must not overlap.
static size_t strip_body_tail(
    const unsigned char *src, size_t size, unsigned char *dst, size_t count)
{
    size_t out = size - count;
    memcpy(dst, src, size - 8 - count);
    memcpy(dst + size - 8 - count, src + size - 8, 8);
    set_body_length(dst, body_length(src) - count);
    refresh_checksum(dst, out);
    return out;
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
    save->lighting_hours = BOAT_SANDBOX_LIGHTING_HOURS_DEFAULT;
    save->lighting_cycle_seconds = BOAT_SANDBOX_LIGHTING_PERIOD_DEFAULT;
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

static void check_autosave(void)
{
    BoatAutosave a;

    // A clean session never writes, even when leaving.
    memset(&a, 0, sizeof(a));
    CHECK(!boat_autosave_due(&a, 5000, false));
    CHECK(!boat_autosave_due(&a, 5000, true));

    // A milestone writes that frame; a successful write clears it.
    memset(&a, 0, sizeof(a));
    boat_autosave_note(&a, 100, true, true);
    CHECK(boat_autosave_due(&a, 100, false));
    boat_autosave_written(&a, 100, true);
    CHECK(!a.dirty && !a.milestone);
    CHECK(!boat_autosave_due(&a, 100, true));

    // Several milestones in one frame still make one write.
    memset(&a, 0, sizeof(a));
    boat_autosave_note(&a, 200, true, true);
    boat_autosave_note(&a, 200, true, true);
    boat_autosave_note(&a, 200, true, true);
    CHECK(boat_autosave_due(&a, 200, false));
    boat_autosave_written(&a, 200, true);
    CHECK(!boat_autosave_due(&a, 200, false));
    CHECK(!boat_autosave_due(&a, 200, true));

    // A plain change writes once the period has passed since the first change.
    memset(&a, 0, sizeof(a));
    boat_autosave_note(&a, 1000, true, false);
    CHECK(!boat_autosave_due(&a, 1000, false));
    CHECK(!boat_autosave_due(&a, 1000 + BOAT_AUTOSAVE_PERIOD - 1, false));
    CHECK(boat_autosave_due(&a, 1000 + BOAT_AUTOSAVE_PERIOD, false));
    boat_autosave_written(&a, 1000 + BOAT_AUTOSAVE_PERIOD, true);
    CHECK(!boat_autosave_due(&a, 1000 + BOAT_AUTOSAVE_PERIOD, false));

    // An idle gap does not count: the clock starts at the first change after a clean state.
    memset(&a, 0, sizeof(a));
    boat_autosave_note(&a, 0, true, true);
    CHECK(boat_autosave_due(&a, 0, false));
    boat_autosave_written(&a, 0, true);
    boat_autosave_note(&a, 100000, true, false);
    CHECK(!boat_autosave_due(&a, 100000, false));

    // A paused session (tick does not advance) never becomes due until it leaves.
    memset(&a, 0, sizeof(a));
    boat_autosave_note(&a, 1500, true, false);
    CHECK(!boat_autosave_due(&a, 1500, false));
    CHECK(!boat_autosave_due(&a, 1500, false));
    CHECK(boat_autosave_due(&a, 1500, true));

    // Leaving writes only when dirty.
    memset(&a, 0, sizeof(a));
    CHECK(!boat_autosave_due(&a, 1, true));
    boat_autosave_note(&a, 1, true, false);
    CHECK(boat_autosave_due(&a, 1, true));

    // A failed write keeps dirty, clears milestone, and restarts the periodic clock.
    memset(&a, 0, sizeof(a));
    boat_autosave_note(&a, 300, true, true);
    CHECK(boat_autosave_due(&a, 300, false));
    boat_autosave_written(&a, 300, false);
    CHECK(a.dirty && !a.milestone);
    CHECK(!boat_autosave_due(&a, 300, false));
    CHECK(boat_autosave_due(&a, 300, true));
    CHECK(boat_autosave_due(&a, 300 + BOAT_AUTOSAVE_PERIOD, false));
    boat_autosave_note(&a, 400, true, true);
    CHECK(boat_autosave_due(&a, 400, false));
    boat_autosave_written(&a, 400, true);
    CHECK(!a.dirty && !a.milestone);
    CHECK(!boat_autosave_due(&a, 400, false));
}

static void check_lighting_clock(void)
{
    static BoatSandboxSave save, back, out;
    static unsigned char a[BOAT_SANDBOX_SAVE_MAX], b[BOAT_SANDBOX_SAVE_MAX];
    static unsigned char copy[BOAT_SANDBOX_SAVE_MAX];

    // The shared validator accepts the full bound and rejects non-finite or out-of-range clocks.
    CHECK(boat_sandbox_lighting_valid(0.0, 1.0));
    CHECK(boat_sandbox_lighting_valid(12.0, 900.0));
    CHECK(boat_sandbox_lighting_valid(
        BOAT_SANDBOX_LIGHTING_HOURS_MAX, BOAT_SANDBOX_LIGHTING_PERIOD_MAX));
    CHECK(!boat_sandbox_lighting_valid(-0.5, 900.0));
    CHECK(!boat_sandbox_lighting_valid(BOAT_SANDBOX_LIGHTING_HOURS_MAX + 1.0, 900.0));
    CHECK(!boat_sandbox_lighting_valid(NAN, 900.0));
    CHECK(!boat_sandbox_lighting_valid(12.0, NAN));
    CHECK(!boat_sandbox_lighting_valid(12.0, INFINITY));
    CHECK(!boat_sandbox_lighting_valid(12.0, BOAT_SANDBOX_LIGHTING_PERIOD_MIN - 0.5));
    CHECK(!boat_sandbox_lighting_valid(12.0, BOAT_SANDBOX_LIGHTING_PERIOD_MAX + 0.5));

    // A multi-cycle hour survives the required binary64 tail and re-encodes byte for byte.
    sample(&save);
    save.lighting_hours = 63.25;
    save.lighting_cycle_seconds = BOAT_SANDBOX_LIGHTING_PERIOD_DEFAULT;
    size_t size = encode(&save, a);
    CHECK(read_double(a + size - 24) == 63.25);
    CHECK(read_double(a + size - 16) == 900.0);
    CHECK(boat_sandbox_save_decode(a, size, &back) == BOAT_SANDBOX_SAVE_OK);
    CHECK(back.lighting_hours == 63.25 && back.lighting_cycle_seconds == 900.0);
    CHECK(encode(&back, b) == size && !memcmp(a, b, size));

    // Encode refuses an invalid clock (finite and bounds).
    const double bad_hours[] = { -0.5, BOAT_SANDBOX_LIGHTING_HOURS_MAX + 1.0, NAN };
    for (size_t i = 0; i < sizeof(bad_hours) / sizeof(bad_hours[0]); ++i) {
        sample(&save);
        save.lighting_hours = bad_hours[i];
        CHECK(!boat_sandbox_save_encode(&save, b, sizeof(b), &size));
    }
    const double bad_periods[] = { 0.0, BOAT_SANDBOX_LIGHTING_PERIOD_MIN - 0.5,
        BOAT_SANDBOX_LIGHTING_PERIOD_MAX + 0.5, NAN, INFINITY };
    for (size_t i = 0; i < sizeof(bad_periods) / sizeof(bad_periods[0]); ++i) {
        sample(&save);
        save.lighting_cycle_seconds = bad_periods[i];
        CHECK(!boat_sandbox_save_encode(&save, b, sizeof(b), &size));
    }

    // Decode refuses an invalid clock even after a valid checksum and leaves OUT untouched.
    sample(&save);
    size = encode(&save, a);
    memset(&out, 0xA5, sizeof(out));
    out.sequence = 1234;
    for (unsigned field = 0; field < 2; ++field) {
        const double bad[] = { -1.0, 1.0e9 + 1.0, NAN, INFINITY };
        for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
            memcpy(copy, a, size);
            write_double(copy + size - (field ? 16 : 24), bad[i]);
            refresh_checksum(copy, size);
            CHECK(boat_sandbox_save_decode(copy, size, &out) != BOAT_SANDBOX_SAVE_OK);
            CHECK(out.sequence == 1234);
        }
    }
    // A zero or out-of-range rate is invalid even when the hours are fine.
    const double bad_rates[]
        = { 0.0, BOAT_SANDBOX_LIGHTING_PERIOD_MIN - 0.5, BOAT_SANDBOX_LIGHTING_PERIOD_MAX + 0.5 };
    for (size_t i = 0; i < sizeof(bad_rates) / sizeof(bad_rates[0]); ++i) {
        memcpy(copy, a, size);
        write_double(copy + size - 16, bad_rates[i]);
        refresh_checksum(copy, size);
        CHECK(boat_sandbox_save_decode(copy, size, &out) != BOAT_SANDBOX_SAVE_OK);
        CHECK(out.sequence == 1234);
    }
    // A zero clock is not a valid required tail.
    memcpy(copy, a, size);
    write_double(copy + size - 24, 0.0);
    write_double(copy + size - 16, 0.0);
    refresh_checksum(copy, size);
    CHECK(boat_sandbox_save_decode(copy, size, &out) != BOAT_SANDBOX_SAVE_OK);
    CHECK(out.sequence == 1234);

    // The required 16-byte v2 tail may not be missing or truncated.
    size_t missing = strip_body_tail(a, size, copy, 16);
    CHECK(boat_sandbox_save_decode(copy, missing, &out) == BOAT_SANDBOX_SAVE_MALFORMED);
    CHECK(out.sequence == 1234);
    size_t truncated = strip_body_tail(a, size, copy, 15);
    CHECK(boat_sandbox_save_decode(copy, truncated, &out) == BOAT_SANDBOX_SAVE_MALFORMED);
    CHECK(out.sequence == 1234);
}

static void check_v1_record(void)
{
    static BoatSandboxSave save, back;
    static unsigned char v2[BOAT_SANDBOX_SAVE_MAX], v1[BOAT_SANDBOX_SAVE_MAX];
    static unsigned char again[BOAT_SANDBOX_SAVE_MAX];
    sample(&save);
    size_t v2_size = encode(&save, v2);

    // Authentic v1 layout: drop exactly the 16-byte v2 clock tail, rewrite format, body length and
    // checksum. The source and destination are separate buffers.
    size_t v1_size = strip_body_tail(v2, v2_size, v1, 16);
    v1[4] = 1;
    v1[5] = 0;
    set_body_length(v1, body_length(v2) - 16);
    refresh_checksum(v1, v1_size);
    CHECK(!memcmp(v1, "PRSV", 4));
    BoatSandboxSummary summary;
    CHECK(boat_sandbox_save_peek(v1, v1_size, &summary) == BOAT_SANDBOX_SAVE_OK);

    CHECK(boat_sandbox_save_decode(v1, v1_size, &back) == BOAT_SANDBOX_SAVE_OK);
    CHECK(back.lighting_hours == BOAT_SANDBOX_LIGHTING_HOURS_DEFAULT
        && back.lighting_cycle_seconds == BOAT_SANDBOX_LIGHTING_PERIOD_DEFAULT);
    CHECK(back.guide == 2 && back.target == 3 && back.sequence == 10);
    CHECK(!strcmp(back.site_ids[1], "jetty") && back.cargo.active == 1);
    CHECK(back.cargo.loads[0].support.deck_local.y == 1.25f);
    CHECK(back.event_count == 3 && !strcmp(back.events[2].id, "landing"));
    CHECK(boat_plugin_objective_digest(&back.objective)
        == boat_plugin_objective_digest(&save.objective));

    // Re-encoding the v1 record adds the required format-2 clock tail.
    CHECK(encode(&back, again) == v2_size && !memcmp(again, v2, v2_size));
    CHECK(read_double(again + v2_size - 24) == BOAT_SANDBOX_LIGHTING_HOURS_DEFAULT
        && read_double(again + v2_size - 16) == BOAT_SANDBOX_LIGHTING_PERIOD_DEFAULT);
}

int main(void)
{
    check_round_trip();
    check_header();
    check_corruption();
    check_bounds();
    check_lighting_clock();
    check_v1_record();
    check_verdicts();
    check_work_digest();
    check_autosave();
    puts("sandbox save ok");
    return 0;
}
