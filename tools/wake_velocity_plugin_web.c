#include "wake_velocity_plugin_web.h"
#include <emscripten.h>

// The JavaScript bodies are private to this file; the C wrappers below are the backend's API.

EM_JS(void, js_plugin_owner,
    (unsigned revision, unsigned low, unsigned high, const char *status), {
    Module.boatPluginOwner = [ revision >>> 0, low >>> 0, high >>> 0 ];
    if (Module.boatPluginNotice)
        Module.boatPluginNotice(UTF8ToString(status));
})
EM_JS(bool, js_plugin_editing, (void), { return !!Module.boatPluginEditing; })
EM_JS(unsigned, js_plugin_actions, (void), {
    const flags = Module.boatPluginActions || 0;
    Module.boatPluginActions = 0;
    return flags;
})

EM_JS(unsigned, js_objective_start, (const unsigned char *bytes, unsigned size), {
    return Module.boatPluginObjective
        ? Module.boatPluginObjective.start(HEAPU8.slice(bytes, bytes + size))
        : 0;
})
EM_JS(int, js_objective_status, (unsigned job),
    { return Module.boatPluginObjective ? Module.boatPluginObjective.status(job) : -1; })
EM_JS(int, js_objective_read, (unsigned job, unsigned char *bytes), {
    const data = Module.boatPluginObjective ?.read(job);
    if (!data || data.length > 49152)
        return -1;
    HEAPU8.set(data, bytes);
    return data.length;
})
EM_JS(void, js_objective_close, (unsigned job), { Module.boatPluginObjective ?.close(job); })
EM_JS(void, js_objective_shutdown, (void), { Module.boatPluginObjective ?.reset(); })
EM_JS(void, js_objective_snapshot,
    (unsigned low, unsigned high, unsigned native_low, unsigned native_high, unsigned generation,
        bool initialized, bool quarantined, unsigned events, bool waiting,
        const unsigned char *bytes, unsigned size),
    {
        Module.boatPluginCheckpoint = {
            tick : [ low, high ],
            nativeTick : [ native_low, native_high ],
            generation,
            initialized : !!initialized,
            quarantined : !!quarantined,
            events,
            waiting : !!waiting,
            state : HEAPU8.slice(bytes, bytes + size)
        };
    })

EM_JS(int, js_plugin_export,
    (const unsigned char *manifest, unsigned manifest_size, const unsigned char *source,
        unsigned source_size),
    {
        if (!Module.boatPluginExport)
            return 0;
        Module.boatPluginExport(HEAPU8.slice(manifest, manifest + manifest_size),
            HEAPU8.slice(source, source + source_size));
        return 1;
    })

EM_JS(void, js_plugin_controls, (bool pending, bool menu_open), {
    Module.boatPluginResetPending = !!pending;
    Module.boatPluginMenuOpen = !!menu_open;
})

void wake_web_plugin_owner(unsigned revision, unsigned low, unsigned high, const char *status)
{
    js_plugin_owner(revision, low, high, status);
}
bool wake_web_plugin_editing(void) { return js_plugin_editing(); }
unsigned wake_web_plugin_actions(void) { return js_plugin_actions(); }
unsigned wake_web_objective_start(const unsigned char *bytes, unsigned size)
{
    return js_objective_start(bytes, size);
}
int wake_web_objective_status(unsigned job) { return js_objective_status(job); }
int wake_web_objective_read(unsigned job, unsigned char *bytes)
{
    return js_objective_read(job, bytes);
}
void wake_web_objective_close(unsigned job) { js_objective_close(job); }
void wake_web_objective_shutdown(void) { js_objective_shutdown(); }
void wake_web_objective_snapshot(unsigned low, unsigned high, unsigned native_low,
    unsigned native_high, unsigned generation, bool initialized, bool quarantined, unsigned events,
    bool waiting, const unsigned char *bytes, unsigned size)
{
    js_objective_snapshot(low, high, native_low, native_high, generation, initialized, quarantined,
        events, waiting, bytes, size);
}
int wake_web_plugin_export(const unsigned char *manifest, unsigned manifest_size,
    const unsigned char *source, unsigned source_size)
{
    return js_plugin_export(manifest, manifest_size, source, source_size);
}
void wake_web_plugin_controls(bool pending, bool menu_open)
{
    js_plugin_controls(pending, menu_open);
}
