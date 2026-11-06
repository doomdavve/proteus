#ifndef CARGO_SANDBOX_SITE_H
#define CARGO_SANDBOX_SITE_H

#include "boat.h"
#include "waterfront_harbor.h"
#include "waterfront_traffic.h"

// S0b temporary work platform in the north Eriksberg inlet; simulation metres.
#define CARGO_SITE_MOORING ((Vector2) { 10, 245 })

// Qualify the platform, workboat approach and complete crane sweep against compiled water,
// existing berths and the retained traffic. Conservative envelopes include the sampling gap.
// No depth, seabed support or human navigation approval is inferred from these checks.
static inline bool cargo_site_clear(const BoatWorld *world)
{
    Vector2 p = CARGO_SITE_MOORING;
    for (int z = -5; z <= 12; ++z)
        for (int x = -3; x <= 11; ++x) {
            Vector2 q = { p.x + x, p.y + z }, normal;
            if (!boat_map_water(&world->resources->map, q, 1)
                || waterfront_harbor_contact_height_scene(
                    world->scene, q, 1, -1, 9, world->water, 0, &normal))
                return false;
        }
    double period = 0;
    for (int i = 0; i < WATERFRONT_TRAFFIC_ALL; ++i) {
        double actor = waterfront_traffic_period(i);
        if (actor > period)
            period = actor;
    }
    for (double now = 0; now <= period; now += .25) {
        WaterfrontTrafficContact contact;
        if (waterfront_traffic_contact_height_scene(world->scene, (Vector2) { p.x + 2, p.y + 2 }, 0,
                12, 12, 10, -1, 9, world->water, now, &contact))
            return false;
    }
    return true;
}

#endif
