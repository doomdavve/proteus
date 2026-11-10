#include "wake_velocity_servers.h"
#include "boat_directory.h"
#include "boat_net_enet.h"
#include <stdio.h>
#include <stdlib.h>

static BoatDirectory *directory;

void wake_servers_start(void)
{
    directory = boat_directory_create();
    boat_directory_refresh(directory);
}

void wake_servers_stop(void)
{
    boat_directory_destroy(directory);
    directory = NULL;
}

void wake_servers_refresh(void) { boat_directory_refresh(directory); }
void wake_servers_poll(void) { boat_directory_poll(directory); }
int wake_servers_count(void) { return (int)boat_directory_list(directory)->count; }

bool wake_servers_compatible(int index, int protocol, int physics)
{
    const BoatDirectoryList *list = boat_directory_list(directory);
    return index >= 0 && (unsigned)index < list->count && list->entries[index].protocol == protocol
        && list->entries[index].physics == physics;
}

void wake_servers_text(int index, WakeServerField field, char *out, size_t size)
{
    const BoatDirectoryList *list = boat_directory_list(directory);
    const char *value = "";
    if (index >= 0 && (unsigned)index < list->count) {
        const BoatDirectoryEntry *entry = &list->entries[index];
        value = field == WAKE_SERVER_NAME      ? entry->name
            : field == WAKE_SERVER_DESCRIPTION ? entry->description
                                               : entry->address;
    }
    snprintf(out, size, "%s", value);
}

void wake_servers_status(char *out, size_t size)
{
    snprintf(out, size, "%s", boat_directory_status(directory));
}

struct WakeLanSearch {
    BoatNetLan *lan;
};

WakeLanSearch *wake_lan_open(void)
{
    if (!boat_net_enet_startup())
        return NULL;
    char error[160];
    BoatNetLan *lan = boat_net_lan_search(error, sizeof(error));
    if (!lan) {
        boat_net_enet_cleanup();
        return NULL;
    }
    WakeLanSearch *search = malloc(sizeof(*search));
    if (!search) {
        boat_net_lan_close(lan);
        boat_net_enet_cleanup();
        return NULL;
    }
    search->lan = lan;
    return search;
}

void wake_lan_query(WakeLanSearch *search)
{
    boat_net_lan_query(search->lan, BOAT_NET_DISCOVERY_PORT);
}

bool wake_lan_receive(WakeLanSearch *search, BoatNetLanRoom *room)
{
    return boat_net_lan_receive(search->lan, room);
}

void wake_lan_close(WakeLanSearch *search)
{
    if (!search)
        return;
    boat_net_lan_close(search->lan);
    boat_net_enet_cleanup();
    free(search);
}

const char *wake_lan_unavailable_hint(void) { return "NO LAN SEARCH: TYPE AN ADDRESS BELOW"; }
