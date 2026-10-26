#include "wake_velocity_net.h"
#include "boat_skin.h"
#include "raylib.h"
#include "raymath.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

static const char *phase_text(BoatNetClientPhase phase)
{
    static const char *names[]
        = { "connecting", "handshake", "loading world", "playing", "rejected", "closed" };
    return names[phase];
}

// Keep the reason for the title screen as well as the console.
static void fail(NetStudy *net, const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(net->failure, sizeof(net->failure), format, arguments);
    va_end(arguments);
    fprintf(stderr, "%s\n", net->failure);
}

static void attach(NetStudy *net, BoatSession *session)
{
    // The session draws the predicted water; its objects pointer borrows that pool.
    session->world = boat_fleet_world(boat_net_client_view(&net->client));
}

bool net_study_connect(NetStudy *net, const char *endpoint, const char *name, const char *token,
    const BoatWorldResources *worlds, BoatSession *session)
{
    char error[160];
    *net = (NetStudy) { 0 };
    BoatNetTransport transport;
#if defined(__EMSCRIPTEN__)
    if (!boat_net_web_connect(endpoint, error, sizeof(error))) {
        fail(net, "Cannot connect: %s", error);
        return false;
    }
    transport = boat_net_web_transport();
#else
    char host[256];
    uint16_t port;
    if (!boat_net_parse_endpoint(endpoint, host, sizeof(host), &port)) {
        fail(net, "The address needs HOST:PORT, for example 127.0.0.1:%d", BOAT_NET_PORT);
        return false;
    }
    if (!boat_net_enet_startup()) {
        fail(net, "Cannot initialize networking.");
        return false;
    }
    net->enet = boat_net_enet_connect(host, port, error, sizeof(error));
    if (!net->transport_open) {
        fail(net, "Cannot connect: %s", error);
        boat_net_enet_cleanup();
        return false;
    }
    transport = boat_net_enet_transport(net->enet);
#endif
    net->transport_open = true;
    BoatNetClientConfig config = { .name = name,
        .token = token,
        .build = "wake_velocity M4",
        .predict = true,
        .resources = worlds };
    boat_net_client_init(&net->client, transport, &config, boat_net_clock());
    fprintf(stdout, "Connecting to %s...\n", endpoint);
    fflush(stdout);
    while (net->client.phase < BOAT_NET_CLIENT_PLAYING) {
        if (!boat_net_client_service(&net->client, boat_net_clock(), (BoatInput) { 0 }, true)) {
            if (net->client.phase == BOAT_NET_CLIENT_REJECTED)
                fail(net, "Rejected by %s: %s (server protocol %u, revision %u; this build %d, %d)",
                    endpoint, boat_net_client_failure(&net->client),
                    (unsigned)net->client.reject.version,
                    (unsigned)net->client.reject.physics_revision, BOAT_NET_VERSION,
                    BOAT_PHYSICS_REVISION);
            else
                fail(net, "Cannot join %s: %s", endpoint, boat_net_client_failure(&net->client));
#if defined(__EMSCRIPTEN__)
            if (boat_net_web_failure()[0])
                fail(net, "%s", boat_net_web_failure());
#endif
            net_study_close(net);
            return false;
        }
#if defined(__EMSCRIPTEN__)
        emscripten_sleep(10); // Let signaling and DataChannel events run during the handshake.
#else
        boat_net_enet_flush(net->enet);
        boat_net_enet_wait(net->enet, .01);
#endif
    }
    fprintf(stdout, "Joined as slot %u at tick %llu (%s, %s water).\n", net->client.slot + 1,
        (unsigned long long)net->client.fleet.state.tick,
        boat_location(net->client.accept.location)->name,
        net->client.accept.waves ? "wave" : "calm");
    net->presented_tick = boat_net_client_view(&net->client)->state.tick;
    net->correction = net->client.prediction.correction;
    attach(net, session);
    boat_reset_location(&session->boat, session->world.definition);
    boat_presentation_reset(&session->presentation);
    for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i)
        boat_presentation_reset(&net->presentations[i]);
    return true;
}

// Fold the newest correction into the visual offsets, then decay them (about 95% in 120 ms).
static void blend_corrections(NetStudy *net, float dt)
{
    const BoatPrediction *p = &net->client.prediction;
    if (p->correction != net->correction) {
        net->correction = p->correction;
        for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i) {
            BoatPredictionError *o = &net->offset[i];
            const BoatPredictionError *e = &p->error[i];
            *o = e->snap ? (BoatPredictionError) { 0 }
                         : (BoatPredictionError) { Vector2Add(o->position, e->position),
                               o->heading + e->heading, o->height + e->height, o->pitch + e->pitch,
                               o->roll + e->roll, false };
            if (Vector2Length(o->position) > BOAT_PREDICTION_SNAP_DISTANCE)
                *o = (BoatPredictionError) { 0 };
        }
    }
    float keep = expf(-dt / .04f);
    for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i) {
        BoatPredictionError *o = &net->offset[i];
        *o = (BoatPredictionError) { Vector2Scale(o->position, keep), o->heading * keep,
            o->height * keep, o->pitch * keep, o->roll * keep, false };
    }
}

static Boat shown(const NetStudy *net, unsigned slot)
{
    Boat boat = boat_net_client_view(&net->client)->state.slots[slot].boat;
    const BoatPredictionError *o = &net->offset[slot];
    boat.position = Vector2Add(boat.position, o->position);
    boat.heading += o->heading;
    boat.height += o->height;
    boat.pitch += o->pitch;
    boat.roll += o->roll;
    return boat;
}

bool net_study_frame(NetStudy *net, BoatSession *session, BoatInput input, float thrust_target,
    float steering_target, bool active, bool reset)
{
    BoatNetClient *c = &net->client;
    double now = boat_net_clock();
    if (!active || reset)
        net->weapons_blocked = true;
    if (!IsKeyDown(KEY_SPACE) && !IsKeyDown(KEY_E) && active)
        net->weapons_blocked = false;
    if (active && !net->weapons_blocked && c->accept.mode == BOAT_NET_MODE_DEATHMATCH) {
        input.gun = IsKeyDown(KEY_SPACE);
        if (IsKeyPressed(KEY_E)) {
            if (++net->grenade_request == 0)
                ++net->grenade_request;
        }
        input.grenade = net->grenade_request;
    }
    // Held boats (grid, countdown, parked) cannot reset; do not ask.
    const BoatFleetSlot *held = boat_net_client_own(c);
    if (reset && !(held && held->held))
        boat_net_client_request_reset(c);
    if (!boat_net_client_service_targets(c, now, input, !active, thrust_target, steering_target)) {
        fail(net, "Disconnected: %s.", boat_net_client_failure(c));
        return false;
    }
#if !defined(__EMSCRIPTEN__)
    boat_net_enet_flush(net->enet);
#endif
    attach(net, session);
    blend_corrections(net, fminf(GetFrameTime(), .1f));
    const BoatFleet *view = boat_net_client_view(c);
    const BoatFleetSlot *own = &view->state.slots[c->slot];
    net->own_visible = own->active;
    if (own->active && own->boat.motion_state == BOAT_CAPSIZED && own->life != net->capsized_life) {
        net->capsized_life = own->life;
        net->capsized_tick = c->fleet.state.tick;
    }
    if (own->active) {
        if (own->life != net->own_life) {
            net->own_life = own->life;
            net->weapons_blocked = true;
            net->grenade_request = 0;
            net->respawned_at = now;
            boat_presentation_reset(&session->presentation);
        }
        session->boat = shown(net, c->slot);
        const BoatCombatPlayer *p = &view->state.combat.players[c->slot];
        if (view->state.combat.enabled && p->health <= 0 && p->defeated)
            session->boat.height
                -= fminf(4, (float)(view->state.tick - p->defeated) * BOAT_STEP * 1.5f);
    }
    // Every remote's proximity mode follows the physical predicted hulls.
    double server_tick = boat_net_client_server_tick(c, now);
    float dt = fminf(GetFrameTime(), .1f);
    for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i) {
        const BoatFleetSlot *slot = &view->state.slots[i];
        net->visible[i] = i != c->slot && slot->active;
        if (!net->visible[i])
            continue;
        float weight = own->active ? boat_remote_proximity_update(&net->proximity[i], &own->boat,
                                         &slot->boat, slot->life, now, dt)
                                   : 0;
        Boat proxy = shown(net, i), past;
        uint32_t life = slot->life;
        BoatRemoteSample sample = boat_remote_history_pose(
            &c->remotes, i, server_tick - BOAT_REMOTE_DELAY_TICKS, &past, &life);
        ++net->samples[sample];
        ++net->frames;
        net->near_frames += weight > 0;
        // A pose of another life than the proxy's is never blended with it.
        if (sample == BOAT_REMOTE_NONE || life != slot->life)
            past = proxy, life = slot->life;
        if (life != net->lives[i]) {
            net->lives[i] = life;
            boat_presentation_reset(&net->presentations[i]);
        }
        net->shown[i] = boat_remote_blend(&past, &proxy, weight);
        const BoatCombatPlayer *p = &view->state.combat.players[i];
        if (view->state.combat.enabled && p->health <= 0 && p->defeated)
            net->shown[i].height
                -= fminf(4, (float)(view->state.tick - p->defeated) * BOAT_STEP * 1.5f);
    }
    uint64_t tick = view->state.tick;
    boat_session_set_tick(session, tick);
    BoatWorld world = session->world;
    for (unsigned steps = 0; net->presented_tick < tick && steps < 12; ++steps) {
        ++net->presented_tick;
        double time = boat_tick_seconds(net->presented_tick);
        if (net->own_visible)
            boat_presentation_step(&session->presentation, &session->boat, &world, time);
        for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i)
            if (net->visible[i])
                boat_presentation_step(&net->presentations[i], &net->shown[i], &world, time);
    }
    net->presented_tick = tick;
    return true;
}

unsigned net_study_skin(unsigned slot) { return slot % BOAT_SKIN_COUNT; }

void net_study_rivals(const NetStudy *net, double time, BoatRivals *rivals)
{
    rivals->count = 0;
    for (unsigned i = 0; i < BOAT_FLEET_SLOTS && rivals->count < WATERFRONT_RIVALS; ++i) {
        if (!net->visible[i])
            continue;
        BoatRival *r = &rivals->boats[rivals->count++];
        *r = (BoatRival) { .view = { .boat = net->shown[i], .time = time },
            .presentation = &net->presentations[i] };
        r->view.skin = net_study_skin(i);
    }
}

// The newest feed entry reporting the own boat's capsize on LIFE, or NULL.
static const BoatNetFeedEntry *own_capsize(const BoatNetClient *c, uint32_t life)
{
    for (uint64_t n = c->feed_count; n > 0 && c->feed_count - n < BOAT_NET_MATCH_FEED; --n) {
        const BoatNetFeedEntry *e = &c->feed[(n - 1) % BOAT_NET_MATCH_FEED];
        if (e->event.kind == BOAT_MATCH_CAPSIZE && e->event.victim == c->slot
            && e->event.victim_life == life)
            return e;
    }
    return NULL;
}

// "CAPSIZED BY NAME / RESPAWN 2": the authority respawns a capsized boat by itself; a reset request
// skips the rest of the wait after skip_ticks.
static const char *capsize_notice(const NetStudy *net, const Boat *boat, uint32_t life)
{
    static char text[96];
    const BoatNetClient *c = &net->client;
    if (boat->motion_state != BOAT_CAPSIZED)
        return NULL;
    const BoatNetFeedEntry *e = own_capsize(c, life);
    uint64_t since = e ? e->tick : net->capsized_tick;
    double left = ((double)since + c->match.respawn_ticks
                      - boat_net_client_server_tick(c, boat_net_clock()))
        / BOAT_TICK_HZ;
    double waited = boat_net_client_server_tick(c, boat_net_clock()) - (double)since;
    char who[48] = "CAPSIZED";
    if (e && e->event.attacker != BOAT_MATCH_NOBODY)
        snprintf(who, sizeof(who), "%s BY %s",
            e->event.reason == BOAT_MATCH_DEFEAT_HULL ? "DESTROYED" : "CAPSIZED", e->attacker);
    else if (e)
        snprintf(who, sizeof(who), "CAPSIZED / -1");
    snprintf(text, sizeof(text), "%s / RESPAWN %.0f%s", who, ceil(fmax(0, left)),
        waited >= c->match.skip_ticks ? " / HOLD BACKSPACE" : "");
    return text;
}

// "CAPSIZED / RECOVERY 2 / HOLD BACKSPACE" while a race recovers the own boat by itself.
static const char *race_capsize_notice(const NetStudy *net, const Boat *boat)
{
    static char text[64];
    const BoatNetClient *c = &net->client;
    if (boat->motion_state != BOAT_CAPSIZED)
        return NULL;
    double waited = boat_net_client_server_tick(c, boat_net_clock()) - (double)net->capsized_tick;
    double left = ((double)c->race.recovery_ticks - waited) / BOAT_TICK_HZ;
    snprintf(text, sizeof(text), "CAPSIZED / RECOVERY %.0f%s", ceil(fmax(0, left)),
        waited >= c->race.skip_ticks ? " / HOLD BACKSPACE" : "");
    return text;
}

const char *net_study_notice(const NetStudy *net)
{
    static char text[80];
    double seconds;
    BoatNetResetState reset = boat_net_client_reset_state(&net->client, boat_net_clock(), &seconds);
    if (!net->own_visible && net_study_race(net) && net->client.race.phase != BOAT_RACE_WAITING)
        return "RACE IN PROGRESS / YOU JOIN THE NEXT GRID";
    if (!net->own_visible)
        return "WAITING FOR A SAFE SPAWN";
    if (net_study_race(net)) {
        const BoatFleetSlot *own
            = &boat_net_client_view(&net->client)->state.slots[net->client.slot];
        const char *capsized = race_capsize_notice(net, &own->boat);
        if (capsized)
            return capsized;
    }
    if (net_study_deathmatch(net)) {
        const BoatFleetSlot *own
            = &boat_net_client_view(&net->client)->state.slots[net->client.slot];
        const char *capsized = capsize_notice(net, &own->boat, own->life);
        if (capsized)
            return capsized;
    }
    if (reset == BOAT_NET_RESET_NONE)
        return NULL;
    // A clear spot is normally found within a tick or two; say so only when it is not.
    if (reset == BOAT_NET_RESET_SENT || seconds < .5)
        return "RESETTING";
    snprintf(text, sizeof(text), "RESETTING / WAITING FOR A CLEAR SPOT %.0f s", seconds);
    return text;
}

float net_study_fade(const NetStudy *net)
{
    double age = boat_net_clock() - net->respawned_at;
    return net->respawned_at > 0 && age < .35 ? (float)(1 - age / .35) : 0;
}

const char *net_study_status(const NetStudy *net)
{
    static char text[160];
    const BoatNetClient *c = &net->client;
    unsigned players = 0;
    for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i)
        players += c->roster.slots[i].state != BOAT_NET_ROSTER_EMPTY;
    snprintf(text, sizeof(text), "NET SLOT %u / %u PLAYERS / RTT %.0f ms / %s%s%s", c->slot + 1,
        players, c->rtt * 1000, phase_text(c->phase),
        boat_net_client_stale(c, boat_net_clock()) ? " / STALE" : "",
        c->wake_consistent ? "" : " / WAKES REPAIRING");
    return text;
}

int net_study_draw_roster(const NetStudy *net, int right, int top)
{
    const BoatNetClient *c = &net->client;
    for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i) {
        if (c->roster.slots[i].state == BOAT_NET_ROSTER_EMPTY)
            continue;
        const char *line = TextFormat("%s %s%s", c->roster.slots[i].name,
            TextToUpper(boat_skin_name(net_study_skin(i))), i == c->slot ? " (YOU)" : "");
        int x = right - MeasureText(line, 12);
        DrawRectangle(x - 4, top - 2, right - x + 8, 15, Fade(BLACK, .45f));
        DrawText(line, x, top, 12, i == c->slot ? RAYWHITE : LIGHTGRAY);
        top += 14;
    }
    return top;
}

bool net_study_deathmatch(const NetStudy *net)
{
    return net->client.accept.mode == BOAT_NET_MODE_DEATHMATCH && net->client.match_valid;
}

static unsigned rank(const BoatNetMatchState *m, unsigned slot)
{
    unsigned place = 1;
    for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i)
        place += m->slots[i].present && m->slots[i].row.score > m->slots[slot].row.score;
    return place;
}

static const char *clock_text(double seconds)
{
    int whole = (int)ceil(fmax(0, seconds));
    return TextFormat("%d:%02d", whole / 60, whole % 60);
}

static void backed_text(const char *text, int x, int y, int size, Color color)
{
    DrawRectangle(x - 6, y - 3, MeasureText(text, size) + 12, size + 6, Fade(BLACK, .45f));
    DrawText(text, x, y, size, color);
}

static const char *feed_text(const BoatNetFeedEntry *e)
{
    const BoatNetMatchEvent *v = &e->event;
    if (v->kind == BOAT_MATCH_ROUND_START)
        return TextFormat("ROUND %u START", (unsigned)v->round);
    if (v->kind == BOAT_MATCH_ROUND_END)
        return TextFormat("ROUND %u OVER", (unsigned)v->round);
    if (v->attacker == BOAT_MATCH_NOBODY)
        return TextFormat("%s CAPSIZED  -1", e->victim);
    return TextFormat("%s DEFEATED %s  %s", e->attacker, e->victim,
        v->bailout                           ? "BAILOUT"
            : v->cause == BOAT_MATCH_GUN     ? "GUN"
            : v->cause == BOAT_MATCH_GRENADE ? "GRENADE"
            : v->cause == BOAT_MATCH_WAKE    ? "WAKE"
                                             : "HIT");
}

static void draw_feed(const BoatNetClient *c, int right, int top, double now)
{
    // The four newest entries younger than five seconds, oldest first.
    uint64_t first = c->feed_count > 4 ? c->feed_count - 4 : 0;
    for (uint64_t n = first; n < c->feed_count; ++n) {
        const BoatNetFeedEntry *e = &c->feed[n % BOAT_NET_MATCH_FEED];
        if (now - e->at > 5)
            continue;
        const BoatNetMatchEvent *v = &e->event;
        Color color = v->victim == c->slot ? ORANGE
            : v->attacker == c->slot       ? (Color) { 120, 220, 140, 255 }
                                           : LIGHTGRAY;
        const char *line = feed_text(e);
        int x = right - MeasureText(line, 12);
        DrawRectangle(x - 4, top - 2, right - x + 8, 15, Fade(BLACK, .45f));
        DrawText(line, x, top, 12, color);
        top += 14;
    }
}

static void draw_scoreboard(const BoatNetClient *c, int width, int height, double server_tick)
{
    const BoatNetMatchState *m = &c->match;
    unsigned order[BOAT_FLEET_SLOTS], count = 0;
    for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i)
        if (m->slots[i].present)
            order[count++] = i;
    for (unsigned i = 1; i < count; ++i) // Highest score first; slot order breaks ties.
        for (unsigned j = i;
            j > 0 && m->slots[order[j]].row.score > m->slots[order[j - 1]].row.score; --j) {
            unsigned swap = order[j];
            order[j] = order[j - 1];
            order[j - 1] = swap;
        }
    // Column starts in font sizes; the widest heading, CAPSIZED, is about nine.
    static const int columns[] = { 0, 2, 11, 17, 25, 36 };
    int font = width >= 42 * 20 && height >= 500 ? 20 : 10, line = font + font / 2;
    int panel = 42 * font, x = (width - panel) / 2 + font;
    int y = (height - (int)(count + 3) * line) / 2;
    DrawRectangle(x - font, y - font, panel, (int)(count + 3) * line + font, Fade(BLACK, .7f));
    const char *title = m->phase == BOAT_MATCH_INTERMISSION
        ? TextFormat("ROUND %u RESULT / NEXT IN %s", (unsigned)m->round,
              clock_text(((double)m->phase_end - server_tick) / BOAT_TICK_HZ))
        : m->phase == BOAT_MATCH_PLAYING ? TextFormat("ROUND %u", (unsigned)m->round)
                                         : "WARMUP";
    DrawText(title, x, y, font, RAYWHITE);
    y += line + line / 2;
    const char *headings[] = { "#", "PLAYER", "SCORE", "KILLS", "DEATHS", "SELF" };
    for (unsigned k = 0; k < 6; ++k)
        DrawText(headings[k], x + columns[k] * font, y, font, GRAY);
    y += line;
    for (unsigned n = 0; n < count; ++n, y += line) {
        unsigned i = order[n];
        const BoatMatchRow *row = &m->slots[i].row;
        Color color = i == c->slot ? RAYWHITE : LIGHTGRAY;
        // Own buffers: TextFormat reuses only a few.
        char cells[6][32];
        snprintf(cells[0], sizeof(cells[0]), "%u", rank(m, i));
        snprintf(cells[1], sizeof(cells[1]), "%s%s", c->roster.slots[i].name,
            i == c->slot ? " (YOU)" : "");
        snprintf(cells[2], sizeof(cells[2]), "%d", (int)row->score);
        snprintf(cells[3], sizeof(cells[3]), "%u", (unsigned)row->caused);
        snprintf(cells[4], sizeof(cells[4]), "%u", (unsigned)row->capsized);
        snprintf(cells[5], sizeof(cells[5]), "%u", (unsigned)row->self);
        for (unsigned k = 0; k < 6; ++k)
            DrawText(cells[k], x + columns[k] * font, y, font, color);
    }
}

void net_study_draw_match(const NetStudy *net, int width, int height, int feed_top, bool scoreboard)
{
    if (!net_study_deathmatch(net))
        return;
    const BoatNetClient *c = &net->client;
    const BoatNetMatchState *m = &c->match;
    double now = boat_net_clock();
    double server_tick = boat_net_client_server_tick(c, now);
    double left = ((double)m->phase_end - server_tick) / BOAT_TICK_HZ;
    const char *top = m->phase == BOAT_MATCH_PLAYING
        ? TextFormat("ROUND %u  %s   SCORE %d  #%u", (unsigned)m->round, clock_text(left),
              (int)m->slots[c->slot].row.score, rank(m, c->slot))
        : m->phase == BOAT_MATCH_INTERMISSION
        ? TextFormat("ROUND %u OVER / NEXT IN %s", (unsigned)m->round, clock_text(left))
        : "WARMUP / WAITING FOR ANOTHER PLAYER";
    backed_text(top, (width - MeasureText(top, 20)) / 2, 12, 20,
        m->phase == BOAT_MATCH_PLAYING && left < 30 ? ORANGE : RAYWHITE);
    draw_feed(c, width - 12, feed_top, now);
    if (scoreboard || m->phase == BOAT_MATCH_INTERMISSION)
        draw_scoreboard(c, width, height, server_tick);
}

bool net_study_race(const NetStudy *net)
{
    return net->client.accept.mode == BOAT_NET_MODE_RACE && net->client.race_valid;
}

void net_study_race_view(const NetStudy *net, BoatViewState *view)
{
    if (!net_study_race(net))
        return;
    const BoatNetRaceRow *row = &net->client.race.slots[net->client.slot];
    view->race.enabled = true;
    view->race.next_gate = row->current ? row->gates : 0;
}

static const char *ordinal(unsigned place)
{
    static const char *const names[] = { "-", "1ST", "2ND", "3RD", "4TH" };
    return names[place <= BOAT_FLEET_SLOTS ? place : 0];
}

static const char *race_time(uint64_t subticks)
{
    long hundredths = lround((double)subticks / BOAT_RACE_SUBTICKS / BOAT_TICK_HZ * 100);
    return TextFormat(
        "%02ld:%02ld.%02ld", hundredths / 6000, hundredths / 100 % 60, hundredths % 100);
}

// The authority's rows as a local race over the drawn fleet, for provisional positions.
static void local_race(const BoatNetClient *c, BoatRace *race, unsigned *entrants)
{
    *race = (BoatRace) { .course = *boat_race_river_course(),
        .phase = (BoatRacePhase)c->race.phase,
        .placed = c->race.placed };
    *entrants = 0;
    for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i) {
        const BoatNetRaceRow *row = &c->race.slots[i];
        if (!row->entrant)
            continue;
        ++*entrants;
        race->entrants[i] = (BoatRaceEntrant) { .identity = 1,
            .finish = row->finish,
            .recoveries = row->recoveries,
            .status = row->status,
            .place = row->place,
            .gates = row->gates };
    }
}

static void draw_race_feed(const BoatNetClient *c, int right, int top, double now)
{
    // Finishes, retirements and abandoned rounds of the last five seconds, oldest first.
    uint64_t first
        = c->race_feed_count > BOAT_NET_MATCH_FEED ? c->race_feed_count - BOAT_NET_MATCH_FEED : 0;
    for (uint64_t n = first; n < c->race_feed_count; ++n) {
        const BoatNetRaceFeedEntry *e = &c->race_feed[n % BOAT_NET_MATCH_FEED];
        const BoatNetRaceEvent *v = &e->event;
        const char *line = v->kind == BOAT_RACE_FINISH
            ? TextFormat("%s FINISHED %s  %s", e->name, ordinal(v->value), race_time(v->time))
            : v->kind == BOAT_RACE_OUT     ? TextFormat("%s DID NOT FINISH", e->name)
            : v->kind == BOAT_RACE_ABANDON ? "RACE ABANDONED"
                                           : NULL;
        if (!line || now - e->at > 5)
            continue;
        Color color = v->slot == c->slot ? RAYWHITE : LIGHTGRAY;
        int x = right - MeasureText(line, 12);
        DrawRectangle(x - 4, top - 2, right - x + 8, 15, Fade(BLACK, .45f));
        DrawText(line, x, top, 12, color);
        top += 14;
    }
}

static void draw_race_board(
    const NetStudy *net, const BoatRace *race, int width, int height, double server_tick)
{
    const BoatNetClient *c = &net->client;
    const BoatNetRaceState *s = &c->race;
    const BoatFleet *fleet = boat_net_client_view(c);
    unsigned order[BOAT_FLEET_SLOTS], positions[BOAT_FLEET_SLOTS], count = 0;
    for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i)
        if (s->slots[i].entrant) {
            positions[i] = boat_race_position(race, fleet, i);
            order[count++] = i;
        }
    for (unsigned i = 1; i < count; ++i) // Position order; slot order breaks ties.
        for (unsigned j = i; j > 0 && positions[order[j]] < positions[order[j - 1]]; --j) {
            unsigned swap = order[j];
            order[j] = order[j - 1];
            order[j - 1] = swap;
        }
    static const int columns[] = { 0, 3, 21, 31 };
    int font = width >= 40 * 20 && height >= 500 ? 20 : 10, line = font + font / 2;
    int panel = 40 * font, x = (width - panel) / 2 + font;
    int y = (height - (int)(count + 3) * line) / 2;
    DrawRectangle(x - font, y - font, panel, (int)(count + 3) * line + font, Fade(BLACK, .7f));
    const char *title = s->phase == BOAT_RACE_RESULTS
        ? TextFormat("RACE %u RESULTS / NEXT IN %s", (unsigned)s->round,
              clock_text(((double)s->phase_end - server_tick) / BOAT_TICK_HZ))
        : TextFormat("RACE %u STANDINGS", (unsigned)s->round);
    DrawText(title, x, y, font, RAYWHITE);
    y += line + line / 2;
    const char *headings[] = { "#", "PLAYER", "TIME", "STATUS" };
    for (unsigned k = 0; k < 4; ++k)
        DrawText(headings[k], x + columns[k] * font, y, font, GRAY);
    y += line;
    for (unsigned n = 0; n < count; ++n, y += line) {
        unsigned i = order[n];
        const BoatNetRaceRow *row = &s->slots[i];
        Color color = i == c->slot ? RAYWHITE : LIGHTGRAY;
        char cells[4][40];
        snprintf(cells[0], sizeof(cells[0]), "%u", positions[i]);
        snprintf(cells[1], sizeof(cells[1]), "%s%s%s", c->roster.slots[i].name,
            i == c->slot ? " (YOU)" : "", row->current ? "" : " (LEFT)");
        snprintf(cells[2], sizeof(cells[2]), "%s",
            row->status == BOAT_RACE_FINISHED ? race_time(row->finish) : "--:--.--");
        snprintf(cells[3], sizeof(cells[3]), "%s",
            row->status == BOAT_RACE_FINISHED ? ordinal(row->place)
                : row->status == BOAT_RACE_DNF
                ? "DNF"
                : TextFormat("%u/%u LINES", row->gates, race->course.gate_count));
        for (unsigned k = 0; k < 4; ++k)
            DrawText(cells[k], x + columns[k] * font, y, font, color);
    }
}

void net_study_draw_race(const NetStudy *net, int width, int height, int feed_top, bool board)
{
    if (!net_study_race(net))
        return;
    const BoatNetClient *c = &net->client;
    const BoatNetRaceState *s = &c->race;
    const BoatNetRaceRow *own = &s->slots[c->slot];
    double now = boat_net_clock();
    double server_tick = boat_net_client_server_tick(c, now);
    double to_go = ((double)s->go_tick - server_tick) / BOAT_TICK_HZ;
    static BoatRace race; // Large; drawing is single-threaded.
    unsigned entrants;
    local_race(c, &race, &entrants);
    const char *top;
    Color color = RAYWHITE;
    if (s->phase == BOAT_RACE_WAITING) {
        top = "RACE / WAITING FOR ANOTHER PLAYER";
    } else if (!own->current) {
        top = s->phase == BOAT_RACE_RESULTS
            ? TextFormat("RACE %u RESULTS", (unsigned)s->round)
            : TextFormat("RACE %u IN PROGRESS / YOU JOIN THE NEXT GRID", (unsigned)s->round);
    } else if (s->phase == BOAT_RACE_GRID || s->phase == BOAT_RACE_COUNTDOWN) {
        top = TextFormat("RACE %u / %u BOATS / SET YOUR THROTTLE", (unsigned)s->round, entrants);
    } else if (own->status == BOAT_RACE_FINISHED) {
        top = TextFormat("FINISHED - %s / %s", ordinal(own->place), race_time(own->finish));
        color = (Color) { 120, 220, 140, 255 };
    } else if (own->status == BOAT_RACE_DNF) {
        top = "DID NOT FINISH";
        color = ORANGE;
    } else {
        unsigned position = boat_race_position(&race, boat_net_client_view(c), c->slot);
        double elapsed = fmax(0, server_tick - (double)s->go_tick) / BOAT_TICK_HZ;
        long hundredths = lround(elapsed * 100);
        const char *next = own->gates + 1u == race.course.gate_count
            ? "FINISH NEXT"
            : TextFormat("GATE %u/%u", own->gates + 1u, race.course.gate_count - 1);
        top = TextFormat("%02ld:%02ld.%02ld   POS %u/%u   %s", hundredths / 6000,
            hundredths / 100 % 60, hundredths % 100, position, entrants, next);
    }
    // After the first finish the round closes at its deadline.
    if (s->phase == BOAT_RACE_RACING && s->placed && own->status == BOAT_RACE_RUNNING) {
        double left = ((double)s->deadline - server_tick) / BOAT_TICK_HZ;
        top = TextFormat("%s   CLOSES IN %.0f", top, ceil(fmax(0, left)));
        color = left < 5 ? ORANGE : color;
    }
    backed_text(top, (width - MeasureText(top, 20)) / 2, 12, 20, color);
    // Countdown: 3, 2, 1 before GO, then GO! for a second.
    if (own->current && s->phase != BOAT_RACE_WAITING && to_go > -1 && to_go <= 3) {
        const char *big = to_go > 0 ? TextFormat("%.0f", ceil(to_go)) : "GO!";
        int size = height / 5;
        DrawText(big, (width - MeasureText(big, size)) / 2, height / 3 - size / 2, size,
            to_go > 0 ? RAYWHITE : (Color) { 255, 216, 93, 255 });
    }
    draw_race_feed(c, width - 12, feed_top, now);
    if (s->round && (board || s->phase == BOAT_RACE_RESULTS))
        draw_race_board(net, &race, width, height, server_tick);
}

void net_study_close(NetStudy *net)
{
    if (!net->transport_open)
        return;
    const BoatPredictionStats *p = &net->client.prediction.stats;
    if (p->corrections)
        fprintf(stdout,
            "prediction: %llu corrections replaying %.1f ticks (max %u); own error mean %.3f m "
            "max %.3f m; remote mean %.3f m max %.3f m; %llu snaps; %llu held ticks; wakes "
            "vanished %llu (%llu visible), carried %llu, incomplete %llu\n",
            (unsigned long long)p->corrections, (double)p->replayed_ticks / (double)p->corrections,
            p->max_replay, p->total_error / (double)p->corrections, p->max_error,
            p->remote_errors ? p->total_remote_error / (double)p->remote_errors : 0,
            p->max_remote_error, (unsigned long long)p->snaps, (unsigned long long)p->held_ticks,
            (unsigned long long)p->wakes_vanished, (unsigned long long)p->visible_vanished,
            (unsigned long long)p->carried, (unsigned long long)p->incomplete_corrections);
    if (net->frames)
        fprintf(stdout,
            "other boat: %llu frames, %.1f%% interpolated, %.1f%% extrapolated, %.1f%% held, "
            "%.1f%% in proximity mode\n",
            (unsigned long long)net->frames,
            100.0 * (double)net->samples[BOAT_REMOTE_INTERPOLATED] / (double)net->frames,
            100.0 * (double)net->samples[BOAT_REMOTE_EXTRAPOLATED] / (double)net->frames,
            100.0 * (double)net->samples[BOAT_REMOTE_HELD] / (double)net->frames,
            100.0 * (double)net->near_frames / (double)net->frames);
    const BoatNetClientStats *n = &net->client.stats;
    if (n->resets || n->resets_dropped)
        fprintf(stdout, "resets: %llu completed, longest %.0f ms; %llu dropped\n",
            (unsigned long long)n->resets, n->max_reset * 1000,
            (unsigned long long)n->resets_dropped);
    boat_net_client_leave(&net->client);
#if defined(__EMSCRIPTEN__)
    boat_net_web_close();
#else
    boat_net_enet_close(net->enet, 1);
    net->enet = NULL;
    boat_net_enet_cleanup();
#endif
    net->transport_open = false;
}

void net_study_combat_hud(const NetStudy *net, int width)
{
    const BoatNetClient *c = &net->client;
    if (c->accept.mode != BOAT_NET_MODE_DEATHMATCH)
        return;
    const BoatFleet *f = boat_net_client_view(c);
    const BoatCombatPlayer *p = &f->state.combat.players[c->slot];
    float cooldown = (float)(p->next_grenade > f->state.tick ? p->next_grenade - f->state.tick : 0)
        / BOAT_TICK_HZ;
    DrawText(TextFormat("HULL %.0f", p->health), 5, 20, 10, p->health < 30 ? ORANGE : RAYWHITE);
    DrawRectangle(5, 33, 72, 4, (Color) { 40, 50, 59, 255 });
    DrawRectangle(5, 33, (int)(72 * p->heat), 4, p->overheated ? RED : ORANGE);
    DrawText(p->overheated ? "GUN: RELEASE" : "SPACE: GUN", 5, 40, 10, RAYWHITE);
    DrawText(cooldown > 0 ? TextFormat("E: %.1fs", cooldown) : "E: GRENADE", 5, 52, 10,
        cooldown > 0 ? GRAY : YELLOW);
    const BoatCombatPlayer *confirmed = &c->fleet.state.combat.players[c->slot];
    if (confirmed->hit_tick && c->fleet.state.tick - confirmed->hit_tick < 18)
        DrawText("+", width / 2 - 3, 70, 10, YELLOW);
}
