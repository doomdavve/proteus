// Display-free P3 review: deterministic inputs, complete checkpoint and bounded inspection.
#include "wake_velocity_plugin.h"
#include "waterfront_traffic.h"

#include <errno.h>
#include <stdlib.h>

static bool tick_argument(const char *text, unsigned *tick)
{
    char *end;
    errno = 0;
    unsigned long parsed = strtoul(text, &end, 10);
    if (errno || end == text || *end || parsed > 72000)
        return false;
    *tick = (unsigned)parsed;
    return true;
}

int main(int argc, char **argv)
{
    unsigned checkpoint_tick = 120, seek_tick = 360;
    if (argc < 2 || argc > 4 || (argc > 2 && !tick_argument(argv[2], &checkpoint_tick))
        || (argc > 3 && !tick_argument(argv[3], &seek_tick)) || checkpoint_tick > seek_tick) {
        fprintf(stderr,
            "Usage: boat_plugin_review DIRECTORY [CHECKPOINT_TICK [SEEK_TICK]]\n"
            "Ticks: 0..72000; default checkpoint 120, seek 360.\n"
            "Calm solo fixture with player/traffic wakes; throttle for 120 ticks,\n"
            "alternating steering every 240 ticks; no wall clock or save polling.\n");
        return 2;
    }
    const BoatWorldDefinition *location = wake_plugin_location(argv[1]);
    BoatWorldResources resources;
    if (!location || !boat_world_resources_init(&resources, location))
        return 1;
    // Large bounded histories belong on the heap, including on Windows' default stack.
    WakePlugin *plugin = calloc(1, sizeof(*plugin));
    WakePluginPreview *origin = calloc(1, sizeof(*origin));
    WakePluginPreview *checkpoint = calloc(1, sizeof(*checkpoint));
    WakePluginReviewTick *ticks = calloc(seek_tick + 1, sizeof(*ticks));
    BoatSession session;
    boat_session_init(&session, &resources, (BoatWater) { 0 });
    BoatWakeObjects objects, traffic;
    int result = 1;
    if (!plugin || !origin || !checkpoint || !ticks)
        goto done;
    if (!boat_wake_objects_init(&objects, BOAT_OBJECT_LIFE_TICKS, BOAT_OBJECT_DEFAULT_HEIGHT)
        || !waterfront_traffic_wakes_init(&traffic, 0))
        goto done;
    session.world.water.objects = &objects;
    session.world.water.traffic_objects = &traffic;
    if (!wake_plugin_open(plugin, argv[1], &session, 0))
        goto done;
    for (unsigned i = 0; i < seek_tick; ++i)
        ticks[i].input = (BoatInput) { .throttle_rate = i < 120 ? 1 : 0,
            .steering_rate = (i / 240) % 2 ? -.1f : .1f };
    if (!wake_plugin_preview_save(origin, plugin, &session)
        || !wake_plugin_preview_seek(
            origin, plugin, &session, &objects, &traffic, ticks, checkpoint_tick)
        || !wake_plugin_preview_save(checkpoint, plugin, &session)
        || !wake_plugin_preview_seek(checkpoint, plugin, &session, &objects, &traffic,
            ticks + checkpoint_tick, seek_tick - checkpoint_tick))
        goto done;
    printf("native_tick=%llu position=%.9g,%.9g player_wakes=%u traffic_wakes=%u checkpoint=%u\n",
        (unsigned long long)session.tick, (double)session.boat.position.x,
        (double)session.boat.position.y, objects.live, traffic.live, checkpoint_tick);
    wake_plugin_inspect(plugin, stdout);
    result = 0;
done:
    boat_session_destroy(&session);
    free(ticks);
    free(checkpoint);
    free(origin);
    free(plugin);
    boat_world_resources_destroy(&resources);
    return result;
}
