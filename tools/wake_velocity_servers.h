#ifndef WAKE_VELOCITY_SERVERS_H
#define WAKE_VELOCITY_SERVERS_H

// AR3 server discovery for the title: the public room directory and the LAN room search. CMake
// selects one implementation: wake_velocity_servers.c (native: boat_directory's background
// client and ENet LAN broadcast) or wake_velocity_servers_web.c (the page's Module.boatServers
// fetch; browsers have no LAN search). No call waits: a refresh or query starts work that later
// polls observe, and the entries stay those of the last completed fetch.
#include "boat_net_protocol.h"
#include <stdbool.h>
#include <stddef.h>

typedef enum { WAKE_SERVER_NAME, WAKE_SERVER_DESCRIPTION, WAKE_SERVER_ADDRESS } WakeServerField;

// Once per process, after the window opens and before it closes. Native start begins the first
// directory fetch; the browser page owns its fetch, so both are no-ops there.
void wake_servers_start(void);
void wake_servers_stop(void);
// Ask for a fresh directory listing; poll once per title frame to collect it.
void wake_servers_refresh(void);
void wake_servers_poll(void);
int wake_servers_count(void);
// Whether entry INDEX speaks this protocol and physics revision (false when out of range).
bool wake_servers_compatible(int index, int protocol, int physics);
// Entry INDEX's FIELD, or "" when out of range; and the directory's status line.
void wake_servers_text(int index, WakeServerField field, char *out, size_t size);
void wake_servers_status(char *out, size_t size);

// LAN room search while the multiplayer panel is open. Open returns NULL when no search is
// available (always in the browser). Query sends one broadcast; receive returns one heard room.
typedef struct WakeLanSearch WakeLanSearch;
WakeLanSearch *wake_lan_open(void);
void wake_lan_query(WakeLanSearch *search);
bool wake_lan_receive(WakeLanSearch *search, BoatNetLanRoom *room);
void wake_lan_close(WakeLanSearch *search); // NULL is allowed.
// The title's hint when the LAN search is unavailable.
const char *wake_lan_unavailable_hint(void);

#endif
