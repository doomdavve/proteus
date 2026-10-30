#include "boat_plugin_host.h"

#include <string.h>

#define SLOTS (BOAT_PLUGIN_UNDO + 2)

static bool load_revision(
    BoatPluginHost *host, const BoatPluginBundle *bundle, BoatPluginRevision *revision)
{
    BoatPluginDiagnostic *d = &host->diagnostic;
    BoatPluginStatus status = host->racing
        ? boat_plugin_load_race(bundle, NULL, &revision->race, d)
        : boat_plugin_load_voyage(bundle, NULL, &revision->voyage, d);
    if (status != BOAT_PLUGIN_OK)
        return false;
    BoatPluginCost cost = d->cost;
    status = host->racing
        ? boat_plugin_race_check_location(&revision->race, host->location, host->map, d)
        : boat_plugin_check_location(&revision->voyage, host->location, host->map, d);
    d->cost = cost; // Location validation clears diagnostics, but did not undo the Lua work.
    if (status != BOAT_PLUGIN_OK)
        return false;
    if (!host->racing && revision->voyage.manifest.objective) {
        BoatPluginObjective probe = { .generation = 1,
            .rng = 12345,
            .timers_enabled = revision->voyage.manifest.timers,
            .state_version = revision->voyage.manifest.state_version };
        status = boat_plugin_objective_step(&probe, bundle->source, bundle->source_size,
            BOAT_PLUGIN_OBJECTIVE_INIT, 0, NULL, 0, NULL, d);
        d->cost.instructions += cost.instructions;
        d->cost.entry_instructions += cost.entry_instructions;
        d->cost.fields += cost.fields;
        d->cost.leaked_bytes += cost.leaked_bytes;
        if (cost.peak_bytes > d->cost.peak_bytes)
            d->cost.peak_bytes = cost.peak_bytes;
        if (status != BOAT_PLUGIN_OK)
            return false;
        memcpy(revision->source, bundle->source, bundle->source_size);
        revision->source_size = bundle->source_size;
    }
    return true;
}

static void copy_revision(
    BoatPluginHost *host, BoatPluginRevision *out, const BoatPluginRevision *in)
{
    out->source_size = in->source_size;
    memcpy(out->source, in->source, in->source_size);
    if (host->racing)
        out->race = in->race;
    else
        boat_plugin_voyage_copy(&out->voyage, &in->voyage);
}

static BoatPluginChange classify(
    const BoatPluginHost *host, const BoatPluginRevision *a, const BoatPluginRevision *b)
{
    BoatPluginChange change = host->racing ? boat_plugin_race_classify(&a->race, &b->race)
                                           : boat_plugin_classify(&a->voyage, &b->voyage);
    // A gull-cue-only scene edit keeps the session clock even though the source bytes changed.
    bool cosmetic_gulls = !host->racing && change == BOAT_PLUGIN_RENAMED
        && a->voyage.has_scene && b->voyage.has_scene
        && boat_scene_same_except_gull_cues(&a->voyage.scene, &b->voyage.scene);
    if (!host->racing && change < BOAT_PLUGIN_RESTART_VOYAGE && !cosmetic_gulls
        && (a->voyage.manifest.objective != b->voyage.manifest.objective
            || a->voyage.manifest.timers != b->voyage.manifest.timers
            || a->voyage.manifest.state_version != b->voyage.manifest.state_version
            || a->source_size != b->source_size || memcmp(a->source, b->source, a->source_size)))
        change = a->voyage.manifest.objective && b->voyage.manifest.objective && b->voyage.migrates
            ? BOAT_PLUGIN_MIGRATE
            : BOAT_PLUGIN_RESTART_VOYAGE;
    return change;
}

bool boat_plugin_host_start(BoatPluginHost *host, const BoatWorldDefinition *location,
    const BoatMap *map, const BoatPluginBundle *bundle)
{
    // Clear in place: a compound literal duplicates the revision history on the stack.
    memset(host, 0, sizeof(*host));
    host->location = location;
    host->map = map;
    BoatPluginManifest manifest;
    if (boat_plugin_parse_manifest(
            bundle->manifest, bundle->manifest_size, NULL, &manifest, &host->diagnostic)
        != BOAT_PLUGIN_OK)
        return false;
    host->racing = !strcmp(manifest.kind, "race");
    BoatPluginRevision revision = { 0 };
    if (!load_revision(host, bundle, &revision))
        return false;
    copy_revision(host, &host->slots[0], &revision);
    host->slots[0].number = host->numbered = 1;
    host->depth = 1;
    return true;
}

BoatPluginHostMemory boat_plugin_host_memory(const BoatPluginHost *host)
{
    BoatPluginHostMemory memory = { .reserved_bytes = sizeof(*host) };
    for (unsigned slot = 0; slot < SLOTS; ++slot) {
        bool live = host->held && slot == host->held_slot;
        for (unsigned i = 0; i < host->depth; ++i)
            live |= host->history[i] == slot;
        if (live) {
            ++memory.retained_revisions;
            memory.retained_source_bytes += host->slots[slot].source_size;
        }
    }
    return memory;
}

static unsigned active_slot(const BoatPluginHost *host) { return host->history[host->depth - 1]; }

const BoatPluginRevision *boat_plugin_host_active(const BoatPluginHost *host)
{
    return &host->slots[active_slot(host)];
}

const BoatPluginRevision *boat_plugin_host_held(const BoatPluginHost *host)
{
    return host->held ? &host->slots[host->held_slot] : NULL;
}

void boat_plugin_host_discard(BoatPluginHost *host) { host->held = host->undo = false; }

// A slot neither in the history nor holding a staged candidate.
static unsigned free_slot(const BoatPluginHost *host)
{
    for (unsigned slot = 0;; ++slot) {
        bool used = host->held && !host->undo && slot == host->held_slot;
        for (unsigned i = 0; i < host->depth; ++i)
            used |= host->history[i] == slot;
        if (!used)
            return slot; // SLOTS exceeds the history by one, so one is always free.
    }
}

bool boat_plugin_host_stage(BoatPluginHost *host, const BoatPluginBundle *bundle)
{
    BoatPluginRevision candidate = { 0 };
    if (!load_revision(host, bundle, &candidate)) {
        boat_plugin_host_discard(host);
        return false;
    }
    return boat_plugin_host_stage_decoded(
        host, &candidate, boat_plugin_host_active(host)->number, host->epoch);
}

bool boat_plugin_host_stage_decoded(
    BoatPluginHost *host, const BoatPluginRevision *candidate, unsigned base, uint64_t epoch)
{
    if (host->epoch != epoch || boat_plugin_host_active(host)->number != base)
        return false;
    const BoatPluginRevision *active = boat_plugin_host_active(host);
    host->change = classify(host, active, candidate);
    // A candidate identical to the one already held keeps its number.
    const BoatPluginRevision *held = boat_plugin_host_held(host);
    bool repeated = held && !host->undo && classify(host, held, candidate) == BOAT_PLUGIN_UNCHANGED;
    boat_plugin_host_discard(host);
    if (host->change == BOAT_PLUGIN_UNCHANGED || host->change == BOAT_PLUGIN_INCOMPATIBLE)
        return true;
    unsigned slot = free_slot(host);
    BoatPluginRevision *revision = &host->slots[slot];
    unsigned number = repeated ? held->number : ++host->numbered;
    copy_revision(host, revision, candidate);
    revision->number = number;
    host->held = true;
    host->held_slot = slot;
    host->base = active->number;
    return true;
}

bool boat_plugin_host_undo(BoatPluginHost *host)
{
    boat_plugin_host_discard(host);
    if (host->depth < 2)
        return false;
    unsigned slot = host->history[host->depth - 2];
    host->change = classify(host, boat_plugin_host_active(host), &host->slots[slot]);
    host->diagnostic = (BoatPluginDiagnostic) { .status = BOAT_PLUGIN_OK };
    host->held = host->undo = true;
    host->held_slot = slot;
    host->base = boat_plugin_host_active(host)->number;
    return true;
}

const BoatPluginRevision *boat_plugin_host_commit(BoatPluginHost *host)
{
    if (!host->held || host->base != boat_plugin_host_active(host)->number) {
        boat_plugin_host_discard(host);
        return NULL;
    }
    ++host->epoch;
    if (host->undo) {
        --host->depth; // The previous revision is active again.
    } else {
        if (host->depth == BOAT_PLUGIN_UNDO + 1) {
            memmove(host->history, host->history + 1, (host->depth - 1) * sizeof(host->history[0]));
            --host->depth;
        }
        host->history[host->depth++] = host->held_slot;
    }
    boat_plugin_host_discard(host);
    host->diagnostic = (BoatPluginDiagnostic) { .status = BOAT_PLUGIN_OK };
    return boat_plugin_host_active(host);
}

void boat_plugin_watch_start(BoatPluginWatch *watch, uint64_t key, double now)
{
    *watch = (BoatPluginWatch) {
        .next_poll = now + BOAT_PLUGIN_POLL, .changed = now, .seen = key, .staged = key
    };
}

bool boat_plugin_watch_due(BoatPluginWatch *watch, double now)
{
    if (now < watch->next_poll)
        return false;
    watch->next_poll = now + BOAT_PLUGIN_POLL;
    return true;
}

bool boat_plugin_watch_settled(BoatPluginWatch *watch, uint64_t key, double now)
{
    if (key != watch->seen) {
        watch->seen = key;
        watch->changed = now;
        return false;
    }
    if (key == watch->staged || now - watch->changed < BOAT_PLUGIN_SETTLE)
        return false;
    watch->staged = key;
    return true;
}

void boat_plugin_watch_staged(BoatPluginWatch *watch, uint64_t key)
{
    watch->seen = watch->staged = key;
}

static void fold(uint64_t *hash, const void *bytes, size_t size)
{
    const unsigned char *p = bytes;
    for (size_t i = 0; i < size; ++i) {
        *hash ^= p[i];
        *hash *= 1099511628211ull;
    }
}

uint64_t boat_plugin_read_key(
    BoatPluginStatus status, const BoatPluginFiles *files, const BoatPluginDiagnostic *diagnostic)
{
    uint64_t hash = 14695981039346656037ull;
    unsigned char code = (unsigned char)status;
    fold(&hash, &code, 1);
    if (status == BOAT_PLUGIN_OK) {
        uint64_t sizes[2] = { files->manifest_size, files->source_size };
        fold(&hash, sizes, sizeof(sizes));
        fold(&hash, files->manifest, files->manifest_size);
        fold(&hash, files->source, files->source_size);
    } else {
        fold(&hash, diagnostic->file, strlen(diagnostic->file));
        fold(&hash, diagnostic->message, strlen(diagnostic->message));
    }
    return hash;
}
