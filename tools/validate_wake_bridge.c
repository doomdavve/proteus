#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
// P4.6 live tool bridge: authentication, revision/epoch pins, deferred isolated staging, Apply,
// Undo and Discard through request files on a virtual clock. --serve DIR runs the same bridge
// over a still session for tools/test_plugin_author.py's end-to-end client checks.
#include "wake_velocity_bridge.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <sys/stat.h>
#endif

#define CHECK(test)                                                                                \
    do {                                                                                           \
        if (!(test)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #test);                             \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static const char *directory = BOAT_BRIDGE_TEST_DIR "/mailbox";
static char token[64];
static char response[32768];
static BoatPluginFiles reference;

static void join(char *out, size_t size, const char *name)
{
    snprintf(out, size, "%s/%s", directory, name);
}

static bool read_file(const char *name, char *out, size_t size)
{
    char path[1024];
    join(path, sizeof(path), name);
    FILE *input = fopen(path, "rb");
    if (!input)
        return false;
    size_t n = fread(out, 1, size - 1, input);
    fclose(input);
    out[n] = 0;
    return true;
}

// The value of KEY=... in TEXT, copied to a static buffer; "" when absent.
static const char *value(const char *text, const char *key)
{
    static char result[512];
    result[0] = 0;
    size_t length = strlen(key);
    for (const char *line = text; line && *line; line = strchr(line, '\n') + 1) {
        if (!strncmp(line, key, length) && line[length] == '=') {
            const char *end = strchr(line, '\n');
            size_t n = (size_t)(end - line) - length - 1;
            n = n < sizeof(result) - 1 ? n : sizeof(result) - 1;
            memcpy(result, line + length + 1, n);
            result[n] = 0;
            break;
        }
        if (!strchr(line, '\n'))
            break;
    }
    return result;
}

static bool is(const char *key, const char *expected)
{
    if (!strcmp(value(response, key), expected))
        return true;
    fprintf(stderr, "%s=%s, expected %s\n", key, value(response, key), expected);
    return false;
}

// Write a request as plugin_author.py does: complete in a temporary file, then visible.
static bool send(const char *id, const char *fields, const BoatPluginFiles *files)
{
    char temporary[1024], path[1024];
    join(temporary, sizeof(temporary), ".request-test");
    join(path, sizeof(path), "request");
    FILE *output = fopen(temporary, "wb");
    if (!output)
        return false;
    size_t payload = files ? 8 + files->manifest_size + files->source_size : 0;
    fprintf(output, "wake-velocity-bridge 1\nid=%s\ntoken=%s\n%s", id, token, fields);
    if (files) {
        fprintf(output, "payload=%zu\n\n", payload);
        size_t sizes[2] = { files->manifest_size, files->source_size };
        for (unsigned i = 0; i < 2; ++i)
            for (int shift = 24; shift >= 0; shift -= 8)
                fputc((int)(sizes[i] >> shift) & 255, output);
        fwrite(files->manifest, 1, files->manifest_size, output);
        fwrite(files->source, 1, files->source_size, output);
    } else {
        fputs("\n", output);
    }
    if (fclose(output))
        return false;
    remove(path);
    return !rename(temporary, path);
}

static bool answered(const char *id)
{
    char name[64];
    snprintf(name, sizeof(name), "response-%s", id);
    return read_file(name, response, sizeof(response));
}

// PC console: the payload is one line rather than a bundle.
static bool send_console(const char *id, const char *line, unsigned long long epoch)
{
    char temporary[1024], path[1024];
    join(temporary, sizeof(temporary), ".request-test");
    join(path, sizeof(path), "request");
    FILE *output = fopen(temporary, "wb");
    if (!output)
        return false;
    fprintf(output,
        "wake-velocity-bridge 1\nid=%s\ntoken=%s\noperation=console\nbase_epoch=%llu\n"
        "payload=%zu\n\n%s",
        id, token, epoch, strlen(line), line);
    if (fclose(output))
        return false;
    remove(path);
    return !rename(temporary, path);
}

// Reference source with one substring replaced, in its own buffer (four in all).
static BoatPluginFiles *variant(const char *from, const char *to)
{
    static BoatPluginFiles buffers[8];
    static unsigned used;
    if (used == 8)
        return NULL;
    BoatPluginFiles *files = &buffers[used++];
    *files = reference;
    char *at = strstr(files->source, from);
    if (!at)
        return NULL;
    size_t head = (size_t)(at - files->source), tail = files->source_size - head - strlen(from);
    memmove(at + strlen(to), at + strlen(from), tail);
    memcpy(at, to, strlen(to));
    files->source_size = head + strlen(to) + tail;
    return files;
}

// The game's order each frame: tool requests, then keys, saves and staging completion.
static bool frame(WakeBridge *bridge, WakePlugin *plugin, BoatSession *session, double now)
{
    bool restart = wake_bridge_frame(bridge, plugin, session, now);
    return wake_plugin_frame(plugin, session, false, false, false, now) || restart;
}

static int serve(WakePlugin *plugin, BoatSession *session, const char *stage)
{
    static WakeBridge bridge;
    plugin->stage_executable = stage;
    if (!wake_bridge_open(&bridge, directory, 0))
        return 1;
    static WakeCommand command;
    wake_command_init(&command, NULL, NULL, NULL, NULL);
    wake_bridge_commands(&bridge, &command);
    // The test writes DIR/stop to end. The clock advances one poll per pass except while
    // staging, whose virtual acceptance deadline must not expire under a fast loop.
    char path[1024];
    join(path, sizeof(path), "stop");
    double now = 0;
    for (;;) {
        if (frame(&bridge, plugin, session, now))
            boat_session_restart(session, (BoatWater) { 0 });
        if (!plugin->staging)
            now += BOAT_PLUGIN_POLL;
        FILE *stop = fopen(path, "rb");
        if (stop) {
            fclose(stop);
            remove(path);
            break;
        }
    }
    wake_bridge_close(&bridge);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "--plugin-stage-worker"))
        return boat_plugin_stage_worker();
    // The headless bridge server can host the combined harbor-life package for L4.3.
    const char *package = argc == 3 && !strcmp(argv[1], "--serve-harbor") ? "plugins/harbor-life"
                                                                          : "plugins/langedrag";
    BoatPluginDiagnostic d;
    CHECK(boat_plugin_read(package, NULL, &reference, &d) == BOAT_PLUGIN_OK);
    BoatWorldResources resources;
    CHECK(boat_world_resources_init(&resources, boat_location_find("outer")));
    static BoatSession session;
    boat_session_init(&session, &resources, (BoatWater) { 0 });
    session.presentation_enabled = false;
    static WakePlugin plugin;
    CHECK(wake_plugin_open(&plugin, package, &session, 0));
    plugin.watching = false; // Only bridge requests stage here.
    if (argc == 3 && (!strcmp(argv[1], "--serve") || !strcmp(argv[1], "--serve-harbor"))) {
        directory = argv[2];
        return serve(&plugin, &session, argv[0]);
    }
    static WakeBridge bridge;
    char path[1024];
    // Leftovers of an interrupted run; the mailbox itself is recreated below.
    static const char *const names[] = { "session", "request", "response-a2", "response-b1",
        "response-b2", "response-b3", "response-c1", "response-c2", "response-d1", "response-d2",
        "response-d3", "response-d4", "response-e1", "response-e2", "response-e3", "response-e4",
        "response-e5", "response-f1", "response-f2", "response-f3", "response-f4", "response-f5",
        "response-a3", "response-d5", "response-c3", "response-c4" };
    for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        join(path, sizeof(path), names[i]);
        remove(path);
    }
#ifndef _WIN32
    // A directory others can read is refused: its permissions are the authentication.
    remove(directory);
    CHECK(!mkdir(directory, 0755));
    CHECK(!wake_bridge_open(&bridge, directory, 0));
    CHECK(!chmod(directory, 0700));
#endif
    CHECK(wake_bridge_open(&bridge, directory, 0));
    static WakeCommand command;
    wake_command_init(&command, NULL, NULL, NULL, NULL);
    wake_bridge_commands(&bridge, &command);
    char session_text[512];
    CHECK(read_file("session", session_text, sizeof(session_text)));
    snprintf(token, sizeof(token), "%s", value(session_text, "token"));
    CHECK(strlen(token) == 32);
    double now = 0;

    // A wrong token is refused without owner state; polls follow BOAT_PLUGIN_POLL.
    char good[64];
    snprintf(good, sizeof(good), "%s", token);
    snprintf(token, sizeof(token), "%s", "0123456789abcdef0123456789abcdef");
    CHECK(send("a1", "operation=inspect\n", NULL));
    CHECK(!frame(&bridge, &plugin, &session, now));
    CHECK(answered("a1") && is("status", "unauthorized") && !value(response, "revision")[0]);
    snprintf(token, sizeof(token), "%s", good);
    CHECK(send("a2", "operation=inspect\n", NULL));
    CHECK(!frame(&bridge, &plugin, &session, now + .1));
    CHECK(!answered("a2")); // Not yet due.
    CHECK(!frame(&bridge, &plugin, &session, now += .25));
    CHECK(answered("a2") && is("status", "ok") && is("revision", "1") && is("epoch", "0"));
    CHECK(is("plugin", "proteus.langedrag") && is("staging", "0") && is("held_revision", "0"));
    CHECK(strstr(response, "\ninspect=revision=1 ") && is("tick", "0") && is("shelter", "0"));
    CHECK(!read_file("response-a1", path, sizeof(path))); // Superseded responses are removed.

    // PC: the console operation runs the same command layer and answers with its lines.
    CHECK(send_console("c3", "/capabilities", 0));
    CHECK(!frame(&bridge, &plugin, &session, now += .25));
    CHECK(answered("c3") && is("status", "ok"));
    CHECK(strstr(response, "\noutput=voyage.define"));
    CHECK(send_console("c4", "/status", 99));
    CHECK(!frame(&bridge, &plugin, &session, now += .25));
    CHECK(answered("c4") && is("status", "stale"));

    // Pins: a mutation without them is malformed; a stale one changes nothing.
    BoatPluginFiles *rename = variant("name = \"BRIDGE\"", "name = \"ALVSBORG\"");
    CHECK(rename && send("b1", "operation=stage\nbase_revision=1\n", rename));
    CHECK(!frame(&bridge, &plugin, &session, now += .25));
    CHECK(answered("b1") && is("status", "bad_request"));
    CHECK(send("b2", "operation=stage\nbase_revision=1\nbase_epoch=4\n", rename));
    CHECK(!frame(&bridge, &plugin, &session, now += .25));
    CHECK(answered("b2") && is("status", "stale") && is("revision", "1"));
    CHECK(send("b3", "operation=stage\nbase_revision=1\nbase_epoch=0\npayload=3\n\nabc", NULL));
    CHECK(!frame(&bridge, &plugin, &session, now += .25));
    CHECK(answered("b3") && is("status", "bad_request"));

    // Isolated staging answers when it finishes; a rename applies at once.
    plugin.stage_executable = argv[0];
    CHECK(send("c1", "operation=stage\nbase_revision=1\nbase_epoch=0\n", rename));
    CHECK(!frame(&bridge, &plugin, &session, now += .25));
    CHECK(plugin.staging && !answered("c1"));
    CHECK(boat_session_step(&session, (BoatInput) { .throttle_rate = 1 })); // World keeps going.
    CHECK(send("c2", "operation=undo\nbase_revision=1\nbase_epoch=0\n", NULL));
    CHECK(!wake_bridge_frame(&bridge, &plugin, &session, now += .25));
    CHECK(answered("c2") && is("status", "busy"));
    while (plugin.staging) // Process completion is external; CTest supplies the deadline.
        CHECK(!frame(&bridge, &plugin, &session, now));
    CHECK(!frame(&bridge, &plugin, &session, now));
    CHECK(answered("c1") && is("status", "ok") && is("outcome", "applied"));
    CHECK(is("revision", "2") && is("epoch", "1") && is("tick", "1"));
    CHECK(!strcmp(session.voyage.route->shelters[1].name, "ALVSBORG"));

    // PC: a route change installs live, keeping the boat and progress.
    plugin.stage_executable = NULL; // Synchronous trusted staging answers at once.
    BoatPluginFiles *route = variant("radius = 150", "radius = 120");
    CHECK(route && send("d1", "operation=stage\nbase_revision=2\nbase_epoch=1\n", route));
    CHECK(!frame(&bridge, &plugin, &session, now += .25));
    CHECK(answered("d1") && is("outcome", "applied") && is("revision", "3"));
    CHECK(is("epoch", "2") && session.voyage.route->shelters[1].radius == 120);

    // A water change is held; Apply must name the held revision.
    BoatPluginFiles *water = variant("height = 1.6", "height = 1.8");
    CHECK(water && send("d2", "operation=stage\nbase_revision=3\nbase_epoch=2\n", water));
    CHECK(!frame(&bridge, &plugin, &session, now += .25));
    CHECK(answered("d2") && is("outcome", "held") && is("held_revision", "4"));
    CHECK(is("held_change", "restart_session") && is("revision", "3"));
    CHECK(send("d3", "operation=apply\nheld_revision=3\nbase_epoch=2\n", NULL));
    CHECK(!frame(&bridge, &plugin, &session, now += .25));
    CHECK(answered("d3") && is("status", "stale") && is("held_revision", "4"));
    CHECK(send("d4", "operation=undo\nbase_revision=3\nbase_epoch=2\n", NULL));
    CHECK(!frame(&bridge, &plugin, &session, now += .25));
    CHECK(answered("d4") && is("status", "conflict"));
    CHECK(send("d5", "operation=apply\nheld_revision=4\nbase_epoch=2\n", NULL));
    CHECK(frame(&bridge, &plugin, &session, now += .25));
    CHECK(answered("d5") && is("status", "ok") && is("outcome", "applied"));
    CHECK(is("revision", "4") && is("epoch", "3"));
    boat_session_restart(&session, (BoatWater) { 0 }); // As the game does on that answer.
    CHECK(session.voyage.route->exposure.height == 1.8f);

    // Undo holds R3 for the same restart rules; Discard drops it; a second Undo and Apply
    // reinstall it.
    CHECK(send("e1", "operation=undo\nbase_revision=4\nbase_epoch=3\n", NULL));
    CHECK(!frame(&bridge, &plugin, &session, now += .25));
    CHECK(answered("e1") && is("outcome", "held") && is("held_revision", "3"));
    CHECK(is("held_undo", "1"));
    CHECK(send("e2", "operation=discard\nbase_epoch=3\n", NULL));
    CHECK(!frame(&bridge, &plugin, &session, now += .25));
    CHECK(answered("e2") && is("outcome", "discarded") && is("held_revision", "0"));
    CHECK(send("e3", "operation=discard\nbase_epoch=3\n", NULL));
    CHECK(!frame(&bridge, &plugin, &session, now += .25));
    CHECK(answered("e3") && is("status", "conflict"));
    CHECK(send("e4", "operation=undo\nbase_revision=4\nbase_epoch=3\n", NULL));
    CHECK(!frame(&bridge, &plugin, &session, now += .25));
    CHECK(send("e5", "operation=apply\nheld_revision=3\nbase_epoch=3\n", NULL));
    CHECK(frame(&bridge, &plugin, &session, now += .25));
    CHECK(answered("e5") && is("outcome", "applied") && is("revision", "3") && is("epoch", "4"));
    boat_session_restart(&session, (BoatWater) { 0 });
    CHECK(session.voyage.route->shelters[1].radius == 120);
    CHECK(session.voyage.route->exposure.height == 1.6f);

    // A rejected candidate keeps the running revision and reports where it failed.
    BoatPluginFiles *broken = variant("max_speed = 6", "max_speed = 'six'");
    CHECK(broken && send("f1", "operation=stage\nbase_revision=3\nbase_epoch=4\n", broken));
    CHECK(!frame(&bridge, &plugin, &session, now += .25));
    CHECK(answered("f1") && is("outcome", "rejected") && is("revision", "3"));
    CHECK(is("diagnostic_field", "voyage.arrival.max_speed"));
    // A water change asks the caller to restart the session.
    BoatPluginFiles *calmer = variant("height = 1.6", "height = 1.2");
    CHECK(calmer && send("f2", "operation=stage\nbase_revision=3\nbase_epoch=4\n", calmer));
    CHECK(!frame(&bridge, &plugin, &session, now += .25));
    CHECK(answered("f2") && is("held_change", "restart_session"));
    CHECK(send("f3", "operation=apply\nheld_revision=5\nbase_epoch=4\n", NULL));
    CHECK(frame(&bridge, &plugin, &session, now += .25));
    CHECK(answered("f3") && is("outcome", "applied") && is("revision", "5"));
    boat_session_restart(&session, (BoatWater) { 0 }); // As the game does on that answer.
    CHECK(session.voyage.route->exposure.height == 1.2f);

    // A pending stage is cancelled by Discard and answered.
    plugin.stage_executable = argv[0];
    CHECK(send("f4", "operation=stage\nbase_revision=5\nbase_epoch=5\n", route));
    CHECK(!frame(&bridge, &plugin, &session, now += .25));
    CHECK(plugin.staging);
    CHECK(send("f5", "operation=discard\nbase_epoch=5\n", NULL));
    CHECK(!wake_bridge_frame(&bridge, &plugin, &session, now += .25));
    CHECK(answered("f5") && is("outcome", "canceled") && is("staging", "0"));
    CHECK(!frame(&bridge, &plugin, &session, now));
    CHECK(answered("f4") && is("status", "ok") && is("outcome", "canceled"));

    // Bytes the package already holds, but no poll has seen yet, are not staged again by
    // polling (which would drop a held Undo candidate as UNCHANGED).
    plugin.stage_executable = NULL;
    plugin.directory = BOAT_BRIDGE_TEST_DIR;
    for (unsigned i = 0; i < 2; ++i) {
        snprintf(path, sizeof(path), "%s/%s", BOAT_BRIDGE_TEST_DIR, i ? "main.lua" : "plugin.json");
        FILE *output = fopen(path, "wb");
        CHECK(output);
        if (i)
            fwrite(route->source, 1, route->source_size, output);
        else
            fwrite(route->manifest, 1, route->manifest_size, output);
        CHECK(!fclose(output));
    }
    plugin.watching = true;
    boat_plugin_watch_start(&plugin.watch, boat_plugin_read_key(BOAT_PLUGIN_OK, calmer, NULL), now);
    CHECK(send("a3", "operation=stage\nbase_revision=5\nbase_epoch=5\n", route));
    CHECK(!frame(&bridge, &plugin, &session, now += .25));
    CHECK(answered("a3") && is("outcome", "held") && is("held_revision", "6"));
    unsigned requests = plugin.requests;
    for (unsigned i = 0; i < 8; ++i)
        CHECK(!frame(&bridge, &plugin, &session, now += .25));
    CHECK(plugin.requests == requests && boat_plugin_host_held(&plugin.host));

    // Closing withdraws the session.
    wake_bridge_close(&bridge);
    CHECK(!read_file("session", session_text, sizeof(session_text)));
    wake_plugin_close(&plugin, &session);
    boat_session_destroy(&session);
    boat_world_resources_destroy(&resources);
    puts("plugin bridge: token, pins, deferred staging, held apply, Undo, Discard, rejection, "
         "restart, cancellation and save-poll agreement OK");
    return 0;
}
