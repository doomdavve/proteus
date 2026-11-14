#ifndef PROTEUS_BOAT_NET_SDP_H
#define PROTEUS_BOAT_NET_SDP_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

// Untrusted SDP offers name the addresses a browser would like to be reached on. Browsers hide
// local addresses behind mDNS names ("<uuid>.local") and a hostile client can name anything. The
// WebRTC library resolves such names with getaddrinfo on its own thread, so one offer could make
// the server open resolver files and sockets and send DNS queries. The authority never needs
// that, so the offer is filtered first: only numeric IP candidates pass through.

// True for a complete dotted-quad IPv4 or colon-hex IPv6 literal (no zone, no name).
bool boat_net_sdp_is_ip_literal(const char *text, size_t length);

// Copies the NUL-terminated `offer` to `out` (capacity `capacity`, at least 1), leaving out every
// `a=candidate:` line whose address is not a numeric IP literal. Returns the length written, or
// SIZE_MAX if the result does not fit. `dropped` (optional) receives how many lines were removed.
size_t boat_net_sdp_filter_offer(const char *offer, char *out, size_t capacity, unsigned *dropped);
#endif
