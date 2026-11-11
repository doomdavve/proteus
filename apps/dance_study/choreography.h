#ifndef PROTEUS_CHOREOGRAPHY_H
#define PROTEUS_CHOREOGRAPHY_H

#include <stdbool.h>
#include <stddef.h>

enum { CHOREOGRAPHY_MAX_MOVES = 256 };
typedef enum {
    CHOREOGRAPHY_SHUFFLE,
    CHOREOGRAPHY_SIDE_STEP,
    CHOREOGRAPHY_RECOVER
} ChoreographyMoveType;
typedef enum { CHOREOGRAPHY_COUNTER, CHOREOGRAPHY_SWEEP, CHOREOGRAPHY_PULL } ChoreographyArmsType;

typedef struct {
    ChoreographyArmsType type;
    int lead; // Independent of the leading foot.
    float amount, delay, span; // Delay and gesture span are in beats.
} ChoreographyArms;

typedef struct {
    float x[2], z[2], root_x;
    int support; // -1 = neutral, 0 = left, 1 = right.
} ChoreographyStance;

typedef struct {
    ChoreographyMoveType type;
    int lead; // 0 = left, 1 = right.
    float travel, lift, groove;
    ChoreographyArms arms;
    float start, duration, arm_start;
    bool flow;
    int arm_group, source_line;
    ChoreographyStance from, to;
    char phrase[32];
} ChoreographyMove;

typedef struct {
    ChoreographyMove moves[CHOREOGRAPHY_MAX_MOVES];
    int count;
    float beats;
} Choreography;

// On failure, leave output unchanged and report the offending line where possible.
bool choreography_load(const char *path, Choreography *output, char *error, size_t error_size);

#endif
