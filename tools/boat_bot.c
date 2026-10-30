/* Multiplayer M3 scripted client: joins a boat_server room over ENet, drives its server-owned
 * boat with the boat_fleet_bots route (or, with --hunt, at the nearest other boat) from the
 * authoritative mirror, and checks the mirror's wake reconstruction against each snapshot's
 * digest. In deathmatch rooms it prints each finished round's scoreboard as received. In race
 * rooms it races: full throttle through the countdown, then each gate in its own lane, and it
 * prints each round's results as received.
 * --discover lists the rooms that answer a LAN search instead. Headless; for process smoke tests.
 */
#include "boat_fleet_bots.h"
#include "boat_net_client.h"
#include "boat_net_enet.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage_line(FILE *out, const char *option, const char *description)
{
    fprintf(out, "  %-29s %s\n", option, description);
}

static void usage(FILE *out)
{
    fprintf(out,
        "Usage: boat_bot --connect HOST:PORT [OPTIONS]\n"
        "       boat_bot --discover [OPTIONS]\n"
        "\n"
        "Joins a boat_server room and drives one boat by script, then leaves and prints a\n"
        "summary. In deathmatch rooms it rams and crosses wakes; in race rooms it races the\n"
        "course. Headless; for process smoke tests.\n");

    fprintf(out, "\nJOIN\n");
    usage_line(out, "--connect HOST:PORT", "Room endpoint (required)");
    usage_line(out, "--name TEXT", "Player name (default bot; at most 16 bytes)");
    usage_line(out, "--token TEXT", "Room token (default empty; at most 32 bytes)");
    usage_line(out, "--seconds S", "Drive for S seconds (default 5)");
    usage_line(out, "--snapshots N", "Leave after N snapshots instead of a duration");
    usage_line(out, "--hunt", "Drive at the nearest other boat to ram or cross its wake");
    usage_line(out, "--predict", "Exercise client prediction");
    usage_line(out, "--reset-every S", "Request a reset every S seconds (2-86400)");
    usage_line(out, "--inactive-every S", "Send inactive controls for 1 s every S seconds");
    usage_line(out, "--expect-reject", "Succeed only when the room refuses the connection");

    fprintf(out, "\nDISCOVER\n");
    usage_line(out, "--discover", "Search for LAN rooms instead of joining one");
    usage_line(out, "--lan-port N", "Discovery port (default 24681)");
    usage_line(out, "--seconds S", "Search for S seconds (default 2)");
    usage_line(out, "--first", "Stop at the first answer");

    fprintf(out,
        "\n--snapshots replaces --seconds; --first requires --discover.\n"
        "Exit status: 0 success, 1 connection failure, 2 usage, 3 rejected.\n");
}

static int discover(uint16_t lan_port, unsigned long seconds, bool first)
{
    char error[160];
    if (!boat_net_enet_startup()) {
        fprintf(stderr, "boat_bot: cannot initialize networking.\n");
        return 1;
    }
    BoatNetLan *lan = boat_net_lan_search(error, sizeof(error));
    if (!lan) {
        fprintf(stderr, "boat_bot: %s\n", error);
        boat_net_enet_cleanup();
        return 1;
    }
    static const char *const modes[] = { "free", "deathmatch", "race" };
    BoatNetLanList list = { 0 };
    double start = boat_net_clock(), queried = -1;
    for (double now = start; now - start < (double)seconds && !(first && list.count);
        now = boat_net_clock()) {
        if (queried < 0 || now - queried >= .5) {
            boat_net_lan_query(lan, lan_port);
            queried = now;
        }
        BoatNetLanRoom room;
        while (boat_net_lan_receive(lan, &room))
            boat_net_lan_list_note(&list, &room, now);
        if (!(first && list.count))
            boat_net_lan_wait(lan, .02); // Wakes at once when an answer arrives.
    }
    boat_net_lan_close(lan);
    boat_net_enet_cleanup();
    for (unsigned i = 0; i < list.count; ++i) {
        const BoatNetLanRoom *r = &list.rooms[i];
        char endpoint[32];
        boat_net_lan_endpoint(r, endpoint, sizeof(endpoint));
        printf("lan room %s name=%s mode=%s players=%u/%u token=%s protocol=%u compatible=%s\n",
            endpoint, r->name[0] ? r->name : "-",
            r->mode < BOAT_NET_MODE_LIMIT ? modes[r->mode] : "unknown", r->players, r->max_players,
            r->token ? "yes" : "no", r->version, boat_net_lan_room_compatible(r) ? "yes" : "no");
    }
    printf("boat_bot discover: rooms=%u\n", list.count);
    return list.count ? 0 : 1;
}

int main(int argc, char **argv)
{
    const char *endpoint = NULL, *name = "bot", *token = "";
    unsigned long seconds = 5, snapshots = 0, reset_every = 0, inactive_every = 0;
    bool expect_reject = false, hunt = false, search = false, timed = false, predict = false;
    bool first = false;
    uint16_t lan_port = BOAT_NET_DISCOVERY_PORT;
    for (int i = 1; i < argc; ++i) {
        const char *value = i + 1 < argc ? argv[i + 1] : NULL;
        if (strcmp(argv[i], "--help") == 0) {
            usage(stdout);
            return 0;
        } else if (strcmp(argv[i], "--connect") == 0 && value) {
            endpoint = argv[++i];
        } else if (strcmp(argv[i], "--name") == 0 && value
            && strlen(value) <= BOAT_NET_NAME_BYTES) {
            name = argv[++i];
        } else if (strcmp(argv[i], "--token") == 0 && value
            && strlen(value) <= BOAT_NET_TOKEN_BYTES) {
            token = argv[++i];
        } else if (strcmp(argv[i], "--seconds") == 0 && value) {
            char *end;
            seconds = strtoul(value, &end, 10);
            if (!*value || *end || value[0] == '-' || seconds < 1 || seconds > 86400) {
                usage(stderr);
                return 2;
            }
            timed = true;
            ++i;
        } else if ((strcmp(argv[i], "--reset-every") == 0
                       || strcmp(argv[i], "--inactive-every") == 0)
            && value) {
            char *end;
            unsigned long interval = strtoul(value, &end, 10);
            if (!*value || *end || value[0] == '-' || interval < 2 || interval > 86400) {
                usage(stderr);
                return 2;
            }
            if (strcmp(argv[i], "--reset-every") == 0)
                reset_every = interval;
            else
                inactive_every = interval;
            ++i;
        } else if (strcmp(argv[i], "--snapshots") == 0 && value) {
            char *end;
            snapshots = strtoul(value, &end, 10);
            if (!*value || *end || value[0] == '-' || snapshots < 1 || snapshots > 10000000) {
                usage(stderr);
                return 2;
            }
            ++i;
        } else if (strcmp(argv[i], "--first") == 0) {
            first = true;
        } else if (strcmp(argv[i], "--predict") == 0) {
            predict = true;
        } else if (strcmp(argv[i], "--discover") == 0) {
            search = true;
        } else if (strcmp(argv[i], "--lan-port") == 0 && value
            && boat_net_parse_port(value, &lan_port)) {
            ++i;
        } else if (strcmp(argv[i], "--hunt") == 0) {
            hunt = true;
        } else if (strcmp(argv[i], "--expect-reject") == 0) {
            expect_reject = true;
        } else {
            fprintf(stderr, "boat_bot: invalid argument: %s\n", argv[i]);
            usage(stderr);
            return 2;
        }
    }
    if (search) {
        if (endpoint || hunt || expect_reject || predict || reset_every || inactive_every
            || snapshots) {
            fprintf(stderr, "boat_bot: --discover does not join a room.\n");
            return 2;
        }
        return discover(lan_port, timed ? seconds : 2, first);
    }
    if (first || (snapshots && timed)) {
        fprintf(stderr, "boat_bot: --first needs --discover; --snapshots replaces --seconds.\n");
        return 2;
    }
    char host[256], error[160];
    uint16_t port;
    if (!endpoint || !boat_net_parse_endpoint(endpoint, host, sizeof(host), &port)) {
        fprintf(stderr, "boat_bot: --connect needs HOST:PORT.\n");
        usage(stderr);
        return 2;
    }
    if (!boat_net_enet_startup()) {
        fprintf(stderr, "boat_bot: cannot initialize networking.\n");
        return 1;
    }
    BoatNetEnet *enet = boat_net_enet_connect(host, port, error, sizeof(error));
    if (!enet) {
        fprintf(stderr, "boat_bot: %s\n", error);
        boat_net_enet_cleanup();
        return 1;
    }
    // The room picks the location, so prepare every one; the bot never queries them after the
    // session loop.
    static BoatWorldResources worlds[BOAT_LOCATION_COUNT];
    for (int i = 0; i < BOAT_LOCATION_COUNT; ++i)
        if (!boat_world_resources_init(&worlds[i], boat_location((BoatLocationId)i))) {
            fprintf(stderr, "boat_bot: cannot prepare the %s world.\n",
                boat_location((BoatLocationId)i)->name);
            boat_net_enet_close(enet, 0);
            boat_net_enet_cleanup();
            return 1;
        }
    static BoatNetClient client;
    BoatNetClientConfig config = { .name = name,
        .token = token,
        .build = "boat_bot M5",
        .predict = predict,
        .resources = worlds };
    boat_net_client_init(&client, boat_net_enet_transport(enet), &config, boat_net_clock());
    BoatFleetBots bots = { 0 };
    double started = 0, end = 0;
    double travelled = 0;
    Vector2 last_position = { 0 };
    uint32_t last_life = 0;
    uint64_t received_inputs = 0, input_samples = 0, last_snapshots = 0;
    unsigned capsized = 0, resets = 0, mtu = 0;
    uint32_t reported_round = 0, reported_race = 0;
    unsigned race_finishes = 0, race_dnf = 0, feed_seen = 0;
    uint64_t held_ticks = 0, last_held_tick = 0;
    bool was_held = false;
    bool joined = false, running = true;
    double next_reset = 0, next_inactive = 0, inactive_until = 0;
    unsigned inactive_periods = 0, max_players = 0, max_wakes = 0;
    while (running) {
        double now = boat_net_clock();
        BoatInput input = { 0 };
        const BoatFleetSlot *own = boat_net_client_own(&client);
        if (own) {
            input = hunt ? boat_fleet_bot_hunt_input(&bots, &client.fleet, client.slot)
                         : boat_fleet_bot_input(&bots, &client.fleet, client.slot);
            const BoatNetRaceRow *row = &client.race.slots[client.slot];
            if (client.accept.mode == BOAT_NET_MODE_RACE && client.race_valid && row->current) {
                if (own->held) // Grid, countdown or parked: prepare full throttle on the grid.
                    input = (BoatInput) { .throttle_rate
                        = boat_throttle_rate(own->boat.throttle, row->parked ? 0 : 1) };
                else if (client.race.phase == BOAT_RACE_RACING
                    || client.race.phase == BOAT_RACE_RESULTS)
                    input = boat_fleet_bot_race_input(&client.fleet, boat_race_river_course(),
                        row->gates, row->status != BOAT_RACE_RUNNING, client.slot);
            }
            // Deathmatch and race rooms recover capsized boats themselves.
            if (own->boat.motion_state == BOAT_CAPSIZED
                && client.accept.mode == BOAT_NET_MODE_FREE) {
                if (++capsized == 2 * 60) { // Two seconds of 60 Hz service.
                    boat_net_client_request_reset(&client);
                    bots.waypoint[client.slot] = 0;
                    ++resets;
                }
            } else {
                capsized = 0;
            }
        }
        if (joined && reset_every && now >= next_reset) {
            if (!client.resetting) {
                boat_net_client_request_reset(&client);
                ++resets;
            }
            next_reset = now + (double)reset_every;
        }
        if (joined && inactive_every && now >= next_inactive) {
            inactive_until = now + 1;
            next_inactive = now + (double)inactive_every;
            ++inactive_periods;
        }
        running = boat_net_client_service(&client, now, input, now < inactive_until);
        boat_net_enet_flush(enet);
        if (!joined && client.phase == BOAT_NET_CLIENT_PLAYING) {
            joined = true;
            started = now;
            end = now + (double)seconds;
            next_reset = now + (double)reset_every;
            next_inactive = now + (double)inactive_every;
            printf("boat_bot joined: slot=%u tick=%" PRIu64 " waves=%d location=%s\n",
                client.slot + 1, client.fleet.state.tick, client.accept.waves,
                boat_location(client.accept.location)->name);
            fflush(stdout);
        }
        if (joined) {
            const BoatFleetSlot *slot = &client.fleet.state.slots[client.slot];
            // Measure observed simulation time, not service-loop iterations. OS timer
            // granularity and load can change polling frequency without shortening the grid.
            bool held = slot->active && slot->held;
            uint64_t tick = client.fleet.state.tick;
            if (held && was_held && tick > last_held_tick)
                held_ticks += tick - last_held_tick;
            last_held_tick = tick;
            was_held = held;
            unsigned players = 0;
            for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i)
                players += client.roster.slots[i].state == BOAT_NET_ROSTER_PLAYING;
            max_players = players > max_players ? players : max_players;
            unsigned live = client.fleet.state.wakes.live;
            max_wakes = live > max_wakes ? live : max_wakes;
        }
        if (joined && client.stats.snapshots != last_snapshots) {
            last_snapshots = client.stats.snapshots;
            own = boat_net_client_own(&client);
            if (own) {
                if (own->life == last_life)
                    travelled += Vector2Distance(last_position, own->boat.position);
                last_life = own->life;
                last_position = own->boat.position;
                ++input_samples;
                received_inputs += client.inputs[client.slot].state == BOAT_NET_INPUT_RECEIVED;
            }
        }
        if (client.result.round != reported_round) {
            reported_round = client.result.round;
            printf("boat_bot round %" PRIu32 " end:", reported_round);
            for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i) {
                const BoatMatchRow *row = &client.result.slots[i].row;
                if (client.result.slots[i].present)
                    printf(" %u=%" PRId32 "/%" PRIu32 "/%" PRIu32 "/%" PRIu32, i + 1, row->score,
                        row->caused, row->capsized, row->self);
            }
            printf(" (score/kills/deaths/self)\n");
            fflush(stdout);
        }
        if (client.race_result.round != reported_race) {
            reported_race = client.race_result.round;
            printf("boat_bot race %" PRIu32 " results:", reported_race);
            for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i) {
                const BoatNetRaceRow *row = &client.race_result.slots[i];
                if (!row->entrant)
                    continue;
                if (row->status == BOAT_RACE_FINISHED)
                    printf(" %u=P%u/%.3fs", i + 1, row->place,
                        (double)row->finish / BOAT_RACE_SUBTICKS / BOAT_TICK_HZ);
                else
                    printf(" %u=DNF", i + 1);
            }
            printf("\n");
            fflush(stdout);
        }
        for (; feed_seen < client.race_feed_count; ++feed_seen) {
            const BoatNetRaceEvent *e = &client.race_feed[feed_seen % BOAT_NET_MATCH_FEED].event;
            race_finishes += e->kind == BOAT_RACE_FINISH;
            race_dnf += e->kind == BOAT_RACE_OUT;
        }
        unsigned peer_mtu = boat_net_enet_peer_mtu(enet, 0);
        mtu = peer_mtu > mtu ? peer_mtu : mtu;
        if (joined && (snapshots ? client.stats.snapshots >= snapshots : now >= end))
            break;
        boat_net_enet_wait(enet, 1.0 / 60);
    }
    BoatNetTransportStats net = boat_net_enet_transport(enet).stats(enet);
    BoatNetClientPhase phase = client.phase;
    const char *failure = boat_net_client_failure(&client);
    if (running)
        boat_net_client_leave(&client);
    boat_net_enet_close(enet, 1);
    boat_net_enet_cleanup();
    for (int i = 0; i < BOAT_LOCATION_COUNT; ++i)
        boat_world_resources_destroy(&worlds[i]);
    if (phase == BOAT_NET_CLIENT_REJECTED) {
        printf("boat_bot rejected: %s (server protocol %u, handling %" PRIu32 ")\n", failure,
            (unsigned)client.reject.version, client.reject.physics_revision);
        return expect_reject ? 0 : 3;
    }
    if (expect_reject) {
        fprintf(stderr, "boat_bot: expected a rejection.\n");
        return 1;
    }
    if (!joined || !running) {
        fprintf(stderr, "boat_bot: %s\n", joined ? failure : "could not join the room");
        return 1;
    }
    const BoatNetClientStats *s = &client.stats;
    printf("boat_bot summary: seconds=%.2f snapshots=%" PRIu64 " stale=%" PRIu64
           " consistent=%" PRIu64 " inconsistent=%" PRIu64 " repaired=%" PRIu64
           " max_mismatch_ms=%.1f resyncs=%" PRIu64 " bootstraps=%" PRIu64 " final=%s\n",
        boat_net_clock() - started, s->snapshots, s->stale_snapshots, s->consistent,
        s->inconsistent, s->repaired, s->max_mismatch * 1000, s->resyncs, s->bootstraps,
        client.wake_consistent ? "consistent" : "inconsistent");
    printf("births: applied=%" PRIu64 " duplicate=%" PRIu64 " held=%" PRIu64 " dropped=%" PRIu64
           " live=%u\n",
        s->births_applied, s->births_duplicate, s->births_held, s->births_dropped,
        client.fleet.state.wakes.live);
    printf("lifecycle: max_players=%u max_wakes=%u resets_completed=%" PRIu64
           " resets_dropped=%" PRIu64 " inactive_periods=%u max_reset_ms=%.1f\n",
        max_players, max_wakes, s->resets, s->resets_dropped, inactive_periods,
        s->max_reset * 1000);
    printf("control: commands=%" PRIu64 " packets=%" PRIu64 " received_ratio=%.3f lead=%u "
           "rtt_ms=%.2f travelled_m=%.1f resets=%u decode_errors=%" PRIu64 "\n",
        s->commands, s->input_packets,
        input_samples ? (double)received_inputs / (double)input_samples : 0, client.lead,
        client.rtt * 1000, travelled, resets, s->decode_errors);
    if (client.accept.mode == BOAT_NET_MODE_RACE)
        printf("race: events=%" PRIu64 " round=%" PRIu32 " phase=%u finishes=%u dnf=%u "
               "results=%" PRIu32 " held_ticks=%" PRIu64 " go=%" PRIu64 "\n",
            client.race_feed_count, client.race.round, (unsigned)client.race.phase, race_finishes,
            race_dnf, client.race_result.round, held_ticks, client.race_go);
    if (client.accept.mode == BOAT_NET_MODE_DEATHMATCH)
        printf("match: events=%" PRIu64 " round=%" PRIu32 " phase=%u finished=%" PRIu32 "\n",
            client.feed_count, client.match.round, (unsigned)client.match.phase,
            client.result.round);
    printf("transport: sent=%" PRIu64 " bytes=%" PRIu64 " received=%" PRIu64 " bytes=%" PRIu64
           " dropped=%" PRIu64 " max_message=%zu max_datagram=%zu peer_mtu=%u\n",
        net.sent, net.sent_bytes, net.received, net.received_bytes, net.dropped, net.max_message,
        net.max_datagram, mtu);
    return 0;
}
