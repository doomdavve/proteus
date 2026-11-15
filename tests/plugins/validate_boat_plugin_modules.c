#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
// Marina M5 W3: a package with modules travels as a source image through every carrier. A real
// isolated worker (this executable, re-run with --plugin-stage-worker) stages it; the host
// classifies, applies and undoes it; module-only edits are real changes; failures name the file.
#include "plugins/boat_plugin_image.h"
#include "plugins/boat_plugin_load.h"
#include "wake_velocity/wake_command.h"
#include "wake_velocity/wake_velocity_plugin.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define MAKE_DIRECTORY(path) _mkdir(path)
#else
#define MAKE_DIRECTORY(path) mkdir(path, 0755)
#endif

#define CHECK(test)                                                                                \
    do {                                                                                           \
        if (!(test)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #test);                             \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static const char *worker_path;

// The tour package's manifest with a module list added; the entry just returns the module.
static const char TOUR_MANIFEST[]
    = "{\"id\": \"test.tour-modules\", \"name\": \"Tour\", \"api\": 1, \"kind\": \"voyage\", "
      "\"entry\": \"main.lua\", \"location\": \"outer\", \"capabilities\": [\"voyage.define\", "
      "\"objective.hud\"], \"state_version\": 1, \"modules\": [\"tour\"]}";
static const char PLAIN_MANIFEST[]
    = "{\"id\": \"test.route-modules\", \"name\": \"Route\", \"api\": 1, \"kind\": \"voyage\", "
      "\"entry\": \"main.lua\", \"location\": \"outer\", \"capabilities\": [\"voyage.define\"], "
      "\"state_version\": 1, \"modules\": [\"route\"]}";
static const char ENTRY[] = "return require('tour')\n";
static const char PLAIN_ENTRY[] = "return require('route')\n";

static int slurp(const char *path, char *out, size_t capacity, size_t *size)
{
    FILE *file = fopen(path, "rb");
    CHECK(file);
    *size = fread(out, 1, capacity, file);
    CHECK(*size > 0 && *size < capacity && fgetc(file) == EOF);
    fclose(file);
    return 0;
}

static int write_file(const char *directory, const char *relative, const char *text, size_t size)
{
    char path[700];
    snprintf(path, sizeof(path), "%s/%s", directory, relative);
    FILE *file = fopen(path, "wb");
    CHECK(file && fwrite(text, 1, size, file) == size && !fclose(file));
    return 0;
}

// One package directory: MANIFEST, ENTRY text and the single module NAME.lua.
static int write_package(const char *directory, const char *manifest, const char *entry,
    const char *module, const char *text, size_t size)
{
    char path[700];
    MAKE_DIRECTORY(directory);
    snprintf(path, sizeof(path), "%s/modules", directory);
    MAKE_DIRECTORY(path);
    char file[128];
    snprintf(file, sizeof(file), "modules/%s.lua", module);
    CHECK(!write_file(directory, "plugin.json", manifest, strlen(manifest)));
    CHECK(!write_file(directory, "main.lua", entry, strlen(entry)));
    CHECK(!write_file(directory, file, text, size));
    return 0;
}

static bool replace_once(
    char *text, size_t *size, size_t capacity, const char *from, const char *to)
{
    char *at = strstr(text, from);
    size_t from_size = strlen(from), to_size = strlen(to);
    if (!at || strstr(at + 1, from) || *size - from_size + to_size > capacity)
        return false;
    memmove(at + to_size, at + from_size, *size - (size_t)(at - text) - from_size);
    memcpy(at, to, to_size);
    *size = *size - from_size + to_size;
    text[*size] = 0;
    return true;
}

// The package at DIRECTORY with its module text replaced, read back as files (a real read).
static int module_edit(const char *directory, const char *module, const char *text, size_t size,
    BoatPluginFiles *files)
{
    char file[128];
    snprintf(file, sizeof(file), "modules/%s.lua", module);
    CHECK(!write_file(directory, file, text, size));
    BoatPluginDiagnostic d;
    CHECK(boat_plugin_read(directory, NULL, files, &d) == BOAT_PLUGIN_OK);
    return 0;
}

static char console_line[192];

static void collect(void *context, const char *line)
{
    (void)context;
    snprintf(console_line, sizeof(console_line), "%s", line);
}

static void console_clear(void *context) { (void)context; }

static bool console_save(
    void *context, const WakePlugin *plugin, const char *directory, char *message, size_t capacity)
{
    (void)context;
    return wake_plugin_save(plugin, directory, message, capacity);
}

static int completed(BoatPluginStageJob *job, BoatPluginHost *host)
{
    int result;
    while (!(result = boat_plugin_stage_poll(job, host, .25))) { }
    return result;
}

static int stage(BoatPluginHost *host, const BoatPluginFiles *files)
{
    BoatPluginStageJob *job = boat_plugin_stage_start(worker_path, host, files, 0);
    if (!job)
        return -2;
    int result = completed(job, host);
    boat_plugin_stage_close(job);
    return result;
}

static int check_source_rules(const BoatPluginFiles *good)
{
    BoatPluginManifest manifest;
    BoatPluginDiagnostic d;
    CHECK(boat_plugin_parse_manifest(good->manifest, good->manifest_size, NULL, &manifest, &d)
        == BOAT_PLUGIN_OK);
    CHECK(
        boat_plugin_check_source(&manifest, good->source, good->source_size, &d) == BOAT_PLUGIN_OK);
    // Declared modules, raw text source.
    CHECK(
        boat_plugin_check_source(&manifest, ENTRY, sizeof(ENTRY) - 1, &d) == BOAT_PLUGIN_MANIFEST);
    CHECK(!strcmp(d.file, "plugin.json") && !strcmp(d.field, "modules")
        && strstr(d.message, "not a module image"));
    // No modules declared, image source.
    BoatPluginManifest bare = manifest;
    bare.module_count = 0;
    CHECK(boat_plugin_check_source(&bare, good->source, good->source_size, &d)
        == BOAT_PLUGIN_MANIFEST);
    CHECK(!strcmp(d.field, "modules") && strstr(d.message, "declares no modules"));
    // Other names, a longer list, another order, another entry.
    BoatPluginManifest other = manifest;
    snprintf(other.modules[0], sizeof(other.modules[0]), "%s", "tours");
    CHECK(boat_plugin_check_source(&other, good->source, good->source_size, &d)
        == BOAT_PLUGIN_MANIFEST);
    CHECK(strstr(d.message, "do not match"));
    other = manifest;
    other.module_count = 2;
    snprintf(other.modules[1], sizeof(other.modules[1]), "%s", "extra");
    CHECK(boat_plugin_check_source(&other, good->source, good->source_size, &d)
        == BOAT_PLUGIN_MANIFEST);
    other = manifest;
    snprintf(other.entry, sizeof(other.entry), "%s", "start.lua");
    CHECK(boat_plugin_check_source(&other, good->source, good->source_size, &d)
        == BOAT_PLUGIN_MANIFEST);
    CHECK(!strcmp(d.field, "entry"));
    // Order matters: an image of {a, b} against a manifest of {b, a}.
    BoatPluginImageFile pair[3] = { { "main.lua", 8, "return {}", 9 }, { "a", 1, "return 1", 8 },
        { "b", 1, "return 2", 8 } };
    char image[256];
    size_t size;
    CHECK(boat_plugin_image_pack(pair, 3, image, sizeof(image), &size));
    BoatPluginManifest two = manifest;
    two.module_count = 2;
    snprintf(two.modules[0], sizeof(two.modules[0]), "%s", "a");
    snprintf(two.modules[1], sizeof(two.modules[1]), "%s", "b");
    CHECK(boat_plugin_check_source(&two, image, size, &d) == BOAT_PLUGIN_OK);
    snprintf(two.modules[0], sizeof(two.modules[0]), "%s", "b");
    snprintf(two.modules[1], sizeof(two.modules[1]), "%s", "a");
    CHECK(boat_plugin_check_source(&two, image, size, &d) == BOAT_PLUGIN_MANIFEST);
    // A malformed image (truncated) is a syntax-class rejection.
    CHECK(boat_plugin_check_source(&manifest, good->source, good->source_size - 1, &d)
        == BOAT_PLUGIN_SYNTAX);
    CHECK(strstr(d.message, "malformed package source image"));
    return 0;
}

int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "--plugin-stage-worker"))
        return boat_plugin_stage_worker();
    if (argc == 2 && !strcmp(argv[1], "--plugin-query-worker"))
        return boat_plugin_query_worker();
    worker_path = argv[0];
    static char tour[BOAT_PLUGIN_SOURCE_MAX], route[BOAT_PLUGIN_SOURCE_MAX];
    size_t tour_size, route_size;
    CHECK(!slurp("plugins/shelter-tour/main.lua", tour, sizeof(tour) - 1, &tour_size));
    CHECK(!slurp("plugins/langedrag/main.lua", route, sizeof(route) - 1, &route_size));
    tour[tour_size] = route[route_size] = 0;
    MAKE_DIRECTORY(BOAT_PLUGIN_MODULES_DIR);
    char directory[600], plain[600];
    snprintf(directory, sizeof(directory), "%s/tour", BOAT_PLUGIN_MODULES_DIR);
    snprintf(plain, sizeof(plain), "%s/route", BOAT_PLUGIN_MODULES_DIR);
    CHECK(!write_package(directory, TOUR_MANIFEST, ENTRY, "tour", tour, tour_size));
    CHECK(!write_package(plain, PLAIN_MANIFEST, PLAIN_ENTRY, "route", route, route_size));

    BoatPluginFiles *files = calloc(1, sizeof(*files));
    BoatPluginFiles *edited = calloc(1, sizeof(*edited));
    BoatPluginFiles *reference = calloc(1, sizeof(*reference));
    BoatPluginHost *host = calloc(1, sizeof(*host));
    CHECK(files && edited && reference && host);
    BoatPluginDiagnostic d;
    CHECK(boat_plugin_read(directory, NULL, files, &d) == BOAT_PLUGIN_OK);
    CHECK(boat_plugin_image_is(files->source, files->source_size));
    CHECK(!check_source_rules(files));

    BoatWorldResources resources;
    CHECK(boat_world_resources_init(&resources, boat_location_find("outer")));
    BoatPluginBundle bundle = boat_plugin_bundle(files);

    // The same voyage as the single-file package: modules change the carrier, not the data.
    CHECK(boat_plugin_read("plugins/shelter-tour", NULL, reference, &d) == BOAT_PLUGIN_OK);
    BoatPluginBundle single = boat_plugin_bundle(reference);
    static BoatPluginVoyage expected;
    CHECK(boat_plugin_load_voyage(&single, NULL, &expected, &d) == BOAT_PLUGIN_OK);
    CHECK(boat_plugin_host_start(host, resources.definition, &resources.map, &bundle));
    const BoatPluginRevision *active = boat_plugin_host_active(host);
    CHECK(active->voyage.digest == expected.digest && active->voyage.manifest.objective);
    CHECK(active->source_size == files->source_size
        && !memcmp(active->source, files->source, files->source_size)); // The retained image.
    CHECK(boat_plugin_host_memory(host).retained_source_bytes == files->source_size);

    // Native stage job: the image reaches the worker and back; an identical package is no change.
    CHECK(stage(host, files) == 1 && host->change == BOAT_PLUGIN_UNCHANGED && !host->held);

    // Module-only edit 1: the decoded descriptor is identical but a callback's text differs, so
    // an objective package is not UNCHANGED (it restarts the voyage: there is no migrate).
    tour_size = strlen(tour);
    CHECK(replace_once(tour, &tour_size, sizeof(tour) - 1, "SHELTERS / 2", "SHELTERS OF 2"));
    CHECK(!module_edit(directory, "tour", tour, tour_size, edited));
    CHECK(edited->source_size != files->source_size
        && boat_plugin_read_key(BOAT_PLUGIN_OK, edited, NULL)
            != boat_plugin_read_key(BOAT_PLUGIN_OK, files, NULL));
    CHECK(stage(host, edited) == 1);
    CHECK(host->change == BOAT_PLUGIN_RESTART_VOYAGE && host->held);
    CHECK(boat_plugin_host_held(host)->voyage.digest == expected.digest);
    CHECK(boat_plugin_host_active(host)->number == 1 && boat_plugin_host_held(host)->number == 2);
    // Apply, then Undo: each carries its own image.
    const BoatPluginRevision *applied = boat_plugin_host_commit(host);
    CHECK(applied && applied->number == 2 && applied->source_size == edited->source_size
        && !memcmp(applied->source, edited->source, edited->source_size));
    CHECK(boat_plugin_host_undo(host) && boat_plugin_host_commit(host));
    active = boat_plugin_host_active(host);
    CHECK(active->number == 1 && active->source_size == files->source_size
        && !memcmp(active->source, files->source, files->source_size));

    // Module-only edit 2: a change the decoded descriptor shows (an arrival radius).
    char changed[BOAT_PLUGIN_SOURCE_MAX];
    memcpy(changed, tour, tour_size + 1);
    size_t changed_size = tour_size;
    CHECK(replace_once(changed, &changed_size, sizeof(changed) - 1, "radius = 75", "radius = 80"));
    CHECK(!module_edit(directory, "tour", changed, changed_size, edited));
    CHECK(stage(host, edited) == 1 && host->held);
    CHECK(host->change != BOAT_PLUGIN_UNCHANGED);
    CHECK(boat_plugin_host_held(host)->voyage.digest != expected.digest);
    boat_plugin_host_discard(host);

    // A failed stage leaves the active revision, its number and the epoch untouched, and the
    // diagnostic names the module file.
    unsigned numbered = host->numbered;
    uint64_t epoch = host->epoch;
    struct {
        const char *from, *to;
        BoatPluginStatus status;
        const char *message;
    } failures[] = {
        { "local function hud", "local function hud(", BOAT_PLUGIN_SYNTAX, NULL },
        { "return {\n  init", "error('top boom')\nreturn {\n  init", BOAT_PLUGIN_RUNTIME,
            "top boom" },
        { "init = function(c)\n", "init = function(c)\n    error('init boom')\n",
            BOAT_PLUGIN_RUNTIME, "init boom" },
    };
    for (unsigned i = 0; i < sizeof(failures) / sizeof(failures[0]); ++i) {
        char broken[BOAT_PLUGIN_SOURCE_MAX];
        memcpy(broken, tour, tour_size + 1);
        size_t broken_size = tour_size;
        CHECK(replace_once(
            broken, &broken_size, sizeof(broken) - 1, failures[i].from, failures[i].to));
        CHECK(!module_edit(directory, "tour", broken, broken_size, edited));
        CHECK(stage(host, edited) == -1);
        CHECK(host->diagnostic.status == failures[i].status);
        CHECK(!strcmp(host->diagnostic.file, "modules/tour.lua") && host->diagnostic.line > 0);
        CHECK(!failures[i].message || strstr(host->diagnostic.message, failures[i].message));
        CHECK(!host->held && host->numbered == numbered && host->epoch == epoch);
        CHECK(boat_plugin_host_active(host)->number == 1
            && !memcmp(boat_plugin_host_active(host)->source, files->source, files->source_size));
        // The in-process path refuses it the same way.
        BoatPluginBundle broken_bundle = boat_plugin_bundle(edited);
        CHECK(!boat_plugin_host_stage(host, &broken_bundle) && !host->held);
        CHECK(!strcmp(host->diagnostic.file, "modules/tour.lua"));
    }

    // Manifest and image disagree: the worker and the host both reject, naming plugin.json.
    BoatPluginFiles *bad = reference;
    *bad = *files;
    CHECK(replace_once(
        bad->manifest, &bad->manifest_size, sizeof(bad->manifest), "\"tour\"", "\"tou\""));
    CHECK(stage(host, bad) == -1 && host->diagnostic.status == BOAT_PLUGIN_MANIFEST);
    CHECK(!strcmp(host->diagnostic.file, "plugin.json")
        && !strcmp(host->diagnostic.field, "modules"));
    BoatPluginBundle bad_bundle = boat_plugin_bundle(bad);
    CHECK(!boat_plugin_host_stage(host, &bad_bundle) && !host->held);
    BoatPluginVoyage scratch;
    CHECK(boat_plugin_load_voyage(&bad_bundle, NULL, &scratch, &d) == BOAT_PLUGIN_MANIFEST);
    memcpy(bad->source, ENTRY, sizeof(ENTRY) - 1);
    bad->source_size = sizeof(ENTRY) - 1;
    bad->manifest_size = files->manifest_size;
    memcpy(bad->manifest, files->manifest, files->manifest_size);
    CHECK(stage(host, bad) == -1 && strstr(host->diagnostic.message, "not a module image"));
    BoatPluginFiles *raw = calloc(1, sizeof(*raw));
    CHECK(raw && boat_plugin_read("plugins/shelter-tour", NULL, raw, &d) == BOAT_PLUGIN_OK);
    memcpy(raw->source, files->source, files->source_size);
    raw->source_size = files->source_size; // The single-file manifest with an image source.
    CHECK(stage(host, raw) == -1 && strstr(host->diagnostic.message, "declares no modules"));
    free(raw);

    // A non-objective package: a module edit that changes no decoded data is UNCHANGED, exactly
    // as an entry edit is (nothing to install); one that changes data is a new revision.
    BoatPluginBundle plain_bundle;
    CHECK(boat_plugin_read(plain, NULL, files, &d) == BOAT_PLUGIN_OK);
    plain_bundle = boat_plugin_bundle(files);
    BoatPluginHost *second = calloc(1, sizeof(*second));
    CHECK(second
        && boat_plugin_host_start(second, resources.definition, &resources.map, &plain_bundle));
    CHECK(boat_plugin_host_active(second)->source_size == 0); // Source is kept for objectives only.
    char commented[BOAT_PLUGIN_SOURCE_MAX];
    snprintf(commented, sizeof(commented), "-- a module-only comment\n%s", route);
    CHECK(!module_edit(plain, "route", commented, strlen(commented), edited));
    CHECK(stage(second, edited) == 1 && second->change == BOAT_PLUGIN_UNCHANGED && !second->held);
    // The single-file twin behaves identically for an entry-only edit.
    CHECK(boat_plugin_read("plugins/langedrag", NULL, reference, &d) == BOAT_PLUGIN_OK);
    BoatPluginBundle single_bundle = boat_plugin_bundle(reference);
    BoatPluginHost *twin = calloc(1, sizeof(*twin));
    CHECK(
        twin && boat_plugin_host_start(twin, resources.definition, &resources.map, &single_bundle));
    snprintf(reference->source, sizeof(reference->source), "-- an entry-only comment\n%s", route);
    reference->source_size = strlen(reference->source);
    CHECK(stage(twin, reference) == 1 && twin->change == BOAT_PLUGIN_UNCHANGED && !twin->held);
    char moved[BOAT_PLUGIN_SOURCE_MAX];
    memcpy(moved, route, route_size + 1);
    size_t moved_size = route_size;
    CHECK(replace_once(moved, &moved_size, sizeof(moved) - 1, "radius = 75", "radius = 80"));
    CHECK(!module_edit(plain, "route", moved, moved_size, edited));
    CHECK(stage(second, edited) == 1 && second->change != BOAT_PLUGIN_UNCHANGED && second->held);
    free(twin);
    free(second);

    // Queries carry the image to the worker too.
    CHECK(boat_plugin_read(directory, NULL, files, &d) == BOAT_PLUGIN_OK);
    char text[2048];
    BoatPluginStageJob *job = boat_plugin_query_start(worker_path, files->manifest,
        files->manifest_size, files->source, files->source_size, "def.voyage.shelters[1].name", 0);
    CHECK(job);
    BoatPluginDiagnostic qd = { 0 };
    int answer;
    while (!(answer = boat_plugin_query_poll(job, text, sizeof(text), &qd, .25))) { }
    CHECK(answer == 1 && strstr(text, "DOCK"));
    boat_plugin_stage_close(job);
    job = boat_plugin_query_start(worker_path, files->manifest, files->manifest_size, ENTRY,
        sizeof(ENTRY) - 1, "def.voyage.shelters[1].name", 0);
    CHECK(job);
    while (!(answer = boat_plugin_query_poll(job, text, sizeof(text), &qd, .25))) { }
    CHECK(answer == -1 && strstr(qd.message, "not a module image"));
    boat_plugin_stage_close(job);

    // The game's --plugin glue opens the directory and saves it back with its modules (a save
    // writes the entry and each module file, not the image as main.lua).
    CHECK(!module_edit(directory, "tour", tour, tour_size, edited)); // The edit 1 text again.
    CHECK(boat_plugin_read(directory, NULL, files, &d) == BOAT_PLUGIN_OK);
    WakePlugin *plugin = calloc(1, sizeof(*plugin));
    CHECK(plugin);
    BoatSession session;
    boat_session_init(&session, &resources, (BoatWater) { 0 });
    CHECK(wake_plugin_open(plugin, directory, &session, 0));
    plugin->watching = false;
    CHECK(boat_plugin_host_active(&plugin->host)->source_size == files->source_size);
    char saved[600], message[200];
    snprintf(saved, sizeof(saved), "%s/saved", BOAT_PLUGIN_MODULES_DIR);
    static const char *const leftovers[]
        = { "plugin.json", "main.lua", "modules/tour.lua", "modules" };
    for (unsigned i = 0; i < sizeof(leftovers) / sizeof(leftovers[0]); ++i) {
        char old[700];
        snprintf(old, sizeof(old), "%s/%s", saved, leftovers[i]);
        remove(old); // A previous run's save; the scratch directory persists.
    }
    bool saved_ok = wake_plugin_save(plugin, saved, message, sizeof(message));
    if (!saved_ok)
        fprintf(stderr, "%s\n", message);
    CHECK(saved_ok);
    BoatPluginFiles *again = calloc(1, sizeof(*again));
    CHECK(again && boat_plugin_read(saved, NULL, again, &d) == BOAT_PLUGIN_OK);
    CHECK(again->source_size == files->source_size
        && !memcmp(again->source, files->source, files->source_size));
    CHECK(boat_plugin_image_is(again->source, again->source_size));
    // A catalog-style save regenerates plugin.json from the parsed manifest; it keeps the modules.
    plugin->authoring = false;
    snprintf(saved, sizeof(saved), "%s/regenerated", BOAT_PLUGIN_MODULES_DIR);
    MAKE_DIRECTORY(saved);
    for (unsigned i = 0; i < sizeof(leftovers) / sizeof(leftovers[0]); ++i) {
        char old[700];
        snprintf(old, sizeof(old), "%s/%s", saved, leftovers[i]);
        remove(old);
    }
    CHECK(wake_plugin_save(plugin, saved, message, sizeof(message)));
    CHECK(boat_plugin_read(saved, NULL, again, &d) == BOAT_PLUGIN_OK);
    CHECK(strstr(again->manifest, "\"modules\":[\"tour\"]")
        && boat_plugin_image_is(again->source, again->source_size)
        && again->source_size == files->source_size
        && !memcmp(again->source, files->source, files->source_size));
    plugin->authoring = true;
    // The console wraps the entry as text, which a module image has no form of: it says so.
    static WakeCommand command;
    wake_command_init(&command, collect, console_clear, console_save, NULL);
    CHECK(!wake_command_submit(&command, plugin, &session, "def.voyage = nil", 0));
    CHECK(strstr(console_line, "HAS MODULES") && !plugin->staging);
    console_line[0] = 0;
    CHECK(!wake_command_submit(&command, plugin, &session, "= def.voyage.shelters[1].name", 0));
    CHECK(strstr(console_line, "HAS MODULES") && !plugin->query);
    free(again);
    boat_session_destroy(&session);
    free(plugin);

    boat_world_resources_destroy(&resources);
    free(host);
    free(reference);
    free(edited);
    free(files);
    puts("package modules: image carried by read, worker, host, query; module-only edits staged, "
         "applied, undone; failures name the module; manifest/image mismatches rejected");
    return 0;
}
