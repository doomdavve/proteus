#include "session/boat_work_menu.h"

#include "platform/boat_file.h"
#include "plugins/boat_plugin_catalog.h"
#include "session/boat_work_session.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// SV1h: discovery over build-tree directories (never the player's data directory) with fixed
// times and a fixed UTC offset, and the SAVE row's confirmations. No wall time.
#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            fprintf(stderr, "work menu check failed at %d: %s\n", __LINE__, #condition);           \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

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
static void edit_digest(BoatSandboxSave *save) { save->summary.identity.work_digest ^= 1; }
static void edit_physics(BoatSandboxSave *save) { save->summary.identity.physics_revision++; }

static char base[900];

static void path_of(char *out, size_t size, const char *dir, const char *name)
{
    snprintf(out, size, "%s/%s/%s", base, dir, name);
}

static void write_bytes(const char *dir, const char *name, const void *bytes, size_t size)
{
    char path[1100];
    path_of(path, sizeof(path), dir, name);
    FILE *file = fopen(path, "wb");
    CHECK(file && fwrite(bytes, 1, size, file) == size && fclose(file) == 0);
}

static void clear(const char *dir, const char *name)
{
    char path[1100];
    path_of(path, sizeof(path), dir, name);
    remove(path);
}

// A fresh directory: known leftovers removed, then created.
static void fresh(const char *dir)
{
    static const char *const names[] = { "cargo-test.pkg.sav", "cargo-test.pkg.sav.bak",
        "cargo-test.pkg.sav.rejected", "cargo-test.pkg.sav.bak.rejected", "cargo-other.sav",
        "cargo-old.sav", "cargo-new.sav", "notes.txt", "cargo-test.pkg.sav.tmp", "cargo-gone.sav" };
    char path[1100];
    for (unsigned i = 0; i < sizeof(names) / sizeof(*names); ++i)
        clear(dir, names[i]);
    for (unsigned i = 0; i < 40; ++i) {
        char name[48];
        snprintf(name, sizeof(name), "cargo-junk-%02u.sav", i);
        clear(dir, name);
    }
    snprintf(path, sizeof(path), "%s/%s", base, dir);
    CHECK(boat_file_make_directory(base) && boat_file_make_directory(path));
}

static size_t encode_sample(
    const char *package, unsigned char *out, void (*edit)(BoatSandboxSave *))
{
    static BoatSandboxSave save;
    size_t size = 0;
    sample(&save);
    snprintf(save.summary.identity.package, sizeof(save.summary.identity.package), "%s", package);
    if (edit)
        edit(&save);
    CHECK(boat_sandbox_save_encode(&save, out, BOAT_SANDBOX_SAVE_MAX, &size));
    return size;
}

static BoatSandboxIdentity installed_identity(void)
{
    static BoatSandboxSave save;
    sample(&save);
    snprintf(save.summary.identity.package, sizeof(save.summary.identity.package), "test.pkg");
    return save.summary.identity;
}

static void check_time(void)
{
    char text[32];
    CHECK(
        boat_work_menu_format_time(0, 0, text, sizeof(text)) && !strcmp(text, "1970-01-01 00:00"));
    CHECK(boat_work_menu_format_time(1793000000, 0, text, sizeof(text))
        && !strcmp(text, "2026-10-26 07:33"));
    CHECK(boat_work_menu_format_time(1793000000, 3600, text, sizeof(text))
        && !strcmp(text, "2026-10-26 08:33"));
    CHECK(boat_work_menu_format_time(1793000000, -8 * 3600, text, sizeof(text))
        && !strcmp(text, "2026-10-25 23:33"));
    CHECK(boat_work_menu_format_time(951782400 + 86399, 0, text, sizeof(text))
        && !strcmp(text, "2000-02-29 23:59")); // A leap day.
    CHECK(!boat_work_menu_format_time(-1, 0, text, sizeof(text)));
    CHECK(!boat_work_menu_format_time(0, 0, text, 8));
}

static void check_discovery(void)
{
    static BoatWorkMenuSave found[BOAT_WORK_MENU_SAVES];
    static unsigned char bytes[BOAT_SANDBOX_SAVE_MAX];
    BoatSandboxIdentity now = installed_identity();
    char dir[1100];

    // No directory and an empty one list nothing.
    snprintf(dir, sizeof(dir), "%s/missing-dir", base);
    CHECK(boat_work_menu_discover(dir, &now, 1, 0, found, BOAT_WORK_MENU_SAVES) == 0);
    fresh("empty");
    snprintf(dir, sizeof(dir), "%s/empty", base);
    CHECK(boat_work_menu_discover(dir, &now, 1, 0, found, BOAT_WORK_MENU_SAVES) == 0);

    // One good save, and files that are not saves.
    fresh("one");
    snprintf(dir, sizeof(dir), "%s/one", base);
    size_t size = encode_sample("test.pkg", bytes, NULL);
    write_bytes("one", "cargo-test.pkg.sav", bytes, size);
    write_bytes("one", "cargo-test.pkg.sav.bak", bytes, size);
    write_bytes("one", "cargo-test.pkg.sav.rejected", bytes, size);
    write_bytes("one", "cargo-test.pkg.sav.tmp", bytes, size);
    write_bytes("one", "notes.txt", "hello", 5);
    CHECK(boat_work_menu_discover(dir, &now, 1, 3600, found, BOAT_WORK_MENU_SAVES) == 1);
    CHECK(found[0].available && !found[0].reason[0] && !strcmp(found[0].package, "test.pkg"));
    CHECK(!strcmp(found[0].name, "cargo-test.pkg.sav"));
    CHECK(!strcmp(found[0].summary, "OUTER - LOAD 2 - JETTY SUPPLIED: 1 - SAVED 2026-10-26 08:33"));
    CHECK(boat_work_menu_find(found, 1, "test.pkg") == &found[0]);
    CHECK(!boat_work_menu_find(found, 1, "other"));
    // Discovery wrote and deleted nothing.
    char path[1100];
    path_of(path, sizeof(path), "one", "cargo-test.pkg.sav");
    CHECK(boat_file_exists(path));
    path_of(path, sizeof(path), "one", "cargo-test.pkg.sav.bak");
    CHECK(boat_file_exists(path));

    // A header read needs only the first 512 bytes: a save cut after them is still available
    // here (its body is judged on Continue).
    fresh("prefix");
    snprintf(dir, sizeof(dir), "%s/prefix", base);
    size = encode_sample("test.pkg", bytes, NULL);
    CHECK(size > BOAT_SANDBOX_SAVE_HEADER_MAX);
    write_bytes("prefix", "cargo-test.pkg.sav", bytes, BOAT_SANDBOX_SAVE_HEADER_MAX);
    CHECK(boat_work_menu_discover(dir, &now, 1, 0, found, BOAT_WORK_MENU_SAVES) == 1
        && found[0].available);

    // Corrupt, truncated and empty headers are unavailable as DAMAGED, and never crash.
    fresh("bad");
    snprintf(dir, sizeof(dir), "%s/bad", base);
    size = encode_sample("test.pkg", bytes, NULL);
    bytes[10] ^= 0x55; // Inside the header: the header checksum fails.
    write_bytes("bad", "cargo-test.pkg.sav", bytes, size);
    write_bytes("bad", "cargo-other.sav", "PR", 2);
    write_bytes("bad", "cargo-gone.sav", "", 0);
    CHECK(boat_work_menu_discover(dir, &now, 1, 0, found, BOAT_WORK_MENU_SAVES) == 3);
    for (unsigned i = 0; i < 3; ++i)
        CHECK(!found[i].available && !strcmp(found[i].reason, "DAMAGED"));
    path_of(path, sizeof(path), "bad", "cargo-test.pkg.sav");
    CHECK(boat_file_exists(path)); // Still there: nothing is deleted or set aside.

    // A newer format, a changed package, physics and progress format, and an uninstalled package.
    fresh("verdicts");
    snprintf(dir, sizeof(dir), "%s/verdicts", base);
    size = encode_sample("test.pkg", bytes, NULL);
    bytes[4] = BOAT_SANDBOX_SAVE_FORMAT + 1; // The format field follows the magic.
    write_bytes("verdicts", "cargo-new.sav", bytes, size);
    size = encode_sample("test.pkg", bytes, edit_digest);
    write_bytes("verdicts", "cargo-test.pkg.sav", bytes, size);
    size = encode_sample("test.pkg", bytes, edit_physics);
    write_bytes("verdicts", "cargo-old.sav", bytes, size);
    size = encode_sample("not.installed", bytes, NULL);
    write_bytes("verdicts", "cargo-other.sav", bytes, size);
    unsigned count = boat_work_menu_discover(dir, &now, 1, 0, found, BOAT_WORK_MENU_SAVES);
    CHECK(count == 4);
    for (unsigned i = 0; i < count; ++i) {
        CHECK(!found[i].available);
        if (!strcmp(found[i].name, "cargo-new.sav"))
            CHECK(!strcmp(found[i].reason, "NEWER FORMAT"));
        else if (!strcmp(found[i].name, "cargo-test.pkg.sav"))
            CHECK(!strcmp(found[i].reason, "PACKAGE CHANGED"));
        else if (!strcmp(found[i].name, "cargo-old.sav"))
            CHECK(!strcmp(found[i].reason, "PHYSICS CHANGED"));
        else {
            CHECK(!strcmp(found[i].name, "cargo-other.sav"));
            CHECK(!strcmp(found[i].reason, "PACKAGE NOT INSTALLED"));
        }
    }

    // Many files: at most 16 are read, and the installed package's own slot is among them.
    fresh("many");
    snprintf(dir, sizeof(dir), "%s/many", base);
    for (unsigned i = 0; i < 40; ++i) {
        char name[48];
        snprintf(name, sizeof(name), "cargo-junk-%02u.sav", i);
        write_bytes("many", name, "junk", 4);
    }
    size = encode_sample("test.pkg", bytes, NULL);
    write_bytes("many", "cargo-test.pkg.sav", bytes, size);
    count = boat_work_menu_discover(dir, &now, 1, 0, found, BOAT_WORK_MENU_SAVES);
    CHECK(count == BOAT_WORK_MENU_SAVES);
    const BoatWorkMenuSave *slot = boat_work_menu_find(found, count, "test.pkg");
    CHECK(slot && slot->available);
    CHECK(boat_work_menu_discover(dir, &now, 1, 0, found, 3) == 3); // The capacity is honoured.
}

static void check_set_aside(void)
{
    static unsigned char bytes[BOAT_SANDBOX_SAVE_MAX];
    char main_path[1100], path[1100];
    fresh("aside");
    size_t size = encode_sample("test.pkg", bytes, NULL);
    write_bytes("aside", "cargo-test.pkg.sav", bytes, size);
    write_bytes("aside", "cargo-test.pkg.sav.bak", bytes, size);
    path_of(main_path, sizeof(main_path), "aside", "cargo-test.pkg.sav");
    CHECK(boat_work_menu_set_aside(main_path));
    CHECK(!boat_file_exists(main_path));
    path_of(path, sizeof(path), "aside", "cargo-test.pkg.sav.rejected");
    CHECK(boat_file_exists(path));
    path_of(path, sizeof(path), "aside", "cargo-test.pkg.sav.bak.rejected");
    CHECK(boat_file_exists(path));
    CHECK(boat_work_menu_set_aside(main_path)); // Nothing left is not a failure.
}

// DELETE removes only the named package's file and its backup.
static void check_delete(void)
{
    static unsigned char bytes[BOAT_SANDBOX_SAVE_MAX];
    char main_path[1100], path[1100];
    fresh("delete");
    size_t size = encode_sample("test.pkg", bytes, NULL);
    write_bytes("delete", "cargo-test.pkg.sav", bytes, size);
    write_bytes("delete", "cargo-test.pkg.sav.bak", bytes, size);
    write_bytes("delete", "cargo-other.sav", bytes, size);
    write_bytes("delete", "cargo-test.pkg.sav.rejected", bytes, size);
    write_bytes("delete", "notes.txt", "hello", 5);
    path_of(main_path, sizeof(main_path), "delete", "cargo-test.pkg.sav");
    CHECK(boat_file_remove_save(main_path));
    CHECK(!boat_file_exists(main_path));
    path_of(path, sizeof(path), "delete", "cargo-test.pkg.sav.bak");
    CHECK(!boat_file_exists(path));
    const char *kept[] = { "cargo-other.sav", "cargo-test.pkg.sav.rejected", "notes.txt" };
    for (unsigned i = 0; i < 3; ++i) {
        path_of(path, sizeof(path), "delete", kept[i]);
        CHECK(boat_file_exists(path));
    }
}

static void check_row(void)
{
    BoatSaveRow row;
    char text[48];
    // No save: New is the only mode and starts at once.
    boat_save_row_set(&row, false, false);
    CHECK(row.mode == BOAT_SAVE_NEW);
    boat_save_row_text(&row, text, sizeof(text));
    CHECK(!strcmp(text, "< NEW >"));
    boat_save_row_turn(&row, 1);
    CHECK(row.mode == BOAT_SAVE_NEW);
    CHECK(boat_save_row_start(&row) == BOAT_SAVE_START_NEW && !row.confirming);

    // An available save: Continue is the default and starts at once.
    boat_save_row_set(&row, true, true);
    CHECK(row.mode == BOAT_SAVE_CONTINUE);
    CHECK(boat_save_row_start(&row) == BOAT_SAVE_START_CONTINUE && !row.confirming);
    boat_save_row_turn(&row, 1);
    CHECK(row.mode == BOAT_SAVE_NEW);
    boat_save_row_turn(&row, 1);
    CHECK(row.mode == BOAT_SAVE_DELETE);
    boat_save_row_turn(&row, 1);
    CHECK(row.mode == BOAT_SAVE_CONTINUE);
    boat_save_row_turn(&row, -1);
    CHECK(row.mode == BOAT_SAVE_DELETE);

    // New over a save asks first; NO is the default; Enter on NO cancels; nothing starts.
    boat_save_row_set(&row, true, true);
    boat_save_row_turn(&row, 1);
    CHECK(boat_save_row_start(&row) == BOAT_SAVE_NOTHING && row.confirming && !row.yes);
    boat_save_row_text(&row, text, sizeof(text));
    CHECK(!strcmp(text, "START OVER? < NO >"));
    CHECK(boat_save_row_start(&row) == BOAT_SAVE_NOTHING && !row.confirming);
    // Again, answered YES.
    CHECK(boat_save_row_start(&row) == BOAT_SAVE_NOTHING && row.confirming);
    boat_save_row_turn(&row, 1);
    boat_save_row_text(&row, text, sizeof(text));
    CHECK(!strcmp(text, "START OVER? < YES >"));
    CHECK(boat_save_row_start(&row) == BOAT_SAVE_START_NEW && !row.confirming);
    // Escape or moving the selection cancels an open question.
    CHECK(boat_save_row_start(&row) == BOAT_SAVE_NOTHING && row.confirming);
    CHECK(boat_save_row_cancel(&row) && !row.confirming && !boat_save_row_cancel(&row));
    CHECK(boat_save_row_start(&row) == BOAT_SAVE_NOTHING); // The question reopens at NO.
    CHECK(!row.yes);

    // Delete needs the same confirmation, and a start in Delete mode does nothing.
    boat_save_row_set(&row, true, true);
    boat_save_row_turn(&row, -1);
    CHECK(row.mode == BOAT_SAVE_DELETE);
    CHECK(boat_save_row_start(&row) == BOAT_SAVE_NOTHING && !row.confirming);
    CHECK(boat_save_row_press(&row) == BOAT_SAVE_NOTHING && row.confirming && !row.yes);
    boat_save_row_text(&row, text, sizeof(text));
    CHECK(!strcmp(text, "DELETE SAVE? < NO >"));
    CHECK(boat_save_row_press(&row) == BOAT_SAVE_NOTHING && !row.confirming); // NO.
    CHECK(boat_save_row_press(&row) == BOAT_SAVE_NOTHING && row.confirming);
    boat_save_row_turn(&row, -1);
    CHECK(boat_save_row_press(&row) == BOAT_SAVE_DELETE_FILE && !row.confirming);

    // An unavailable save: New (with confirmation) and Delete, never Continue.
    boat_save_row_set(&row, true, false);
    CHECK(row.mode == BOAT_SAVE_NEW && !row.available);
    CHECK(boat_save_row_start(&row) == BOAT_SAVE_NOTHING && row.confirming);
    boat_save_row_cancel(&row);
    boat_save_row_turn(&row, 1);
    CHECK(row.mode == BOAT_SAVE_DELETE);
    boat_save_row_turn(&row, 1);
    CHECK(row.mode == BOAT_SAVE_NEW);

    // Pressing the row when it is not asking cycles it.
    boat_save_row_set(&row, true, true);
    CHECK(boat_save_row_press(&row) == BOAT_SAVE_NOTHING && row.mode == BOAT_SAVE_NEW);
}

static void check_writer(void)
{
    static BoatPluginRevision revision;
    BoatPluginDiagnostic diagnostic = { 0 };
    const BoatPluginCatalogEntry *entry = boat_plugin_catalog_work_default("outer");
    CHECK(entry && boat_plugin_catalog_decode(entry, &revision, &diagnostic));
    BoatWorkSession session = { .revision = &revision, .writer = "wake_velocity" };
    BoatSandboxIdentity id;
    boat_work_session_identity(&session, &id);
    CHECK(!strcmp(id.build, "wake_velocity") && !strcmp(id.package, entry->id));
    session.writer = "cargo_sandbox";
    boat_work_session_identity(&session, &id);
    CHECK(!strcmp(id.build, "cargo_sandbox"));
    // The build string is a message only: a different writer is still compatible.
    BoatSandboxIdentity other;
    session.writer = "wake_velocity";
    boat_work_session_identity(&session, &other);
    CHECK(boat_sandbox_save_check(&id, &other, false, NULL, 0) == BOAT_SANDBOX_RESUME);
}

int main(int argc, char **argv)
{
    CHECK(argc == 2);
    snprintf(base, sizeof(base), "%s", argv[1]);
    check_time();
    check_discovery();
    check_set_aside();
    check_delete();
    check_row();
    check_writer();
    printf("work menu checks passed\n");
    return 0;
}
