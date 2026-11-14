// P5g: streaming headless reconstruction. Lua is invoked only with --diagnostic.
#include "room/boat_session_record.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t bytes[BOAT_SESSION_RECORD_HEADER + BOAT_SESSION_RECORD_BYTES_MAX];
static BoatSessionPlayback playback;
static BoatRoomObjective trial;

// 1 record, 0 clean EOF, -1 malformed/truncated/I/O failure.
static int read_record(FILE *file, BoatSessionRecord *record)
{
    size_t n = fread(bytes, 1, BOAT_SESSION_RECORD_HEADER, file);
    if (!n)
        return ferror(file) ? -1 : 0;
    if (n != BOAT_SESSION_RECORD_HEADER)
        return -1;
    uint32_t size = 0;
    for (unsigned i = 8; i < 12; ++i)
        size = (size << 8) | bytes[i];
    if (size > BOAT_SESSION_RECORD_BYTES_MAX
        || fread(bytes + BOAT_SESSION_RECORD_HEADER, 1, size, file) != size)
        return -1;
    return boat_session_record_decode(bytes, BOAT_SESSION_RECORD_HEADER + size, record) ? 1 : -1;
}
static bool same_result(const BoatRoomObjective *a, const BoatRoomObjective *b)
{
    if (boat_plugin_objective_digest(&a->objective) != boat_plugin_objective_digest(&b->objective)
        || a->objective.quarantined != b->objective.quarantined || a->revision != b->revision
        || a->event_count != b->event_count || a->sequence != b->sequence
        || a->objective.command_count != b->objective.command_count)
        return false;
    for (unsigned i = 0; i < a->objective.command_count; ++i) {
        const BoatPluginCommand *x = &a->objective.commands[i], *y = &b->objective.commands[i];
        if (strcmp(x->kind, y->kind) || strcmp(x->id, y->id) || strcmp(x->label, y->label)
            || x->value != y->value || x->generation != y->generation)
            return false;
    }
    return true;
}
static int reconstruct(FILE *file, bool diagnostic, uint64_t seek, bool use_seek)
{
    BoatSessionRecord record;
    uint32_t key_sequence = 0;
    bool key_found = false;
    int status;
    while (true) {
        status = read_record(file, &record);
        if (status != 1)
            break;
        if (record.kind == BOAT_SESSION_RECORD_KEYFRAME && record.tick <= seek) {
            key_sequence = record.sequence;
            key_found = true;
        }
        if (diagnostic
            && (record.kind == BOAT_SESSION_RECORD_OBJECTIVE_AFTER
                || record.kind == BOAT_SESSION_RECORD_DEFINITION_AFTER)) {
            trial = playback.before;
            trial.source = &trial.owned;
            if (record.kind == BOAT_SESSION_RECORD_OBJECTIVE_AFTER)
                boat_room_objective_after_tick(&trial, record.tick);
            else {
                BoatNetRules rules = boat_room_objective_rules(&trial);
                rules.definition(rules.context, record.tick, &playback.callback_room);
            }
        }
        if (!boat_session_record_apply(&playback, &record, false)
            || (diagnostic
                && (record.kind == BOAT_SESSION_RECORD_OBJECTIVE_AFTER
                    || record.kind == BOAT_SESSION_RECORD_DEFINITION_AFTER)
                && !same_result(&trial, &playback.objective))) {
            fprintf(stderr, "record %u kind %u tick %" PRIu64 ": reconstruction mismatch\n",
                record.sequence, record.kind, record.tick);
            return 1;
        }
    }
    if (status < 0 || !playback.ended) {
        fprintf(stderr, "recording is truncated, corrupt, incompatible, or lacks its end marker\n");
        return 1;
    }
    if (use_seek) {
        if (!key_found || seek > playback.fleet.state.tick) {
            fprintf(stderr, "seek tick is outside this recording\n");
            return 1;
        }
        boat_session_record_close(&playback);
        rewind(file);
        if (read_record(file, &record) != 1
            || !boat_session_record_apply(&playback, &record, false))
            return 1;
        do {
            if (read_record(file, &record) != 1)
                return 1;
        } while (record.sequence != key_sequence);
        if (!boat_session_record_apply(&playback, &record, true))
            return 1;
        while ((status = read_record(file, &record)) == 1) {
            if (record.tick > seek)
                break;
            if (!boat_session_record_apply(&playback, &record, false))
                return 1;
        }
        if (status < 0 || playback.fleet.state.tick != seek || playback.awaiting_result)
            return 1;
    }
    printf("boat_session_record=ok tick=%" PRIu64 " hash=%016" PRIx64 " objective=%016" PRIx64
           " definition=%u source=%u hud=%u records=%" PRIu64 " keyframes=%" PRIu64
           " diagnostic=%s\n",
        playback.fleet.state.tick, boat_fleet_hash(&playback.fleet.state),
        boat_plugin_objective_digest(&playback.objective.objective), playback.revision,
        playback.objective.revision, playback.hud.revision, playback.records, playback.keyframes,
        diagnostic ? "yes" : "no");
    return 0;
}
int main(int argc, char **argv)
{
    bool diagnostic = false, use_seek = false;
    uint64_t seek = 0;
    const char *path = NULL;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--diagnostic"))
            diagnostic = true;
        else if (!strcmp(argv[i], "--seek") && i + 1 < argc) {
            char *end;
            errno = 0;
            const char *value = argv[++i];
            unsigned long long n = strtoull(value, &end, 10);
            if (errno || !value[0] || value[0] == '-' || *end)
                return 2;
            seek = (uint64_t)n;
            use_seek = true;
        } else if (!path && argv[i][0] != '-')
            path = argv[i];
        else
            return 2;
    }
    if (!path) {
        fprintf(stderr, "usage: validate_boat_session_record PATH [--seek TICK] [--diagnostic]\n");
        return 2;
    }
    FILE *file = fopen(path, "rb");
    if (!file) {
        perror(path);
        return 1;
    }
    int result = reconstruct(file, diagnostic, seek, use_seek);
    boat_session_record_close(&playback);
    if (fclose(file))
        result = 1;
    return result;
}
