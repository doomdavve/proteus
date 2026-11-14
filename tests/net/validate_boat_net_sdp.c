// The SDP offer filter keeps name-based ICE candidates away from the WebRTC library's resolver.
#include "net/boat_net_sdp.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
            ++failures; \
        } \
    } while (0)

static bool literal(const char *s) { return boat_net_sdp_is_ip_literal(s, strlen(s)); }

static void literals(void)
{
    const char *good[] = { "10.0.21.3", "0.0.0.0", "255.255.255.255", "::", "::1", "fe80::1",
        "2001:db8::8a2e:370:7334", "2001:db8:0:0:0:0:0:1", "::ffff:192.0.2.1", "1::" };
    const char *bad[] = { "", "a.local", "3f2b1c9e-0000-4000-8000-000000000000.local", "localhost",
        "example.com", "10.0.21", "10.0.21.3.4", "256.1.1.1", "01.2.3.4", "1.2.3.", ".1.2.3",
        "1..2.3", ":", ":::", "1:2:3:4:5:6:7:8:9", "1::2::3", "12345::1", "fe80::1%en0", "g::1",
        "1:2:3:4:5:6:7", "::1.2.3", "1:2:3:4:5:6:7:1.2.3.4", "0x7f.0.0.1", "127.1", "::ffff:1.2.3.256" };
    for (size_t i = 0; i < sizeof(good) / sizeof(*good); ++i)
        CHECK(literal(good[i]));
    for (size_t i = 0; i < sizeof(bad) / sizeof(*bad); ++i)
        CHECK(!literal(bad[i]));
}

static void filtering(void)
{
    const char *offer = "v=0\r\n"
                        "a=candidate:1 1 udp 2113937151 7c9e6679-7425-40de-944b-e07fc1f90ae7.local "
                        "54321 typ host\r\n"
                        "a=candidate:2 1 udp 2113937151 192.0.2.7 54322 typ host\r\n"
                        "a=candidate:3 1 udp 1677729535 2001:db8::7 54323 typ srflx\r\n"
                        "a=candidate:4 1 udp 100 evil.example.com 9 typ host\r\n"
                        "a=candidate:5 1 udp 100\r\n"
                        "a=end-of-candidates\r\n"
                        "m=application 9 UDP/DTLS/SCTP webrtc-datachannel\n";
    char out[1024];
    unsigned dropped = 99;
    size_t length = boat_net_sdp_filter_offer(offer, out, sizeof(out), &dropped);
    CHECK(length == strlen(out));
    CHECK(dropped == 3);
    CHECK(strstr(out, "192.0.2.7") && strstr(out, "2001:db8::7"));
    CHECK(!strstr(out, ".local") && !strstr(out, "evil") && !strstr(out, "candidate:5"));
    CHECK(strstr(out, "v=0\r\n") == out && strstr(out, "a=end-of-candidates\r\n"));
    CHECK(strstr(out, "webrtc-datachannel\n")); // A final line keeps its ending.

    // No candidate lines: the text is copied unchanged. Unrelated lines are never touched.
    const char *plain = "v=0\r\na=ice-ufrag:abc\r\nc=IN IP4 0.0.0.0";
    length = boat_net_sdp_filter_offer(plain, out, sizeof(out), &dropped);
    CHECK(length == strlen(plain) && strcmp(out, plain) == 0 && dropped == 0);

    // Exact fit works; one byte too small is refused; a hostile line is removed, not truncated.
    char exact[sizeof("v=0\r\n")];
    CHECK(boat_net_sdp_filter_offer("v=0\r\n", exact, sizeof(exact), NULL) == 5);
    CHECK(boat_net_sdp_filter_offer("v=0\r\n", exact, sizeof(exact) - 1, NULL) == SIZE_MAX);
    CHECK(boat_net_sdp_filter_offer(NULL, out, sizeof(out), NULL) == SIZE_MAX);
    CHECK(boat_net_sdp_filter_offer("", out, 1, &dropped) == 0 && out[0] == 0);
}

int main(void)
{
    literals();
    filtering();
    if (failures)
        return 1;
    puts("boat_net_sdp=ok");
    return 0;
}
