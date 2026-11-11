#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L // clock_gettime and nanosleep under strict C17.
#endif
#ifndef _WIN32
#define _DEFAULT_SOURCE // getifaddrs, interface flags and SO_REUSEPORT.
#define _DARWIN_C_SOURCE
#endif
#include "boat_net_enet.h"

#ifdef _WIN32
// ENet includes windows.h after raylib; exclude the GDI/user names raylib also declares.
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#define NOUSER
#endif
#include <enet/enet.h>
#ifdef _WIN32
#include <ws2tcpip.h> // INTERFACE_INFO and SIO_GET_INTERFACE_LIST.
#else
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <time.h>
#endif
#include <ctype.h>

#define STASH_EVENTS 256 // Events received by a wait or settle before the next poll.

struct BoatNetEnet {
    ENetHost *host;
    ENetPeer *server; // Client side only.
    bool client;
    ENetEvent stash[STASH_EVENTS]; // A ring, oldest first.
    unsigned stash_first, stashed;
    BoatNetTransportStats stats;
};

// Service HOST for at most MS milliseconds, keeping any event for the next poll. False when
// nothing arrived or the stash is full.
static bool stash_service(BoatNetEnet *e, enet_uint32 ms)
{
    if (e->stashed == STASH_EVENTS)
        return false;
    ENetEvent *slot = &e->stash[(e->stash_first + e->stashed) % STASH_EVENTS];
    if (enet_host_service(e->host, slot, ms) <= 0)
        return false;
    ++e->stashed;
    return true;
}

// Largest UDP datagram any host in this process received (the intercept has no context).
static size_t max_datagram;

static int intercept(ENetHost *host, ENetEvent *event)
{
    (void)event;
    if (host->receivedDataLength > max_datagram)
        max_datagram = host->receivedDataLength;
    return 0; // Continue with ENet's own processing.
}

bool boat_net_enet_startup(void) { return enet_initialize() == 0; }

void boat_net_enet_cleanup(void) { enet_deinitialize(); }

static BoatNetEnet *create(const ENetAddress *address, size_t peers, char *error, size_t size)
{
    BoatNetEnet *e = calloc(1, sizeof(*e));
    if (!e) {
        snprintf(error, size, "out of memory");
        return NULL;
    }
    e->host = enet_host_create(address, peers, BOAT_NET_CHANNELS, 0, 0);
    if (!e->host) {
        free(e);
        return NULL;
    }
    e->host->mtu = BOAT_NET_ENET_MTU;
    e->host->maximumPacketSize = BOAT_NET_MAX_MESSAGE;
    e->host->maximumWaitingData = BOAT_NET_ENET_WAITING_BYTES;
    e->host->intercept = intercept;
    return e;
}

BoatNetEnet *boat_net_enet_listen(const char *address, uint16_t port, char *error, size_t size)
{
    ENetAddress bind = { .port = port };
    if (enet_address_set_host_ip(&bind, address) != 0) {
        snprintf(error, size, "invalid IPv4 bind address '%s'", address);
        return NULL;
    }
    BoatNetEnet *e = create(&bind, BOAT_NET_MAX_PEERS, error, size);
    if (!e)
        snprintf(error, size, "cannot listen on %s:%u (address in use or not available)", address,
            (unsigned)port);
    return e;
}

BoatNetEnet *boat_net_enet_connect(const char *host, uint16_t port, char *error, size_t size)
{
    ENetAddress address = { .port = port };
    if (enet_address_set_host(&address, host) != 0) {
        snprintf(error, size, "cannot resolve '%s' to an IPv4 address", host);
        return NULL;
    }
    BoatNetEnet *e = create(NULL, 1, error, size);
    if (!e) {
        snprintf(error, size, "cannot create a UDP socket");
        return NULL;
    }
    e->client = true;
    e->server = enet_host_connect(e->host, &address, BOAT_NET_CHANNELS, 0);
    if (!e->server) {
        snprintf(error, size, "cannot start connecting to %s:%u", host, (unsigned)port);
        enet_host_destroy(e->host);
        free(e);
        return NULL;
    }
    return e;
}

static ENetPeer *peer_of(BoatNetEnet *e, unsigned peer)
{
    if (e->client)
        return peer == 0 ? e->server : NULL;
    return peer < e->host->peerCount ? &e->host->peers[peer] : NULL;
}

static unsigned index_of(BoatNetEnet *e, const ENetPeer *peer)
{
    return e->client ? 0 : (unsigned)(peer - e->host->peers);
}

static bool poll_event(void *context, BoatNetEvent *event)
{
    BoatNetEnet *e = context;
    for (;;) {
        ENetEvent in;
        if (e->stashed) {
            in = e->stash[e->stash_first];
            e->stash_first = (e->stash_first + 1) % STASH_EVENTS;
            --e->stashed;
        } else if (enet_host_service(e->host, &in, 0) <= 0) {
            return false;
        }
        event->peer = index_of(e, in.peer);
        event->size = 0;
        switch (in.type) {
        case ENET_EVENT_TYPE_CONNECT:
            event->kind = BOAT_NET_EVENT_CONNECT;
            return true;
        case ENET_EVENT_TYPE_DISCONNECT:
            event->kind = BOAT_NET_EVENT_DISCONNECT;
            return true;
        case ENET_EVENT_TYPE_RECEIVE: {
            ENetPacket *packet = in.packet;
            bool valid = packet->dataLength > 0 && packet->dataLength <= BOAT_NET_MAX_MESSAGE
                && in.channelID < BOAT_NET_CHANNELS;
            if (valid) {
                event->kind = BOAT_NET_EVENT_MESSAGE;
                event->channel = (BoatNetChannel)in.channelID;
                event->size = packet->dataLength;
                memcpy(event->data, packet->data, packet->dataLength);
                ++e->stats.received;
                e->stats.received_bytes += packet->dataLength;
                if (packet->dataLength > e->stats.max_message)
                    e->stats.max_message = packet->dataLength;
            } else {
                ++e->stats.dropped;
            }
            enet_packet_destroy(packet);
            if (valid)
                return true;
            break;
        }
        default:
            break;
        }
    }
}

static bool send_message(
    void *context, unsigned peer, BoatNetChannel channel, const void *data, size_t size)
{
    BoatNetEnet *e = context;
    ENetPeer *p = peer_of(e, peer);
    if (!p || p->state != ENET_PEER_STATE_CONNECTED || size == 0 || size > BOAT_NET_MAX_MESSAGE
        || channel >= BOAT_NET_CHANNELS) {
        ++e->stats.dropped;
        return false;
    }
    // Messages fit one datagram, so unreliable ones never take ENet's reliable fragment path.
    enet_uint32 flags = channel == BOAT_NET_CONTROL || channel == BOAT_NET_BOOTSTRAP
        ? ENET_PACKET_FLAG_RELIABLE
        : channel == BOAT_NET_REGISTRY ? ENET_PACKET_FLAG_UNSEQUENCED
                                       : 0;
    ENetPacket *packet = enet_packet_create(data, size, flags);
    if (!packet || enet_peer_send(p, (enet_uint8)channel, packet) < 0) {
        if (packet)
            enet_packet_destroy(packet);
        ++e->stats.dropped;
        return false;
    }
    ++e->stats.sent;
    e->stats.sent_bytes += size;
    if (size > e->stats.max_message)
        e->stats.max_message = size;
    return true;
}

static void disconnect(void *context, unsigned peer)
{
    BoatNetEnet *e = context;
    ENetPeer *p = peer_of(e, peer);
    if (p && p->state != ENET_PEER_STATE_DISCONNECTED)
        enet_peer_disconnect_later(p, 0);
}

static BoatNetTransportStats stats(void *context)
{
    BoatNetEnet *e = context;
    BoatNetTransportStats s = e->stats;
    s.max_datagram = max_datagram;
    return s;
}

BoatNetTransport boat_net_enet_transport(BoatNetEnet *enet)
{
    return (BoatNetTransport) { enet, poll_event, send_message, disconnect, stats };
}

void boat_net_enet_flush(BoatNetEnet *enet) { enet_host_flush(enet->host); }

void boat_net_enet_wait(BoatNetEnet *enet, double seconds)
{
    if (enet->stashed)
        return;
    enet_uint32 ms = seconds <= 0 ? 0 : seconds > 1 ? 1000 : (enet_uint32)(seconds * 1000);
    stash_service(enet, ms);
}

bool boat_net_enet_settle(BoatNetEnet *const *hosts, unsigned count, double timeout)
{
    double deadline = boat_net_clock() + timeout;
    for (;;) {
        uint64_t sent = 0, received = 0;
        bool full = false;
        for (unsigned i = 0; i < count; ++i) {
            while (stash_service(hosts[i], 0)) { }
            full |= hosts[i]->stashed == STASH_EVENTS;
            sent += hosts[i]->host->totalSentPackets;
            received += hosts[i]->host->totalReceivedPackets;
        }
        if (sent == received)
            return true;
        if (full || boat_net_clock() > deadline)
            return false;
    }
}

void boat_net_enet_close(BoatNetEnet *enet, double seconds)
{
    if (!enet)
        return;
    for (size_t i = 0; i < enet->host->peerCount; ++i) {
        ENetPeer *p = &enet->host->peers[i];
        if (p->state != ENET_PEER_STATE_DISCONNECTED)
            enet_peer_disconnect_later(p, 0);
    }
    double deadline = boat_net_clock() + seconds;
    while (enet->host->connectedPeers > 0 && boat_net_clock() < deadline) {
        ENetEvent event;
        if (enet_host_service(enet->host, &event, 10) > 0 && event.type == ENET_EVENT_TYPE_RECEIVE)
            enet_packet_destroy(event.packet);
    }
    enet_host_destroy(enet->host);
    free(enet);
}

unsigned boat_net_enet_peer_mtu(BoatNetEnet *enet, unsigned peer)
{
    ENetPeer *p = peer_of(enet, peer);
    return p && p->state == ENET_PEER_STATE_CONNECTED ? p->mtu : 0;
}

static uint16_t socket_port(ENetSocket socket)
{
    ENetAddress address = { 0 };
    return enet_socket_get_address(socket, &address) == 0 ? address.port : 0;
}

uint16_t boat_net_enet_port(const BoatNetEnet *enet) { return socket_port(enet->host->socket); }

double boat_net_clock(void)
{
#ifdef _WIN32
    LARGE_INTEGER frequency, counter;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart / (double)frequency.QuadPart;
#else
    struct timespec stamp;
    clock_gettime(CLOCK_MONOTONIC, &stamp);
    return (double)stamp.tv_sec + (double)stamp.tv_nsec * 1e-9;
#endif
}

void boat_net_sleep(double seconds)
{
    if (seconds <= 0)
        return;
#ifdef _WIN32
    Sleep((DWORD)(seconds * 1000));
#else
    struct timespec wait = { (time_t)seconds, (long)((seconds - (double)(time_t)seconds) * 1e9) };
    nanosleep(&wait, NULL);
#endif
}

bool boat_net_parse_port(const char *text, uint16_t *port)
{
    char *end;
    unsigned long value = strtoul(text, &end, 10);
    if (!*text || *end || text[0] == '-' || text[0] == '+' || value < 1 || value > 65535)
        return false;
    *port = (uint16_t)value;
    return true;
}

bool boat_net_parse_ipv4(const char *text, uint32_t *address)
{
    uint32_t value = 0;
    for (int part = 0; part < 4; ++part) {
        if (part && *text++ != '.')
            return false;
        if (!isdigit((unsigned char)*text))
            return false;
        unsigned number = 0;
        for (int digits = 0; isdigit((unsigned char)*text); ++digits, ++text) {
            number = number * 10 + (unsigned)(*text - '0');
            if (digits == 3 || number > 255)
                return false;
        }
        value = value << 8 | number;
    }
    if (*text)
        return false;
    *address = value;
    return true;
}

bool boat_net_parse_endpoint(const char *text, char *host_out, size_t host_size, uint16_t *port)
{
    const char *colon = strrchr(text, ':');
    if (!colon || colon == text || (size_t)(colon - text) >= host_size
        || !boat_net_parse_port(colon + 1, port))
        return false;
    memcpy(host_out, text, (size_t)(colon - text));
    host_out[colon - text] = 0;
    return strchr(host_out, ':') == NULL; // IPv4 or a name; no IPv6 literals in v1.
}

struct BoatNetLan {
    ENetSocket socket;
};

static BoatNetLan *lan_open(ENetAddress address, bool shared, char *error, size_t size)
{
    BoatNetLan *lan = malloc(sizeof(*lan));
    if (!lan) {
        snprintf(error, size, "out of memory");
        return NULL;
    }
    lan->socket = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
    if (lan->socket == ENET_SOCKET_NULL) {
        snprintf(error, size, "cannot create a UDP socket");
        free(lan);
        return NULL;
    }
    enet_socket_set_option(lan->socket, ENET_SOCKOPT_NONBLOCK, 1);
    enet_socket_set_option(lan->socket, ENET_SOCKOPT_BROADCAST, 1);
    if (shared) {
        // Every room on this machine then receives broadcast queries.
        enet_socket_set_option(lan->socket, ENET_SOCKOPT_REUSEADDR, 1);
#if defined(SO_REUSEPORT) && !defined(__linux__)
        int on = 1;
        setsockopt(lan->socket, SOL_SOCKET, SO_REUSEPORT, &on, sizeof(on));
#endif
    }
    if (enet_socket_bind(lan->socket, &address) != 0) {
        snprintf(error, size, "UDP port %u is not available", (unsigned)address.port);
        enet_socket_destroy(lan->socket);
        free(lan);
        return NULL;
    }
    return lan;
}

BoatNetLan *boat_net_lan_listen(const char *bind, uint16_t port, char *error, size_t size)
{
    ENetAddress address = { .port = port };
    if (enet_address_set_host_ip(&address, bind) != 0) {
        snprintf(error, size, "invalid IPv4 bind address '%s'", bind);
        return NULL;
    }
    // Broadcasts only reach sockets bound to every interface; a loopback room stays local.
    if (ENET_NET_TO_HOST_32(address.host) >> 24 != 127)
        address.host = ENET_HOST_ANY;
    return lan_open(address, true, error, size);
}

unsigned boat_net_lan_answer(BoatNetLan *lan, const BoatNetLanRoom *room)
{
    uint8_t answer[BOAT_NET_DISCOVERY_BYTES];
    if (!boat_net_lan_room_encode(room, answer))
        return 0;
    unsigned answered = 0;
    for (int i = 0; i < 4; ++i) { // Bounded work per room service.
        uint8_t query[BOAT_NET_DISCOVERY_BYTES + 1];
        ENetBuffer in = { .data = query, .dataLength = sizeof(query) };
        ENetAddress from;
        int received = enet_socket_receive(lan->socket, &from, &in, 1);
        if (received <= 0)
            break;
        if (!boat_net_lan_query_decode(query, (size_t)received))
            continue;
        ENetBuffer out = { .data = answer, .dataLength = sizeof(answer) };
        answered += enet_socket_send(lan->socket, &from, &out, 1) > 0;
    }
    return answered;
}

BoatNetLan *boat_net_lan_search(char *error, size_t size)
{
    return lan_open((ENetAddress) { .host = ENET_HOST_ANY, .port = 0 }, false, error, size);
}

// Subnet broadcast addresses (network order) of the interfaces that support broadcast.
static unsigned subnet_broadcasts(BoatNetLan *lan, uint32_t *out, unsigned capacity)
{
    unsigned count = 0;
#ifdef _WIN32
    INTERFACE_INFO list[32];
    DWORD bytes = 0;
    if (WSAIoctl(lan->socket, SIO_GET_INTERFACE_LIST, NULL, 0, list, (DWORD)sizeof(list), &bytes,
            NULL, NULL)
        != 0)
        return 0;
    for (DWORD i = 0; i < bytes / sizeof(*list) && count < capacity; ++i) {
        u_long flags = list[i].iiFlags;
        if ((flags & IFF_UP) && (flags & IFF_BROADCAST) && !(flags & IFF_LOOPBACK))
            out[count++] = (uint32_t)(list[i].iiAddress.AddressIn.sin_addr.s_addr
                | ~list[i].iiNetmask.AddressIn.sin_addr.s_addr);
    }
#else
    (void)lan;
    struct ifaddrs *list;
    if (getifaddrs(&list) != 0)
        return 0;
    for (struct ifaddrs *p = list; p && count < capacity; p = p->ifa_next) {
        if (!p->ifa_addr || !p->ifa_netmask || p->ifa_addr->sa_family != AF_INET
            || !(p->ifa_flags & IFF_UP) || !(p->ifa_flags & IFF_BROADCAST)
            || (p->ifa_flags & IFF_LOOPBACK))
            continue;
        struct sockaddr_in address, mask;
        memcpy(&address, p->ifa_addr, sizeof(address));
        memcpy(&mask, p->ifa_netmask, sizeof(mask));
        out[count++] = (uint32_t)(address.sin_addr.s_addr | ~mask.sin_addr.s_addr);
    }
    freeifaddrs(list);
#endif
    return count;
}

void boat_net_lan_query(BoatNetLan *lan, uint16_t port)
{
    uint32_t targets[18] = { ENET_HOST_TO_NET_32(0x7f000001u), ENET_HOST_BROADCAST };
    unsigned count = 2 + subnet_broadcasts(lan, targets + 2, 16);
    uint8_t query[BOAT_NET_DISCOVERY_BYTES];
    boat_net_lan_query_encode(query);
    for (unsigned i = 0; i < count; ++i) {
        bool repeated = false;
        for (unsigned j = 0; j < i; ++j)
            repeated |= targets[j] == targets[i];
        if (repeated)
            continue;
        ENetAddress to = { .host = targets[i], .port = port };
        ENetBuffer out = { .data = query, .dataLength = sizeof(query) };
        enet_socket_send(lan->socket, &to, &out, 1); // Unreachable subnets just fail.
    }
}

bool boat_net_lan_receive(BoatNetLan *lan, BoatNetLanRoom *room)
{
    for (;;) {
        uint8_t answer[BOAT_NET_DISCOVERY_BYTES + 1];
        ENetBuffer in = { .data = answer, .dataLength = sizeof(answer) };
        ENetAddress from;
        int received = enet_socket_receive(lan->socket, &from, &in, 1);
        if (received == -2)
            continue; // A truncated (oversized) datagram is not an answer.
        if (received <= 0)
            return false;
        if (!boat_net_lan_room_decode(answer, (size_t)received, room))
            continue;
        if (!room->address)
            room->address = ENET_NET_TO_HOST_32(from.host);
        return true;
    }
}

uint16_t boat_net_lan_port(const BoatNetLan *lan) { return socket_port(lan->socket); }

bool boat_net_lan_wait(BoatNetLan *lan, double timeout)
{
    enet_uint32 condition = ENET_SOCKET_WAIT_RECEIVE;
    return enet_socket_wait(lan->socket, &condition, (enet_uint32)(timeout * 1000)) == 0
        && (condition & ENET_SOCKET_WAIT_RECEIVE);
}

void boat_net_lan_close(BoatNetLan *lan)
{
    if (!lan)
        return;
    enet_socket_destroy(lan->socket);
    free(lan);
}

void boat_net_host_name(char out[BOAT_NET_NAME_BYTES + 1])
{
    char name[256] = { 0 };
    out[0] = 0;
    if (gethostname(name, sizeof(name) - 1) != 0)
        return;
    name[strcspn(name, ".")] = 0; // The first label: "ferry.local" or "ferry.lan" is "ferry".
    size_t length = 0;
    for (; name[length] && length < BOAT_NET_NAME_BYTES; ++length)
        out[length] = isprint((unsigned char)name[length]) ? name[length] : '?';
    out[length] = 0;
}
