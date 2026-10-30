#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L // poll and read for --exit-with-stdin under strict C17.
#endif
/* Multiplayer authority process. Headless: no window, audio, asset or file access.
 * --scripted (M2) runs the local fleet with bots as fast as possible and exits.
 * Otherwise (M3) it listens for wake_velocity/boat_bot clients over ENet and runs the room at
 * 120 Hz wall-clock time until --ticks/--seconds elapse or it is interrupted. The same entry
 * point is the game's `--serve` mode (boat_server.h). */
#include "boat_server.h"
#include "boat_fleet.h"
#include "boat_fleet_bots.h"
#include "boat_match.h"
#include "boat_net_enet.h"
#ifdef PROTEUS_WEBRTC
#include "boat_net_mux.h"
#include "boat_net_rtc.h"
#endif
#include "boat_net_server.h"
#include "cli_usage.h"

#include <inttypes.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
// Exclude the GDI/user names raylib also declares (headers below may include raylib.h).
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#define NOUSER
#include <windows.h>
#else
#include <poll.h>
#include <unistd.h>
#endif

static void usage(FILE *out)
{
    fprintf(out,
        "Usage: boat_server [ROOM OPTIONS]\n"
        "       boat_server --scripted [--ticks N] [--boats 1-4] [--location NAME]\n"
        "                   [--mode free|deathmatch] [--report-every N]\n"
        "\n"
        "Room mode (default) listens on --bind and UDP --port for up to four\n"
        "wake_velocity --connect or boat_bot clients. It runs the 120 Hz authority until\n"
        "--ticks/--seconds or Ctrl+C. A diagnostic: no internet reachability, encryption\n"
        "or authentication; clients must present the same --token.\n");

    cli_usage_section(out, "MODES");
    cli_usage_line(
        out, "--scripted", "Run bots headless, then print the fleet hash and per-tick cost");
    cli_usage_line(out, "--mode free", "Shared free boating (default)");
    cli_usage_line(
        out, "--mode deathmatch", "Timed rounds: +1 defeat, -1 self-capsize, 3 s respawn");
    cli_usage_line(out, "--mode race", "River sprint: gridded start, 3 s countdown, places");

    cli_usage_section(out, "ROOM");
    cli_usage_line(out, "--bind IPV4", "Listen address (default 127.0.0.1; use a LAN address)");
    cli_usage_line(out, "--port N", "UDP port (default 24680)");
    cli_usage_line(out, "--token TEXT", "Shared join token (default empty)");
    cli_usage_line(out, "--players 1-4", "Seats in the room (default 4)");
    cli_usage_line(out, "--location NAME", "basin|river|outer (default basin; race uses river)");
    cli_usage_line(out, "--waves", "Gentle crossing waves");
    cli_usage_line(out, "--lan-port N", "LAN search port (default 24681); loopback --bind answers");
    cli_usage_line(out, "", "only this machine, otherwise the local network");
    cli_usage_line(out, "--no-lan", "Do not answer LAN searches");
    cli_usage_line(
        out, "--exit-with-stdin", "Close when standard input ends (wake_velocity --host)");
    cli_usage_line(out, "--until-empty", "Close once every accepted player has left or timed out");
    cli_usage_line(
        out, "--peer-timeout S", "Drop a silent client after 1-60 s (default 5); for tests");

    cli_usage_section(out, "SIMULATION");
    cli_usage_line(out, "--ticks N", "Stop after N ticks (max 24 h at 120 Hz)");
    cli_usage_line(out, "--seconds S", "Stop after S seconds (max 24 h); not with --scripted");
    cli_usage_line(out, "--boats 1-4", "Scripted bots (default 4)");
    cli_usage_line(out, "--report-every N", "Print progress every N ticks");
    cli_usage_line(out, "--round-seconds S", "Deathmatch round length, 2-3600 (default 300)");

    cli_usage_section(out, "WEBRTC (optional build)");
    cli_usage_line(out, "--rtc-port N", "WebRTC signalling port");
    cli_usage_line(out, "--ice-server URL", "ICE server; requires --rtc-port");

    fprintf(out,
        "\nRoom options and --seconds do not apply to --scripted.\n"
        "Exit status: 0 success, 1 runtime failure, 2 usage.\n");
}

static bool parse_count(const char *text, unsigned long low, unsigned long high, unsigned long *out)
{
    char *end;
    unsigned long value = strtoul(text, &end, 10);
    if (!*text || *end || text[0] == '-' || value < low || value > high)
        return false;
    *out = value;
    return true;
}

static double now_seconds(void)
{
    struct timespec t;
    timespec_get(&t, TIME_UTC);
    return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
}

static int compare_double(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

static const char *const cause_names[]
    = { "self", "contact", "wake", "gun", "grenade", "cannon", "torpedo" };

static const char *slot_name(const BoatNetServer *server, unsigned slot)
{
    static const char *const bots[] = { "bot1", "bot2", "bot3", "bot4" };
    if (!server)
        return bots[slot];
    int peer = server->slot_peer[slot];
    return peer < 0 ? "-" : server->peers[peer].name;
}

static void print_event(
    const BoatNetServer *server, const BoatMatch *match, const BoatMatchEvent *e)
{
    if (e->kind == BOAT_MATCH_ROUND_START) {
        printf("tick %" PRIu64 " round %" PRIu32 " start\n", e->tick, match->round);
    } else if (e->kind == BOAT_MATCH_ROUND_END) {
        printf("tick %" PRIu64 " round %" PRIu32 " end:", e->tick, match->round);
        if (match->phase == BOAT_MATCH_WARMUP)
            printf(" abandoned (too few players)");
        for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i)
            if (match->slots[i].present)
                printf(" %u:%s=%" PRId32 " (#%u, %" PRIu32 " kills, %" PRIu32 " deaths, %" PRIu32
                       " self)",
                    i + 1, slot_name(server, i), match->rows[i].score, boat_match_rank(match, i),
                    match->rows[i].caused, match->rows[i].capsized, match->rows[i].self);
        printf("\n");
    } else {
        printf("tick %" PRIu64 " capsize %u:%s life %" PRIu32, e->tick, e->victim + 1,
            slot_name(server, e->victim), e->victim_life);
        if (e->attacker == BOAT_MATCH_NOBODY)
            printf(" self -1\n");
        else
            printf(" by %u:%s %s%s +1\n", e->attacker + 1, slot_name(server, e->attacker),
                cause_names[e->cause], e->bailout ? " bailout" : "");
    }
    fflush(stdout);
}

static const char *const race_phases[] = { "waiting", "grid", "countdown", "racing", "results" };

static void print_race_event(const BoatNetServer *server, const BoatRaceEvent *e)
{
    const BoatRace *race = &server->race;
    const char *name = e->slot < BOAT_FLEET_SLOTS ? slot_name(server, e->slot) : "-";
    switch (e->kind) {
    case BOAT_RACE_PHASE:
        printf("tick %" PRIu64 " race %" PRIu32 " %s", e->tick, race->round, race_phases[e->value]);
        if (e->value == BOAT_RACE_GRID)
            printf(" go=%" PRIu64, race->go_tick);
        if (e->value == BOAT_RACE_RESULTS) {
            printf(":");
            for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i) {
                const BoatRaceEntrant *entrant = &race->entrants[i];
                if (!entrant->identity)
                    continue;
                if (entrant->status == BOAT_RACE_FINISHED)
                    printf(" %u:%s=P%u/%.3fs", i + 1, slot_name(server, i), entrant->place,
                        (double)entrant->finish / BOAT_RACE_SUBTICKS / BOAT_TICK_HZ);
                else
                    printf(" %u:%s=DNF", i + 1, slot_name(server, i));
            }
        }
        printf("\n");
        break;
    case BOAT_RACE_GATE:
        printf("tick %" PRIu64 " gate %u:%s %u\n", e->tick, e->slot + 1, name, e->value + 1);
        break;
    case BOAT_RACE_FINISH:
        printf("tick %" PRIu64 " finish %u:%s P%u %.3fs\n", e->tick, e->slot + 1, name, e->value,
            (double)e->time / BOAT_RACE_SUBTICKS / BOAT_TICK_HZ);
        break;
    case BOAT_RACE_OUT:
        printf("tick %" PRIu64 " dnf %u:%s\n", e->tick, e->slot + 1, name);
        break;
    case BOAT_RACE_RECOVER:
        printf("tick %" PRIu64 " recover %u:%s\n", e->tick, e->slot + 1, name);
        break;
    case BOAT_RACE_PARK:
        printf("tick %" PRIu64 " park %u:%s\n", e->tick, e->slot + 1, name);
        break;
    default:
        printf("tick %" PRIu64 " race %" PRIu32 " abandoned\n", e->tick, race->round);
    }
    fflush(stdout);
}

static int scripted(unsigned long ticks, unsigned long boats, unsigned long report,
    const BoatMatchRules *rules, const BoatWorldResources *world)
{
    static BoatMatch match;
    if (rules && !boat_match_init(&match, rules)) {
        fprintf(stderr, "boat_server: invalid deathmatch rules.\n");
        return 1;
    }
    static BoatFleet fleet;
    if (!boat_fleet_init(
            &fleet, world, (BoatWater) { 0 }, BOAT_OBJECT_LIFE_TICKS, BOAT_OBJECT_DEFAULT_HEIGHT)) {
        fprintf(stderr, "boat_server: cannot create the fleet.\n");
        return 1;
    }
    if (rules)
        boat_combat_enable(&fleet);
    for (unsigned slot = 0; slot < boats; ++slot)
        boat_fleet_request_spawn(&fleet, slot);
    double *cost = malloc(ticks * sizeof(*cost));
    if (!cost) {
        fprintf(stderr, "boat_server: cannot allocate the timing record.\n");
        return 1;
    }
    BoatFleetBots bots = { 0 };
    unsigned long contacts = 0, residual = 0, passes = 0, spawned = 0, blocked = 0, failures = 0;
    unsigned max_passes = 0, max_pair_tests = 0, peak_live = 0, flights = 0;
    for (unsigned long tick = 0; tick < ticks; ++tick) {
        BoatInput inputs[BOAT_FLEET_SLOTS];
        if (rules) {
            // The match owns recovery: capsized boats wait for its automatic respawn.
            for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i)
                inputs[i] = fleet.state.slots[i].active
                    ? boat_fleet_bot_hunt_input(&bots, &fleet, i)
                    : (BoatInput) { 0 };
        } else {
            boat_fleet_bots_drive(&bots, &fleet, inputs);
        }
        bool airborne[BOAT_FLEET_SLOTS];
        for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i)
            airborne[i] = fleet.state.slots[i].boat.motion_state == BOAT_AIRBORNE;
        double started = now_seconds();
        boat_fleet_step(&fleet, inputs);
        cost[tick] = (now_seconds() - started) * 1000;
        if (rules) {
            BoatMatchOutput out;
            boat_match_observe(&match, &fleet, &out);
            fleet.state.combat.firing = match.phase != BOAT_MATCH_INTERMISSION;
            for (unsigned i = 0; i < out.count; ++i)
                if (out.events[i].kind == BOAT_MATCH_ROUND_START)
                    boat_combat_clear(&fleet);
            for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i)
                if (out.spawn[i]) {
                    boat_fleet_request_spawn(&fleet, i);
                    bots.waypoint[i] = 0;
                    ++bots.resets;
                }
            for (unsigned i = 0; i < out.count; ++i)
                print_event(NULL, &match, &out.events[i]);
        }
        const BoatFleetStats *s = &fleet.stats;
        contacts += s->contacts;
        residual += s->residual;
        passes += s->passes;
        spawned += s->spawned;
        blocked += s->spawn_blocked;
        failures += s->emission_failures;
        max_passes = s->passes > max_passes ? s->passes : max_passes;
        max_pair_tests = s->pair_tests > max_pair_tests ? s->pair_tests : max_pair_tests;
        peak_live = fleet.state.wakes.live > peak_live ? fleet.state.wakes.live : peak_live;
        for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i)
            flights += fleet.state.slots[i].active && !airborne[i]
                && fleet.state.slots[i].boat.motion_state == BOAT_AIRBORNE;
        if (report && (tick + 1) % report == 0)
            printf("tick %" PRIu64 " hash %016" PRIx64 " live %u\n", fleet.state.tick,
                boat_fleet_hash(&fleet.state), fleet.state.wakes.live);
    }
    qsort(cost, ticks, sizeof(*cost), compare_double);
    double total = 0;
    for (unsigned long i = 0; i < ticks; ++i)
        total += cost[i];
    printf("boat_server scripted: boats=%lu ticks=%" PRIu64 " hash=%016" PRIx64 "\n", boats,
        fleet.state.tick, boat_fleet_hash(&fleet.state));
    if (rules) {
        printf("deathmatch: round=%" PRIu32 " phase=%d", match.round, (int)match.phase);
        for (unsigned i = 0; i < boats; ++i)
            printf(" %u=%" PRId32 "/%" PRIu32 "/%" PRIu32 "/%" PRIu32, i + 1, match.rows[i].score,
                match.rows[i].caused, match.rows[i].capsized, match.rows[i].self);
        printf(" (score/kills/deaths/self)\n");
    }
    printf("wakes: emitted=%" PRIu64 " live=%u peak_live=%u failures=%lu\n",
        fleet.state.wakes.emitted, fleet.state.wakes.live, peak_live, failures);
    printf("contacts: pairs=%lu passes=%lu max_passes=%u max_pair_tests=%u residual=%lu\n",
        contacts, passes, max_passes, max_pair_tests, residual);
    printf("lives: spawned=%lu blocked_retries=%lu bot_recoveries=%u flights=%u\n", spawned,
        blocked, bots.resets, flights);
    printf("tick_ms: mean=%.4f p50=%.4f p95=%.4f p99=%.4f max=%.4f checkpoint_bytes=%zu\n",
        total / (double)ticks, cost[ticks / 2], cost[ticks * 95 / 100], cost[ticks * 99 / 100],
        cost[ticks - 1], sizeof(BoatFleetState));
    free(cost);
    if (failures) {
        fprintf(stderr, "boat_server: wake emission contract failed.\n");
        return 1;
    }
    return 0;
}

static volatile sig_atomic_t stop_requested;

// Standard input has ended: the hosting game closed its end of the pipe or exited.
static bool stdin_closed(void)
{
#ifdef _WIN32
    HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
    DWORD waiting = 0;
    if (in == NULL || in == INVALID_HANDLE_VALUE)
        return true;
    if (!PeekNamedPipe(in, NULL, 0, NULL, &waiting, NULL))
        return true; // ERROR_BROKEN_PIPE once the writer is gone.
    char discard[256];
    DWORD read = 0;
    if (waiting
        && !ReadFile(
            in, discard, waiting < sizeof(discard) ? waiting : sizeof(discard), &read, NULL))
        return true;
    return false;
#else
    struct pollfd in = { .fd = STDIN_FILENO, .events = POLLIN };
    while (poll(&in, 1, 0) > 0) {
        if (in.revents & (POLLERR | POLLNVAL))
            return true;
        char discard[256];
        if (read(STDIN_FILENO, discard, sizeof(discard)) <= 0)
            return true; // End of file: every writer has closed.
    }
    return false;
#endif
}

static void request_stop(int signal_number)
{
    (void)signal_number;
    stop_requested = 1;
}

typedef struct {
    const char *bind, *token;
    uint16_t rtc_port;
    const char *ice_server;
    uint16_t port, lan_port; // LAN discovery off when zero.
    bool waves;
    unsigned long players, ticks, report;
    unsigned long peer_timeout; // Seconds; 0 keeps BOAT_NET_TIMEOUT.
    const BoatMatchRules *rules; // Deathmatch rules, or NULL for free play.
    bool race, exit_with_stdin, until_empty;
    BoatLocationId location;
    const BoatWorldResources *world; // The location's resources, owned by main.
} RoomOptions;

static void print_roster(const BoatNetServer *server)
{
    printf("players: %u [", boat_net_server_players(server));
    for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i) {
        int peer = server->slot_peer[i];
        printf("%s%u:%s", i ? " " : "", i + 1, peer < 0 ? "-" : server->peers[peer].name);
    }
    printf("]\n");
    fflush(stdout);
}

static bool open_world(BoatWorldResources *world, BoatLocationId location)
{
    if (boat_world_resources_init(world, boat_location(location)))
        return true;
    fprintf(stderr, "boat_server: cannot prepare the %s world.\n", boat_location(location)->name);
    return false;
}

static int room(const RoomOptions *options)
{
    char error[160];
    if (!boat_net_enet_startup()) {
        fprintf(stderr, "boat_server: cannot initialize networking.\n");
        return 1;
    }
    BoatNetEnet *enet = boat_net_enet_listen(options->bind, options->port, error, sizeof(error));
    if (!enet) {
        fprintf(stderr, "boat_server: %s\n", error);
        boat_net_enet_cleanup();
        return 1;
    }
    BoatNetTransport transport = boat_net_enet_transport(enet);
#ifdef PROTEUS_WEBRTC
    BoatNetRtc *rtc = NULL;
    BoatNetMux mux;
    if (options->rtc_port) {
        rtc = boat_net_rtc_listen(
            options->bind, options->rtc_port, options->ice_server, error, sizeof(error));
        if (!rtc) {
            fprintf(stderr, "boat_server: %s\n", error);
            boat_net_enet_close(enet, 0);
            boat_net_enet_cleanup();
            return 1;
        }
        boat_net_mux_init(&mux, transport, boat_net_rtc_transport(rtc));
        transport = boat_net_mux_transport(&mux);
        printf("WebRTC signaling: ws://%s:%u (ENet still on UDP %u)\n", options->bind,
            options->rtc_port, options->port);
        fflush(stdout);
    }
#endif
    // Through u64: a double above 2^32 saturates (on arm64) when converted to u32 directly.
    uint32_t epoch = (uint32_t)time(NULL) ^ (uint32_t)(uint64_t)(boat_net_clock() * 1e6);
    BoatNetServerConfig config = { .token = options->token,
        .waves = options->waves,
        .epoch = epoch ? epoch : 1,
        .max_players = (unsigned)options->players,
        .resources = options->world,
        .deathmatch = options->rules != NULL,
        .match = options->rules ? *options->rules : (BoatMatchRules) { 0 },
        .race = options->race,
        .race_rules = boat_race_default_rules() };
    static BoatNetServer server;
    if (!boat_net_server_init(&server, transport, &config)) {
        fprintf(stderr, "boat_server: invalid room configuration.\n");
#ifdef PROTEUS_WEBRTC
        boat_net_rtc_close(rtc);
#endif
        boat_net_enet_close(enet, 0);
        boat_net_enet_cleanup();
        return 1;
    }
    server.tick_limit = options->ticks; // --ticks/--seconds run exactly that many ticks.
    server.peer_timeout = (double)options->peer_timeout;
    BoatNetLan *lan = NULL;
    char lan_note[64] = "LAN discovery off";
    if (options->lan_port) {
        lan = boat_net_lan_listen(options->bind, options->lan_port, error, sizeof(error));
        if (lan)
            snprintf(lan_note, sizeof(lan_note), "LAN discovery on UDP port %u",
                (unsigned)options->lan_port);
        else
            fprintf(stderr, "boat_server: not answering LAN searches: %s\n", error);
    }
    BoatNetLanRoom advert = { .version = BOAT_NET_VERSION,
        .physics_revision = BOAT_PHYSICS_REVISION,
        .content_digest = server.content_digest,
        .epoch = config.epoch,
        .port = options->port,
        .mode = options->race ? BOAT_NET_MODE_RACE
            : options->rules  ? BOAT_NET_MODE_DEATHMATCH
                              : BOAT_NET_MODE_FREE,
        .max_players = (uint8_t)options->players,
        .token = options->token[0] != 0 };
    // Answers name a specific bind address; 0.0.0.0 leaves it to the sender's address.
    boat_net_parse_ipv4(options->bind, &advert.address);
    boat_net_host_name(advert.name);
    signal(SIGINT, request_stop);
    signal(SIGTERM, request_stop);
    printf("boat_server listening on %s:%u (%s, %s water, %s, protocol %d, handling %d, "
           "epoch %08" PRIx32 ", up to %lu players, %s)\n",
        options->bind, (unsigned)options->port, boat_location(options->location)->name,
        options->waves ? "wave" : "calm",
        options->race        ? "race"
            : options->rules ? "deathmatch"
                             : "free play",
        BOAT_NET_VERSION, BOAT_PHYSICS_REVISION, config.epoch, options->players, lan_note);
    fflush(stdout);
    unsigned players = 0;
    unsigned max_mtu = 0;
    uint64_t events = 0, race_events = 0;
    double busy = 0, started = boat_net_clock();
    int result = 0;
    while (!stop_requested && (!options->ticks || server.fleet.state.tick < options->ticks)) {
        if (options->exit_with_stdin && stdin_closed()) {
            printf("boat_server: standard input closed; closing the room.\n");
            break;
        }
        const BoatNetServerStats *st = &server.stats;
        if (options->until_empty && st->accepted && st->left + st->timeouts >= st->accepted) {
            printf("boat_server: every player has left; closing the room.\n");
            break;
        }
        double now = boat_net_clock();
        uint64_t before = server.fleet.state.tick;
        if (!boat_net_server_service(&server, now)) {
            fprintf(stderr,
                "boat_server: overloaded (more than 500 ms behind for a second); "
                "closing the room.\n");
            result = 1;
            break;
        }
        boat_net_enet_flush(enet);
        if (lan) {
            advert.players = (uint8_t)boat_net_server_players(&server);
            boat_net_lan_answer(lan, &advert);
        }
        busy += boat_net_clock() - now;
        for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i) {
            int peer = server.slot_peer[i];
#ifdef PROTEUS_WEBRTC
            if (rtc && peer >= 0)
                peer = mux.owner[peer] == 0 ? (int)mux.local[peer] : -1;
#endif
            unsigned mtu = peer < 0 ? 0 : boat_net_enet_peer_mtu(enet, (unsigned)peer);
            max_mtu = mtu > max_mtu ? mtu : max_mtu;
        }
        // Ring overruns between services are impossible: at most 8 ticks, 8 events each, 16 kept.
        for (; events < server.event_count; ++events)
            print_event(&server, &server.match, &server.events[events % BOAT_NET_MATCH_EVENTS]);
        for (; race_events < server.race_event_count; ++race_events)
            print_race_event(&server, &server.race_events[race_events % BOAT_NET_MATCH_EVENTS]);
        if (boat_net_server_players(&server) != players) {
            players = boat_net_server_players(&server);
            print_roster(&server);
        }
        if (options->report
            && server.fleet.state.tick / options->report != before / options->report)
            printf("tick %" PRIu64 " hash %016" PRIx64 " live %u players %u\n",
                server.fleet.state.tick, boat_fleet_hash(&server.fleet.state),
                server.fleet.state.wakes.live, players);
        boat_net_enet_wait(enet, boat_net_server_next_tick(&server) - boat_net_clock());
    }
    boat_net_server_shutdown(&server);
    boat_net_enet_flush(enet);
    BoatNetTransportStats net = transport.stats(transport.context);
#ifdef PROTEUS_WEBRTC
    boat_net_rtc_close(rtc);
#endif
    boat_net_enet_close(enet, 1);
    boat_net_lan_close(lan);
    boat_net_enet_cleanup();
    const BoatNetServerStats *s = &server.stats;
    double elapsed = boat_net_clock() - started;
    printf("boat_server room: ticks=%" PRIu64 " seconds=%.2f accepted=%" PRIu64 " left=%" PRIu64
           " timeouts=%" PRIu64 "\n",
        server.fleet.state.tick, elapsed, s->accepted, s->left, s->timeouts);
    printf("rejected: version=%" PRIu64 " physics=%" PRIu64 " content=%" PRIu64 " token=%" PRIu64
           " full=%" PRIu64 " malformed=%" PRIu64 "\n",
        s->rejected[BOAT_NET_REJECT_VERSION], s->rejected[BOAT_NET_REJECT_PHYSICS],
        s->rejected[BOAT_NET_REJECT_CONTENT], s->rejected[BOAT_NET_REJECT_TOKEN],
        s->rejected[BOAT_NET_REJECT_FULL], s->rejected[BOAT_NET_REJECT_MALFORMED]);
    printf("sent: snapshots=%" PRIu64 " birth_batches=%" PRIu64 " births=%" PRIu64
           " bootstraps=%" PRIu64 " bootstrap_bytes=%" PRIu64 " resyncs=%" PRIu64 "\n",
        s->snapshots, s->birth_batches, s->births_sent, s->bootstraps, s->bootstrap_bytes,
        s->resyncs);
    printf("commands: stored=%" PRIu64 " duplicate=%" PRIu64 " late=%" PRIu64 " future=%" PRIu64
           " stale_life=%" PRIu64 " stale_packets=%" PRIu64 " rate_limited=%" PRIu64 "\n",
        s->commands, s->duplicate_commands, s->late_commands, s->future_commands,
        s->stale_life_commands, s->stale_packets, s->rate_limited);
    printf("inputs: received=%" PRIu64 " repeated=%" PRIu64 " zero=%" PRIu64 " neutral=%" PRIu64
           "\n",
        s->input_states[BOAT_NET_INPUT_RECEIVED], s->input_states[BOAT_NET_INPUT_REPEATED],
        s->input_states[BOAT_NET_INPUT_ZERO], s->input_states[BOAT_NET_INPUT_NEUTRAL]);
    printf("faults: malformed=%" PRIu64 " forbidden=%" PRIu64 " misbehaving=%" PRIu64
           " output=%" PRIu64 " emission=%" PRIu64 " max_backlog=%u\n",
        s->malformed, s->forbidden, s->misbehaving, s->output_faults, s->emission_failures,
        s->max_backlog);
    printf("transport: sent=%" PRIu64 " bytes=%" PRIu64 " received=%" PRIu64 " bytes=%" PRIu64
           " dropped=%" PRIu64 " max_message=%zu max_datagram=%zu peer_mtu=%u\n",
        net.sent, net.sent_bytes, net.received, net.received_bytes, net.dropped, net.max_message,
        net.max_datagram, max_mtu);
    printf("service_ms_per_tick: %.4f\n",
        server.fleet.state.tick ? busy * 1000 / (double)server.fleet.state.tick : 0);
    if (s->output_faults || s->emission_failures)
        result = 1;
    return result;
}

int boat_server_main(int argc, char **argv)
{
    unsigned long ticks = 0, boats = BOAT_FLEET_SLOTS, report = 0, seconds = 0, round = 0;
    bool deathmatch = false, race = false, location_given = false;
    bool scripted_mode = false, room_option = false;
    static BoatWorldResources world;
    RoomOptions room_options = { .bind = "127.0.0.1",
        .token = "",
        .port = BOAT_NET_PORT,
        .lan_port = BOAT_NET_DISCOVERY_PORT,
        .players = BOAT_FLEET_SLOTS };
    for (int i = 1; i < argc; ++i) {
        const char *value = i + 1 < argc ? argv[i + 1] : NULL;
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(stdout);
            return 0;
        } else if (strcmp(argv[i], "--scripted") == 0) {
            scripted_mode = true;
        } else if (strcmp(argv[i], "--ticks") == 0 && value
            && parse_count(value, 1, 120ul * 60 * 60 * 24, &ticks)) {
            ++i;
        } else if (strcmp(argv[i], "--seconds") == 0 && value
            && parse_count(value, 1, 60ul * 60 * 24, &seconds)) {
            ++i;
        } else if (strcmp(argv[i], "--boats") == 0 && value
            && parse_count(value, 1, BOAT_FLEET_SLOTS, &boats)) {
            ++i;
        } else if (strcmp(argv[i], "--report-every") == 0 && value
            && parse_count(value, 1, 120ul * 60 * 60 * 24, &report)) {
            ++i;
        } else if (strcmp(argv[i], "--bind") == 0 && value) {
            room_options.bind = value;
            room_option = true;
            ++i;
        } else if (strcmp(argv[i], "--port") == 0 && value) {
            if (!boat_net_parse_port(value, &room_options.port)) {
                fprintf(stderr, "boat_server: --port needs a UDP port 1-65535, not '%s'.\n", value);
                return 2;
            }
            room_option = true;
            ++i;
        } else if (strcmp(argv[i], "--rtc-port") == 0 && value) {
#ifdef PROTEUS_WEBRTC
            if (!boat_net_parse_port(value, &room_options.rtc_port))
                return 2;
            room_option = true;
            ++i;
#else
            fprintf(stderr, "boat_server: --rtc-port requires a PROTEUS_WEBRTC=ON build.\n");
            return 2;
#endif
        } else if (strcmp(argv[i], "--ice-server") == 0 && value) {
            room_options.ice_server = value;
            room_option = true;
            ++i;
        } else if (strcmp(argv[i], "--lan-port") == 0 && value) {
            if (!boat_net_parse_port(value, &room_options.lan_port)) {
                fprintf(
                    stderr, "boat_server: --lan-port needs a UDP port 1-65535, not '%s'.\n", value);
                return 2;
            }
            room_option = true;
            ++i;
        } else if (strcmp(argv[i], "--exit-with-stdin") == 0) {
            room_options.exit_with_stdin = true;
            room_option = true;
        } else if (strcmp(argv[i], "--until-empty") == 0) {
            room_options.until_empty = true;
            room_option = true;
        } else if (strcmp(argv[i], "--no-lan") == 0) {
            room_options.lan_port = 0;
            room_option = true;
        } else if (strcmp(argv[i], "--token") == 0 && value) {
            if (strlen(value) > BOAT_NET_TOKEN_BYTES) {
                fprintf(stderr, "boat_server: --token is at most %d characters.\n",
                    BOAT_NET_TOKEN_BYTES);
                return 2;
            }
            room_options.token = value;
            room_option = true;
            ++i;
        } else if (strcmp(argv[i], "--players") == 0 && value
            && parse_count(value, 1, BOAT_FLEET_SLOTS, &room_options.players)) {
            room_option = true;
            ++i;
        } else if (strcmp(argv[i], "--waves") == 0) {
            room_options.waves = true;
            room_option = true;
        } else if (strcmp(argv[i], "--peer-timeout") == 0 && value
            && parse_count(value, 1, 60, &room_options.peer_timeout)) {
            room_option = true;
            ++i;
        } else if (strcmp(argv[i], "--mode") == 0 && value
            && (strcmp(value, "free") == 0 || strcmp(value, "deathmatch") == 0
                || strcmp(value, "race") == 0)) {
            deathmatch = strcmp(value, "deathmatch") == 0;
            race = strcmp(value, "race") == 0;
            ++i;
        } else if (strcmp(argv[i], "--round-seconds") == 0 && value
            && parse_count(value, 2, 60ul * 60, &round)) {
            ++i;
        } else if (strcmp(argv[i], "--location") == 0 && value) {
            const BoatWorldDefinition *location = boat_location_find(value);
            if (!location) {
                fprintf(stderr, "boat_server: unknown location '%s'.\n", value);
                return 2;
            }
            room_options.location = location->id;
            location_given = true;
            ++i;
        } else {
            fprintf(stderr, "boat_server: invalid argument: %s\n", argv[i]);
            usage(stderr);
            return 2;
        }
    }
    if (room_options.ice_server && !room_options.rtc_port) {
        fprintf(stderr, "boat_server: --ice-server needs --rtc-port.\n");
        return 2;
    }
    if (round && !deathmatch) {
        fprintf(stderr, "boat_server: --round-seconds needs --mode deathmatch.\n");
        return 2;
    }
    if (race && location_given && room_options.location != BOAT_LOCATION_RIVER) {
        fprintf(stderr, "boat_server: --mode race runs on the river location.\n");
        return 2;
    }
    if (race)
        room_options.location = BOAT_LOCATION_RIVER;
    BoatMatchRules rules = boat_match_default_rules();
    if (round)
        rules.round_ticks = round * BOAT_TICK_HZ;
    if (scripted_mode) {
        if (race) {
            fprintf(stderr,
                "boat_server: --scripted has no race mode; validate_boat_race runs scripted "
                "races.\n");
            return 2;
        }
        if (room_option || seconds) {
            fprintf(stderr,
                "boat_server: --scripted runs without a socket; room options "
                "(--bind, --port, --lan-port, --no-lan, --exit-with-stdin, --until-empty, --token, "
                "--players, --waves, --peer-timeout, --seconds) do not apply.\n");
            return 2;
        }
        if (!open_world(&world, room_options.location))
            return 1;
        int status
            = scripted(ticks ? ticks : 7200, boats, report, deathmatch ? &rules : NULL, &world);
        boat_world_resources_destroy(&world);
        return status;
    }
    if (boats != BOAT_FLEET_SLOTS) {
        fprintf(stderr, "boat_server: --boats applies to --scripted; rooms use --players.\n");
        return 2;
    }
    if (ticks && seconds) {
        fprintf(stderr, "boat_server: give --ticks or --seconds, not both.\n");
        return 2;
    }
    room_options.ticks = seconds ? seconds * BOAT_TICK_HZ : ticks;
    room_options.report = report;
    room_options.rules = deathmatch ? &rules : NULL;
    room_options.race = race;
    if (!open_world(&world, room_options.location))
        return 1;
    room_options.world = &world;
    int status = room(&room_options);
    boat_world_resources_destroy(&world);
    return status;
}
