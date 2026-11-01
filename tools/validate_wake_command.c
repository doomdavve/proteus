// PC (docs/PLUGIN_DEFAULT_PLAN.md): headless acceptance for the live console's command layer.
// Every case uses a virtual clock and the real isolated worker, spawned from this test binary.
#include "boat_plugin_catalog.h"
#include "boat_plugin_lua.h"
#include "boat_plugin_stage.h"
#include "boat_session.h"
#include "wake_command.h"
#include "wake_console.h"
#include "wake_velocity_plugin.h"
#include "waterfront_harbor.h"

#include "raylib.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#else
#include <unistd.h>
#endif

static unsigned failures;

#define CHECK(condition, ...)                                                                      \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);                                   \
            fprintf(stderr, __VA_ARGS__);                                                          \
            fputc('\n', stderr);                                                                   \
            ++failures;                                                                            \
        }                                                                                          \
    } while (0)

typedef struct {
    char lines[32][192];
    unsigned count;
} Output;

static void collect(void *context, const char *line)
{
    Output *output = context;
    if (output->count < 32)
        snprintf(output->lines[output->count++], sizeof(output->lines[0]), "%s", line);
}

static void clear_output(void *context)
{
    Output *output = context;
    output->count = 0;
}

static bool saved(
    void *context, const WakePlugin *plugin, const char *directory, char *message, size_t capacity)
{
    (void)context;
    return wake_plugin_save(plugin, directory, message, capacity);
}

static bool has_line(const Output *output, const char *text)
{
    for (unsigned i = 0; i < output->count; ++i)
        if (strstr(output->lines[i], text))
            return true;
    return false;
}

static void test_console(void)
{
    WakeConsole console;
    wake_console_init(&console);
    console.open = true;
    char line[WAKE_CONSOLE_INPUT];
    const char *text = "def.scene = nil";
    for (const char *c = text; *c; ++c)
        CHECK(!wake_console_input(&console, 0, *c, line, sizeof(line)), "char submitted");
    CHECK(!strcmp(console.input, text), "console input is '%s'", console.input);
    CHECK(wake_console_input(&console, KEY_ENTER, 0, line, sizeof(line)), "Enter submits");
    CHECK(!strcmp(line, text), "submitted '%s'", line);
    CHECK(console.history_count == 1 && !console.length, "history recorded and input cleared");
    CHECK(!wake_console_input(&console, KEY_UP, 0, line, sizeof(line)), "history recall");
    CHECK(!strcmp(console.input, text), "recalled input");
    wake_console_print(&console, "one");
    wake_console_print(&console, "two");
    CHECK(console.scroll_count == 2, "scrollback has 2 lines");
    wake_console_clear(&console);
    CHECK(console.scroll_count == 0, "clear empties scrollback");
    console.open = true;
    CHECK(wake_console_input(&console, KEY_ESCAPE, 0, line, sizeof(line)) == false && !console.open,
        "Escape closes");
    CHECK(wake_console_open_key(KEY_GRAVE) && wake_console_open_key(KEY_F2)
            && wake_console_open_key(161),
        "opening keys");
}

static void test_source(void)
{
    WakeCommand command;
    wake_command_init(&command, NULL, NULL, NULL, NULL);
    snprintf(command.chunks[0], sizeof(command.chunks[0]), "def.scene.marina.berths[1].x = 15");
    snprintf(command.chunks[1], sizeof(command.chunks[1]), "def.scene.gulls.flocks[1].count = 4");
    command.chunk_count = 2;
    const char *base = "return { scene = {} }\n";
    char source[BOAT_PLUGIN_SOURCE_MAX];
    size_t size = wake_command_source(&command, base, strlen(base), source, sizeof(source));
    CHECK(size > 0, "composition");
    CHECK(strstr(source, "local def = (function()") == source, "opens the base function");
    CHECK(strstr(source, "do def.scene.marina.berths[1].x = 15 end -- console 1"),
        "first chunk attributed");
    CHECK(strstr(source, "do def.scene.gulls.flocks[1].count = 4 end -- console 2"),
        "second chunk attributed");
    CHECK(strstr(source, "return def\n"), "returns def");
    CHECK(strstr(source, "end)()"), "closes the base function");
}

static const BoatPluginCatalogEntry *open_catalog(
    const char *id, BoatWorldResources *worlds, WakePlugin *out, BoatSession *target)
{
    const BoatPluginCatalogEntry *entry = boat_plugin_catalog_find(id);
    CHECK(entry != NULL, "catalog entry %s", id);
    if (!entry)
        return NULL;
    CHECK(boat_world_resources_init(worlds, boat_location(BOAT_LOCATION_OUTER)), "outer resources");
    boat_session_init(target, worlds,
        boat_world_test_water(boat_location(BOAT_LOCATION_OUTER), false, false, false));
    CHECK(wake_plugin_open_catalog(out, entry, target), "open %s: %s", id,
        out->host.diagnostic.message);
    return entry;
}

// Run the staging request to completion without reading the wall clock. The child process takes
// real time but no simulated time; the bounded count is a failure deadline, not an assertion.
static bool pump(WakePlugin *out, BoatSession *target, double now, unsigned limit)
{
    for (unsigned i = 0; i < limit; ++i) {
        wake_plugin_frame(out, target, false, false, false, now);
        if (!out->staging)
            return true;
    }
    return false;
}

static uint64_t trace(WakePlugin *out, BoatSession *target, unsigned ticks, bool edit, double *now,
    const char *edit_line, WakeCommand *command)
{
    uint64_t hash = 14695981039346656037ull;
    BoatInput input = { .throttle_rate = .4f, .steering_rate = .1f };
    for (unsigned tick = 1; tick <= ticks; ++tick) {
        if (edit && tick == 20) {
            wake_command_submit(command, out, target, edit_line, *now);
            CHECK(command->pending, "edit submission");
            if (out->staging)
                pump(out, target, *now, 200000000);
            wake_command_frame(command, out, target, *now);
        }
        wake_plugin_before_tick(out);
        if (!boat_session_step(target, input)) {
            ++failures;
            break;
        }
        wake_plugin_after_tick(out, target);
        if (command)
            wake_command_frame(command, out, target, *now);
        double values[] = { target->boat.position.x, target->boat.position.y, target->boat.heading,
            (double)target->voyage.shelter };
        for (unsigned i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
            uint64_t bits;
            memcpy(&bits, &values[i], sizeof(bits));
            hash ^= bits;
            hash *= 1099511628211ull;
        }
        *now += .01;
    }
    return hash;
}

static void test_live_scene(const char *executable)
{
    // The key proof: one session gets a berth edit far from the boat, the control gets none.
    // Boat and voyage traces stay identical tick for tick, and the edit is visible in the scene.
    static BoatWorldResources resource_a, resource_b, resource_c;
    static WakePlugin edited, control, repeat;
    static BoatSession a, b, r;
    if (!open_catalog("proteus.harbor-life", &resource_a, &edited, &a)
        || !open_catalog("proteus.harbor-life", &resource_b, &control, &b)
        || !open_catalog("proteus.harbor-life", &resource_c, &repeat, &r))
        return;
    static WakeCommand edit_command, idle_command, repeat_command;
    Output output = { 0 }, repeat_output = { 0 };
    wake_command_init(&edit_command, collect, clear_output, NULL, &output);
    wake_command_init(&idle_command, collect, clear_output, NULL, &output);
    wake_command_init(&repeat_command, collect, clear_output, NULL, &repeat_output);
    edited.stage_executable = executable;
    repeat.stage_executable = executable;
    double now = 0;
    uint64_t first
        = trace(&edited, &a, 120, true, &now, "def.scene.marina.berths[1].x = 11", &edit_command);
    pump(&edited, &a, now, 200000000);
    wake_command_frame(&edit_command, &edited, &a, now);
    double control_now = 0;
    uint64_t both = trace(&control, &b, 120, false, &control_now, NULL, &idle_command);
    CHECK(first == both, "live edit kept the boat and voyage trace identical: %016llx vs %016llx",
        (unsigned long long)first, (unsigned long long)both);
    printf("live edit trace %016llx\n", (unsigned long long)first);
    CHECK(edited.host.depth && boat_plugin_host_active(&edited.host)->voyage.has_scene,
        "edited session has a scene");
    if (edited.host.depth) {
        const BoatSceneDefinition *scene = &boat_plugin_host_active(&edited.host)->voyage.scene;
        CHECK(scene->berths_present && scene->berths[0].x == 11, "berth moved to x = %g",
            scene->berths[0].x);
    }
    CHECK(control.host.depth
            && boat_plugin_host_active(&control.host)->voyage.scene.berths[0].x == 13,
        "the control berth did not move");
    CHECK(has_line(&output, "CONSOLE 1"), "the console reported the edit");

    // The same edit at the same tick gives the same hash.
    double repeat_now = 0;
    uint64_t again = trace(
        &repeat, &r, 120, true, &repeat_now, "def.scene.marina.berths[1].x = 11", &repeat_command);
    pump(&repeat, &r, repeat_now, 200000000);
    CHECK(again == first,
        "repeating the edit at the same tick hashes identically: %016llx vs "
        "%016llx",
        (unsigned long long)first, (unsigned long long)again);

    // Moving a berth under the boat holds the edit and never displaces the boat. The edit
    // commits at the first clear boundary after the boat leaves.
    double held_now = now + 1;
    a.boat.position = (Vector2) { 13, 151 };
    a.boat.heading = 0;
    const BoatSceneDefinition *before = &boat_plugin_host_active(&edited.host)->voyage.scene;
    float heading_before = before->berths[1].heading;
    CHECK(wake_command_submit(
              &edit_command, &edited, &a, "def.scene.marina.berths[2].heading = -1.45", held_now)
            == false,
        "held edit submission");
    CHECK(pump(&edited, &a, held_now, 200000000), "held edit staged");
    wake_command_frame(&edit_command, &edited, &a, held_now);
    CHECK(boat_plugin_host_held(&edited.host) != NULL, "the edit is held under the boat");
    CHECK(boat_plugin_host_active(&edited.host)->voyage.scene.berths[1].heading == heading_before,
        "the active scene is unchanged while held");
    a.boat.position = (Vector2) { 0, 100 };
    CHECK(pump(&edited, &a, held_now, 200000000), "held edit commits when clear");
    CHECK(boat_plugin_host_held(&edited.host) == NULL, "the held edit committed");
    CHECK(a.boat.position.x == 0 && a.boat.position.y == 100, "the boat was not displaced");
    wake_command_frame(&edit_command, &edited, &a, held_now);
    CHECK(boat_plugin_host_active(&edited.host)->voyage.scene.berths[1].heading == -1.45f,
        "the new heading is active");

    // Undo restores the prior scene with the boat kept; consumers read the reverted envelope.
    Vector2 contact = { 13, 151 };
    Vector2 normal;
    CHECK(
        waterfront_harbor_contact_height_scene(&boat_plugin_host_active(&edited.host)->voyage.scene,
            contact, .1f, -1, 3, a.world.water, 0, &normal),
        "the edited envelope is a contact");
    wake_plugin_frame(&edited, &a, false, true, false, held_now + 1);
    CHECK(boat_plugin_host_active(&edited.host)->voyage.scene.berths[1].heading == heading_before,
        "Undo restored the prior heading");
    CHECK(a.boat.position.x == 0 && a.boat.position.y == 100, "Undo kept the boat");
    wake_command_frame(&edit_command, &edited, &a, held_now + 1);
    CHECK(edit_command.chunk_count == 1, "Undo stepped the chunks back with the revision");

    // Saving writes a complete package that reads back.
    char message[256];
    remove(WAKE_COMMAND_TEST_DIR "/saved/plugin.json");
    remove(WAKE_COMMAND_TEST_DIR "/saved/main.lua");
    CHECK(wake_plugin_save(&edited, WAKE_COMMAND_TEST_DIR "/saved", message, sizeof(message)),
        "save failed: %s", message);
    BoatPluginFiles files;
    BoatPluginDiagnostic diagnostic;
    CHECK(boat_plugin_read(WAKE_COMMAND_TEST_DIR "/saved", NULL, &files, &diagnostic)
            == BOAT_PLUGIN_OK,
        "saved package reads back: %s", diagnostic.message);
    CHECK(files.source_size == boat_plugin_host_active(&edited.host)->source_size,
        "saved source is the active revision");

    // A query runs in the isolated worker, renders a bounded value and creates no revision.
    unsigned revisions = edited.host.numbered;
    Output query_output = { 0 };
    static WakeCommand query_command;
    wake_command_init(&query_command, collect, clear_output, NULL, &query_output);
    CHECK(wake_command_submit(
              &query_command, &edited, &a, "= def.voyage.shelters[1].name", held_now + 2)
            == false,
        "query submission");
    double query_now = held_now + 2;
    for (unsigned i = 0; i < 200000000 && query_command.query_pending; ++i)
        wake_command_frame(&query_command, &edited, &a, query_now);
    CHECK(!query_command.query_pending, "query completed");
    CHECK(has_line(&query_output, "DOCK"), "query rendered the shelter name");
    CHECK(edited.host.numbered == revisions, "a query created no revision");

    // A malformed edit is rejected and leaves the active revision running.
    unsigned active_number = boat_plugin_host_active(&edited.host)->number;
    CHECK(wake_command_submit(
              &edit_command, &edited, &a, "def.scene.marina.berths[1].x = ", query_now + 1)
            == false,
        "broken edit submission");
    CHECK(pump(&edited, &a, query_now, 200000000), "broken edit staged");
    wake_command_frame(&edit_command, &edited, &a, query_now);
    CHECK(has_line(&output, "REJECTED"), "the console reported the rejection");
    CHECK(boat_plugin_host_active(&edited.host)->number == active_number,
        "the active revision survived the broken edit");

    // A second edit while one is staging is refused rather than queued behind it.
    CHECK(!wake_command_submit(
              &edit_command, &edited, &a, "def.scene.marina.berths[1].x = 12", query_now + 2),
        "first queued edit");
    CHECK(edit_command.pending, "the first edit is staging");
    CHECK(!wake_command_submit(
              &edit_command, &edited, &a, "def.scene.marina.berths[1].x = 13", query_now + 2),
        "second edit refused");
    CHECK(has_line(&output, "STAGING"), "the refusal names the staging state");
    CHECK(pump(&edited, &a, query_now + 2, 20000000), "queued edit staged");
    wake_command_frame(&edit_command, &edited, &a, query_now + 2);

    // Every edit and query above ran in the isolated worker: this process never opened a Lua VM.
    CHECK(boat_plugin_lua_states() == 0, "plain play and the console create no game-thread Lua");

    wake_plugin_close(&edited, &a);
    wake_plugin_close(&control, &b);
    wake_plugin_close(&repeat, &r);
    boat_session_destroy(&a);
    boat_session_destroy(&b);
    boat_session_destroy(&r);
    boat_world_resources_destroy(&resource_a);
    boat_world_resources_destroy(&resource_b);
    boat_world_resources_destroy(&resource_c);
}

static void test_commands(const char *executable)
{
    BoatWorldResources worlds = { 0 };
    static WakePlugin plugin;
    static BoatSession session;
    if (!open_catalog("proteus.langedrag", &worlds, &plugin, &session))
        return;
    plugin.stage_executable = executable;
    Output output = { 0 };
    static WakeCommand command;
    wake_command_init(&command, collect, clear_output, saved, &output);
    CHECK(wake_command_submit(&command, &plugin, &session, "/capabilities", 0) == false,
        "capabilities");
    CHECK(has_line(&output, "voyage.define"), "capabilities lists the voyage grant");
    output.count = 0;
    CHECK(wake_command_submit(&command, &plugin, &session, "/recipes", 0) == false, "recipes");
    CHECK(has_line(&output, "skiff"), "recipes lists registered hulls");
    output.count = 0;
    CHECK(wake_command_submit(&command, &plugin, &session, "/anchors", 0) == false, "anchors");
    CHECK(has_line(&output, "LANGEDRAG"), "anchors lists the destination");
    output.count = 0;
    CHECK(wake_command_submit(&command, &plugin, &session, "/status", 0) == false, "status");
    CHECK(has_line(&output, "CHUNKS 0"), "status reports the chunk count");
    CHECK(wake_command_submit(&command, &plugin, &session, "/nonsense", 0) == false, "unknown");
    CHECK(has_line(&output, "UNKNOWN COMMAND"), "unknown command reported");
    CHECK(wake_command_submit(&command, &plugin, &session, "/restart", 0), "restart requested");
    // Granting scene.define to a package without it changes only the session's manifest copy.
    CHECK(wake_command_submit(&command, &plugin, &session, "/capability scene.define", 0) == false,
        "grant");
    CHECK(has_line(&output, "GRANTED scene.define"), "grant reported");
    CHECK(plugin.authoring, "a grant promotes the session");
    const char *manifest;
    size_t size = wake_plugin_manifest(&plugin, &manifest);
    CHECK(size && strstr(manifest, "scene.define"), "the manifest copy carries the grant");
    wake_plugin_close(&plugin, &session);
    boat_session_destroy(&session);
    boat_world_resources_destroy(&worlds);
}

// Replace TEXT once in BYTES of CAPACITY, holding SIZE bytes.
static void edit_bytes(char *bytes, size_t *size, size_t capacity, const char *text, const char *to)
{
    char *at = strstr(bytes, text);
    CHECK(at != NULL, "edit target %s", text);
    if (!at)
        return;
    size_t tail = *size - (size_t)(at - bytes) - strlen(text);
    CHECK(*size - strlen(text) + strlen(to) < capacity, "edit fits");
    memmove(at + strlen(to), at + strlen(text), tail + 1);
    memcpy(at, to, strlen(to));
    *size = *size - strlen(text) + strlen(to);
}

static void step_ticks(BoatSession *target, unsigned ticks)
{
    for (unsigned i = 0; i < ticks; ++i)
        CHECK(boat_session_step(target, (BoatInput) { 0 }), "tick %u", i);
}

// A revision's record is a union selected by kind: free and course revisions hold no route, so
// their edits must never start a voyage.
static void test_non_voyage_revisions(void)
{
    static WakePlugin plugin;
    static BoatSession session;
    static BoatPluginFiles files;
    BoatWorldResources worlds = { 0 };
    const char *directories[] = { "plugins/basin-free", "plugins/time-trial" };
    for (unsigned kind = 0; kind < 2; ++kind) {
        const BoatWorldDefinition *location = wake_plugin_location(directories[kind]);
        CHECK(location && boat_world_resources_init(&worlds, location), "%s", directories[kind]);
        boat_session_init(&session, &worlds, boat_world_test_water(location, false, false, false));
        CHECK(wake_plugin_open(&plugin, directories[kind], &session, 0), "open %s: %s",
            directories[kind], plugin.host.diagnostic.message);
        bool course = plugin.host.kind == BOAT_PLUGIN_KIND_COURSE;
        CHECK(course == (kind == 1), "kind %u", plugin.host.kind);
        if (course)
            boat_session_start_course(&session);

        files = plugin.files;
        files.manifest[files.manifest_size] = 0;
        files.source[files.source_size] = 0;
        edit_bytes(files.manifest, &files.manifest_size, sizeof(files.manifest), "\"name\": \"",
            "\"name\": \"Renamed ");
        wake_plugin_stage_files(&plugin, &session, &files, 1);
        CHECK(boat_plugin_host_active(&plugin.host)->number == 2, "rename applies now");
        CHECK(!session.voyage.route && !session.voyage_route, "rename starts no voyage");
        step_ticks(&session, 10);
        boat_session_recover(&session);
        step_ticks(&session, 10);
        if (course) {
            const BoatCourseDefinition *bound
                = &boat_plugin_host_active(&plugin.host)->course.course;
            CHECK(session.course_definition == bound && session.course.definition == bound,
                "rename follows the active revision");

            edit_bytes(files.source, &files.source_size, sizeof(files.source), "half_width = 12",
                "half_width = 13");
            wake_plugin_stage_files(&plugin, &session, &files, 2);
            CHECK(boat_plugin_host_held(&plugin.host), "a course edit is held");
            CHECK(wake_plugin_frame(&plugin, &session, false, false, true, 2), "apply restarts");
            boat_session_restart(&session, session.world.water);
            CHECK(session.course.enabled && session.course.definition == session.course_definition,
                "restart keeps the course");
            CHECK(session.course.definition->gates[0].half_width == 13, "restart uses the edit");
            CHECK(!session.voyage.route && !session.voyage_route, "restart starts no voyage");
            step_ticks(&session, 10);
            CHECK(boat_course_digest(session.course_definition) != BOAT_COURSE_STANDARD_DIGEST,
                "the edit is another course");

            wake_plugin_frame(&plugin, &session, false, true, false, 3);
            if (boat_plugin_host_held(&plugin.host))
                CHECK(wake_plugin_frame(&plugin, &session, false, false, true, 3), "undo restarts");
            boat_session_restart(&session, session.world.water);
            CHECK(boat_course_digest(session.course_definition) == BOAT_COURSE_STANDARD_DIGEST,
                "undo returns to the standard course");
            step_ticks(&session, 10);
        }
        wake_plugin_close(&plugin, &session);
        CHECK(!session.voyage.route && !session.course_definition, "close leaves no borrower");
        boat_session_destroy(&session);
        boat_world_resources_destroy(&worlds);
    }
}

int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "--plugin-stage-worker"))
        return boat_plugin_stage_worker();
    if (argc == 2 && !strcmp(argv[1], "--plugin-query-worker"))
        return boat_plugin_query_worker();
    test_console();
    test_source();
    test_commands(argv[0]);
    test_live_scene(argv[0]);
    test_non_voyage_revisions();
    if (failures) {
        fprintf(stderr, "%u wake_command failure(s)\n", failures);
        return 1;
    }
    puts("wake_command=ok");
    return 0;
}
