#ifndef PROTEUS_BOAT_PLUGIN_LOAD_H
#define PROTEUS_BOAT_PLUGIN_LOAD_H

// Plugin evaluation: load a bundle by running its Lua entry, then decode and check it.
#include "boat_plugin.h"

BoatPluginStatus boat_plugin_load_race(const BoatPluginBundle *bundle,
    const BoatPluginLimits *limits, BoatPluginRace *out, BoatPluginDiagnostic *diagnostic);
BoatPluginStatus boat_plugin_load_free(const BoatPluginBundle *bundle,
    const BoatPluginLimits *limits, BoatPluginFree *out, BoatPluginDiagnostic *diagnostic);
BoatPluginStatus boat_plugin_load_course(const BoatPluginBundle *bundle,
    const BoatPluginLimits *limits, BoatPluginCourse *out, BoatPluginDiagnostic *diagnostic);
BoatPluginStatus boat_plugin_load_match(const BoatPluginBundle *bundle,
    const BoatPluginLimits *limits, BoatPluginMatch *out, BoatPluginDiagnostic *diagnostic);
// Evaluate BUNDLE and decode its voyage. OUT is written only on success, so a failure leaves the
// caller's last good definition intact. LIMITS NULL selects the defaults.
BoatPluginStatus boat_plugin_load_voyage(const BoatPluginBundle *bundle,
    const BoatPluginLimits *limits, BoatPluginVoyage *out, BoatPluginDiagnostic *diagnostic);

#endif
