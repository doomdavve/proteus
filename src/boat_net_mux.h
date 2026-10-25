#ifndef PROTEUS_BOAT_NET_MUX_H
#define PROTEUS_BOAT_NET_MUX_H
#include "boat_net_transport.h"
// Two transports share one authority's peer namespace. No platform types enter the core.
typedef struct {
    BoatNetTransport transports[2];
    bool occupied[BOAT_NET_MAX_PEERS];
    unsigned owner[BOAT_NET_MAX_PEERS], local[BOAT_NET_MAX_PEERS], cursor;
} BoatNetMux;
void boat_net_mux_init(BoatNetMux *mux, BoatNetTransport first, BoatNetTransport second);
BoatNetTransport boat_net_mux_transport(BoatNetMux *mux);
#endif
