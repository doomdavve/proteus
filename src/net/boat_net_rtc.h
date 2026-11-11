#ifndef PROTEUS_BOAT_NET_RTC_H
#define PROTEUS_BOAT_NET_RTC_H
#include "boat_net_transport.h"

// Optional native WebRTC endpoint. WebSocket signaling exchanges complete SDP; gameplay
// uses five negotiated DataChannels with the protocol's existing delivery classes.
// Poll on the authority thread. Only libdatachannel's status/accept callbacks cross threads.
typedef struct BoatNetRtc BoatNetRtc;
BoatNetRtc *boat_net_rtc_listen(
    const char *bind, uint16_t port, const char *ice_server, char *error, size_t size);
BoatNetTransport boat_net_rtc_transport(BoatNetRtc *rtc);
void boat_net_rtc_close(BoatNetRtc *rtc);
#endif
