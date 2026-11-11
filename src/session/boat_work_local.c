#include "boat_work_local.h"

#include "boat_net_private.h"
#include "boat_steering.h"
#include "boat_throttle.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

// --- Codec ---------------------------------------------------------------------------------

typedef struct {
    uint8_t *p, *end;
    bool error;
} Writer;

typedef struct {
    const uint8_t *p, *end;
    bool error;
} Reader;

static void put8(Writer *w, unsigned value)
{
    if (w->p >= w->end) {
        w->error = true;
        return;
    }
    *w->p++ = (uint8_t)value;
}

static void put32(Writer *w, uint32_t value)
{
    for (int shift = 24; shift >= 0; shift -= 8)
        put8(w, (uint8_t)(value >> shift));
}

static void put64(Writer *w, uint64_t value)
{
    put32(w, (uint32_t)(value >> 32));
    put32(w, (uint32_t)value);
}

static void putf(Writer *w, float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    put32(w, bits);
}

static void putd(Writer *w, double value)
{
    uint64_t bits;
    memcpy(&bits, &value, sizeof(bits));
    put64(w, bits);
}

static void put2(Writer *w, Vector2 v)
{
    putf(w, v.x);
    putf(w, v.y);
}

static void put3(Writer *w, Vector3 v)
{
    putf(w, v.x);
    putf(w, v.y);
    putf(w, v.z);
}

// Text of at most CAPACITY - 1 bytes; NULL is empty.
static void put_text(Writer *w, const char *text, size_t capacity)
{
    size_t n = text ? strlen(text) : 0;
    if (n >= capacity) {
        w->error = true;
        return;
    }
    put8(w, (unsigned)n);
    for (size_t i = 0; i < n; ++i)
        put8(w, (uint8_t)text[i]);
}

static unsigned get8(Reader *r)
{
    if (r->p >= r->end) {
        r->error = true;
        return 0;
    }
    return *r->p++;
}

static uint32_t get32(Reader *r)
{
    uint32_t value = 0;
    for (int i = 0; i < 4; ++i)
        value = value << 8 | get8(r);
    return value;
}

static uint64_t get64(Reader *r)
{
    uint64_t high = get32(r);
    return high << 32 | get32(r);
}

// A finite float; non-finite values are malformed.
static float getf(Reader *r)
{
    uint32_t bits = get32(r);
    float value;
    memcpy(&value, &bits, sizeof(value));
    if (!isfinite(value))
        r->error = true;
    return value;
}

static float get_any_float(Reader *r)
{
    uint32_t bits = get32(r);
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static double getd(Reader *r)
{
    uint64_t bits = get64(r);
    double value;
    memcpy(&value, &bits, sizeof(value));
    if (!isfinite(value))
        r->error = true;
    return value;
}

static Vector2 get2(Reader *r)
{
    Vector2 v;
    v.x = getf(r);
    v.y = getf(r);
    return v;
}

static Vector3 get3(Reader *r)
{
    Vector3 v;
    v.x = getf(r);
    v.y = getf(r);
    v.z = getf(r);
    return v;
}

static bool get_bool(Reader *r)
{
    unsigned value = get8(r);
    if (value > 1)
        r->error = true;
    return value != 0;
}

static void get_text(Reader *r, char *out, size_t capacity)
{
    size_t n = get8(r);
    if (n >= capacity) {
        r->error = true;
        return;
    }
    for (size_t i = 0; i < n; ++i) {
        char c = (char)get8(r);
        if (!c)
            r->error = true;
        out[i] = c;
    }
    out[n] = 0;
}

size_t boat_work_encode_frame(const BoatWorkFrame *f, uint8_t *out)
{
    Writer w = { out, out + BOAT_NET_MAX_PAYLOAD, false };
    put8(&w, f->actions & BOAT_WORK_ACTIONS);
    put8(&w, f->aim <= 3 ? f->aim : 0);
    put8(&w, f->active);
    put8(&w, f->demo);
    putf(&w, f->slew);
    putf(&w, f->reach);
    putf(&w, f->hoist);
    putf(&w, f->steering_target);
    putf(&w, f->steering_rate);
    putf(&w, f->thrust_target);
    return w.error ? 0 : (size_t)(w.p - out);
}

bool boat_work_decode_frame(const uint8_t *data, size_t size, BoatWorkFrame *frame)
{
    Reader r = { data, data + size, false };
    BoatWorkFrame f = { 0 };
    f.actions = get8(&r);
    f.aim = get8(&r);
    f.active = get_bool(&r);
    f.demo = get_bool(&r);
    f.slew = getf(&r);
    f.reach = getf(&r);
    f.hoist = getf(&r);
    f.steering_target = get_any_float(&r); // Non-finite: steer by rate.
    f.steering_rate = getf(&r);
    f.thrust_target = getf(&r);
    if (r.error || r.p != r.end || (f.actions & ~BOAT_WORK_ACTIONS) || f.aim > 3
        || fabsf(f.slew) > 1 || fabsf(f.reach) > 1 || fabsf(f.hoist) > 1)
        return false;
    *frame = f;
    return true;
}

static void put_boat(Writer *w, const Boat *b)
{
    put2(w, b->position);
    put2(w, b->velocity);
    const float values[] = { b->heading, b->throttle, b->engine_response, b->planing, b->height,
        b->vertical_velocity, b->pitch, b->pitch_velocity, b->roll, b->roll_velocity, b->dry_time,
        b->landing_time, b->inverted_contact_time, b->turn_roll_load, b->wave_spray, b->wheel,
        b->engine_angle, b->yaw_velocity, b->physical_wake_time };
    for (size_t i = 0; i < sizeof(values) / sizeof(*values); ++i)
        putf(w, values[i]);
    put2(w, b->physical_wake_start);
    put8(w, (unsigned)b->motion_state);
    put32(w, b->landings);
}

static void get_boat(Reader *r, Boat *b)
{
    b->position = get2(r);
    b->velocity = get2(r);
    float *values[] = { &b->heading, &b->throttle, &b->engine_response, &b->planing, &b->height,
        &b->vertical_velocity, &b->pitch, &b->pitch_velocity, &b->roll, &b->roll_velocity,
        &b->dry_time, &b->landing_time, &b->inverted_contact_time, &b->turn_roll_load,
        &b->wave_spray, &b->wheel, &b->engine_angle, &b->yaw_velocity, &b->physical_wake_time };
    for (size_t i = 0; i < sizeof(values) / sizeof(*values); ++i)
        *values[i] = getf(r);
    b->physical_wake_start = get2(r);
    unsigned state = get8(r);
    if (state > BOAT_CAPSIZED)
        r->error = true;
    b->motion_state = (BoatMotionState)state;
    b->landings = get32(r);
}

static void put_cargo(Writer *w, const BoatCargo *c)
{
    put8(w, (unsigned)c->state);
    put3(w, c->position);
    put3(w, c->velocity);
    put3(w, c->deck_local);
    put3(w, c->rotation);
    putf(w, c->settled);
    put8(w, c->pad);
    put8(w, c->deck_contact);
    put8(w, c->delivered);
    put8(w, c->receivers);
    put8(w, (unsigned)c->support.kind);
    put8(w, c->support.pad);
    put3(w, c->support.deck_local);
    put32(w, c->load);
    put8(w, c->wet);
    putf(w, c->rest);
}

static void get_cargo(Reader *r, BoatCargo *c)
{
    unsigned state = get8(r);
    if (state > BOAT_CARGO_FLOATING)
        r->error = true;
    c->state = (BoatCargoState)state;
    c->position = get3(r);
    c->velocity = get3(r);
    c->deck_local = get3(r);
    c->rotation = get3(r);
    c->settled = getf(r);
    c->pad = (unsigned char)get8(r);
    c->deck_contact = get_bool(r);
    c->delivered = get_bool(r);
    c->receivers = (unsigned char)get8(r);
    unsigned kind = get8(r);
    if (kind > BOAT_CARGO_SUPPORT_DECK)
        r->error = true;
    c->support.kind = (BoatCargoSupportKind)kind;
    c->support.pad = (unsigned char)get8(r);
    c->support.deck_local = get3(r);
    c->load = get32(r);
    c->wet = get_bool(r);
    c->rest = getf(r);
}

static void put_segment(Writer *w, const BoatWakeSegment *s)
{
    put2(w, s->position);
    put2(w, s->direction);
    putd(w, s->emitted);
    putf(w, s->length);
    putf(w, s->amplitude);
    putf(w, s->inverse_speed);
}

static void get_segment(Reader *r, BoatWakeSegment *s)
{
    s->position = get2(r);
    s->direction = get2(r);
    s->emitted = getd(r);
    s->length = getf(r);
    s->amplitude = getf(r);
    s->inverse_speed = getf(r);
}

size_t boat_work_encode_motion(
    const BoatWorkSession *e, unsigned previous_next, bool checkpoint, uint8_t *out)
{
    Writer w = { out, out + BOAT_NET_MAX_PAYLOAD, false };
    const BoatCargoSandbox *s = &e->sandbox;
    const BoatWater *water = &e->world.water;
    put8(&w, checkpoint);
    put8(&w, e->crane_view);
    put8(&w, e->braking);
    put8(&w, s->moored);
    put8(&w, s->mooring);
    put8(&w, e->aim >= 0 && e->aim <= 3 ? (unsigned)e->aim : 0);
    put8(&w, e->target);
    put8(&w, s->active);
    put8(&w, s->source);
    putd(&w, e->lighting_hours);
    putd(&w, e->lighting_cycle_seconds);
    put_boat(&w, &e->boat);
    put_cargo(&w, &s->cargo);
    put8(&w, s->crane_count);
    for (unsigned i = 0; i < s->crane_count && i < BOAT_SCENE_WORK_SITES; ++i) {
        const BoatCargoCrane *c = &s->cranes[i];
        put3(&w, c->origin);
        putf(&w, c->heading);
        putf(&w, c->angle);
        putf(&w, c->reach);
        putf(&w, c->rope);
        put8(&w, c->preset);
        put8(&w, c->hidden);
    }
    // The trail's segments appended during the tick, oldest first.
    unsigned next = (unsigned)water->player_wake_next;
    unsigned added
        = checkpoint ? 0 : (next + BOAT_WAKE_SEGMENTS - previous_next) % BOAT_WAKE_SEGMENTS;
    put8(&w, next);
    put8(&w, (unsigned)water->player_wake_count);
    put8(&w, added);
    for (unsigned i = 0; i < added; ++i) {
        unsigned index = (previous_next + i) % BOAT_WAKE_SEGMENTS;
        put8(&w, index);
        put_segment(&w, &water->player_wake[index]);
    }
    return w.error ? 0 : (size_t)(w.p - out);
}

bool boat_work_decode_motion(const uint8_t *data, size_t size, uint64_t tick, BoatWorkSession *view)
{
    Reader r = { data, data + size, false };
    BoatWorkSession v = *view;
    BoatCargoSandbox *s = &v.sandbox;
    BoatWater *water = &v.world.water;
    get_bool(&r); // Checkpoint: its pages follow.
    v.crane_view = get_bool(&r);
    v.braking = get_bool(&r);
    s->moored = get_bool(&r);
    s->mooring = get_bool(&r);
    v.aim = (int)get8(&r);
    v.target = get8(&r);
    s->active = get8(&r);
    s->source = get8(&r);
    v.lighting_hours = getd(&r);
    v.lighting_cycle_seconds = getd(&r);
    get_boat(&r, &v.boat);
    get_cargo(&r, &s->cargo);
    s->crane_count = get8(&r);
    if (s->crane_count > BOAT_SCENE_WORK_SITES || v.aim > 3)
        return false;
    for (unsigned i = 0; i < s->crane_count; ++i) {
        BoatCargoCrane *c = &s->cranes[i];
        c->origin = get3(&r);
        c->heading = getf(&r);
        c->angle = getf(&r);
        c->reach = getf(&r);
        c->rope = getf(&r);
        c->preset = (unsigned char)get8(&r);
        c->hidden = get_bool(&r);
    }
    unsigned next = get8(&r), count = get8(&r), added = get8(&r);
    if (next >= BOAT_WAKE_SEGMENTS || count > BOAT_WAKE_SEGMENTS || added > BOAT_WAKE_SEGMENTS)
        return false;
    for (unsigned i = 0; i < added && !r.error; ++i) {
        unsigned index = get8(&r);
        if (index >= BOAT_WAKE_SEGMENTS)
            return false;
        get_segment(&r, &water->player_wake[index]);
    }
    if (r.error || r.p != r.end || v.target >= BOAT_SCENE_WORK_SITES
        || s->active >= BOAT_SCENE_WORK_SITES || s->source >= BOAT_SCENE_WORK_SITES
        || !boat_sandbox_lighting_valid(v.lighting_hours, v.lighting_cycle_seconds))
        return false;
    water->player_wake_next = (int)next;
    water->player_wake_count = (int)count;
    v.tick = tick;
    *view = v;
    return true;
}

size_t boat_work_encode_status(const BoatWorkSession *e, uint8_t *out)
{
    Writer w = { out, out + BOAT_NET_MAX_PAYLOAD, false };
    const BoatPluginObjective *o = &e->objective;
    put8(&w, o->quarantined);
    put8(&w, e->guide);
    put8(&w, o->hud_count <= BOAT_PLUGIN_HUD ? o->hud_count : 0);
    for (unsigned i = 0; i < o->hud_count && i < BOAT_PLUGIN_HUD; ++i) {
        put_text(&w, o->hud[i].id, BOAT_PLUGIN_TEXT);
        put_text(&w, o->hud[i].label, BOAT_PLUGIN_TEXT);
        put64(&w, (uint64_t)o->hud[i].value);
    }
    put_text(&w, e->notice, BOAT_WORK_LOCAL_TEXT);
    put8(&w, e->waiting != NULL);
    put_text(&w, e->waiting, BOAT_WORK_LOCAL_TEXT);
    return w.error ? 0 : (size_t)(w.p - out);
}

bool boat_work_decode_status(const uint8_t *data, size_t size, BoatWorkSession *view,
    char notice[BOAT_WORK_LOCAL_TEXT], char waiting[BOAT_WORK_LOCAL_TEXT])
{
    Reader r = { data, data + size, false };
    BoatPluginObjective o = view->objective;
    char notice_text[BOAT_WORK_LOCAL_TEXT], waiting_text[BOAT_WORK_LOCAL_TEXT];
    o.quarantined = get_bool(&r);
    unsigned guide = get8(&r);
    o.hud_count = get8(&r);
    if (o.hud_count > BOAT_PLUGIN_HUD || guide > BOAT_SCENE_WORK_SITES)
        return false;
    for (unsigned i = 0; i < o.hud_count && !r.error; ++i) {
        get_text(&r, o.hud[i].id, BOAT_PLUGIN_TEXT);
        get_text(&r, o.hud[i].label, BOAT_PLUGIN_TEXT);
        o.hud[i].value = (int64_t)get64(&r);
    }
    get_text(&r, notice_text, sizeof(notice_text));
    bool has_waiting = get_bool(&r);
    get_text(&r, waiting_text, sizeof(waiting_text));
    if (r.error || r.p != r.end)
        return false;
    view->objective = o;
    view->guide = guide;
    memcpy(notice, notice_text, sizeof(notice_text));
    memcpy(waiting, waiting_text, sizeof(waiting_text));
    view->notice = notice;
    view->waiting = has_waiting ? waiting : NULL;
    return true;
}

size_t boat_work_encode_trail(const BoatWorkSession *e, unsigned first, uint8_t *out)
{
    Writer w = { out, out + BOAT_NET_MAX_PAYLOAD, false };
    if (first >= BOAT_WAKE_SEGMENTS)
        return 0;
    unsigned n = BOAT_WAKE_SEGMENTS - first < BOAT_WORK_TRAIL_PAGE ? BOAT_WAKE_SEGMENTS - first
                                                                   : BOAT_WORK_TRAIL_PAGE;
    put8(&w, first);
    put8(&w, n);
    for (unsigned i = 0; i < n; ++i)
        put_segment(&w, &e->world.water.player_wake[first + i]);
    return w.error ? 0 : (size_t)(w.p - out);
}

bool boat_work_decode_trail(const uint8_t *data, size_t size, BoatWorkSession *view)
{
    Reader r = { data, data + size, false };
    unsigned first = get8(&r), n = get8(&r);
    if (first >= BOAT_WAKE_SEGMENTS || n > BOAT_WORK_TRAIL_PAGE || first + n > BOAT_WAKE_SEGMENTS)
        return false;
    BoatWakeSegment segments[BOAT_WORK_TRAIL_PAGE];
    for (unsigned i = 0; i < n; ++i)
        get_segment(&r, &segments[i]);
    if (r.error || r.p != r.end)
        return false;
    memcpy(&view->world.water.player_wake[first], segments, n * sizeof(segments[0]));
    return true;
}

// --- The authority's per-frame rules -----------------------------------------------------------

void boat_work_apply_frame(
    BoatWorkSession *s, const BoatWorkFrame *f, float *slew, float *reach, float *hoist)
{
    *slew = *reach = *hoist = 0;
    if (f->actions & BOAT_WORK_RETURN_CRATE)
        boat_work_recover_action(s);
    s->braking = false;
    if (!f->active)
        return;
    if (f->actions & BOAT_WORK_HOOK)
        boat_work_hook_action(s);
    if (f->actions & BOAT_WORK_LASH)
        boat_work_lash_action(s);
    if (f->actions & BOAT_WORK_MOOR)
        boat_work_moor_action(s);
    if (f->actions & BOAT_WORK_VIEW)
        boat_work_view_action(s);
    if (s->crane_view && s->sandbox.moored && f->aim >= 1 && f->aim <= 3)
        s->aim = (int)f->aim;
    if ((f->actions & BOAT_WORK_NEXT_TARGET) && !s->sandbox.moored && !s->sandbox.mooring)
        s->target = boat_work_next_site(s, s->target);
    if ((f->actions & BOAT_WORK_BRAKE) && !s->crane_view) {
        s->boat.throttle = 0;
        s->braking = !s->sandbox.moored && !s->sandbox.mooring;
    }
    if (s->crane_view) {
        *slew = f->slew;
        *reach = f->reach;
        *hoist = f->hoist;
    }
}

BoatInput boat_work_helm(const BoatWorkSession *s, const BoatWorkFrame *f)
{
    // The helm is the game's own (throttle, wheel, mouse); the crane view and a brake take it
    // over.
    BoatInput helm = { 0 };
    if (!s->crane_view && !f->demo) {
        helm.steering_rate = isfinite(f->steering_target)
            ? boat_steering_rate(s->boat.wheel, f->steering_target)
            : f->steering_rate;
        helm.throttle_rate
            = s->braking ? 0 : boat_throttle_rate(s->boat.throttle, f->thrust_target);
    }
    return helm;
}

// --- Endpoints ---------------------------------------------------------------------------------

enum { TO_CLIENT, TO_SERVER };

static bool fail(BoatWorkLocal *local, const char *message)
{
    if (!local->failure[0])
        snprintf(local->failure, sizeof(local->failure), "%s", message);
    return false;
}

static bool send(BoatWorkLocal *local, unsigned direction, uint8_t type, uint64_t tick,
    const uint8_t *payload, size_t size)
{
    BoatNetHeader header
        = { .epoch = local->generation, .sequence = local->sequence[direction]++, .tick = tick };
    uint8_t message[BOAT_NET_MAX_MESSAGE];
    size_t n = boat_net_private_encode_payload(&header, type, payload, size, message);
    BoatNetTransport *t = direction == TO_CLIENT ? &local->server : &local->client;
    if (!n || !boat_net_send(t, 0, BOAT_NET_CONTROL, message, n))
        return fail(local, "the work endpoint could not send a message");
    return true;
}

static bool send_motion(BoatWorkLocal *local, unsigned previous_next, bool checkpoint)
{
    uint8_t payload[BOAT_NET_MAX_PAYLOAD];
    size_t n = boat_work_encode_motion(&local->authority, previous_next, checkpoint, payload);
    ++local->stats.motions;
    return n
        && send(local, TO_CLIENT, BOAT_NET_PRIVATE_WORK_MOTION, local->authority.tick, payload, n);
}

// The status after a chunk, when it changed (or always, for a checkpoint).
static bool send_status(BoatWorkLocal *local, bool always)
{
    uint8_t payload[BOAT_NET_MAX_PAYLOAD];
    size_t n = boat_work_encode_status(&local->authority, payload);
    if (!n)
        return fail(local, "the work status does not fit a message");
    if (!always && n == local->status_size && !memcmp(payload, local->status, n))
        return true;
    memcpy(local->status, payload, n);
    local->status_size = n;
    ++local->stats.statuses;
    return send(local, TO_CLIENT, BOAT_NET_PRIVATE_WORK_STATUS, local->authority.tick, payload, n);
}

static bool send_checkpoint(BoatWorkLocal *local)
{
    if (!send_motion(local, 0, true))
        return false;
    uint8_t payload[BOAT_NET_MAX_PAYLOAD];
    for (unsigned first = 0; first < BOAT_WAKE_SEGMENTS; first += BOAT_WORK_TRAIL_PAGE) {
        size_t n = boat_work_encode_trail(&local->authority, first, payload);
        if (!n
            || !send(
                local, TO_CLIENT, BOAT_NET_PRIVATE_WORK_TRAIL, local->authority.tick, payload, n))
            return fail(local, "the work trail could not be sent");
    }
    return send_status(local, true);
}

// The authority's service: connections and frames, then TICKS ticks, then the status (STATUS).
static bool serve_authority(BoatWorkLocal *local, unsigned ticks, bool status)
{
    BoatNetEvent event;
    bool applied = false;
    for (unsigned handled = 0; handled < 128 && boat_net_poll(&local->server, &event); ++handled) {
        if (event.kind == BOAT_NET_EVENT_CONNECT) {
            local->server_connected = true;
            continue;
        }
        if (event.kind == BOAT_NET_EVENT_DISCONNECT)
            return fail(local, "the work client disconnected");
        if (event.kind != BOAT_NET_EVENT_MESSAGE)
            continue;
        uint8_t payload[BOAT_NET_MAX_PAYLOAD];
        size_t size;
        BoatNetHeader header;
        BoatWorkFrame frame;
        if (boat_net_private_decode_payload(
                event.data, event.size, BOAT_NET_PRIVATE_WORK_FRAME, &header, payload, &size)
                != BOAT_NET_OK
            || header.epoch != local->generation || header.tick != local->authority.tick
            || !boat_work_decode_frame(payload, size, &frame))
            return fail(local, "the work authority received a malformed frame");
        local->frame = frame;
        boat_work_apply_frame(
            &local->authority, &frame, &local->slew, &local->reach, &local->hoist);
        applied = true;
    }
    // Actions in a frame without ticks (paused, or a grant of zero) still change what the client
    // presents: refresh its mirror at the same tick.
    if (applied && !ticks && !send_motion(local, 0, true))
        return false;
    for (unsigned i = 0; i < ticks; ++i) {
        BoatWorkSession *s = &local->authority;
        unsigned previous = (unsigned)s->world.water.player_wake_next;
        boat_work_session_tick(
            s, local->slew, local->reach, local->hoist, boat_work_helm(s, &local->frame));
        if (!send_motion(local, previous, false))
            return false;
    }
    return !status || send_status(local, false);
}

// The client's service: the authority's motions (each observed), status and trail pages.
static bool serve_client(BoatWorkLocal *local, const BoatWorkObserver *observer)
{
    BoatNetEvent event;
    for (unsigned handled = 0; handled < 128 && boat_net_poll(&local->client, &event); ++handled) {
        if (event.kind == BOAT_NET_EVENT_CONNECT) {
            local->client_connected = true;
            continue;
        }
        if (event.kind == BOAT_NET_EVENT_DISCONNECT)
            return fail(local, "the work authority disconnected");
        if (event.kind != BOAT_NET_EVENT_MESSAGE)
            continue;
        uint8_t type = boat_net_private_type(event.data, event.size);
        uint8_t payload[BOAT_NET_MAX_PAYLOAD];
        size_t size;
        BoatNetHeader header;
        if (boat_net_private_decode_payload(event.data, event.size, type, &header, payload, &size)
                != BOAT_NET_OK
            || header.epoch != local->generation)
            return fail(local, "the work client received a malformed message");
        bool ok = false;
        if (type == BOAT_NET_PRIVATE_WORK_MOTION) {
            bool checkpoint = size && payload[0];
            ok = (checkpoint || (local->checkpointed && header.tick == local->view_tick + 1))
                && boat_work_decode_motion(payload, size, header.tick, &local->view);
            if (ok) {
                local->view_tick = header.tick;
                if (!checkpoint && observer && observer->tick)
                    observer->tick(observer->context, &local->view);
            }
        } else if (type == BOAT_NET_PRIVATE_WORK_STATUS) {
            ok = boat_work_decode_status(
                payload, size, &local->view, local->notice, local->waiting);
            local->checkpointed |= ok;
        } else if (type == BOAT_NET_PRIVATE_WORK_TRAIL) {
            ok = boat_work_decode_trail(payload, size, &local->view);
        }
        if (!ok)
            return fail(local, "the work client could not apply a message");
    }
    return true;
}

bool boat_work_local_start(
    BoatWorkLocal *local, const BoatWorkSession *initial, uint32_t generation, double now)
{
    BoatWorkLocalStats stats = local->stats;
    *local = (BoatWorkLocal) { .generation = generation, .stats = stats };
    if (!generation || !initial)
        return fail(local, "invalid work session configuration");
    local->network = boat_net_memory_create(generation);
    if (!local->network)
        return fail(local, "the work transport could not be created");
    boat_net_memory_set_time(local->network, now);
    local->server = boat_net_memory_server(local->network);
    unsigned client;
    if (!boat_net_memory_client(local->network, &client, &local->client)) {
        boat_work_local_stop(local);
        return fail(local, "the work client could not connect");
    }
    local->authority = *initial;
    // A notice formatted into the session's own buffer points into the copy.
    if (initial->notice == initial->notice_text)
        local->authority.notice = local->authority.notice_text;
    // The client starts from the package's fresh session; nothing it presents comes from the
    // authority except through the checkpoint below.
    local->view = *initial;
    boat_work_session_reset_world(&local->view);
    local->view.objective = (BoatPluginObjective) { 0 };
    local->view.event_count = 0;
    local->view.waiting = NULL;
    local->view.notice = NULL;
    local->running = true;
    for (unsigned round = 0; round < 8 && !(local->server_connected && local->client_connected);
        ++round) {
        if (!serve_authority(local, 0, false) || !serve_client(local, NULL))
            break;
    }
    if (!local->server_connected || !local->client_connected || !send_checkpoint(local)
        || !serve_client(local, NULL) || !local->checkpointed
        || local->view_tick != local->authority.tick) {
        char reason[128];
        snprintf(reason, sizeof(reason), "%s",
            local->failure[0] ? local->failure : "the work session did not start");
        boat_work_local_stop(local);
        snprintf(local->failure, sizeof(local->failure), "%s", reason);
        return false;
    }
    return true;
}

void boat_work_local_stop(BoatWorkLocal *local)
{
    if (local->network)
        boat_net_memory_destroy(local->network);
    local->network = NULL;
    local->running = false;
}

bool boat_work_local_restart(BoatWorkLocal *local, double now)
{
    static BoatWorkSession session; // A session is large; one restart at a time.
    session = local->authority;
    if (local->authority.notice == local->authority.notice_text)
        session.notice = session.notice_text;
    uint32_t generation = local->generation + 1;
    boat_work_local_stop(local);
    ++local->stats.restarts;
    return boat_work_local_start(local, &session, generation, now);
}

bool boat_work_local_frame(BoatWorkLocal *local, double now, unsigned grant,
    const BoatWorkFrame *frame, const BoatWorkObserver *observer)
{
    if (!local->running)
        return fail(local, "the work session is not running");
    if (grant > BOAT_WORK_LOCAL_MAX_GRANT)
        return fail(local, "the grant exceeds the frame bound");
    boat_net_memory_set_time(local->network, now);
    uint8_t payload[BOAT_NET_MAX_PAYLOAD];
    size_t n = boat_work_encode_frame(frame, payload);
    if (!n || !send(local, TO_SERVER, BOAT_NET_PRIVATE_WORK_FRAME, local->view_tick, payload, n))
        return fail(local, "the work frame could not be sent");
    unsigned remaining = grant;
    do {
        uint64_t tick = local->authority.tick;
        unsigned chunk
            = BOAT_WORK_LOCAL_CHUNK_TICKS - (unsigned)(tick % BOAT_WORK_LOCAL_CHUNK_TICKS);
        if (chunk > remaining)
            chunk = remaining;
        if (!serve_authority(local, chunk, true) || !serve_client(local, observer)) {
            local->running = false;
            return false;
        }
        if (local->view_tick != local->authority.tick) {
            local->running = false;
            return fail(local, "the work client fell behind its authority");
        }
        remaining -= chunk;
        ++local->stats.chunks;
        local->stats.ticks += chunk;
    } while (remaining);
    ++local->stats.frames;
    return true;
}

const BoatWorkSession *boat_work_local_view(const BoatWorkLocal *local) { return &local->view; }

BoatWorkSession *boat_work_local_authority(BoatWorkLocal *local) { return &local->authority; }
