#include "boat_plugin_host.h"

#include <string.h>

#define SLOTS (BOAT_PLUGIN_UNDO + 2)

bool boat_plugin_host_start(BoatPluginHost *host, const BoatWorldDefinition *location,
    const BoatMap *map, const BoatPluginBundle *bundle)
{
    *host = (BoatPluginHost) { .location = location, .map = map };
    BoatPluginVoyage voyage;
    if (boat_plugin_load_voyage(bundle, NULL, &voyage, &host->diagnostic) != BOAT_PLUGIN_OK
        || boat_plugin_check_location(&voyage, location, map, &host->diagnostic) != BOAT_PLUGIN_OK)
        return false;
    boat_plugin_voyage_copy(&host->slots[0].voyage, &voyage);
    host->slots[0].number = host->numbered = 1;
    host->depth = 1;
    return true;
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
    BoatPluginVoyage voyage;
    if (boat_plugin_load_voyage(bundle, NULL, &voyage, &host->diagnostic) != BOAT_PLUGIN_OK
        || boat_plugin_check_location(&voyage, host->location, host->map, &host->diagnostic)
            != BOAT_PLUGIN_OK) {
        boat_plugin_host_discard(host);
        return false;
    }
    const BoatPluginRevision *active = boat_plugin_host_active(host);
    host->change = boat_plugin_classify(&active->voyage, &voyage);
    // A candidate identical to the one already held keeps its number.
    const BoatPluginRevision *held = boat_plugin_host_held(host);
    bool repeated = held && !host->undo
        && boat_plugin_classify(&held->voyage, &voyage) == BOAT_PLUGIN_UNCHANGED;
    boat_plugin_host_discard(host);
    if (host->change == BOAT_PLUGIN_UNCHANGED || host->change == BOAT_PLUGIN_INCOMPATIBLE)
        return true;
    unsigned slot = free_slot(host);
    BoatPluginRevision *revision = &host->slots[slot];
    unsigned number = repeated ? held->number : ++host->numbered;
    boat_plugin_voyage_copy(&revision->voyage, &voyage);
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
    host->change
        = boat_plugin_classify(&boat_plugin_host_active(host)->voyage, &host->slots[slot].voyage);
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
