#ifndef PROTEUS_BOAT_INPUT_H
#define PROTEUS_BOAT_INPUT_H

#include <stdbool.h>
#include <stdint.h>

// One tick of controls for one boat: signed adjustment rates, not absolute wheel or throttle
// positions. Values are clamped to -1..1 when applied. Zero steering lets the wheel spring
// back to centre; zero throttle holds the current throttle setting.
typedef struct {
    float steering_rate;
    float throttle_rate;
    bool gun;
    uint16_t grenade; // Nonzero request serial; held/replayed requests are consumed once.
} BoatInput;

#endif
