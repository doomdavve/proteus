#include "session/boat_sandbox_save.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

// Layout: HEADER (at most 512 bytes) | u32 body length | BODY | u64 checksum of everything before.
// Header: "PRSV", u16 format, u16 header length, u64 FNV-1a of header bytes 16.., then fields.

#define HEADER_FIXED 16
#define FNV_BASIS UINT64_C(14695981039346656037)

static uint64_t fnv(uint64_t hash, const unsigned char *bytes, size_t size)
{
    for (size_t i = 0; i < size; ++i)
        hash = (hash ^ bytes[i]) * UINT64_C(1099511628211);
    return hash;
}

uint64_t boat_sandbox_source_hash(const char *source, size_t size)
{
    return source ? fnv(FNV_BASIS, (const unsigned char *)source, size) : FNV_BASIS;
}

typedef struct {
    unsigned char *bytes;
    size_t capacity, size;
    bool ok;
} Writer;

static void put(Writer *w, const void *bytes, size_t size)
{
    if (!w->ok || size > w->capacity - w->size) {
        w->ok = false;
        return;
    }
    memcpy(w->bytes + w->size, bytes, size);
    w->size += size;
}

static void put_uint(Writer *w, uint64_t value, unsigned count)
{
    unsigned char bytes[8];
    for (unsigned i = 0; i < count; ++i)
        bytes[i] = (unsigned char)(value >> (i * 8));
    put(w, bytes, count);
}

static void put_float(Writer *w, float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    put_uint(w, bits, 4);
}

static void put_double(Writer *w, double value)
{
    uint64_t bits;
    memcpy(&bits, &value, sizeof(bits));
    put_uint(w, bits, 8);
}

bool boat_sandbox_lighting_valid(double hours, double period)
{
    return isfinite(hours) && isfinite(period) && hours >= 0.0
        && hours <= BOAT_SANDBOX_LIGHTING_HOURS_MAX && period >= BOAT_SANDBOX_LIGHTING_PERIOD_MIN
        && period <= BOAT_SANDBOX_LIGHTING_PERIOD_MAX;
}

// Text must be terminated inside its array and printable ASCII, so a decoded copy fits the same
// array.
static bool text_ok(const char *text, size_t capacity)
{
    const char *end = memchr(text, 0, capacity);
    if (!end)
        return false;
    for (const char *p = text; p < end; ++p)
        if ((unsigned char)*p < 32 || (unsigned char)*p > 126)
            return false;
    return true;
}

static void put_text(Writer *w, const char *text, size_t capacity)
{
    if (!text_ok(text, capacity)) {
        w->ok = false;
        return;
    }
    size_t size = strlen(text);
    put_uint(w, size, 1);
    put(w, text, size);
}

typedef struct {
    const unsigned char *bytes;
    size_t size, offset;
    bool ok;
} Reader;

static uint64_t get_uint(Reader *r, unsigned count)
{
    if (!r->ok || count > r->size - r->offset) {
        r->ok = false;
        return 0;
    }
    uint64_t value = 0;
    for (unsigned i = 0; i < count; ++i)
        value |= (uint64_t)r->bytes[r->offset++] << (i * 8);
    return value;
}

static float get_float(Reader *r)
{
    uint32_t bits = (uint32_t)get_uint(r, 4);
    float value;
    memcpy(&value, &bits, sizeof(value));
    if (!isfinite(value))
        r->ok = false;
    return value;
}

static double get_double(Reader *r)
{
    uint64_t bits = get_uint(r, 8);
    double value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static void get_text(Reader *r, char *out, size_t capacity)
{
    size_t size = (size_t)get_uint(r, 1);
    if (!r->ok || size >= capacity || size > r->size - r->offset) {
        r->ok = false;
        return;
    }
    memcpy(out, r->bytes + r->offset, size);
    out[size] = 0;
    r->offset += size;
    if (!text_ok(out, capacity))
        r->ok = false;
}

static void write_summary(Writer *w, const BoatSandboxSummary *s)
{
    const BoatSandboxIdentity *id = &s->identity;
    put_text(w, id->package, sizeof(id->package));
    put_text(w, id->build, sizeof(id->build));
    put_uint(w, id->work_digest, 8);
    put_uint(w, id->source_hash, 8);
    put_uint(w, id->state_version, 4);
    put_uint(w, id->physics_revision, 4);
    put_uint(w, id->tick_hz, 4);
    put_text(w, s->name, sizeof(s->name));
    put_text(w, s->location, sizeof(s->location));
    put_uint(w, (uint64_t)s->saved_at, 8);
    put_uint(w, s->tick, 8);
    put_uint(w, s->load, 4);
    put_uint(w, s->built, 4);
    put_uint(w, s->sites, 4);
    put_uint(w, s->has_progress, 1);
    put_text(w, s->progress_label, sizeof(s->progress_label));
    put_uint(w, (uint64_t)s->progress_value, 8);
}

static void read_summary(Reader *r, BoatSandboxSummary *s)
{
    memset(s, 0, sizeof(*s));
    BoatSandboxIdentity *id = &s->identity;
    get_text(r, id->package, sizeof(id->package));
    get_text(r, id->build, sizeof(id->build));
    id->work_digest = get_uint(r, 8);
    id->source_hash = get_uint(r, 8);
    id->state_version = (unsigned)get_uint(r, 4);
    id->physics_revision = (unsigned)get_uint(r, 4);
    id->tick_hz = (unsigned)get_uint(r, 4);
    get_text(r, s->name, sizeof(s->name));
    get_text(r, s->location, sizeof(s->location));
    s->saved_at = (int64_t)get_uint(r, 8);
    s->tick = get_uint(r, 8);
    s->load = (unsigned)get_uint(r, 4);
    s->built = (unsigned)get_uint(r, 4);
    s->sites = (unsigned)get_uint(r, 4);
    unsigned flag = (unsigned)get_uint(r, 1);
    if (flag > 1)
        r->ok = false;
    s->has_progress = flag;
    get_text(r, s->progress_label, sizeof(s->progress_label));
    s->progress_value = (int64_t)get_uint(r, 8);
    if (s->sites > BOAT_SCENE_WORK_SITES || s->built > s->sites)
        r->ok = false;
}

// A site is written as its id and read back as an index into the record's own site list, so a
// reordered package never silently moves a load. The index SITES (none) is the empty id.
static void put_site(Writer *w, const BoatSandboxSave *save, unsigned index)
{
    if (index >= save->summary.sites) {
        if (index != save->summary.sites)
            w->ok = false;
        put_uint(w, 0, 1);
        return;
    }
    put_text(w, save->site_ids[index], sizeof(save->site_ids[index]));
}

static unsigned get_site(Reader *r, const BoatSandboxSave *save, bool allow_none)
{
    char id[BOAT_PLUGIN_TEXT];
    get_text(r, id, sizeof(id));
    if (!r->ok)
        return 0;
    if (!id[0]) {
        if (!allow_none)
            r->ok = false;
        return save->summary.sites;
    }
    for (unsigned i = 0; i < save->summary.sites; ++i)
        if (!strcmp(id, save->site_ids[i]))
            return i;
    r->ok = false; // Unknown site id.
    return 0;
}

static bool valid_snapshot(const BoatSandboxSave *save)
{
    const BoatCargoSnapshot *c = &save->cargo;
    unsigned sites = save->summary.sites;
    if (sites > BOAT_SCENE_WORK_SITES || c->crane_count != sites || c->active >= (sites ? sites : 1)
        || c->load_count > BOAT_CARGO_SNAPSHOT_LOADS || save->guide > sites || save->target > sites
        || save->event_count > BOAT_PLUGIN_EVENTS || !isfinite(c->position.x)
        || !isfinite(c->position.y) || !isfinite(c->heading))
        return false;
    unsigned built = 0;
    for (unsigned i = 0; i < sites; ++i)
        built += c->built[i];
    if (built != save->summary.built)
        return false;
    for (unsigned i = 0; i < sites; ++i) {
        if (!save->site_ids[i][0] || !text_ok(save->site_ids[i], sizeof(save->site_ids[i])))
            return false;
        for (unsigned j = 0; j < i; ++j)
            if (!strcmp(save->site_ids[i], save->site_ids[j]))
                return false;
    }
    for (unsigned i = 0; i < c->load_count; ++i) {
        const BoatCargoLoadSnapshot *l = &c->loads[i];
        const Vector3 *d = &l->support.deck_local;
        if (!l->load || l->source >= sites || l->receivers >> sites
            || (unsigned)l->support.kind > BOAT_CARGO_SUPPORT_DECK
            || (l->support.kind == BOAT_CARGO_SUPPORT_PAD && l->support.pad >= sites)
            || !isfinite(d->x) || !isfinite(d->y) || !isfinite(d->z))
            return false;
    }
    for (unsigned i = 0; i < save->event_count; ++i) {
        const BoatPluginEvent *e = &save->events[i];
        if (!text_ok(e->kind, sizeof(e->kind)) || !text_ok(e->id, sizeof(e->id)) || e->load < 0)
            return false;
    }
    if (!boat_sandbox_lighting_valid(save->lighting_hours, save->lighting_cycle_seconds))
        return false;
    return true;
}

bool boat_sandbox_save_encode(
    const BoatSandboxSave *save, unsigned char *out, size_t capacity, size_t *size)
{
    if (!save || !out || !size || !valid_snapshot(save))
        return false;
    Writer w
        = { out, capacity < BOAT_SANDBOX_SAVE_MAX ? capacity : BOAT_SANDBOX_SAVE_MAX, 0, true };
    put(&w, "PRSV", 4);
    put_uint(&w, BOAT_SANDBOX_SAVE_FORMAT, 2);
    put_uint(&w, 0, 2); // Header length, patched below.
    put_uint(&w, 0, 8); // Header checksum, patched below.
    write_summary(&w, &save->summary);
    if (!w.ok || w.size > BOAT_SANDBOX_SAVE_HEADER_MAX)
        return false;
    size_t header = w.size;
    out[6] = (unsigned char)header;
    out[7] = (unsigned char)(header >> 8);
    uint64_t sum = fnv(FNV_BASIS, out + HEADER_FIXED, header - HEADER_FIXED);
    for (unsigned i = 0; i < 8; ++i)
        out[8 + i] = (unsigned char)(sum >> (i * 8));

    size_t length_at = w.size;
    put_uint(&w, 0, 4);
    size_t body = w.size;
    const BoatCargoSnapshot *c = &save->cargo;
    unsigned sites = save->summary.sites;
    put_uint(&w, sites, 1);
    for (unsigned i = 0; i < sites; ++i)
        put_text(&w, save->site_ids[i], sizeof(save->site_ids[i]));
    unsigned built = 0;
    for (unsigned i = 0; i < sites; ++i)
        built |= (unsigned)c->built[i] << i;
    put_uint(&w, built, 1);
    put_site(&w, save, c->active);
    put_site(&w, save, save->guide);
    put_site(&w, save, save->target);
    put_uint(&w, c->load_count, 1);
    for (unsigned i = 0; i < c->load_count; ++i) {
        const BoatCargoLoadSnapshot *l = &c->loads[i];
        put_uint(&w, l->load, 4);
        put_site(&w, save, l->source);
        put_uint(&w, l->delivered, 1);
        put_uint(&w, l->receivers, 1);
        put_uint(&w, (unsigned)l->support.kind, 1);
        if (l->support.kind == BOAT_CARGO_SUPPORT_PAD)
            put_site(&w, save, l->support.pad);
        else {
            put_float(&w, l->support.deck_local.x);
            put_float(&w, l->support.deck_local.y);
            put_float(&w, l->support.deck_local.z);
        }
    }
    put_uint(&w, c->moored, 1);
    put_float(&w, c->position.x);
    put_float(&w, c->position.y);
    put_float(&w, c->heading);
    put_uint(&w, save->has_objective, 1);
    if (save->has_objective) {
        unsigned char blob[20000];
        size_t blob_size = 0;
        if (!boat_plugin_objective_encode(&save->objective, blob, sizeof(blob), &blob_size))
            return false;
        put_uint(&w, blob_size, 4);
        put(&w, blob, blob_size);
    }
    put_uint(&w, save->event_count, 1);
    for (unsigned i = 0; i < save->event_count; ++i) {
        const BoatPluginEvent *e = &save->events[i];
        put_uint(&w, (uint64_t)e->tick, 8);
        put_uint(&w, (uint64_t)e->sequence, 8);
        put_text(&w, e->kind, sizeof(e->kind));
        put_text(&w, e->id, sizeof(e->id));
        put_uint(&w, (uint64_t)e->load, 8);
    }
    put_uint(&w, save->generation, 4);
    put_uint(&w, (uint64_t)save->sequence, 8);
    // Format 2 requires the presentation clock as the final 16 bytes of the body.
    put_double(&w, save->lighting_hours);
    put_double(&w, save->lighting_cycle_seconds);
    if (!w.ok)
        return false;
    size_t body_size = w.size - body;
    for (unsigned i = 0; i < 4; ++i)
        out[length_at + i] = (unsigned char)(body_size >> (i * 8));
    put_uint(&w, fnv(FNV_BASIS, out, w.size), 8);
    if (!w.ok)
        return false;
    *size = w.size;
    return true;
}

BoatSandboxSaveStatus boat_sandbox_save_peek(
    const unsigned char *bytes, size_t size, BoatSandboxSummary *out)
{
    if (!bytes || !out)
        return BOAT_SANDBOX_SAVE_MALFORMED;
    if (size < 4)
        return memcmp(bytes, "PRSV", size) ? BOAT_SANDBOX_SAVE_MAGIC : BOAT_SANDBOX_SAVE_TRUNCATED;
    if (memcmp(bytes, "PRSV", 4))
        return BOAT_SANDBOX_SAVE_MAGIC;
    if (size < HEADER_FIXED)
        return BOAT_SANDBOX_SAVE_TRUNCATED;
    unsigned format = bytes[4] | (unsigned)bytes[5] << 8;
    size_t header = bytes[6] | (size_t)bytes[7] << 8;
    if (!format)
        return BOAT_SANDBOX_SAVE_MALFORMED;
    if (format > BOAT_SANDBOX_SAVE_FORMAT)
        return BOAT_SANDBOX_SAVE_NEWER;
    if (header < HEADER_FIXED || header > BOAT_SANDBOX_SAVE_HEADER_MAX)
        return BOAT_SANDBOX_SAVE_MALFORMED;
    if (size < header)
        return BOAT_SANDBOX_SAVE_TRUNCATED;
    uint64_t stored = 0;
    for (unsigned i = 0; i < 8; ++i)
        stored |= (uint64_t)bytes[8 + i] << (i * 8);
    if (stored != fnv(FNV_BASIS, bytes + HEADER_FIXED, header - HEADER_FIXED))
        return BOAT_SANDBOX_SAVE_CHECKSUM;
    Reader r = { bytes, header, HEADER_FIXED, true };
    BoatSandboxSummary summary;
    read_summary(&r, &summary);
    if (!r.ok || r.offset != header)
        return BOAT_SANDBOX_SAVE_MALFORMED;
    *out = summary;
    return BOAT_SANDBOX_SAVE_OK;
}

BoatSandboxSaveStatus boat_sandbox_save_decode(
    const unsigned char *bytes, size_t size, BoatSandboxSave *out)
{
    if (!out)
        return BOAT_SANDBOX_SAVE_MALFORMED;
    static BoatSandboxSave scratch; // Not reentrant; keeps the large record off the stack.
    BoatSandboxSave *save = &scratch;
    memset(save, 0, sizeof(*save));
    BoatSandboxSaveStatus status = boat_sandbox_save_peek(bytes,
        size < BOAT_SANDBOX_SAVE_HEADER_MAX ? size : BOAT_SANDBOX_SAVE_HEADER_MAX, &save->summary);
    if (status != BOAT_SANDBOX_SAVE_OK)
        return status;
    if (size > BOAT_SANDBOX_SAVE_MAX)
        return BOAT_SANDBOX_SAVE_MALFORMED;
    size_t header = bytes[6] | (size_t)bytes[7] << 8;
    if (size < header + 4)
        return BOAT_SANDBOX_SAVE_TRUNCATED;
    size_t body = 0;
    for (unsigned i = 0; i < 4; ++i)
        body |= (size_t)bytes[header + i] << (i * 8);
    if (body > BOAT_SANDBOX_SAVE_MAX || size < header + 4 + body + 8)
        return BOAT_SANDBOX_SAVE_TRUNCATED;
    if (size != header + 4 + body + 8)
        return BOAT_SANDBOX_SAVE_MALFORMED;
    uint64_t stored = 0;
    for (unsigned i = 0; i < 8; ++i)
        stored |= (uint64_t)bytes[size - 8 + i] << (i * 8);
    if (stored != fnv(FNV_BASIS, bytes, size - 8))
        return BOAT_SANDBOX_SAVE_CHECKSUM;

    Reader r = { bytes + header + 4, body, 0, true };
    BoatCargoSnapshot *c = &save->cargo;
    unsigned sites = (unsigned)get_uint(&r, 1);
    if (sites != save->summary.sites)
        return BOAT_SANDBOX_SAVE_MALFORMED;
    for (unsigned i = 0; i < sites; ++i)
        get_text(&r, save->site_ids[i], sizeof(save->site_ids[i]));
    c->crane_count = sites;
    unsigned built = (unsigned)get_uint(&r, 1);
    if (built >> sites)
        return BOAT_SANDBOX_SAVE_MALFORMED;
    for (unsigned i = 0; i < sites; ++i)
        c->built[i] = (built >> i) & 1;
    c->active = get_site(&r, save, sites == 0);
    save->guide = get_site(&r, save, true);
    save->target = get_site(&r, save, true);
    c->load_count = (unsigned)get_uint(&r, 1);
    if (c->load_count > BOAT_CARGO_SNAPSHOT_LOADS)
        return BOAT_SANDBOX_SAVE_MALFORMED;
    for (unsigned i = 0; i < c->load_count && r.ok; ++i) {
        BoatCargoLoadSnapshot *l = &c->loads[i];
        l->load = (unsigned)get_uint(&r, 4);
        l->source = get_site(&r, save, false);
        unsigned delivered = (unsigned)get_uint(&r, 1);
        l->delivered = delivered == 1;
        l->receivers = (unsigned char)get_uint(&r, 1);
        unsigned kind = (unsigned)get_uint(&r, 1);
        if (delivered > 1 || kind > BOAT_CARGO_SUPPORT_DECK)
            r.ok = false;
        l->support.kind = (BoatCargoSupportKind)kind;
        if (kind == BOAT_CARGO_SUPPORT_PAD)
            l->support.pad = (unsigned char)get_site(&r, save, false);
        else {
            l->support.deck_local.x = get_float(&r);
            l->support.deck_local.y = get_float(&r);
            l->support.deck_local.z = get_float(&r);
        }
    }
    unsigned moored = (unsigned)get_uint(&r, 1);
    c->moored = moored == 1;
    c->position.x = get_float(&r);
    c->position.y = get_float(&r);
    c->heading = get_float(&r);
    unsigned has_objective = (unsigned)get_uint(&r, 1);
    if (moored > 1 || has_objective > 1)
        r.ok = false;
    save->has_objective = has_objective;
    if (has_objective && r.ok) {
        size_t blob = (size_t)get_uint(&r, 4);
        if (!r.ok || blob > r.size - r.offset
            || !boat_plugin_objective_decode(r.bytes + r.offset, blob, &save->objective))
            return BOAT_SANDBOX_SAVE_MALFORMED;
        r.offset += blob;
    }
    save->event_count = (unsigned)get_uint(&r, 1);
    if (save->event_count > BOAT_PLUGIN_EVENTS)
        return BOAT_SANDBOX_SAVE_MALFORMED;
    for (unsigned i = 0; i < save->event_count && r.ok; ++i) {
        BoatPluginEvent *e = &save->events[i];
        e->tick = (int64_t)get_uint(&r, 8);
        e->sequence = (int64_t)get_uint(&r, 8);
        get_text(&r, e->kind, sizeof(e->kind));
        get_text(&r, e->id, sizeof(e->id));
        e->load = (int64_t)get_uint(&r, 8);
    }
    save->generation = (unsigned)get_uint(&r, 4);
    save->sequence = (int64_t)get_uint(&r, 8);
    save->lighting_hours = BOAT_SANDBOX_LIGHTING_HOURS_DEFAULT;
    save->lighting_cycle_seconds = BOAT_SANDBOX_LIGHTING_PERIOD_DEFAULT;
    unsigned format = bytes[4] | (unsigned)bytes[5] << 8;
    if (format >= 2) {
        save->lighting_hours = get_double(&r);
        save->lighting_cycle_seconds = get_double(&r);
        if (!boat_sandbox_lighting_valid(save->lighting_hours, save->lighting_cycle_seconds))
            r.ok = false;
    }
    if (!r.ok || r.offset != r.size || !valid_snapshot(save))
        return BOAT_SANDBOX_SAVE_MALFORMED;
    *out = *save;
    return BOAT_SANDBOX_SAVE_OK;
}

BoatSandboxVerdict boat_sandbox_save_check(const BoatSandboxIdentity *saved,
    const BoatSandboxIdentity *current, bool current_migrates, char *reason, size_t capacity)
{
    const char *why = "";
    BoatSandboxVerdict verdict = BOAT_SANDBOX_REJECT;
    if (!saved || !current)
        why = "DAMAGED";
    else if (strcmp(saved->package, current->package))
        why = "PACKAGE CHANGED";
    else if (saved->physics_revision != current->physics_revision
        || saved->tick_hz != current->tick_hz)
        why = "PHYSICS CHANGED";
    else if (saved->work_digest != current->work_digest)
        why = "PACKAGE CHANGED";
    else if (saved->state_version != current->state_version) {
        if (current_migrates)
            verdict = BOAT_SANDBOX_MIGRATE;
        else
            why = "PROGRESS FORMAT CHANGED";
    } else
        verdict = BOAT_SANDBOX_RESUME;
    if (reason && capacity)
        snprintf(reason, capacity, "%s", why);
    return verdict;
}

void boat_autosave_note(BoatAutosave *autosave, uint64_t tick, bool changed, bool milestone)
{
    if (!autosave || (!changed && !milestone))
        return;
    if (!autosave->dirty)
        autosave->last_write = tick;
    autosave->dirty = true;
    if (milestone)
        autosave->milestone = true;
}

bool boat_autosave_due(const BoatAutosave *autosave, uint64_t tick, bool leaving)
{
    if (!autosave || !autosave->dirty)
        return false;
    if (leaving || autosave->milestone)
        return true;
    return tick >= autosave->last_write && tick - autosave->last_write >= BOAT_AUTOSAVE_PERIOD;
}

void boat_autosave_written(BoatAutosave *autosave, uint64_t tick, bool ok)
{
    if (!autosave)
        return;
    autosave->dirty = !ok;
    autosave->milestone = false;
    autosave->last_write = tick;
}
