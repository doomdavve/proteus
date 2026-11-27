#include "render/boat_archipelago_view.h"

#include "core/boat_archipelago.h"
#include "core/boat_scene_definition.h"
#include "raymath.h"
#include "scenery/building_windows.h"

#include <math.h>

BoatViewProps boat_archipelago_lighting(double time)
{
    return (BoatViewProps) { .lighting_cycle = isfinite(time),
        .lighting_hours = 12 + time * (24.0 / 900),
        .lighting_cycle_seconds = 900 };
}

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
    // The baked piers supply static collision. Borrow only authored hull recipes and poses;
    // the Gothenburg ferry, quay and furniture do not belong to this local marina.
    frame->harbor.modules_authored = true;
    frame->harbor.omit_ferry = true;
    frame->harbor.berth_count = (int)life->berth_count;
    for (unsigned i = 0; i < life->berth_count; ++i) {
        WaterfrontBerth berth = waterfront_harbor_berth_at(life, i);
        BoatModelSpec spec = waterfront_harbor_berth_spec(&berth);
        frame->harbor.recipes[i] = berth.recipe;
        frame->harbor.specs[i] = spec;
        frame->harbor.visible[i] = true;
        frame->harbor.boats[i]
            = boat_water_pose(water, time, berth.position, berth.heading, spec.length, spec.beam);
        const Vector3 *ties = boat_archipelago_moorings(i);
        for (int end = 0; ties && end < 2; ++end) {
            Transform3D pose = frame->harbor.boats[i];
            Vector3 local = Vector3Transform(
                Vector3Subtract(ties[end], pose.position), MatrixRotateY(-berth.heading));
            Vector3 attachment = boat_model_point(pose,
                (Vector3) { copysignf(spec.beam * .4f, local.x), spec.freeboard,
                    (end ? -.3f : .3f) * spec.length });
            Vector3 delta = Vector3Subtract(ties[end], attachment);
            float length = Vector3Length(delta);
            frame->harbor.lines[frame->harbor.line_count++]
                = (Transform3D) { .position = Vector3Lerp(attachment, ties[end], .5f),
                      .rotation = { -asinf(delta.y / length), atan2f(delta.x, delta.z), 0 },
                      .scale = { .045f, .045f, length } };
        }
    }
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
        for (int side = -1; side <= 1; side += 2) {
            // Swing from the shoulder, with a relaxed elbow and hands beside the hips.
            float upper = -side * stride * .55f;
            float lower = upper - .18f;
            Vector3 shoulder = { side * .33f, 1.29f, 0 };
            Vector3 elbow = Vector3Add(
                shoulder, Vector3Transform((Vector3) { 0, -.29f, 0 }, MatrixRotateX(upper)));
            Vector3 hand = Vector3Add(
                elbow, Vector3Transform((Vector3) { 0, -.27f, 0 }, MatrixRotateX(lower)));
            part(frame, position, heading, Vector3Lerp(shoulder, elbow, .5f),
                (Vector3) { .16f, .31f, .18f }, r->coat, upper);
            part(frame, position, heading, Vector3Lerp(elbow, hand, .5f),
                (Vector3) { .14f, .29f, .16f }, r->coat, lower);
            part(frame, position, heading, hand, (Vector3) { .13f, .13f, .14f }, skin, lower);
        }
        part(frame, position, heading, (Vector3) { 0, 1.56f, 0 }, (Vector3) { .3f, .34f, .3f },
            skin, 0);
        if (i % 3 == 0)
            part(frame, position, heading, (Vector3) { 0, .94f, -.38f },
                (Vector3) { .65f, .45f, .45f }, (Color) { 151, 116, 71, 255 }, 0);
    }
}

void boat_archipelago_lanterns_evaluate(double time, WaterfrontSceneFrame *frame)
{
    const WaterfrontLighting *light = &frame->lighting;
    bool night = light->window_clock
        ? light->window_minute >= 18 * 60 || light->window_minute < 6 * 60
        : light->phase >= .75f;
    if (!night || !isfinite(time))
        return;
    size_t count;
    const BoatArchipelagoLantern *lamps = boat_archipelago_lanterns(&count);
    float rotation = (float)remainder(time, 8) * (2 * PI / 8);
    for (size_t lamp = 0; lamp < count; ++lamp) {
        for (int face = 0; face < 4; ++face) {
            if (frame->scenery_count >= (int)(sizeof(frame->scenery) / sizeof(frame->scenery[0])))
                return;
            float angle = face * PI / 2;
            float gain = .4f + .6f * powf(fmaxf(0, cosf(angle - rotation)), 12);
            int i = frame->scenery_count++;
            frame->scenery[i].transform
                = (Transform3D) { .position = Vector3Add(lamps[lamp].position,
                                      (Vector3) { sinf(angle) * (lamps[lamp].width / 2 + .035f), 0,
                                          cosf(angle) * (lamps[lamp].width / 2 + .035f) }),
                      .rotation = { 0, angle, 0 },
                      .scale = { lamps[lamp].width, 2.6f, .04f } };
            frame->scenery[i].color = (Color) { 255 * gain, 228 * gain, 156 * gain, 255 };
            frame->scenery[i].emissive = true;
        }
    }
}

void boat_archipelago_windows_evaluate(WaterfrontSceneFrame *frame)
{
    size_t count;
    const BoatArchipelagoWindow *windows = boat_archipelago_windows(&count);
    for (size_t window = 0; window < count; ++window) {
        const BoatArchipelagoWindow *w = &windows[window];
        float dx = w->position.x - frame->camera.position.x;
        float dz = w->position.z - frame->camera.position.z;
        if (dx * dx + dz * dz > BUILDING_WINDOW_NEAR_RANGE * BUILDING_WINDOW_NEAR_RANGE)
            continue;
        float lit = building_window_lit(BUILDING_PROFILE_RESIDENTIAL, w->seed,
                        waterfront_window_minute(&frame->lighting))
            ? 1.0f
            : 0.0f;
        Color glass = building_window_glass(w->seed);
        for (int pane = 0; pane < 3; ++pane) {
            if (frame->scenery_count >= (int)(sizeof(frame->scenery) / sizeof(frame->scenery[0])))
                return;
            Vector3 position = w->position;
            float width = w->width, height = w->height;
            Color tint = glass;
            if (pane) {
                position = Vector3Add(position, Vector3Scale(w->normal, .055f));
                position.y += (pane == 1 ? .25f : -.28f) * w->height;
                width -= .08f;
                height *= pane == 1 ? .42f : .40f;
                float shade = pane == 1 ? 1.2f : .8f;
                tint = (Color) { Clamp(glass.r * shade, 0, 255), Clamp(glass.g * shade, 0, 255),
                    Clamp(glass.b * shade, 0, 255), 255 };
            }
            int i = frame->scenery_count++;
            frame->scenery[i].transform = (Transform3D) { .position = position,
                .scale = { w->normal.x ? .09f : width, height, w->normal.x ? width : .09f } };
            frame->scenery[i].color = waterfront_window_tint(tint, lit, &frame->lighting);
            frame->scenery[i].emissive = true;
        }
    }
}
