#include "boat_cargo.h"
#include "boat_map.h"
#include "raymath.h"
#include "waterfront_harbor.h"
#include "waterfront_traffic.h"

#include <math.h>
#include <string.h>

static const Vector3 crane_base = { -4, 5.5f, 3 };

static const BoatCargoWorkboat workboats[] = {
    { .name = "flat_deck",
        .hull = { 2.3f, 2.3f, 1.3f, 5 },
        .thrust = 3.1f,
        .deck_x = BOAT_CARGO_DECK_X,
        .deck_y = BOAT_CARGO_DECK_Y,
        .deck_z = BOAT_CARGO_DECK_Z,
        .berth_left = BOAT_CARGO_BERTH_LEFT,
        .berth_right = BOAT_CARGO_BERTH_RIGHT,
        .berth_front = BOAT_CARGO_BERTH_FRONT,
        .berth_back = BOAT_CARGO_BERTH_BACK },
};

const BoatCargoWorkboat *boat_cargo_workboat(unsigned preset)
{
    unsigned count = (unsigned)(sizeof(workboats) / sizeof(workboats[0]));
    return &workboats[preset < count ? preset : 0];
}

void boat_cargo_reset(BoatCargoSandbox *s) { boat_cargo_reset_at(s, (Vector2) { 0 }); }

void boat_cargo_reset_at(BoatCargoSandbox *s, Vector2 mooring)
{
    BoatSceneWorkSite site = { .x = mooring.x, .z = mooring.y };
    boat_cargo_reset_sites(s, &site, 1);
}

bool boat_cargo_reset_sites(BoatCargoSandbox *s, const BoatSceneWorkSite *sites, unsigned count)
{
    return boat_cargo_reset_sites_with(s, 0, sites, count);
}

bool boat_cargo_reset_sites_with(
    BoatCargoSandbox *s, unsigned workboat, const BoatSceneWorkSite *sites, unsigned count)
{
    *s = (BoatCargoSandbox) { .cargo = { .state = BOAT_CARGO_PAD, .load = 1 },
        .moored = true,
        .boat = boat_cargo_workboat(workboat) };
    if (!count || count > BOAT_SCENE_WORK_SITES)
        return false;
    s->crane_count = count;
    s->active = count;
    for (unsigned i = 0; i < count; ++i) {
        s->cranes[i] = (BoatCargoCrane) { .origin = { -sites[i].x, 0, sites[i].z },
            .heading = sites[i].heading,
            .angle = -2.819842f,
            .reach = 3.162278f,
            .rope = 3.2f,
            .preset = sites[i].preset,
            .hidden = sites[i].hidden };
        if (s->active == count && sites[i].preset == BOAT_SCENE_WORK_SERVICE_PLATFORM
            && !sites[i].hidden)
            s->active = i;
    }
    if (s->active == count) {
        s->active = 0;
        return false;
    }
    s->source = s->active;
    s->cargo.pad = (unsigned char)s->active;
    s->cargo.position = boat_cargo_site_point(&s->cranes[s->active], (Vector3) { -5, 1.4f, 0 });
    return true;
}

static void event(BoatCargoSandbox *s, BoatCargoEventKind kind, unsigned site)
{
    if (s->event_count < BOAT_CARGO_EVENTS)
        s->events[s->event_count++] = (BoatCargoEvent) { kind, site, s->cargo.load };
}

unsigned boat_cargo_take_events(BoatCargoSandbox *s, BoatCargoEvent *events, unsigned capacity)
{
    unsigned count = s->event_count < capacity ? s->event_count : capacity;
    memcpy(events, s->events, count * sizeof(events[0]));
    memmove(s->events, s->events + count, (s->event_count - count) * sizeof(events[0]));
    s->event_count -= count;
    return count;
}

bool boat_cargo_recover(BoatCargoSandbox *s)
{
    BoatCargo *c = &s->cargo;
    if (c->state == BOAT_CARGO_HANGING || c->state == BOAT_CARGO_SECURED)
        return false;
    *c = (BoatCargo) { .state = BOAT_CARGO_PAD,
        .position = boat_cargo_site_point(&s->cranes[s->source], (Vector3) { -5, 1.4f, 0 }),
        .pad = (unsigned char)s->source,
        .delivered = c->delivered,
        .receivers = c->receivers,
        .load = c->load };
    event(s, BOAT_CARGO_EVENT_RESET, s->source);
    return true;
}

bool boat_cargo_supply_ready(const BoatCargoSandbox *s)
{
    const BoatCargo *c = &s->cargo;
    return c->delivered && (c->state == BOAT_CARGO_PAD || c->state == BOAT_CARGO_FLOATING);
}

bool boat_cargo_supply(BoatCargoSandbox *s, unsigned site)
{
    if (site >= s->crane_count || s->cranes[site].hidden
        || s->cranes[site].preset != BOAT_SCENE_WORK_SERVICE_PLATFORM
        || !boat_cargo_supply_ready(s))
        return false;
    s->source = site;
    s->cargo = (BoatCargo) { .state = BOAT_CARGO_PAD,
        .position = boat_cargo_site_point(&s->cranes[site], (Vector3) { -5, 1.4f, 0 }),
        .pad = (unsigned char)site,
        .load = s->cargo.load + 1 };
    return true;
}

Vector3 boat_cargo_site_direction(const BoatCargoCrane *c, Vector3 v)
{
    // Boat heading's renderer rotation, MatrixRotateY(heading); heading zero is exact.
    float cs = cosf(c->heading), sn = sinf(c->heading);
    return (Vector3) { cs * v.x + sn * v.z, v.y, cs * v.z - sn * v.x };
}

Vector3 boat_cargo_site_point(const BoatCargoCrane *c, Vector3 local)
{
    return Vector3Add(c->origin, boat_cargo_site_direction(c, local));
}

Vector3 boat_cargo_site_local(const BoatCargoCrane *c, Vector3 p)
{
    Vector3 d = Vector3Subtract(p, c->origin);
    float cs = cosf(c->heading), sn = sinf(c->heading);
    return (Vector3) { cs * d.x - sn * d.z, d.y, cs * d.z + sn * d.x };
}

Vector2 boat_cargo_berth(const BoatCargoCrane *c)
{
    return (Vector2) { -c->origin.x, c->origin.z };
}

static BoatCargoCrane site_frame(const BoatSceneWorkSite *site)
{
    return (BoatCargoCrane) { .origin = { -site->x, 0, site->z }, .heading = site->heading };
}

// The layout's survey envelope in the site frame, with simulation x (-renderer X) relative to
// the berth.
#define SITE_MIN_X -3
#define SITE_MAX_X 11
#define SITE_MIN_Z -5
#define SITE_MAX_Z 12

static Vector2 site_sim(const BoatCargoCrane *frame, float x, float z)
{
    Vector3 p = boat_cargo_site_point(frame, (Vector3) { -x, 0, z });
    return (Vector2) { -p.x, p.z };
}

bool boat_cargo_site_clear(const BoatMap *map, const BoatSceneDefinition *scene, BoatWater water,
    const BoatSceneWorkSite *site)
{
    BoatCargoCrane frame = site_frame(site);
    for (int z = SITE_MIN_Z; z <= SITE_MAX_Z; ++z)
        for (int x = SITE_MIN_X; x <= SITE_MAX_X; ++x) {
            Vector2 q = site_sim(&frame, (float)x, (float)z), normal;
            if (!boat_map_water(map, q, 1)
                || waterfront_harbor_contact_height_scene(scene, q, 1, -1, 9, water, 0, &normal))
                return false;
        }
    double period = 0;
    for (int i = 0; i < WATERFRONT_TRAFFIC_ALL; ++i)
        period = fmax(period, waterfront_traffic_period(i));
    for (unsigned i = 0; scene && scene->traffic_present && i < scene->traffic.actor_count; ++i)
        period = fmax(period, waterfront_traffic_period_routes(&scene->traffic, (int)i));
    for (double now = 0; now <= period; now += .25) {
        WaterfrontTrafficContact contact;
        if (waterfront_traffic_contact_height_scene(scene, site_sim(&frame, 2, 2), site->heading,
                12, 12, 10, -1, 9, water, now, &contact))
            return false;
    }
    return true;
}

// Separating axes of two turned envelope rectangles, in renderer X/Z.
bool boat_cargo_sites_apart(const BoatSceneWorkSite *a, const BoatSceneWorkSite *b)
{
    const BoatSceneWorkSite *site[2] = { a, b };
    Vector2 centre[2], axis[2][2];
    float half[2] = { (SITE_MAX_X - SITE_MIN_X) * .5f, (SITE_MAX_Z - SITE_MIN_Z) * .5f };
    for (int i = 0; i < 2; ++i) {
        BoatCargoCrane frame = site_frame(site[i]);
        Vector3 p = boat_cargo_site_point(&frame,
            (Vector3) { -(SITE_MIN_X + SITE_MAX_X) * .5f, 0, (SITE_MIN_Z + SITE_MAX_Z) * .5f });
        Vector3 u = boat_cargo_site_direction(&frame, (Vector3) { 1, 0, 0 });
        Vector3 v = boat_cargo_site_direction(&frame, (Vector3) { 0, 0, 1 });
        centre[i] = (Vector2) { p.x, p.z };
        axis[i][0] = (Vector2) { u.x, u.z };
        axis[i][1] = (Vector2) { v.x, v.z };
    }
    Vector2 d = Vector2Subtract(centre[1], centre[0]);
    for (int i = 0; i < 4; ++i) {
        Vector2 n = axis[i / 2][i % 2];
        float reach = 0;
        for (int j = 0; j < 2; ++j)
            for (int k = 0; k < 2; ++k)
                reach += half[k] * fabsf(Vector2DotProduct(axis[j][k], n));
        if (fabsf(Vector2DotProduct(d, n)) > reach)
            return true;
    }
    return false;
}

static bool near_frame(const BoatCargoCrane *frame, Vector3 point, float margin)
{
    Vector3 local = boat_cargo_site_local(frame, point);
    return -local.x > SITE_MIN_X - margin && -local.x < SITE_MAX_X + margin
        && local.z > SITE_MIN_Z - margin && local.z < SITE_MAX_Z + margin;
}

bool boat_cargo_site_near(const BoatSceneWorkSite *site, Vector2 point, float margin)
{
    BoatCargoCrane frame = site_frame(site);
    return near_frame(&frame, (Vector3) { -point.x, 0, point.y }, margin);
}

bool boat_cargo_site_occupied(const BoatCargoSandbox *s, unsigned site, const Boat *boat)
{
    // The workboat's hull reaches about 3 m from its centre; the crate is one bounded body.
    const BoatCargoCrane *c = &s->cranes[site];
    return near_frame(c, (Vector3) { -boat->position.x, 0, boat->position.y }, 3)
        || near_frame(c, s->cargo.position, 1);
}

bool boat_cargo_site_enable(BoatCargoSandbox *s, unsigned site, const Boat *boat)
{
    if (site >= s->crane_count || !s->cranes[site].hidden
        || boat_cargo_site_occupied(s, site, boat))
        return false;
    s->cranes[site].hidden = false;
    return true;
}

Vector3 boat_cargo_tip(const BoatCargoCrane *c)
{
    // Grouped as origin + base + arm so heading zero repeats the S0 rounding exactly.
    return Vector3Add(boat_cargo_site_point(c, crane_base),
        boat_cargo_site_direction(
            c, (Vector3) { sinf(c->angle) * c->reach, 0, cosf(c->angle) * c->reach }));
}

Vector3 boat_cargo_hook(const BoatCargoSandbox *s)
{
    if (s->cargo.state == BOAT_CARGO_HANGING)
        return Vector3Add(s->cargo.position, (Vector3) { 0, BOAT_CARGO_HALF, 0 });
    const BoatCargoCrane *c = &s->cranes[s->active];
    return Vector3Subtract(boat_cargo_tip(c), (Vector3) { 0, c->rope, 0 });
}

void boat_cargo_motor(BoatCargoCrane *c, float slew, float reach, float hoist)
{
    c->angle = remainderf(c->angle + Clamp(slew, -1, 1) * .55f * BOAT_STEP, 2 * PI);
    c->reach = Clamp(c->reach + Clamp(reach, -1, 1) * 1.5f * BOAT_STEP, 2, 7);
    c->rope = Clamp(c->rope - Clamp(hoist, -1, 1) * 1.3f * BOAT_STEP, .7f, 5.2f);
}

bool boat_cargo_hook_toggle(BoatCargoSandbox *s)
{
    BoatCargo *c = &s->cargo;
    if (c->state == BOAT_CARGO_HANGING) {
        c->state = c->deck_contact ? BOAT_CARGO_LOOSE : BOAT_CARGO_FLOATING;
        c->settled = 0;
        return true;
    }
    if (!s->moored || c->state == BOAT_CARGO_SECURED)
        return false;
    Vector3 top = Vector3Add(c->position, (Vector3) { 0, BOAT_CARGO_HALF, 0 });
    float distance = Vector3Distance(top, boat_cargo_hook(s));
    if (distance > 1.25f)
        return false;
    // Preserve position/velocity; no teleport or initial constraint impulse.
    BoatCargoCrane *crane = &s->cranes[s->active];
    crane->rope = Clamp(Vector3Distance(boat_cargo_tip(crane), top), .7f, 5.2f);
    c->state = BOAT_CARGO_HANGING;
    c->settled = 0;
    return true;
}

static bool deck_inside(const BoatCargoWorkboat *wb, Vector3 p)
{
    return fabsf(p.x) <= wb->deck_x - BOAT_CARGO_HALF && fabsf(p.z) <= wb->deck_z - BOAT_CARGO_HALF;
}

bool boat_cargo_lash_toggle(BoatCargoSandbox *s, const Boat *boat)
{
    BoatCargo *c = &s->cargo;
    if (s->mooring || Vector2Length(boat->velocity) > .5f || boat_pose_up(boat) < .9f)
        return false;
    if (c->state == BOAT_CARGO_SECURED) {
        c->velocity = boat_pose_motion(boat, c->deck_local).velocity;
        c->state = BOAT_CARGO_LOOSE;
        c->settled = 0;
        return true;
    }
    // A crate resting across the deck edge is supported but cannot be lashed.
    if (c->state != BOAT_CARGO_LOOSE || !c->deck_contact || c->settled < .5f
        || !deck_inside(s->boat, c->deck_local))
        return false;
    c->state = BOAT_CARGO_SECURED;
    event(s, BOAT_CARGO_EVENT_SECURED, s->active);
    return true;
}

bool boat_cargo_moor_toggle(BoatCargoSandbox *s, const Boat *boat)
{
    if (s->mooring) {
        s->mooring = false;
        return true;
    }
    if (s->moored) {
        if (s->cargo.state == BOAT_CARGO_HANGING || s->cargo.state == BOAT_CARGO_LOOSE)
            return false;
        s->moored = false;
        return true;
    }
    BoatCargoMoorStatus status = boat_cargo_moor_status(s, boat);
    if (status.reason != BOAT_CARGO_MOOR_READY)
        return false;
    s->active = status.site;
    s->mooring = true;
    return true;
}

BoatCargoMoorStatus boat_cargo_moor_status(const BoatCargoSandbox *s, const Boat *boat)
{
    unsigned site = s->active;
    Vector3 hull = { -boat->position.x, 0, boat->position.y };
    if (!s->moored && !s->mooring)
        for (unsigned i = 0; i < s->crane_count; ++i)
            if (!s->cranes[i].hidden
                && (s->cranes[site].hidden
                    || Vector3Distance(hull, s->cranes[i].origin)
                        < Vector3Distance(hull, s->cranes[site].origin)))
                site = i;
    const BoatCargoCrane *c = &s->cranes[site];
    Vector3 local = boat_cargo_site_local(c, hull);
    Vector2 offset = { local.x, local.z };
    BoatCargoMoorStatus status = { .offset = offset,
        .distance = Vector2Length(offset),
        .speed = Vector2Length(boat->velocity),
        .heading_error = -remainderf(boat->heading - c->heading, 2 * PI),
        .site = site };
    if (boat_pose_up(boat) < .75f || boat->motion_state == BOAT_CAPSIZED)
        status.reason = BOAT_CARGO_MOOR_UPRIGHT;
    else if (offset.x < s->boat->berth_left || offset.x > s->boat->berth_right
        || offset.y < s->boat->berth_front || offset.y > s->boat->berth_back)
        status.reason = BOAT_CARGO_MOOR_DISTANCE;
    else if (fabsf(status.heading_error) > BOAT_CARGO_MOOR_ANGLE)
        status.reason = BOAT_CARGO_MOOR_HEADING;
    else if (status.speed > BOAT_CARGO_MOOR_SPEED)
        status.reason = BOAT_CARGO_MOOR_FAST;
    return status;
}

void boat_cargo_brake(Boat *boat)
{
    boat->throttle = 0;
    boat->velocity = Vector2MoveTowards(boat->velocity, (Vector2) { 0 }, 2 * BOAT_STEP);
}

void boat_cargo_moor_step(BoatCargoSandbox *s, Boat *boat)
{
    if (!s->mooring)
        return;
    if (boat_pose_up(boat) < .75f || boat->motion_state == BOAT_CAPSIZED) {
        s->mooring = false;
        return;
    }
    const BoatCargoCrane *c = &s->cranes[s->active];
    Vector2 berth = boat_cargo_berth(c);
    Vector2 previous = boat->position;
    float previous_heading = boat->heading;
    boat->position = Vector2MoveTowards(boat->position, berth, 1.5f * BOAT_STEP);
    boat->heading += Clamp(
        -remainderf(boat->heading - c->heading, 2 * PI), -.6f * BOAT_STEP, .6f * BOAT_STEP);
    boat->heading = remainderf(boat->heading, 2 * PI);
    boat->velocity = Vector2Scale(Vector2Subtract(boat->position, previous), 1 / BOAT_STEP);
    boat->yaw_velocity = remainderf(boat->heading - previous_heading, 2 * PI) / BOAT_STEP;
    boat->throttle = boat->engine_response = 0;
    boat->wheel = boat->engine_angle = 0;
    if (Vector2Distance(boat->position, berth) < .0001f
        && fabsf(remainderf(boat->heading - c->heading, 2 * PI)) < .0001f) {
        boat->position = berth;
        boat->heading = c->heading;
        boat->velocity = (Vector2) { 0 };
        boat->yaw_velocity = 0;
        s->mooring = false;
        s->moored = true;
    }
}

BoatCargoShadow boat_cargo_shadow(const BoatCargoSandbox *s, const Boat *boat,
    const BoatWorld *world, double now, BoatCargoSurface surface)
{
    BoatCargoShadow result = { 0 };
    Vector3 p = s->cargo.position;
    float level = 0, min_x = p.x - .48f, max_x = p.x + .48f;
    float min_z = p.z - .48f, max_z = p.z + .48f;
    if (surface == BOAT_CARGO_SHADOW_DECK) {
        Vector3 normal = Vector3Transform((Vector3) { 0, 1, 0 }, boat_pose_rotation(boat));
        if (normal.y < .75f)
            return result;
        Vector3 deck = boat_pose_attachment(boat, (Vector3) { 0, s->boat->deck_y, 0 });
        p.y = deck.y - (normal.x * (p.x - deck.x) + normal.z * (p.z - deck.z)) / normal.y;
        level = p.y;
        p = Vector3Transform(Vector3Subtract(p, boat_pose_hull(boat).position),
            MatrixTranspose(boat_pose_rotation(boat)));
        min_x = fmaxf(p.x - .48f, -s->boat->deck_x);
        max_x = fminf(p.x + .48f, s->boat->deck_x);
        min_z = fmaxf(p.z - .48f, -s->boat->deck_z);
        max_z = fminf(p.z + .48f, s->boat->deck_z);
        p.y = s->boat->deck_y + .04f;
    } else if (surface == BOAT_CARGO_SHADOW_PAD) {
        // The active crane's pad; clip in its site frame.
        p = boat_cargo_site_local(&s->cranes[s->active], p);
        min_x = fmaxf(p.x - .48f, -6.5f);
        max_x = fminf(p.x + .48f, -3.5f);
        min_z = fmaxf(p.z - .48f, -2);
        max_z = fminf(p.z + .48f, 4);
        level = 1;
        p.y = level + .04f;
    }
    if (max_x <= min_x || max_z <= min_z)
        return result;
    if (surface != BOAT_CARGO_SHADOW_WATER && s->cargo.position.y - BOAT_CARGO_HALF < level - .08f)
        return result;
    result.points[0] = (Vector3) { min_x, p.y, min_z };
    result.points[1] = (Vector3) { min_x, p.y, max_z };
    result.points[2] = (Vector3) { max_x, p.y, max_z };
    result.points[3] = (Vector3) { max_x, p.y, min_z };
    for (int i = 0; i < 4; ++i) {
        if (surface == BOAT_CARGO_SHADOW_DECK)
            result.points[i] = boat_pose_attachment(boat, result.points[i]);
        else if (surface == BOAT_CARGO_SHADOW_PAD)
            result.points[i] = boat_cargo_site_point(&s->cranes[s->active], result.points[i]);
        else if (surface == BOAT_CARGO_SHADOW_WATER) {
            result.points[i].y = boat_water_query_borrowed(&world->water,
                                     (Vector2) { -result.points[i].x, result.points[i].z }, now)
                                     .height
                + .04f;
            level += (result.points[i].y - .04f) * .25f;
        }
    }
    result.count = 4;
    result.clearance = s->cargo.position.y - BOAT_CARGO_HALF - level;
    return result;
}

static void contact(BoatCargo *c, const Boat *boat, const BoatCargoWorkboat *wb)
{
    Matrix inverse = MatrixTranspose(boat_pose_rotation(boat));
    Vector3 centre = boat_pose_attachment(boat, (Vector3) { 0 });
    Vector3 local = Vector3Transform(Vector3Subtract(c->position, centre), inverse);
    float level = wb->deck_y + BOAT_CARGO_HALF;
    c->deck_contact = false;
    // The deck tops a solid hull box. Before this, a crate overlapping the deck edge found no
    // support and sank through the hull to the water.
    float over_x = wb->deck_x + BOAT_CARGO_HALF - fabsf(local.x);
    float over_z = wb->deck_z + BOAT_CARGO_HALF - fabsf(local.z);
    if (boat_pose_up(boat) < .75f || over_x <= 0 || over_z <= 0 || local.y > level + .03f
        || local.y < level - 1.5f)
        return;
    if (fabsf(local.x) > wb->deck_x || fabsf(local.z) > wb->deck_z) {
        // Its centre is past the edge: below deck level, the hull's side pushes it out along
        // the shallower overlap and stops it moving inward; it then falls beside the boat.
        if (local.y >= level)
            return;
        Vector3 point_velocity = boat_pose_motion(boat, local).velocity;
        Vector3 relative = Vector3Transform(Vector3Subtract(c->velocity, point_velocity), inverse);
        if (over_x < over_z) {
            local.x += copysignf(over_x, local.x);
            relative.x = local.x > 0 ? fmaxf(relative.x, 0) : fminf(relative.x, 0);
        } else {
            local.z += copysignf(over_z, local.z);
            relative.z = local.z > 0 ? fmaxf(relative.z, 0) : fminf(relative.z, 0);
        }
        c->position = boat_pose_attachment(boat, local);
        c->velocity
            = Vector3Add(point_velocity, Vector3Transform(relative, boat_pose_rotation(boat)));
        return;
    }
    if (local.y < level - .35f)
        return;
    Vector3 point_velocity = boat_pose_motion(boat, local).velocity;
    Vector3 relative = Vector3Transform(Vector3Subtract(c->velocity, point_velocity), inverse);
    if (relative.y > .6f)
        return;
    local.y = level;
    c->position = boat_pose_attachment(boat, local);
    // Bounded sliding friction in the support frame, retaining deck-point velocity.
    relative.x *= .93f;
    relative.z *= .93f;
    relative.y = fmaxf(0, -relative.y * .08f);
    c->velocity = Vector3Add(point_velocity, Vector3Transform(relative, boat_pose_rotation(boat)));
    c->deck_local = local;
    c->deck_contact = true;
    c->rotation = boat_pose_hull(boat).rotation;
    c->settled = Vector3Length(relative) < .18f ? c->settled + BOAT_STEP : 0;
}

void boat_cargo_step(BoatCargoSandbox *s, const Boat *boat, const BoatWorld *world, double now)
{
    BoatCargo *c = &s->cargo;
    if (c->state == BOAT_CARGO_SECURED) {
        c->position = boat_pose_attachment(boat, c->deck_local);
        c->velocity = boat_pose_motion(boat, c->deck_local).velocity;
        c->rotation = boat_pose_hull(boat).rotation;
        // An inverted boat cannot keep its cargo; recovery always remains available.
        if (boat_pose_up(boat) < .5f) {
            c->state = BOAT_CARGO_FLOATING;
            c->settled = 0;
            c->deck_contact = false;
        }
        return;
    }
    Vector3 old = c->position;
    c->velocity.y -= 9.81f * BOAT_STEP;
    c->velocity = Vector3Scale(c->velocity, .999f);
    c->position = Vector3Add(old, Vector3Scale(c->velocity, BOAT_STEP));
    if (c->state == BOAT_CARGO_HANGING) {
        const BoatCargoCrane *crane = &s->cranes[s->active];
        Vector3 anchor
            = Vector3Subtract(boat_cargo_tip(crane), (Vector3) { 0, BOAT_CARGO_HALF, 0 });
        // Unilateral cable: a supported load may slacken the rope. Four fixed projections.
        for (int iteration = 0; iteration < 4; ++iteration) {
            Vector3 delta = Vector3Subtract(c->position, anchor);
            float length = Vector3Length(delta);
            if (length > crane->rope) {
                Vector3 correction = Vector3Scale(delta, crane->rope / length - 1);
                c->position = Vector3Add(c->position, correction);
                c->velocity = Vector3Add(c->velocity, Vector3Scale(correction, 1 / BOAT_STEP));
            }
        }
        // Apply only constraint corrections to velocity. Differencing global positions here
        // amplifies float rounding by 120 and changes sway when the site is translated.
        // Damping keeps sway readable without an endless precision wait.
        c->velocity.x *= .997f;
        c->velocity.z *= .997f;
    }
    contact(c, boat, s->boat);
    if (!c->deck_contact) {
        c->settled = 0;
        c->rotation = (Vector3) { 0 };
    }
    bool pad = false;
    for (unsigned i = 0; i < s->crane_count && !pad; ++i) {
        if (s->cranes[i].hidden)
            continue;
        Vector3 local = boat_cargo_site_local(&s->cranes[i], c->position);
        pad = fabsf(local.x + 5) < 1.1f && local.z > -1.6f && local.z < 3.6f;
        if (pad)
            c->pad = (unsigned char)i;
    }
    float floor = pad
        ? 1.4f
        : boat_water_query_borrowed(&world->water, (Vector2) { -c->position.x, c->position.z }, now)
                .height
            + .12f;
    bool water = !pad && !c->deck_contact && c->position.y < floor;
    if (!c->deck_contact && c->position.y < floor) {
        c->position.y = floor;
        c->velocity.y = fmaxf(0, -c->velocity.y * .12f);
        c->velocity.x *= .96f;
        c->velocity.z *= .96f;
    }
    if (c->state != BOAT_CARGO_HANGING) {
        c->state = c->deck_contact ? BOAT_CARGO_LOOSE : pad ? BOAT_CARGO_PAD : BOAT_CARGO_FLOATING;
    }
    // A single bounded body; falling/suspended simulation never depends on the camera.
    c->velocity = Vector3ClampValue(c->velocity, 0, 12);
    // S1c events. A delivery commits once per load and receiver after the released load has
    // rested on a receiving pad; a loss is the first water contact after a deck or pad support.
    if (pad || c->deck_contact)
        c->wet = false;
    else if (water && c->state == BOAT_CARGO_FLOATING && !c->wet) {
        c->wet = true;
        event(s, BOAT_CARGO_EVENT_LOST, s->active);
    }
    bool receiving = c->state == BOAT_CARGO_PAD
        && s->cranes[c->pad].preset == BOAT_SCENE_WORK_RECEIVING_PLATFORM;
    c->rest = receiving && Vector3Length(c->velocity) < .18f ? c->rest + BOAT_STEP : 0;
    if (!(c->receivers & 1u << c->pad) && c->rest >= .5f) {
        c->delivered = true;
        c->receivers |= (unsigned char)(1u << c->pad);
        event(s, BOAT_CARGO_EVENT_DELIVERED, c->pad);
    }
    if (c->state == BOAT_CARGO_FLOATING && world->definition->mapped
        && !boat_map_water(&world->resources->map, (Vector2) { -c->position.x, c->position.z },
            BOAT_CARGO_HALF + .1f)) {
        // A dropped crate stops at the bank instead of drifting through rendered land.
        // Suspended loads use the surveyed crane envelope, avoiding a second rope constraint.
        c->position.x = old.x;
        c->position.z = old.z;
        c->velocity.x = c->velocity.z = 0;
    }
}

void boat_cargo_load_effect(const BoatCargoSandbox *s, const Boat *before, Boat *boat)
{
    if (s->cargo.state != BOAT_CARGO_SECURED)
        return;
    // Added mass reduces changes of speed (including braking), preserving empty physics.
    boat->velocity = Vector2Lerp(before->velocity, boat->velocity, .68f);
    float trim = Clamp(s->cargo.deck_local.z * .035f, -.055f, .055f);
    float heel = Clamp(-s->cargo.deck_local.x * .09f, -.075f, .075f);
    // Approximate the existing six supports' angular stiffness, giving a readable 3–4 degree
    // equilibrium at the deck edges. This is authored balance, not a wider buoyancy model.
    boat->pitch_velocity += (trim * 45 - boat->pitch_velocity * .6f) * BOAT_STEP;
    boat->roll_velocity += (heel * 60 - boat->roll_velocity * .6f) * BOAT_STEP;
}
