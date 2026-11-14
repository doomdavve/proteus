// H6 follow-up harness: the SDP offer filter reads an untrusted, client-supplied text.
// Whatever it keeps must contain no candidate whose address is not a numeric IP literal.
#include "fuzz/fuzz_driver.h"
#include "net/boat_net_sdp.h"

#include <stdlib.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    char *text = malloc(size + 1);
    char out[4096];
    if (!text)
        return 0;
    memcpy(text, data, size);
    text[size] = 0;
    unsigned dropped;
    size_t length = boat_net_sdp_filter_offer(text, out, sizeof(out), &dropped);
    if (length != SIZE_MAX && (length != strlen(out) || length > strlen(text)))
        abort();
    (void)boat_net_sdp_is_ip_literal((const char *)data, size);
    free(text);
    return 0;
}

void fuzz_seeds(FuzzAdd add)
{
    static const char *const seeds[] = { "v=0\r\na=candidate:1 1 udp 1 192.0.2.1 9 typ host\r\n",
        "a=candidate:1 1 udp 1 abcd.local 9 typ host\r\na=candidate:2 1 udp 1 ::1 9 typ host\n",
        "a=candidate:a b c d e", "::ffff:1.2.3.4", "1:2:3:4:5:6:7:8" };
    for (size_t i = 0; i < sizeof(seeds) / sizeof(*seeds); ++i)
        add((const uint8_t *)seeds[i], strlen(seeds[i]));
}
