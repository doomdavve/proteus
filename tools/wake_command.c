#include "wake_command.h"

#include "boat_scene_definition.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static void emit(WakeCommand *command, const char *format, ...)
{
    if (!command->print && !command->tee)
        return;
    char line[192];
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(line, sizeof(line), format, arguments);
    va_end(arguments);
    if (command->print)
        command->print(command->context, line);
    if (command->tee)
        command->tee(command->tee_context, line);
}

static const char *trim(const char *line)
{
    while (*line == ' ' || *line == '\t' || *line == '\n' || *line == '\r')
        ++line;
    return line;
}

static bool builder_add(char *out, size_t capacity, size_t *at, const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    int n = vsnprintf(out + *at, capacity - *at, format, arguments);
    va_end(arguments);
    if (n < 0 || (size_t)n >= capacity - *at)
        return false;
    *at += (size_t)n;
    return true;
}

size_t wake_command_source(
    const WakeCommand *command, const char *base, size_t base_size, char *out, size_t capacity)
{
    size_t at = 0;
    if (!builder_add(out, capacity, &at, "local def = (function()\n"))
        return 0;
    if (base_size >= capacity - at)
        return 0;
    memcpy(out + at, base, base_size);
    at += base_size;
    if (base_size && base[base_size - 1] != '\n' && !builder_add(out, capacity, &at, "\n"))
        return 0;
    if (!builder_add(out, capacity, &at, "end)()\n"))
        return 0;
    for (unsigned i = 0; i < command->chunk_count; ++i)
        if (!builder_add(
                out, capacity, &at, "do %s end -- console %u\n", command->chunks[i], i + 1))
            return 0;
    if (!builder_add(out, capacity, &at, "return def\n"))
        return 0;
    return at;
}

// Captures the package entry once and moves a runtime session to authoring. A query can run in
// runtime mode; only an edit changes the mode.
static bool prepare_base(WakeCommand *command, WakePlugin *plugin, bool edit)
{
    if (edit && !plugin->authoring && !wake_plugin_promote(plugin)) {
        emit(command, "PROMOTION FAILED: %.150s", plugin->host.diagnostic.message);
        return false;
    }
    if (command->base_ready)
        return true;
    const char *text;
    size_t size = wake_plugin_source(plugin, &text);
    if (!size || size > sizeof(command->base)) {
        emit(command, "THE ACTIVE PACKAGE HAS NO SOURCE TO EDIT");
        return false;
    }
    memcpy(command->base, text, size);
    command->base_size = size;
    command->base_ready = true;
    command->counts[1] = 0;
    command->counts_known[1] = true;
    command->last_revision = 1;
    return true;
}

static bool submit_edit(
    WakeCommand *command, WakePlugin *plugin, BoatSession *session, const char *line, double now)
{
    size_t length = strlen(line);
    if (!length || length >= WAKE_COMMAND_CHUNK_TEXT) {
        emit(command, "EDIT IS EMPTY OR LONGER THAN %d CHARACTERS", WAKE_COMMAND_CHUNK_TEXT - 1);
        return false;
    }
    if (strchr(line, '\n') || strchr(line, '\r')) {
        emit(command, "ONE EDIT PER LINE");
        return false;
    }
    if (command->chunk_count == WAKE_COMMAND_CHUNKS) {
        emit(command, "EDIT LIMIT REACHED (%d); /undo OR /save", WAKE_COMMAND_CHUNKS);
        return false;
    }
    if (!prepare_base(command, plugin, true))
        return false;
    snprintf(command->chunks[command->chunk_count], WAKE_COMMAND_CHUNK_TEXT, "%s", line);
    ++command->chunk_count;
    static char source[BOAT_PLUGIN_SOURCE_MAX];
    size_t size
        = wake_command_source(command, command->base, command->base_size, source, sizeof(source));
    if (!size) {
        --command->chunk_count;
        emit(command, "SOURCE LIMIT (%d BYTES) REACHED; /undo OR /save", BOAT_PLUGIN_SOURCE_MAX);
        return false;
    }
    command->chunk = command->chunk_count;
    command->pending = true;
    wake_plugin_stage_source(plugin, session, source, size, now);
    if (!plugin->staging) {
        command->pending = false;
        --command->chunk_count;
        const BoatPluginDiagnostic *d = &plugin->host.diagnostic;
        emit(command, "CANNOT STAGE THE EDIT%s%s", d->message[0] ? ": " : "", d->message);
        return false;
    }
    return false;
}

static bool submit_query(
    WakeCommand *command, WakePlugin *plugin, const char *expression, double now)
{
    if (command->query_pending) {
        emit(command, "A QUERY IS STILL RUNNING");
        return false;
    }
    if (plugin->staging) {
        emit(command, "AN EDIT IS STAGING; WAIT FOR ITS RESULT");
        return false;
    }
    if (!prepare_base(command, plugin, false))
        return false;
    static char source[BOAT_PLUGIN_SOURCE_MAX];
    size_t size
        = wake_command_source(command, command->base, command->base_size, source, sizeof(source));
    if (!size || !wake_plugin_query_begin(plugin, source, size, expression, now)) {
        emit(command, "CANNOT RUN THE ISOLATED QUERY");
        return false;
    }
    command->query_pending = true;
    return false;
}

static void print_capabilities(WakeCommand *command, const WakePlugin *plugin)
{
    if (!plugin->host.depth) {
        emit(command, "NO ACTIVE PACKAGE");
        return;
    }
    const BoatPluginManifest *m = &boat_plugin_host_active(&plugin->host)->manifest;
    emit(command, "%s.define", m->kind[0] ? m->kind : "none");
    if (m->scene)
        emit(command, "scene.define");
    if (m->objective)
        emit(command, "objective.hud");
    if (m->timers)
        emit(command, "objective.timers");
}

static void print_recipes(WakeCommand *command)
{
    for (unsigned i = 0; i < BOAT_SCENE_BERTH_RECIPES; ++i)
        emit(command, "%s", boat_scene_berth_recipe_name((BoatSceneBerthRecipe)i));
}

static void print_anchors(WakeCommand *command, const WakePlugin *plugin)
{
    if (!plugin->host.depth || plugin->host.kind != BOAT_PLUGIN_KIND_VOYAGE
        || !boat_plugin_host_active(&plugin->host)->voyage.route.shelter_count) {
        emit(command, "NO VOYAGE ANCHORS IN THIS SESSION");
        return;
    }
    const BoatPluginRevision *active = boat_plugin_host_active(&plugin->host);
    const BoatVoyageRoute *route = &active->voyage.route;
    for (unsigned i = 0; i < route->shelter_count; ++i)
        emit(command, "%u %-12s %-10s %.0f, %.0f", i + 1, active->voyage.shelter_ids[i],
            route->shelters[i].name, route->shelters[i].position.x, route->shelters[i].position.y);
}

void wake_command_status(
    const WakeCommand *command, const WakePlugin *plugin, char *out, size_t capacity)
{
    const BoatPluginRevision *active = boat_plugin_host_active(&plugin->host);
    const BoatPluginRevision *held = boat_plugin_host_held(&plugin->host);
    snprintf(out, capacity, "R%u %s [%s @ %s] CHUNKS %u%s%s", active->number, active->manifest.name,
        active->manifest.kind, active->manifest.location, command->chunk_count,
        command->pending ? " STAGING" : "",
        held ? boat_plugin_change_name(plugin->host.change) : "");
}

static bool run_command(
    WakeCommand *command, WakePlugin *plugin, BoatSession *session, const char *line, double now)
{
    if (!strcmp(line, "/help")) {
        emit(command, "EDIT: def.path = value    = EXPR: READ-ONLY QUERY");
        emit(command, "/status /capabilities /recipes /anchors /undo /discard");
        emit(command, "/apply /restart /clear /save DIR /capability scene.define");
        return false;
    }
    if (!strcmp(line, "/clear")) {
        if (command->clear)
            command->clear(command->context);
        return false;
    }
    if (!strcmp(line, "/status")) {
        char status[256];
        wake_command_status(command, plugin, status, sizeof(status));
        emit(command, "%s", status);
        emit(command, "TICK %llu  BOAT %.1f, %.1f  HEADING %.2f", (unsigned long long)session->tick,
            session->boat.position.x, session->boat.position.y, session->boat.heading);
        if (plugin->staging)
            emit(command, "ONE EDIT IS STAGING");
        if (plugin->query)
            emit(command, "ONE QUERY IS RUNNING");
        if (plugin->directory)
            emit(command, "DIRECTORY %s", plugin->directory);
        return false;
    }
    if (!strcmp(line, "/capabilities")) {
        print_capabilities(command, plugin);
        return false;
    }
    if (!strcmp(line, "/recipes")) {
        print_recipes(command);
        return false;
    }
    if (!strcmp(line, "/anchors")) {
        print_anchors(command, plugin);
        return false;
    }
    if (!strncmp(line, "/capability ", 12)) {
        char message[192];
        const char *name = trim(line + 12);
        if (wake_plugin_grant(plugin, name, message, sizeof(message)))
            emit(command, "GRANTED %s", name);
        else
            emit(command, "REFUSED: %s", message);
        return false;
    }
    if (!strncmp(line, "/save", 5) && (line[5] == ' ' || !line[5])) {
        const char *directory = trim(line + 5);
        if (!*directory) {
            emit(command, "USAGE: /save DIR");
            return false;
        }
        char message[256];
        message[0] = 0;
        if (command->save
            && command->save(command->context, plugin, directory, message, sizeof(message)))
            emit(command, "%s", message);
        else
            emit(command, "NOT SAVED%s%s", message[0] ? ": " : "", message);
        return false;
    }
    if (!strcmp(line, "/apply")) {
        if (plugin->staging) {
            emit(command, "THE EDIT IS STILL STAGING");
            return false;
        }
        bool restart = wake_plugin_frame(plugin, session, false, false, true, now);
        emit(command, "%s", plugin->status);
        return restart;
    }
    if (!strcmp(line, "/discard")) {
        if (!plugin->staging && !boat_plugin_host_held(&plugin->host)) {
            emit(command, "NOTHING IS HELD");
            return false;
        }
        command->pending = false;
        bool restart = wake_plugin_frame(plugin, session, false, true, false, now);
        if (command->chunk_count && command->chunk == command->chunk_count)
            --command->chunk_count;
        emit(command, "%s", plugin->status);
        return restart;
    }
    if (!strcmp(line, "/undo")) {
        bool restart = wake_plugin_frame(plugin, session, false, true, false, now);
        emit(command, "%s", plugin->status);
        return restart;
    }
    if (!strcmp(line, "/restart")) {
        emit(command, "RESTARTING THE SESSION");
        return true;
    }
    emit(command, "UNKNOWN COMMAND %s; TRY /help", line);
    return false;
}

bool wake_command_submit(
    WakeCommand *command, WakePlugin *plugin, BoatSession *session, const char *line, double now)
{
    line = trim(line);
    if (!*line)
        return false;
    if (*line == '=')
        return submit_query(command, plugin, trim(line + 1), now);
    if (*line == '/')
        return run_command(command, plugin, session, line, now);
    return submit_edit(command, plugin, session, line, now);
}

bool wake_command_frame(WakeCommand *command, WakePlugin *plugin, BoatSession *session, double now)
{
    (void)session;
    if (plugin->host.depth) {
        unsigned revision = boat_plugin_host_active(&plugin->host)->number;
        if (revision != command->last_revision && revision < 256) {
            if (command->counts_known[revision])
                command->chunk_count = command->counts[revision];
            else {
                command->counts[revision] = command->chunk_count;
                command->counts_known[revision] = true;
            }
            command->last_revision = revision;
        }
    }
    if (command->pending && !plugin->staging) {
        command->pending = false;
        if (plugin->host.diagnostic.status != BOAT_PLUGIN_OK) {
            if (command->chunk_count && command->chunk == command->chunk_count)
                --command->chunk_count;
            emit(command, "CONSOLE %u REJECTED: %s", command->chunk,
                plugin->host.diagnostic.message);
            emit(command, "ACTIVE REVISION KEPT");
        } else {
            unsigned revision = plugin->host.held ? boat_plugin_host_held(&plugin->host)->number
                : plugin->host.depth              ? boat_plugin_host_active(&plugin->host)->number
                                                  : 0;
            if (revision && revision < 256) {
                command->counts[revision] = command->chunk_count;
                command->counts_known[revision] = true;
            }
            emit(command, "CONSOLE %u %s", command->chunk, plugin->status);
            if (plugin->detail[0])
                emit(command, "%s", plugin->detail);
        }
    }
    if (command->query_pending) {
        static char text[BOAT_PLUGIN_QUERY_REPLY_MAX];
        int result = wake_plugin_query_result(plugin, text, sizeof(text), now);
        if (result) {
            command->query_pending = false;
            if (result > 0)
                emit(command, "%s", text);
            else
                emit(command, "QUERY FAILED: %s", text);
        }
    }
    return false;
}

void wake_command_init(WakeCommand *command, WakeCommandPrint print, WakeCommandClear clear,
    WakeCommandSave save, void *context)
{
    memset(command, 0, sizeof(*command));
    command->print = print;
    command->clear = clear;
    command->save = save;
    command->context = context;
}

void wake_command_tee(WakeCommand *command, WakeCommandPrint tee, void *context)
{
    command->tee = tee;
    command->tee_context = context;
}
