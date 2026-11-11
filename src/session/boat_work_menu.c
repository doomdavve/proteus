#include "session/boat_work_menu.h"

#include "platform/boat_file.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

bool boat_work_menu_format_time(int64_t unix_seconds, int utc_offset, char *out, size_t capacity)
{
    // Civil date from days since 1970-01-01 (Howard Hinnant's algorithm).
    if (unix_seconds < 0 || unix_seconds > INT64_C(253402300799) || utc_offset < -86400
        || utc_offset > 86400)
        return false;
    int64_t local = unix_seconds + utc_offset;
    int64_t days = local / 86400, rest = local % 86400;
    if (rest < 0) {
        rest += 86400;
        --days;
    }
    int64_t z = days + 719468, era = (z >= 0 ? z : z - 146096) / 146097;
    int64_t doe = z - era * 146097, yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
    int day = (int)(doy - (153 * mp + 2) / 5 + 1), month = (int)(mp < 10 ? mp + 3 : mp - 9);
    int64_t year = yoe + era * 400 + (month <= 2);
    int n = snprintf(out, capacity, "%04lld-%02d-%02d %02d:%02d", (long long)year, month, day,
        (int)(rest / 3600), (int)(rest % 3600 / 60));
    return n > 0 && (size_t)n < capacity;
}

bool boat_work_menu_file_name(const char *package, char *out, size_t capacity)
{
    int n = snprintf(out, capacity, "cargo-%s.sav", package);
    return n > 0 && (size_t)n < capacity && (size_t)n < BOAT_FILE_NAME;
}

static void upper(char *out, size_t capacity, const char *text)
{
    size_t i = 0;
    for (; text[i] && i + 1 < capacity; ++i)
        out[i] = (char)toupper((unsigned char)text[i]);
    out[i] = 0;
}

static void describe(BoatWorkMenuSave *save, const BoatSandboxSummary *s, int utc_offset)
{
    char place[sizeof(s->location)], label[BOAT_PLUGIN_TEXT], when[24] = "";
    upper(place, sizeof(place), s->location);
    size_t at = 0;
    at += (size_t)snprintf(save->summary + at, sizeof(save->summary) - at, "%s - LOAD %u",
        place[0] ? place : "?", s->load);
    if (s->has_progress && at < sizeof(save->summary)) {
        upper(label, sizeof(label), s->progress_label);
        at += (size_t)snprintf(save->summary + at, sizeof(save->summary) - at, " - %s: %lld", label,
            (long long)s->progress_value);
    }
    if (at < sizeof(save->summary)
        && boat_work_menu_format_time(s->saved_at, utc_offset, when, sizeof(when)))
        snprintf(save->summary + at, sizeof(save->summary) - at, " - SAVED %s", when);
}

// Read one file's header and judge it. Never writes.
static void inspect(BoatWorkMenuSave *save, const char *directory, const char *name,
    const BoatSandboxIdentity *installed, unsigned installed_count, int utc_offset)
{
    unsigned char bytes[BOAT_SANDBOX_SAVE_HEADER_MAX];
    size_t size = 0;
    BoatSandboxSummary s;
    *save = (BoatWorkMenuSave) { 0 };
    snprintf(save->name, sizeof(save->name), "%s", name);
    snprintf(save->path, sizeof(save->path), "%s/%s", directory, name);
    snprintf(save->reason, sizeof(save->reason), "DAMAGED");
    if (!boat_file_read_prefix(save->path, bytes, sizeof(bytes), &size))
        return;
    BoatSandboxSaveStatus status = boat_sandbox_save_peek(bytes, size, &s);
    if (status == BOAT_SANDBOX_SAVE_NEWER)
        snprintf(save->reason, sizeof(save->reason), "NEWER FORMAT");
    if (status != BOAT_SANDBOX_SAVE_OK)
        return;
    snprintf(save->package, sizeof(save->package), "%s", s.identity.package);
    describe(save, &s, utc_offset);
    const BoatSandboxIdentity *current = NULL;
    for (unsigned i = 0; i < installed_count; ++i)
        if (!strcmp(installed[i].package, s.identity.package))
            current = &installed[i];
    if (!current) {
        snprintf(save->reason, sizeof(save->reason), "PACKAGE NOT INSTALLED");
        return;
    }
    char reason[48] = "";
    if (boat_sandbox_save_check(&s.identity, current, current->migrates, reason, sizeof(reason))
        != BOAT_SANDBOX_REJECT) {
        save->available = true;
        save->reason[0] = 0;
    } else
        snprintf(save->reason, sizeof(save->reason), "%s", reason[0] ? reason : "PACKAGE CHANGED");
}

unsigned boat_work_menu_discover(const char *directory, const BoatSandboxIdentity *installed,
    unsigned installed_count, int utc_offset, BoatWorkMenuSave *out, unsigned capacity)
{
    static char names[BOAT_WORK_MENU_SAVES][BOAT_FILE_NAME];
    char candidates[BOAT_WORK_MENU_SAVES][BOAT_FILE_NAME];
    unsigned count = 0;
    if (capacity > BOAT_WORK_MENU_SAVES)
        capacity = BOAT_WORK_MENU_SAVES;
    // The installed packages' own slots come first, so a crowded directory cannot hide them.
    char path[1100];
    for (unsigned i = 0; i < installed_count && count < capacity; ++i) {
        char name[BOAT_FILE_NAME];
        if (boat_work_menu_file_name(installed[i].package, name, sizeof(name))
            && snprintf(path, sizeof(path), "%s/%s", directory, name) < (int)sizeof(path)
            && boat_file_exists(path))
            memcpy(candidates[count++], name, sizeof(name));
    }
    unsigned listed = boat_file_list(directory, "cargo-", ".sav", names, BOAT_WORK_MENU_SAVES);
    for (unsigned i = 0; i < listed && count < capacity; ++i) {
        bool seen = false;
        for (unsigned j = 0; j < count; ++j)
            seen |= !strcmp(candidates[j], names[i]);
        if (!seen)
            memcpy(candidates[count++], names[i], BOAT_FILE_NAME);
    }
    for (unsigned i = 0; i < count; ++i)
        inspect(&out[i], directory, candidates[i], installed, installed_count, utc_offset);
    return count;
}

const BoatWorkMenuSave *boat_work_menu_find(
    const BoatWorkMenuSave *saves, unsigned count, const char *package)
{
    char name[BOAT_FILE_NAME];
    if (!boat_work_menu_file_name(package, name, sizeof(name)))
        return NULL;
    for (unsigned i = 0; i < count; ++i)
        if (!strcmp(saves[i].name, name))
            return &saves[i];
    return NULL;
}

bool boat_work_menu_set_aside(const char *path)
{
    char backup[1100];
    snprintf(backup, sizeof(backup), "%s.bak", path);
    bool main_gone = !boat_file_exists(path) || boat_file_set_aside(path, ".rejected");
    bool backup_gone = !boat_file_exists(backup) || boat_file_set_aside(backup, ".rejected");
    return main_gone && backup_gone;
}

void boat_save_row_set(BoatSaveRow *row, bool present, bool available)
{
    *row = (BoatSaveRow) { .present = present,
        .available = present && available,
        .mode = present && available ? BOAT_SAVE_CONTINUE : BOAT_SAVE_NEW };
}

bool boat_save_row_cancel(BoatSaveRow *row)
{
    bool was = row->confirming;
    row->confirming = row->yes = false;
    row->pending = BOAT_SAVE_NOTHING;
    return was;
}

static void ask(BoatSaveRow *row, BoatSaveAction action)
{
    row->confirming = true;
    row->yes = false;
    row->pending = action;
}

// The answer to an open question: YES gives the pending action, NO nothing.
static BoatSaveAction answer(BoatSaveRow *row)
{
    BoatSaveAction action = row->yes ? row->pending : BOAT_SAVE_NOTHING;
    boat_save_row_cancel(row);
    return action;
}

void boat_save_row_turn(BoatSaveRow *row, int turn)
{
    if (!turn)
        return;
    if (row->confirming) {
        row->yes = !row->yes;
        return;
    }
    // Modes that apply: Continue only with an available save; New always; Delete with any file.
    BoatSaveMode modes[3];
    unsigned count = 0, at = 0;
    if (row->available)
        modes[count++] = BOAT_SAVE_CONTINUE;
    modes[count++] = BOAT_SAVE_NEW;
    if (row->present)
        modes[count++] = BOAT_SAVE_DELETE;
    for (unsigned i = 0; i < count; ++i)
        if (modes[i] == row->mode)
            at = i;
    row->mode = modes[(at + count + (turn > 0 ? 1 : count - 1)) % count];
}

BoatSaveAction boat_save_row_start(BoatSaveRow *row)
{
    if (row->confirming)
        return answer(row);
    if (row->mode == BOAT_SAVE_CONTINUE && row->available)
        return BOAT_SAVE_START_CONTINUE;
    if (row->mode == BOAT_SAVE_NEW) {
        if (!row->present)
            return BOAT_SAVE_START_NEW;
        ask(row, BOAT_SAVE_START_NEW);
    }
    return BOAT_SAVE_NOTHING;
}

BoatSaveAction boat_save_row_press(BoatSaveRow *row)
{
    if (row->confirming)
        return answer(row);
    if (row->mode == BOAT_SAVE_DELETE && row->present)
        ask(row, BOAT_SAVE_DELETE_FILE);
    else
        boat_save_row_turn(row, 1);
    return BOAT_SAVE_NOTHING;
}

void boat_save_row_text(const BoatSaveRow *row, char *out, size_t capacity)
{
    if (row->confirming)
        snprintf(out, capacity, "%s < %s >",
            row->pending == BOAT_SAVE_DELETE_FILE ? "DELETE SAVE?" : "START OVER?",
            row->yes ? "YES" : "NO");
    else
        snprintf(out, capacity, "< %s >",
            row->mode == BOAT_SAVE_CONTINUE     ? "CONTINUE"
                : row->mode == BOAT_SAVE_DELETE ? "DELETE"
                                                : "NEW");
}
