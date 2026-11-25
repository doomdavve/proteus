#include "render/boat_archipelago_view.h"

#include "core/boat_archipelago.h"
#include "core/boat_scene_definition.h"
#include "raymath.h"

#include <math.h>

static void part(WaterfrontSceneFrame *frame, Vector3 origin, float heading, Vector3 offset,
    Vector3 size, Color color, float swing)
{
    if (frame->scenery_count >= (int)(sizeof(frame->scenery) / sizeof(frame->scenery[0])))
        return;
    offset = Vector3Transform(offset, MatrixRotateY(heading));
    int i = frame->scenery_count++;
    frame->scenery[i].transform = (Transform3D) {
        .position = Vector3Add(origin, offset), .rotation = { swing, heading, 0 }, .scale = size
    };
    frame->scenery[i].color = color;
}

void boat_archipelago_evaluate(
    BoatWater water, double time, Vector3 player, Vector3 velocity, WaterfrontSceneFrame *frame)
{
    const BoatSceneDefinition *life = boat_archipelago_life();
    waterfront_traffic_evaluate_routes(&life->traffic, water, time, &frame->traffic);
    waterfront_boating_gulls_scene(life, time, player, velocity, frame);
    for (int i = 0; i < frame->gull_count; ++i) {
        Vector3 p = frame->gulls[i][0].position;
        float lift = fmaxf(0, boat_archipelago_ground((Vector2) { -p.x, p.z }) + 4 - p.y);
        for (int part = 0; part < WATERFRONT_GULL_PARTS; ++part)
            frame->gulls[i][part].position.y += lift;
    }
    size_t count;
    const BoatArchipelagoResident *residents = boat_archipelago_residents(&count);
    for (size_t i = 0; i < count; ++i) {
        const BoatArchipelagoResident *r = &residents[i];
        double phase = remainder(time + r->phase, r->period) * (2 * PI / r->period);
        float progress = (float)(.5 - .5 * cos(phase));
        Vector3 position = Vector3Lerp(r->start, r->end, progress);
        position.x = -position.x;
        if (Vector3DistanceSqr(position, player) > 600 * 600)
            continue;
        float heading = atan2f(r->end.x - r->start.x, -(r->end.z - r->start.z));
        if (sin(phase) < 0)
            heading += PI;
        float stride = (float)sin(remainder(time, 120) * 5 + i) * .32f * fabsf((float)sin(phase));
        Color trousers = { 51, 62, 70, 255 }, skin = { 186, 153, 117, 255 };
        part(frame, position, heading, (Vector3) { -.15f, .4f, 0 }, (Vector3) { .2f, .75f, .24f },
            trousers, stride);
        part(frame, position, heading, (Vector3) { .15f, .4f, 0 }, (Vector3) { .2f, .75f, .24f },
            trousers, -stride);
        part(frame, position, heading, (Vector3) { 0, 1.03f, 0 }, (Vector3) { .52f, .65f, .3f },
            r->coat, 0);
        for (int side = -1; side <= 1; side += 2)
            part(frame, position, heading, (Vector3) { side * .34f, 1.0f, 0 },
                (Vector3) { .16f, .58f, .18f }, r->coat, side * -stride);
        part(frame, position, heading, (Vector3) { 0, 1.56f, 0 }, (Vector3) { .3f, .34f, .3f },
            skin, 0);
        if (i % 3 == 0)
            part(frame, position, heading, (Vector3) { 0, .94f, -.38f },
                (Vector3) { .65f, .45f, .45f }, (Color) { 151, 116, 71, 255 }, 0);
    }
}
