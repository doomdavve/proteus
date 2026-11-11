#include "choreography.h"
#include "dance.h"
#include "raymath.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static bool write_score(const char *path, const char *text)
{
    FILE *file = fopen(path, "w");
    if (!file)
        return false;
    bool ok = fputs(text, file) >= 0;
    return fclose(file) == 0 && ok;
}

static bool check_arms(const char *path)
{
    char text[512], error[512];
    const char *styles[] = { "counter", "sweep", "pull" };
    enum { SAMPLES = 465 };
    DancePose baseline[SAMPLES];
    const char *neutral = "phrase arms\ngroove jack .8\narms counter left 0 0\n"
                          "shuffle left .24 .1\nside_step right .24 .1\nend\nplay arms 1\n";
    if (!write_score(path, neutral) || !dance_load_choreography(path, error, sizeof(error)))
        return false;
    float strongest_bend = 1;
    for (int sample = 0; sample < SAMPLES; ++sample) {
        dance_evaluate(sample / 240.0f, &baseline[sample]);
        DancePose pose = baseline[sample];
        Vector3 lower = Vector3Normalize(
            Vector3Subtract(pose.joints[DANCE_WAIST], pose.joints[DANCE_PELVIS]));
        Vector3 upper
            = Vector3Normalize(Vector3Subtract(pose.joints[DANCE_CHEST], pose.joints[DANCE_WAIST]));
        strongest_bend = fminf(strongest_bend, Vector3DotProduct(lower, upper));
    }
    // Guard against regressing to two collinear sticks with no actual waist articulation.
    if (strongest_bend > cosf(15 * DEG2RAD))
        return false;
    for (int style = 0; style < 3; ++style) {
        for (int lead = 0; lead < 2; ++lead) {
            for (int settings = 0; settings < 4; ++settings) {
                int amount = settings & 1;
                float delay = settings & 2 ? .25f : 0;
                snprintf(text, sizeof(text),
                    "phrase arms\ngroove jack .8\narms %s %s %d %.2f\n"
                    "shuffle left .24 .1\narms %s %s %d %.2f\n"
                    "side_step right .24 .1\nend\nplay arms 1\n",
                    styles[style], lead ? "right" : "left", amount, delay, styles[(style + 1) % 3],
                    lead ? "left" : "right", amount, .25f - delay);
                if (!write_score(path, text)
                    || !dance_load_choreography(path, error, sizeof(error)))
                    return false;
                DancePose previous = { 0 };
                float hand_change = 0;
                for (int sample = 0; sample < SAMPLES; ++sample) {
                    DancePose pose;
                    dance_evaluate(sample / 240.0f, &pose);
                    // Arm edits must not disturb the authored support or foot paths.
                    for (int joint = DANCE_LEFT_HIP; joint <= DANCE_RIGHT_TOE; ++joint) {
                        if (Vector3Distance(pose.joints[joint], baseline[sample].joints[joint])
                            > .00001f)
                            return false;
                    }
                    if (memcmp(pose.contact, baseline[sample].contact, sizeof(pose.contact)) != 0)
                        return false;
                    for (int joint = 0; joint < DANCE_JOINT_COUNT; ++joint) {
                        Vector3 v = pose.joints[joint];
                        if (!isfinite(v.x) || !isfinite(v.y) || !isfinite(v.z)
                            || (sample && Vector3Distance(v, previous.joints[joint]) > .04f))
                            return false;
                        if (!amount && Vector3Distance(v, baseline[sample].joints[joint]) > .00001f)
                            return false;
                    }
                    for (int side = 0; side < 2; ++side) {
                        int shoulder = side ? DANCE_RIGHT_SHOULDER : DANCE_LEFT_SHOULDER;
                        if (fabsf(Vector3Distance(pose.joints[shoulder], pose.joints[shoulder + 1])
                                - .32f)
                                > .0001f
                            || fabsf(Vector3Distance(
                                         pose.joints[shoulder + 1], pose.joints[shoulder + 2])
                                   - .29f)
                                > .0001f)
                            return false;
                        hand_change = fmaxf(hand_change,
                            Vector3Distance(
                                pose.joints[shoulder + 2], baseline[sample].joints[shoulder + 2]));
                    }
                    if (fabsf(Vector3Distance(pose.joints[DANCE_PELVIS], pose.joints[DANCE_WAIST])
                            - .18f)
                            > .0001f
                        || fabsf(Vector3Distance(pose.joints[DANCE_WAIST], pose.joints[DANCE_CHEST])
                               - .21f)
                            > .0001f
                        || fabsf(Vector3Distance(pose.joints[DANCE_CHEST], pose.joints[DANCE_NECK])
                               - .20f)
                            > .0001f)
                        return false;
                    previous = pose;
                }
                if (amount && hand_change < .12f)
                    return false;
            }
        }
    }
    return true;
}

static bool check_flow(const char *path)
{
    const char *text = "phrase fast\ngroove jack .8\narms sweep left .8 .1 4\n"
                       "shuffle left .12 .05 .5 flow\nshuffle right .12 .05 .5 flow\n"
                       "side_step left .15 .06 1 flow\nrecover 2\nend\nplay fast 2\n";
    Choreography score;
    char error[512];
    if (!write_score(path, text) || !choreography_load(path, &score, error, sizeof(error))
        || !dance_load_choreography(path, error, sizeof(error)))
        return false;
    if (score.count != 8 || score.beats != 8 || fabsf(dance_duration() - 3.84f) > .00001f
        || score.moves[1].start != .5f || score.moves[2].start != 1 || score.moves[3].start != 2
        || score.moves[4].start != 4 || score.moves[0].to.support != 0
        || score.moves[1].from.support != 0 || score.moves[1].to.support != 1
        || score.moves[3].to.support != -1)
        return false;
    for (int i = 0; i < score.count; ++i) {
        ChoreographyMove move = score.moves[i];
        if (i
            && (memcmp(&score.moves[i - 1].to, &move.from, sizeof(move.from)) != 0
                || (i < 4 && move.arm_start != 0)))
            return false;
        // Inspect either side of every join and the loop. No stance or arm reset is hidden here.
        float time = move.start * 60 / 125;
        DancePose before, after;
        dance_evaluate(fmodf(time - .00001f + dance_duration(), dance_duration()), &before);
        dance_evaluate(time + .00001f, &after);
        for (int joint = 0; joint < DANCE_JOINT_COUNT; ++joint) {
            if (Vector3Distance(before.joints[joint], after.joints[joint]) > .001f)
                return false;
        }
    }
    // Exercise fast flow across both move types and leads at the permitted fast-step limits.
    for (int kind = 0; kind < 2; ++kind) {
        for (int lead = 0; lead < 2; ++lead) {
            char edge[1024];
            const char *type = kind ? "side_step" : "shuffle";
            snprintf(edge, sizeof(edge),
                "phrase edge\ngroove jack 1\narms pull right 1 .25 4\n"
                "%s %s .15 .06 .5 flow\n%s %s .15 .06 .5 flow\n"
                "%s %s %.2f .12 1 flow\nrecover 2\nend\nplay edge 1\n",
                type, lead ? "right" : "left", type, lead ? "left" : "right", type,
                lead ? "right" : "left", kind ? .20 : .30);
            if (!write_score(path, edge) || !dance_load_choreography(path, error, sizeof(error)))
                return false;
            DancePose previous = { 0 };
            for (int sample = 0; sample < 465; ++sample) {
                DancePose pose;
                dance_evaluate(sample / 240.0f, &pose);
                for (int side = 0; side < 2; ++side) {
                    int hip = side ? DANCE_RIGHT_HIP : DANCE_LEFT_HIP;
                    if (fabsf(Vector3Distance(pose.joints[hip], pose.joints[hip + 1]) - .52f)
                            > .0001f
                        || fabsf(Vector3Distance(pose.joints[hip + 1], pose.joints[hip + 2]) - .50f)
                            > .0001f)
                        return false;
                    if (sample && pose.contact[side] == DANCE_PLANTED
                        && previous.contact[side] == DANCE_PLANTED
                        && Vector3Distance(pose.joints[hip + 2], previous.joints[hip + 2]) > .0001f)
                        return false;
                }
                for (int joint = 0; joint < DANCE_JOINT_COUNT; ++joint) {
                    Vector3 v = pose.joints[joint];
                    if (!isfinite(v.x) || !isfinite(v.y) || !isfinite(v.z)
                        || (sample && Vector3Distance(v, previous.joints[joint]) > .04f))
                        return false;
                }
                if (pose.contact[0] == DANCE_AIRBORNE && pose.contact[1] == DANCE_AIRBORNE)
                    return false;
                previous = pose;
            }
        }
    }
    return true;
}

int main(int argc, char **argv)
{
    if (argc != 2)
        return 2;
    const char *path = argv[1];
    const char *valid
        = "# test\nphrase a\ngroove jack 0.6\narms sweep right .8 .2\nshuffle left .24 .1\n"
          "arms pull left .7 .1\nside_step right .2 .08\nend\n"
          "phrase b\ngroove jack 1\nshuffle right .3 .18\n"
          "side_step left .05 .04\nend\nplay b 1\nplay a 2\n";
    const char *invalid[] = {
        "",
        "play missing 1\n",
        "phrase a\ngroove jack 0.6\n",
        "phrase a\ngroove jack nan\n",
        "phrase a\ngroove jack 1.1\n",
        "phrase a\nshuffle left .2 .1\n",
        "phrase a\ngroove jack .6\nshuffle up .2 .1\n",
        "phrase a\ngroove jack .6\nshuffle left inf .1\n",
        "phrase a\ngroove jack .6\nshuffle left .31 .1\n",
        "phrase a\ngroove jack .6\nshuffle left .2 -.1\n",
        "phrase a\ngroove jack .6\nshuffle left .2 .1 extra\n",
        "phrase a\ngroove jack .6\nshuffle left .2 .1\nend\n",
        "phrase a\ngroove jack .6\nshuffle left .2 .1\nshuffle right .2 .1\n"
        "end\nphrase a\n",
        "phrase a\ngroove jack .6\nshuffle left .2 .1\nshuffle right .2 .1\n"
        "end\nplay a 1.5\n",
        "phrase a\ngroove jack .6\nshuffle left .2 .1\nshuffle right .2 .1\n"
        "end\nplay a 32\nplay a 1\n",
        "unknown command with too many extra arguments here\n",
        "arms sweep left .6 .1\n",
        "phrase a\narms wave left .6 .1\n",
        "phrase a\narms sweep up .6 .1\n",
        "phrase a\narms sweep left nan .1\n",
        "phrase a\narms pull right 1.1 .1\n",
        "phrase a\narms counter right .6 -.1\n",
        "phrase a\narms sweep left .6 .26\n",
        "phrase a\narms sweep left .6\n",
        "phrase a\narms sweep left .6 .1 3\n",
        "phrase a\ngroove jack .8\nshuffle left .12 .05 .5 reset\n",
        "phrase a\ngroove jack .8\nshuffle left .24 .1 .5 flow\n",
        "phrase a\ngroove jack .8\nshuffle left .12 .05 .75 flow\n",
        "phrase a\ngroove jack .8\nshuffle left .12 .05 1 flow\n"
        "shuffle left .12 .05 1 flow\nrecover 2\nend\nplay a 1\n",
        "phrase a\ngroove jack .8\nshuffle left .12 .05 1 flow\n"
        "shuffle right .12 .05 1 flow\nshuffle left .12 .05 2 reset\nend\nplay a 1\n",
        "phrase a\ngroove jack .8\nshuffle left .12 .05 2 flow\n"
        "shuffle right .12 .05 2 flow\nend\nplay a 1\n",
        "phrase a\ngroove jack .8\nrecover 2\nrecover 2\nend\nplay a 1\n",
        "phrase a\ngroove jack .8\narms sweep left .8 .1 4\nshuffle left .12 .05 1 flow\n"
        "arms pull right .8 .1 2\nshuffle right .12 .05 1 flow\nrecover 2\nend\nplay a 1\n",
    };
    char error[512];
    Choreography score = { 0 };
    if (!write_score(path, valid) || !choreography_load(path, &score, error, sizeof(error)))
        goto fail;
    if (score.count != 6 || score.moves[0].lead != 1 || score.moves[2].lead != 0
        || score.moves[3].type != CHOREOGRAPHY_SIDE_STEP || strcmp(score.moves[4].phrase, "a") != 0
        || score.moves[0].arms.type != CHOREOGRAPHY_COUNTER
        || score.moves[2].arms.type != CHOREOGRAPHY_SWEEP || score.moves[2].arms.lead != 1
        || score.moves[3].arms.type != CHOREOGRAPHY_PULL || score.moves[3].arms.lead != 0
        || fabsf(score.moves[2].arms.delay - .2f) > .00001f)
        goto fail;
    Choreography original = score;
    if (!dance_load_choreography(path, error, sizeof(error)))
        goto fail;
    DancePose original_pose;
    dance_evaluate(.7f, &original_pose);
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        if (!write_score(path, invalid[i]) || choreography_load(path, &score, error, sizeof(error))
            || !error[0] || memcmp(&score, &original, sizeof(score)) != 0
            || dance_load_choreography(path, error, sizeof(error))) {
            fprintf(stderr, "Invalid case %zu was not rejected atomically.\n", i);
            goto fail;
        }
        DancePose unchanged;
        dance_evaluate(.7f, &unchanged);
        if (memcmp(original_pose.joints, unchanged.joints, sizeof(unchanged.joints)) != 0)
            goto fail;
    }
    // Check the allowed parameter corners on both leads, including lead and travel changes at
    // joins.
    for (int kind = 0; kind < 2; ++kind) {
        for (int lead = 0; lead < 2; ++lead) {
            for (int corner = 0; corner < 8; ++corner) {
                char text[512];
                const char *move = kind ? "side_step" : "shuffle";
                snprintf(text, sizeof(text),
                    "phrase edge\ngroove jack %d\n%s %s %.2f %.2f\n"
                    "%s %s %.2f %.2f\nend\nplay edge 1\n",
                    corner & 1, move, lead ? "right" : "left", corner & 2 ? .30 : .05,
                    corner & 4 ? .18 : .04, move, lead ? "left" : "right", corner & 2 ? .05 : .30,
                    corner & 4 ? .04 : .18);
                if (!write_score(path, text)
                    || !dance_load_choreography(path, error, sizeof(error)))
                    goto fail;
                DancePose previous = { 0 };
                for (int sample = 0; sample < 465; ++sample) {
                    DancePose pose;
                    dance_evaluate(sample / 240.0f, &pose);
                    for (int side = 0; side < 2; ++side) {
                        int hip = side ? DANCE_RIGHT_HIP : DANCE_LEFT_HIP;
                        float thigh = Vector3Distance(pose.joints[hip], pose.joints[hip + 1]);
                        float shin = Vector3Distance(pose.joints[hip + 1], pose.joints[hip + 2]);
                        if (!isfinite(thigh) || !isfinite(shin) || fabsf(thigh - .52f) > .0001f
                            || fabsf(shin - .50f) > .0001f) {
                            fprintf(stderr, "Unreachable %s: lead=%d corner=%d sample=%d.\n", move,
                                lead, corner, sample);
                            goto fail;
                        }
                    }
                    for (int joint = 0; joint < DANCE_JOINT_COUNT; ++joint) {
                        if (sample
                            && Vector3Distance(pose.joints[joint], previous.joints[joint]) > .04f) {
                            fprintf(stderr, "Discontinuous %s: lead=%d corner=%d sample=%d.\n",
                                move, lead, corner, sample);
                            goto fail;
                        }
                    }
                    previous = pose;
                }
            }
        }
    }
    if (!check_arms(path)) {
        fputs("Arm layer constraints failed.\n", stderr);
        goto fail;
    }
    if (!check_flow(path)) {
        fputs("Flow timing or stance validation failed.\n", stderr);
        goto fail;
    }
    remove(path);
    puts("Choreography: composition, invalid edits, atomic reload, parameter limits and "
         "flow joins and independent arm layer pass.");
    return 0;
fail:
    fprintf(stderr, "Choreography validation failed; fixture retained at %s.\n", path);
    return 1;
}
