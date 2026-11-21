/* R0 audit of the shipped authored service scene through production evaluators. */
#include "core/waterfront_harbor.h"
#include "core/waterfront_service.h"
#include "core/waterfront_traffic.h"
#include "plugins/boat_plugin_io.h"
#include "plugins/boat_plugin_load.h"
#include "raymath.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "Usage: wake_coverage VOYAGE_PLUGIN OUTPUT.csv\n");
        return 2;
    }
    static BoatPluginFiles files;
    static BoatPluginVoyage voyage;
    BoatPluginDiagnostic diagnostic;
    if (boat_plugin_read(argv[1], NULL, &files, &diagnostic) != BOAT_PLUGIN_OK) {
        fprintf(stderr, "%s: %s\n", diagnostic.field, diagnostic.message);
        return 1;
    }
    BoatPluginBundle bundle = boat_plugin_bundle(&files);
    if (boat_plugin_load_voyage(&bundle, NULL, &voyage, &diagnostic) != BOAT_PLUGIN_OK
        || !voyage.has_scene) {
        fprintf(
            stderr, "Need a voyage with a scene: %s: %s\n", diagnostic.field, diagnostic.message);
        return 1;
    }
    FILE *out = fopen(argv[2], "w");
    if (!out)
        return 1;
    fputs("tick,service,vessel,owner,present,speed,x,z,traffic_hidden,scene_trail_quads,vessel_"
          "trail_quads,physical_"
          "pool_present\n",
        out);
    unsigned moving = 0, missing = 0;
    BoatWater water = { 0 }; // Same default: no scenery physical pool enabled by the scene.
    for (unsigned tick = 0; tick <= 3600; tick += 12) {
        double time = tick / 120.0;
        WaterfrontTrafficFrame traffic;
        waterfront_traffic_evaluate_scene(&voyage.scene, water, time, &traffic);
        for (unsigned v = 0; v < voyage.scene.vessel_count; ++v) {
            WaterfrontServiceHull hull = { 0 };
            bool present = waterfront_service_hull(&voyage.scene, v, time, &hull);
            float speed = Vector3Length(hull.velocity);
            int hidden = -1;
            for (unsigned r = 0; r < voyage.scene.traffic.actor_count; ++r) {
                int service = waterfront_service_for_route(&voyage.scene, r);
                if (service >= 0 && present && (unsigned)service == hull.service)
                    hidden = traffic.hidden[r];
            }
            fprintf(out, "%u,%u,%s,%d,%d,%.9g,%.9g,%.9g,%d,%d,%d,%d\n", tick,
                present ? hull.service : 0, voyage.scene.vessels[v].id,
                present ? (int)hull.owner : -1, present, (double)speed, (double)hull.position.x,
                (double)hull.position.z, hidden, traffic.wake_count, traffic.service_wake_count[v],
                water.traffic_objects != NULL);
            if (present && speed > .1f) {
                ++moving;
                if (hidden == 1 && traffic.service_wake_count[v] == 0 && !water.traffic_objects)
                    ++missing;
            }
        }
    }
    if (fclose(out))
        return 1;
    printf(
        "scene_digest=%016llx samples_moving=%u samples_without_scene_trails_or_physical_pool=%u\n",
        (unsigned long long)voyage.scene_digest, moving, missing);
    return moving ? 0 : 1;
}
