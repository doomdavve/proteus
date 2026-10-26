#ifndef PROTEUS_BOAT_NET_ENET_H
#define PROTEUS_BOAT_NET_ENET_H

#include "boat_net_transport.h"

// ENet adapter for the transport boundary: IPv4 only, five channels mapped from the delivery
// classes, a 1200-byte UDP payload MTU and bounded packet/waiting-data allocations. ENet types
// stay inside boat_net_enet.c.
#define BOAT_NET_ENET_MTU 1200
#define BOAT_NET_ENET_WAITING_BYTES (64 * 1024) // Per peer, as ENet counts it.

typedef struct BoatNetEnet BoatNetEnet;

// Once per process before any host, and after the last one.
bool boat_net_enet_startup(void);
void boat_net_enet_cleanup(void);
// Listen on a numeric IPv4 ADDRESS for BOAT_NET_MAX_PEERS connections. NULL with ERROR set.
BoatNetEnet *boat_net_enet_listen(const char *address, uint16_t port, char *error, size_t size);
// Start connecting to HOST (numeric or a name resolved to IPv4); the transport reports the
// connection as an event. NULL with ERROR set.
BoatNetEnet *boat_net_enet_connect(const char *host, uint16_t port, char *error, size_t size);
BoatNetTransport boat_net_enet_transport(BoatNetEnet *enet);
// Send queued packets now.
void boat_net_enet_flush(BoatNetEnet *enet);
// Block up to SECONDS for network activity; the event (if any) is returned by the next poll.
void boat_net_enet_wait(BoatNetEnet *enet, double seconds);
// Tests with every host in one process: service HOSTS until each datagram any of them sent has
// been received by one of them (loopback delivery is asynchronous), keeping the events for the
// next polls. False if that takes longer than TIMEOUT seconds or an event stash fills.
bool boat_net_enet_settle(BoatNetEnet *const *hosts, unsigned count, double timeout);
// Disconnect every peer gracefully, waiting up to SECONDS, then destroy the host.
void boat_net_enet_close(BoatNetEnet *enet, double seconds);
// Negotiated UDP payload MTU of transport peer PEER, or zero when not connected.
unsigned boat_net_enet_peer_mtu(BoatNetEnet *enet, unsigned peer);
// The local UDP port, as bound (a listen on port 0 gets an ephemeral one); 0 if unknown.
uint16_t boat_net_enet_port(const BoatNetEnet *enet);

// Monotonic seconds for scheduling (not wall-clock time).
double boat_net_clock(void);
void boat_net_sleep(double seconds);
// Parse "HOST:PORT" (PORT 1..65535). HOST is copied into HOST_OUT.
bool boat_net_parse_endpoint(const char *text, char *host_out, size_t host_size, uint16_t *port);
bool boat_net_parse_port(const char *text, uint16_t *port);
// Dotted-quad IPv4 TEXT as a << 24 | b << 16 | c << 8 | d.
bool boat_net_parse_ipv4(const char *text, uint32_t *address);

// LAN discovery sockets (plain UDP, protocol in boat_net_protocol.h). A room's answering socket
// listens on PORT: loopback only when BIND is a loopback address, otherwise every interface,
// shared with other rooms on this machine where the system allows it. NULL with ERROR set.
typedef struct BoatNetLan BoatNetLan;
BoatNetLan *boat_net_lan_listen(const char *bind, uint16_t port, char *error, size_t size);
// Answer the queries waiting (a few per call) with ROOM; returns how many were answered.
unsigned boat_net_lan_answer(BoatNetLan *lan, const BoatNetLanRoom *room);
// A searching socket on an ephemeral port.
BoatNetLan *boat_net_lan_search(char *error, size_t size);
// Send one query to PORT on loopback, the limited broadcast address and every broadcast-capable
// interface's subnet broadcast address.
void boat_net_lan_query(BoatNetLan *lan, uint16_t port);
// The next answer without blocking. A room advertising no address gets its sender's.
bool boat_net_lan_receive(BoatNetLan *lan, BoatNetLanRoom *room);
// The local UDP port, as bound; 0 if unknown.
uint16_t boat_net_lan_port(const BoatNetLan *lan);
// Block until LAN has a datagram to read, at most TIMEOUT seconds; false if none arrived.
bool boat_net_lan_wait(BoatNetLan *lan, double timeout);
void boat_net_lan_close(BoatNetLan *lan);
// This machine's name for answers: its first label in printable ASCII, at most
// BOAT_NET_NAME_BYTES; empty when unknown.
void boat_net_host_name(char out[BOAT_NET_NAME_BYTES + 1]);

#endif
