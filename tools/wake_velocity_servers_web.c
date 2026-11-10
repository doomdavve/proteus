#include "wake_velocity_servers.h"
#include <emscripten.h>

// clang-format off
EM_JS(void, servers_refresh, (void), { Module['boatServers'].refresh(); })
EM_JS(int, servers_count, (void), { return Module['boatServers'].count(); })
EM_JS(int, servers_compatible, (int index, int protocol, int physics),
    { return Module['boatServers'].compatible(index, protocol, physics); })
EM_JS(void, servers_text, (int index, int field, char *out, int size), {
    const server = Module['boatServers'].server(index);
    const value = field === 3 ? Module['boatServers'].status()
        : server ? [server.name, server.description, server.websocket][field] : "";
    stringToUTF8(value, out, size);
})
// clang-format on

void wake_servers_start(void) { }
void wake_servers_stop(void) { }
void wake_servers_refresh(void) { servers_refresh(); }
void wake_servers_poll(void) { }
int wake_servers_count(void) { return servers_count(); }

bool wake_servers_compatible(int index, int protocol, int physics)
{
    return servers_compatible(index, protocol, physics) != 0;
}

void wake_servers_text(int index, WakeServerField field, char *out, size_t size)
{
    servers_text(index, (int)field, out, (int)size);
}

void wake_servers_status(char *out, size_t size) { servers_text(0, 3, out, (int)size); }

WakeLanSearch *wake_lan_open(void) { return NULL; }

void wake_lan_query(WakeLanSearch *search) { (void)search; }

bool wake_lan_receive(WakeLanSearch *search, BoatNetLanRoom *room)
{
    (void)search;
    (void)room;
    return false;
}

void wake_lan_close(WakeLanSearch *search) { (void)search; }

const char *wake_lan_unavailable_hint(void) { return "JOIN A WEBRTC ROOM: WS:// OR WSS://"; }
