#include "wake_velocity_state.h"
#include <stdio.h>
#include <stdlib.h>

static void value(uint64_t *hash, const char *name, double number)
{
    char text[128];
    int n = snprintf(text, sizeof(text), "%s=%a;", name, number);
    if (n < 0 || (size_t)n >= sizeof(text))
        abort();
    for (int i = 0; i < n; ++i) {
        *hash ^= (unsigned char)text[i];
        *hash *= UINT64_C(1099511628211);
    }
}
uint64_t wake_velocity_state_hash(
    uint64_t hash, const Boat *b, double time, const BoatWakeObjects *w)
{
#define B(f) value(&hash, #f, b->f)
    B(position.x);
    B(position.y);
    B(velocity.x);
    B(velocity.y);
    value(&hash, "time", time);
    B(heading);
    B(throttle);
    B(engine_response);
    B(planing);
    B(height);
    B(vertical_velocity);
    B(pitch);
    B(pitch_velocity);
    B(roll);
    B(roll_velocity);
    B(motion_state);
    B(dry_time);
    B(landing_time);
    B(inverted_contact_time);
    B(landings);
    B(turn_roll_load);
    B(wheel);
    B(engine_angle);
    B(yaw_velocity);
    B(physical_wake_time);
    B(physical_wake_start.x);
    B(physical_wake_start.y);
#undef B
#define W(f) value(&hash, #f, w->f)
    W(tick);
    W(emitted);
    W(expired);
    W(life_ticks);
    W(live);
    W(peak_live);
    W(crest_height);
#undef W
    for (unsigned i = 0; i < BOAT_OBJECT_CAPACITY; ++i) {
        const BoatWakeObject *o = &w->objects[i];
#define O(f) value(&hash, #f, o->f)
        O(position.x);
        O(position.y);
        O(direction.x);
        O(direction.y);
        O(length);
        O(amplitude);
        O(inverse_speed);
        O(born);
#undef O
    }
    for (unsigned i = 0; i < BOAT_OBJECT_BOATS; ++i) {
        const BoatWakeObjectEmitter *e = &w->emitters[i];
#define E(f) value(&hash, #f, e->f)
        E(start.x);
        E(start.y);
        E(started);
        E(tracked);
        E(next);
        E(strength);
        E(strength_sum);
        E(initialized);
#undef E
    }
    return hash;
}
