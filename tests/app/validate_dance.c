#include "dance.h"

#include "raymath.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static bool near(float a, float b) { return fabsf(a - b) < .0001f; }

int main(int argc, char **argv)
{
    bool house = argc == 2;
    if (house) {
        char error[512];
        if (!dance_load_choreography(argv[1], error, sizeof(error))) {
            fprintf(stderr, "%s\n", error);
            return 1;
        }
    }
    DancePose previous = { 0 };
    int frame = 0, failure_line = 0;
    int slides = 0;
    // Sample at 240 Hz to catch reach violations and foot drift throughout both phrases.
    for (; frame <= (int)ceilf(dance_duration() * 240) + 1; ++frame) {
        DancePose pose;
        dance_evaluate(frame / 240.0f, &pose);
        for (int joint = 0; joint < DANCE_JOINT_COUNT; ++joint) {
            Vector3 p = pose.joints[joint];
            if (!isfinite(p.x) || !isfinite(p.y) || !isfinite(p.z) || p.y < 0) {
                failure_line = __LINE__;
                goto invalid;
            }
            if (frame && Vector3Distance(p, previous.joints[joint]) > .04f) {
                fprintf(stderr, "Joint %d moved %.4f m in one sample.\n", joint,
                    Vector3Distance(p, previous.joints[joint]));
                failure_line = __LINE__;
                goto invalid;
            }
        }
        for (int side = 0; side < 2; ++side) {
            int hip = side == 0 ? DANCE_LEFT_HIP : DANCE_RIGHT_HIP;
            int shoulder = side == 0 ? DANCE_LEFT_SHOULDER : DANCE_RIGHT_SHOULDER;
            if (!near(Vector3Distance(pose.joints[hip], pose.joints[hip + 1]), .52f)
                || !near(Vector3Distance(pose.joints[hip + 1], pose.joints[hip + 2]), .50f)
                || !near(Vector3Distance(pose.joints[shoulder], pose.joints[shoulder + 1]), .32f)
                || !near(
                    Vector3Distance(pose.joints[shoulder + 1], pose.joints[shoulder + 2]), .29f)) {
                failure_line = __LINE__;
                goto invalid;
            }
            if (pose.contact[side] != DANCE_AIRBORNE) {
                Vector3 ankle = pose.joints[hip + 2];
                if (!near(ankle.y, .075f)) {
                    failure_line = __LINE__;
                    goto invalid;
                }
                if (frame && pose.contact[side] == DANCE_PLANTED
                    && previous.contact[side] == DANCE_PLANTED
                    && Vector3Distance(ankle, previous.joints[hip + 2]) > .0001f) {
                    failure_line = __LINE__;
                    goto invalid;
                }
                if (pose.contact[side] == DANCE_SLIDING
                    && Vector3Distance(ankle, previous.joints[hip + 2]) > .0001f)
                    ++slides;
            }
        }
        if (pose.contact[0] == DANCE_AIRBORNE && pose.contact[1] == DANCE_AIRBORNE) {
            failure_line = __LINE__;
            goto invalid;
        }
        previous = pose;
    }
    DancePose start, end, sought;
    dance_evaluate(0, &start);
    dance_evaluate(dance_duration(), &end);
    for (int i = 0; i < DANCE_JOINT_COUNT; ++i) {
        if (Vector3Distance(start.joints[i], end.joints[i]) > .0001f) {
            failure_line = __LINE__;
            goto invalid;
        }
    }
    dance_evaluate(8.25f, &start);
    dance_evaluate(1, &end);
    dance_evaluate(8.25f, &sought);
    if (memcmp(start.joints, sought.joints, sizeof(start.joints)) != 0) {
        failure_line = __LINE__;
        goto invalid;
    }
    // Guard against the rapid pelvis reversals seen in the first upper-body pass.
    // These are presentation limits, not a physical balance model.
    if (house) {
        const float dt = 1.0f / 120;
        for (int sample = 0; sample < (int)(dance_duration() / dt); ++sample) {
            float t = sample * dt;
            DancePose a, b, c;
            dance_evaluate(fmodf(t - dt + dance_duration(), dance_duration()), &a);
            dance_evaluate(t, &b);
            dance_evaluate(t + dt, &c);
            Vector3 velocity = Vector3Scale(
                Vector3Subtract(c.joints[DANCE_PELVIS], a.joints[DANCE_PELVIS]), 1 / (2 * dt));
            Vector3 acceleration = Vector3Scale(
                Vector3Subtract(Vector3Add(a.joints[DANCE_PELVIS], c.joints[DANCE_PELVIS]),
                    Vector3Scale(b.joints[DANCE_PELVIS], 2)),
                1 / (dt * dt));
            if (Vector3Length(velocity) > 2.1f || Vector3Length(acceleration) > 36) {
                failure_line = __LINE__;
                goto invalid;
            }
        }
    }
    // Both phrase treatments have a body hold with an independent head turn.
    for (int phrase = 0; !house && phrase < 4; ++phrase) {
        dance_evaluate((phrase * 8 + 4.25f) * 60 / 125, &start);
        dance_evaluate((phrase * 8 + 5.2f) * 60 / 125, &end);
        for (int joint = 0; joint < DANCE_JOINT_COUNT; ++joint) {
            if (joint != DANCE_HEAD
                && Vector3Distance(start.joints[joint], end.joints[joint]) > .0001f) {
                failure_line = __LINE__;
                goto invalid;
            }
        }
        if (fabsf(start.head_yaw - end.head_yaw) < .5f || start.contact[0] != DANCE_PLANTED
            || start.contact[1] != DANCE_PLANTED
            || start.joints[DANCE_LEFT_ANKLE].x <= start.joints[DANCE_RIGHT_ANKLE].x
            || start.joints[DANCE_LEFT_ANKLE].z <= start.joints[DANCE_RIGHT_ANKLE].z + .2f) {
            failure_line = __LINE__;
            goto invalid;
        }
    }
    printf("Dance: limb lengths, contact, clearance, continuity and seeking pass; sliding "
           "samples=%d.\n",
        slides);
    return 0;
invalid:
    fprintf(
        stderr, "Dance constraint validation failed at line %d, sample %d.\n", failure_line, frame);
    return 1;
}
