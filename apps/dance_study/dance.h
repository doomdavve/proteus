#ifndef PROTEUS_DANCE_H
#define PROTEUS_DANCE_H

#include "renderer3d.h"

enum {
    DANCE_PELVIS,
    DANCE_WAIST,
    DANCE_CHEST,
    DANCE_NECK,
    DANCE_HEAD,
    DANCE_LEFT_HIP,
    DANCE_LEFT_KNEE,
    DANCE_LEFT_ANKLE,
    DANCE_LEFT_TOE,
    DANCE_RIGHT_HIP,
    DANCE_RIGHT_KNEE,
    DANCE_RIGHT_ANKLE,
    DANCE_RIGHT_TOE,
    DANCE_LEFT_SHOULDER,
    DANCE_LEFT_ELBOW,
    DANCE_LEFT_HAND,
    DANCE_RIGHT_SHOULDER,
    DANCE_RIGHT_ELBOW,
    DANCE_RIGHT_HAND,
    DANCE_JOINT_COUNT
};

typedef enum { DANCE_PLANTED, DANCE_SLIDING, DANCE_AIRBORNE } DanceContact;

typedef struct {
    Vector3 joints[DANCE_JOINT_COUNT];
    DanceContact contact[2];
    const char *action;
    const char *phrase;
    float head_yaw;
    float energy;
    float beat;
} DancePose;

// The study uses starter.xm's 125 BPM, 4/4 clock. NULL restores the original study.
bool dance_load_choreography(const char *path, char *error, size_t error_size);
float dance_duration(void);
void dance_evaluate(float time, DancePose *pose);
bool dance_draw(
    Renderer3D *renderer, Color *pixels, int width, int height, float time, bool side_view);

#endif
