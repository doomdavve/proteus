#ifndef BOAT_MUSIC_H
#define BOAT_MUSIC_H

#include <stdbool.h>
#include <string.h>

enum {
    BOAT_MUSIC_LITTLE_BOAT,
    BOAT_MUSIC_ACID_WAKE,
    BOAT_MUSIC_COZY,
    BOAT_MUSIC_WANDER,
    BOAT_MUSIC_OFF,
    BOAT_MUSIC_COUNT
};

typedef struct {
    const char *id, *label, *asset;
    float volume;
} BoatMusic;

static inline const BoatMusic *boat_music(unsigned choice)
{
    static const BoatMusic tracks[BOAT_MUSIC_COUNT] = {
        { "a-little-boat", "A LITTLE BOAT", "music/a-little-boat.xm", .35f },
        { "acid-wake", "ACID WAKE", "music/acid-wake.xm", .35f },
        { "a-little-boat-cozy", "LITTLE BOAT: COZY", "music/a-little-boat-cozy.xm", .35f },
        { "a-little-boat-wander", "BOAT: WANDERING", "music/a-little-boat-wander.xm", .35f },
        { "off", "OFF", NULL, 0 },
    };
    return &tracks[choice < BOAT_MUSIC_COUNT ? choice : BOAT_MUSIC_LITTLE_BOAT];
}

static inline bool boat_music_parse(const char *name, unsigned *choice)
{
    for (unsigned i = 0; i < BOAT_MUSIC_COUNT; ++i)
        if (!strcmp(name, boat_music(i)->id)) {
            *choice = i;
            return true;
        }
    return false;
}

#endif
