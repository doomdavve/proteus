#ifdef _WIN32
#define _CRT_RAND_S // rand_s; windows.h would clash with raylib's names.
#elif !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#include "wake_velocity_bridge.h"

#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#include <process.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#define HEADER_MAX 1024
#define PAYLOAD_MAX (8 + BOAT_PLUGIN_MANIFEST_MAX + BOAT_PLUGIN_SOURCE_MAX)
#define RESPONSE_MAX 32768

typedef struct {
    char text[RESPONSE_MAX];
    size_t size;
    bool truncated;
} Response;

typedef struct {
    char id[WAKE_BRIDGE_ID + 1], token[33], operation[16];
    long long base_revision, base_epoch, held_revision;
    bool has_base_revision, has_base_epoch, has_held_revision;
    size_t payload_size;
    const unsigned char *payload;
} Request;

static void path(char *out, size_t size, const WakeBridge *bridge, const char *name)
{
    snprintf(out, size, "%s/%s", bridge->directory, name);
}

static bool random_hex(char *out, size_t digits)
{
    unsigned char bytes[16];
    size_t count = (digits + 1) / 2;
    if (count > sizeof(bytes))
        return false;
#ifdef _WIN32
    for (size_t i = 0; i < count; ++i) {
        unsigned value;
        if (rand_s(&value))
            return false;
        bytes[i] = (unsigned char)value;
    }
#else
    FILE *source = fopen("/dev/urandom", "rb");
    bool ok = source && fread(bytes, 1, count, source) == count;
    if (source)
        fclose(source);
    if (!ok)
        return false;
#endif
    for (size_t i = 0; i < digits; ++i)
        out[i] = "0123456789abcdef"[(bytes[i / 2] >> (i % 2 ? 0 : 4)) & 15];
    out[digits] = 0;
    return true;
}

// The directory must be the user's own and private: its permissions are the authentication.
static bool private_directory(const char *directory)
{
#ifdef _WIN32
    if (_mkdir(directory) && errno != EEXIST)
        return false;
    return true; // Inherits the parent's ACL; keep it under the user's profile.
#else
    struct stat info;
    if (lstat(directory, &info)
        && (errno != ENOENT || mkdir(directory, 0700) || lstat(directory, &info)))
        return false;
    if (!S_ISDIR(info.st_mode) || info.st_uid != getuid() || (info.st_mode & 077)) {
        fprintf(stderr,
            "--plugin-bridge %s must be a private directory (mode 0700) owned by you.\n",
            directory);
        return false;
    }
    return true;
#endif
}

// Write NAME atomically: readers see the complete old file, the complete new one, or none.
static bool publish(const WakeBridge *bridge, const char *name, const char *text, size_t size)
{
    char temporary[1100], target[1100];
    path(temporary, sizeof(temporary), bridge, ".bridge-tmp");
    path(target, sizeof(target), bridge, name);
#ifdef _WIN32
    FILE *output = fopen(temporary, "wb");
#else
    int fd = open(temporary, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    FILE *output = fd >= 0 ? fdopen(fd, "wb") : NULL;
    if (fd >= 0 && !output)
        close(fd);
#endif
    if (!output)
        return false;
    bool ok = fwrite(text, 1, size, output) == size;
    ok = !fclose(output) && ok;
#ifdef _WIN32
    remove(target); // rename does not replace on Windows; readers retry a missing file.
#endif
    if (!ok || rename(temporary, target)) {
        remove(temporary);
        return false;
    }
    return true;
}

static void add(Response *r, const char *format, ...)
{
    if (r->truncated)
        return;
    va_list arguments;
    va_start(arguments, format);
    int n = vsnprintf(r->text + r->size, sizeof(r->text) - r->size, format, arguments);
    va_end(arguments);
    if (n < 0 || (size_t)n >= sizeof(r->text) - r->size) {
        r->truncated = true;
        r->text[r->size] = 0;
        return;
    }
    r->size += (size_t)n;
}

// One KEY=VALUE line; VALUE never contains a line break.
static void field(Response *r, const char *key, const char *value)
{
    add(r, "%s=", key);
    for (const char *c = value; *c && !r->truncated; ++c)
        add(r, "%c", *c == '\n' || *c == '\r' ? ' ' : *c);
    add(r, "\n");
}

static void owner(Response *r, const WakePlugin *plugin, const BoatSession *session)
{
    const BoatPluginHost *host = &plugin->host;
    const BoatPluginRevision *active = boat_plugin_host_active(host);
    const BoatPluginRevision *held = boat_plugin_host_held(host);
    field(r, "plugin", active->voyage.manifest.id);
    add(r, "revision=%u\nepoch=%" PRIu64 "\ndigest=%016" PRIx64 "\nundo_depth=%u\n", active->number,
        host->epoch, active->voyage.digest, host->depth - 1);
    add(r, "staging=%d\nheld_revision=%u\n", plugin->staging != NULL, held ? held->number : 0);
    if (held) {
        char change[32]; // A machine word: "restart voyage" becomes "restart_voyage".
        snprintf(change, sizeof(change), "%s", boat_plugin_change_name(host->change));
        for (char *c = change; *c; ++c)
            *c = *c == ' ' ? '_' : *c;
        add(r, "held_change=%s\nheld_undo=%d\nheld_base=%u\n", change, host->undo, host->base);
    }
    field(r, "outcome", plugin->outcome ? plugin->outcome : "");
    field(r, "message", plugin->status);
    field(r, "detail", plugin->detail);
    add(r, "alert=%d\n", plugin->alert);
    const BoatPluginDiagnostic *d = &host->diagnostic;
    if (d->status != BOAT_PLUGIN_OK) {
        field(r, "diagnostic_status", boat_plugin_status_name(d->status));
        field(r, "diagnostic_file", d->file);
        add(r, "diagnostic_line=%d\n", d->line);
        field(r, "diagnostic_field", d->field);
        field(r, "diagnostic_message", d->message);
    }
    add(r, "tick=%" PRIu64 "\nboat_x=%.3f\nboat_z=%.3f\nheading=%.4f\n", session->tick,
        session->boat.position.x, session->boat.position.y, session->boat.heading);
    if (session->voyage.route)
        add(r, "shelter=%u\nexposed=%d\narrived=%d\n", session->voyage.shelter,
            session->voyage.exposed, session->voyage.arrived);
}

static void respond(WakeBridge *bridge, const char *id, const char *status, const char *message,
    const WakePlugin *plugin, const BoatSession *session, bool inspect)
{
    static Response r;
    r.size = 0;
    r.truncated = false;
    add(&r, "wake-velocity-bridge %d\n", WAKE_BRIDGE_PROTOCOL);
    add(&r, "id=%s\nsession=%s\nstatus=%s\n", id, bridge->session, status);
    if (message)
        field(&r, "status_message", message);
    if (plugin && plugin->host.depth)
        owner(&r, plugin, session);
    if (inspect && plugin) {
        char *text = NULL;
        size_t size = 0;
        FILE *stream = tmpfile();
        if (stream) {
            wake_plugin_inspect(plugin, stream);
            long length = ftell(stream);
            if (length > 0 && length < RESPONSE_MAX && (text = malloc((size_t)length + 1))) {
                rewind(stream);
                size = fread(text, 1, (size_t)length, stream);
                text[size] = 0;
            }
            fclose(stream);
        }
        for (char *line = text; line && *line;) {
            char *end = strchr(line, '\n');
            if (end)
                *end = 0;
            field(&r, "inspect", line);
            line = end ? end + 1 : line + strlen(line);
        }
        free(text);
    }
    if (r.truncated) {
        r.size = 0;
        r.truncated = false;
        add(&r, "wake-velocity-bridge %d\nid=%s\nsession=%s\nstatus=failed\n", WAKE_BRIDGE_PROTOCOL,
            id, bridge->session);
        field(&r, "status_message", "response exceeds the bridge limit");
    }
    char name[64];
    if (bridge->last_response[0]) {
        char previous[1100];
        snprintf(name, sizeof(name), "response-%s", bridge->last_response);
        path(previous, sizeof(previous), bridge, name);
        remove(previous);
    }
    snprintf(name, sizeof(name), "response-%s", id);
    if (publish(bridge, name, r.text, r.size))
        snprintf(bridge->last_response, sizeof(bridge->last_response), "%s", id);
    else
        fprintf(stderr, "Plugin bridge: cannot write %s/%s\n", bridge->directory, name);
}

bool wake_bridge_open(WakeBridge *bridge, const char *directory, double now)
{
    memset(bridge, 0, sizeof(*bridge));
    if (strlen(directory) >= sizeof(bridge->directory) - 64 || !private_directory(directory)) {
        fprintf(stderr, "Plugin bridge: cannot use directory %s\n", directory);
        return false;
    }
    snprintf(bridge->directory, sizeof(bridge->directory), "%s", directory);
    if (!random_hex(bridge->token, 32) || !random_hex(bridge->session, 16)) {
        fprintf(stderr, "Plugin bridge: no random source for the session token\n");
        return false;
    }
    char stale[1100];
    path(stale, sizeof(stale), bridge, "request");
    remove(stale); // Addressed to an earlier launch's token.
    char text[256];
#ifdef _WIN32
    int pid = _getpid();
#else
    int pid = (int)getpid();
#endif
    int size
        = snprintf(text, sizeof(text), "wake-velocity-bridge %d\nsession=%s\ntoken=%s\npid=%d\n",
            WAKE_BRIDGE_PROTOCOL, bridge->session, bridge->token, pid);
    if (!publish(bridge, "session", text, (size_t)size)) {
        fprintf(stderr, "Plugin bridge: cannot write %s/session\n", directory);
        return false;
    }
    bridge->next_poll = now;
    bridge->open = true;
    fprintf(stderr, "Plugin bridge: session %s at %s\n", bridge->session, directory);
    return true;
}

void wake_bridge_close(WakeBridge *bridge)
{
    if (!bridge->open)
        return;
    if (bridge->pending[0])
        respond(bridge, bridge->pending, "cancelled", "the game closed", NULL, NULL, false);
    char name[1100];
    path(name, sizeof(name), bridge, "session");
    remove(name);
    bridge->open = false;
}

static bool hex_id(const char *value, size_t max)
{
    size_t n = strlen(value);
    if (!n || n > max)
        return false;
    for (size_t i = 0; i < n; ++i)
        if (!((value[i] >= '0' && value[i] <= '9') || (value[i] >= 'a' && value[i] <= 'f')))
            return false;
    return true;
}

static bool number(const char *value, long long *out)
{
    char *end;
    errno = 0;
    long long n = strtoll(value, &end, 10);
    if (end == value || *end || errno || n < 0)
        return false;
    *out = n;
    return true;
}

// Header lines until a blank line; any payload follows it. The id is parsed first, so even
// a bad request receives an answer whenever its id is readable.
static const char *parse(unsigned char *data, size_t size, Request *q)
{
    memset(q, 0, sizeof(*q));
    unsigned char *body = NULL;
    for (size_t i = 0; i + 1 < size && i < HEADER_MAX; ++i)
        if (data[i] == '\n' && data[i + 1] == '\n') {
            data[i + 1] = 0;
            body = data + i + 2;
            break;
        }
    if (!body)
        return "missing header terminator";
    char *line = (char *)data, *end;
    end = strchr(line, '\n');
    if (!end)
        return "missing protocol line";
    *end = 0;
    if (strcmp(line, "wake-velocity-bridge 1"))
        return "unsupported protocol";
    long long payload = 0;
    bool has_payload = false;
    for (line = end + 1; *line; line = end + 1) {
        end = strchr(line, '\n');
        *end = 0;
        char *value = strchr(line, '=');
        if (!value)
            return "malformed header line";
        *value++ = 0;
        if (!strcmp(line, "id") && hex_id(value, WAKE_BRIDGE_ID))
            snprintf(q->id, sizeof(q->id), "%s", value);
        else if (!strcmp(line, "token") && strlen(value) < sizeof(q->token))
            snprintf(q->token, sizeof(q->token), "%s", value);
        else if (!strcmp(line, "operation") && strlen(value) < sizeof(q->operation))
            snprintf(q->operation, sizeof(q->operation), "%s", value);
        else if (!strcmp(line, "base_revision") && number(value, &q->base_revision))
            q->has_base_revision = true;
        else if (!strcmp(line, "base_epoch") && number(value, &q->base_epoch))
            q->has_base_epoch = true;
        else if (!strcmp(line, "held_revision") && number(value, &q->held_revision))
            q->has_held_revision = true;
        else if (!strcmp(line, "payload") && number(value, &payload) && payload <= PAYLOAD_MAX)
            has_payload = true;
        else
            return "unknown or invalid header field";
    }
    if (!q->id[0])
        return "missing request id";
    size_t remaining = size - (size_t)(body - data);
    if ((has_payload ? (size_t)payload : 0) != remaining)
        return "payload size does not match";
    q->payload = body;
    q->payload_size = remaining;
    return NULL;
}

static bool same_token(const char *a, const char *b)
{
    unsigned char difference = (unsigned char)(strlen(a) != strlen(b));
    for (size_t i = 0; a[i] && b[i]; ++i)
        difference |= (unsigned char)(a[i] ^ b[i]);
    return !difference;
}

// The validation snapshot: big-endian manifest/source sizes, then both files' bytes.
static bool unpack(const Request *q, BoatPluginFiles *files)
{
    if (q->payload_size < 8)
        return false;
    const unsigned char *p = q->payload;
    size_t manifest = (size_t)p[0] << 24 | (size_t)p[1] << 16 | (size_t)p[2] << 8 | p[3];
    size_t source = (size_t)p[4] << 24 | (size_t)p[5] << 16 | (size_t)p[6] << 8 | p[7];
    if (manifest > BOAT_PLUGIN_MANIFEST_MAX || source > BOAT_PLUGIN_SOURCE_MAX
        || 8 + manifest + source != q->payload_size)
        return false;
    memcpy(files->manifest, p + 8, manifest);
    files->manifest_size = manifest;
    memcpy(files->source, p + 8 + manifest, source);
    files->source_size = source;
    return true;
}

static bool handle(
    WakeBridge *bridge, WakePlugin *plugin, BoatSession *session, const Request *q, double now)
{
    const BoatPluginHost *host = &plugin->host;
    const BoatPluginRevision *held = boat_plugin_host_held(host);
    unsigned active = boat_plugin_host_active(host)->number;
    const char *op = q->operation;
    bool stage = !strcmp(op, "stage"), apply = !strcmp(op, "apply"), undo = !strcmp(op, "undo"),
         discard = !strcmp(op, "discard");
    if (!strcmp(op, "inspect")) {
        respond(bridge, q->id, "ok", NULL, plugin, session, true);
        return false;
    }
    if (!stage && !apply && !undo && !discard) {
        respond(bridge, q->id, "bad_request", "unknown operation", plugin, session, false);
        return false;
    }
    // Every mutation names the commit epoch it saw; stage/undo also name the active revision
    // and apply the held one, so no request acts on state its sender has not inspected.
    if (!q->has_base_epoch || ((stage || undo) && !q->has_base_revision)
        || (apply && !q->has_held_revision)) {
        respond(
            bridge, q->id, "bad_request", "missing revision or epoch pin", plugin, session, false);
        return false;
    }
    if ((uint64_t)q->base_epoch != host->epoch
        || ((stage || undo) && (uint64_t)q->base_revision != active)
        || (apply && (!held || (uint64_t)q->held_revision != held->number))) {
        respond(bridge, q->id, "stale", "pinned revision or epoch is no longer current", plugin,
            session, false);
        return false;
    }
    if (plugin->staging && !discard) {
        respond(
            bridge, q->id, "busy", "another staging request is running", plugin, session, false);
        return false;
    }
    if ((apply && !held) || (undo && held) || (discard && !held && !plugin->staging)) {
        respond(bridge, q->id, "conflict",
            apply      ? "nothing is held"
                : undo ? "discard the held candidate first"
                       : "nothing is held or staging",
            plugin, session, false);
        return false;
    }
    bool restart;
    if (stage) {
        static BoatPluginFiles files;
        if (!unpack(q, &files)) {
            respond(
                bridge, q->id, "bad_request", "malformed bundle payload", plugin, session, false);
            return false;
        }
        restart = wake_plugin_stage_files(plugin, session, &files, now);
        if (plugin->staging) {
            snprintf(bridge->pending, sizeof(bridge->pending), "%s", q->id);
            bridge->pending_requests = plugin->requests;
            return restart;
        }
    } else {
        restart = wake_plugin_frame(plugin, session, false, undo || discard, apply, now);
    }
    respond(bridge, q->id, "ok", NULL, plugin, session, false);
    return restart;
}

bool wake_bridge_frame(WakeBridge *bridge, WakePlugin *plugin, BoatSession *session, double now)
{
    if (!bridge->open)
        return false;
    if (bridge->pending[0] && (plugin->requests != bridge->pending_requests || !plugin->staging)) {
        // Superseded by a save, F5 or cancellation, or finished: the outcome says which.
        bool superseded = plugin->requests != bridge->pending_requests;
        respond(bridge, bridge->pending, superseded ? "superseded" : "ok",
            superseded ? "a later staging request replaced it" : NULL, plugin, session, false);
        bridge->pending[0] = 0;
    }
    if (now < bridge->next_poll)
        return false;
    bridge->next_poll = now + BOAT_PLUGIN_POLL;
    char name[1100];
    path(name, sizeof(name), bridge, "request");
    FILE *input = fopen(name, "rb");
    if (!input)
        return false;
    static unsigned char data[HEADER_MAX + PAYLOAD_MAX + 1];
    size_t size = fread(data, 1, sizeof(data), input);
    fclose(input);
    remove(name);
    Request q = { 0 };
    const char *error
        = size == sizeof(data) ? "request exceeds the bridge limit" : parse(data, size, &q);
    if (error && !q.id[0]) {
        fprintf(stderr, "Plugin bridge: unanswerable request: %s\n", error);
        return false;
    }
    // Authentication precedes any answer that reveals owner state.
    if (!same_token(q.token, bridge->token)) {
        respond(
            bridge, q.id, "unauthorized", "token does not match this session", NULL, NULL, false);
        return false;
    }
    if (error) {
        respond(bridge, q.id, "bad_request", error, plugin, session, false);
        return false;
    }
    // While a stage request runs, only inspection and Discard (which cancels it) proceed.
    if (bridge->pending[0] && strcmp(q.operation, "inspect") && strcmp(q.operation, "discard")) {
        respond(bridge, q.id, "busy", "an earlier stage request is still running", plugin, session,
            false);
        return false;
    }
    return handle(bridge, plugin, session, &q, now);
}
