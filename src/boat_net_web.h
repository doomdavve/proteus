#ifndef PROTEUS_BOAT_NET_WEB_H
#define PROTEUS_BOAT_NET_WEB_H
#include "boat_net_transport.h"
// Browser-side singleton: JavaScript owns the WebRTC connection and bounded event queue.
bool boat_net_web_connect(const char *url, char *error, size_t size);
BoatNetTransport boat_net_web_transport(void);
void boat_net_web_close(void);
const char *boat_net_web_failure(void);
double boat_net_clock(void);
#endif
