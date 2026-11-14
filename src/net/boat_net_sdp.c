#include "net/boat_net_sdp.h"

#include <stdint.h>
#include <string.h>

static bool hex_digit(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static bool ipv4(const char *s, size_t n)
{
    unsigned parts = 0, digits = 0, value = 0;
    for (size_t i = 0; i <= n; ++i) {
        char c = i < n ? s[i] : '.';
        if (c >= '0' && c <= '9') {
            if (digits && value == 0)
                return false; // Leading zero.
            value = value * 10 + (unsigned)(c - '0');
            if (++digits > 3 || value > 255)
                return false;
        } else if (c == '.' && digits) {
            ++parts;
            digits = value = 0;
        } else {
            return false;
        }
    }
    return parts == 4;
}

static bool ipv6(const char *s, size_t n)
{
    unsigned groups = 0;
    bool compressed = false;
    size_t i = 0;
    if (n >= 2 && s[0] == ':' && s[1] == ':') {
        compressed = true;
        i = 2;
        if (n == 2)
            return true;
    } else if (n && s[0] == ':') {
        return false;
    }
    while (i < n) {
        size_t start = i;
        while (i < n && s[i] != ':')
            ++i;
        size_t length = i - start;
        if (length == 0)
            return false;
        if (memchr(s + start, '.', length)) { // Embedded IPv4 closes the address.
            if (i != n || !ipv4(s + start, length))
                return false;
            groups += 2;
            return compressed ? groups <= 7 : groups == 8;
        }
        if (length > 4)
            return false;
        for (size_t k = start; k < i; ++k)
            if (!hex_digit(s[k]))
                return false;
        ++groups;
        if (i == n)
            break;
        ++i; // The ':' after the group.
        if (i < n && s[i] == ':') {
            if (compressed)
                return false;
            compressed = true;
            ++i;
            if (i == n)
                break;
        } else if (i == n) {
            return false; // Trailing single colon.
        }
    }
    return compressed ? groups <= 7 : groups == 8;
}

bool boat_net_sdp_is_ip_literal(const char *text, size_t length)
{
    if (!text || length == 0 || length > 45)
        return false;
    return memchr(text, ':', length) ? ipv6(text, length) : ipv4(text, length);
}

// `a=candidate:<foundation> <component> <transport> <priority> <address> <port> ...`
static bool candidate_allowed(const char *line, size_t length)
{
    static const char prefix[] = "a=candidate:";
    if (length < sizeof(prefix) - 1 || memcmp(line, prefix, sizeof(prefix) - 1) != 0)
        return true;
    size_t at = sizeof(prefix) - 1;
    for (unsigned field = 0; field < 4; ++field) { // Skip foundation..priority.
        while (at < length && line[at] != ' ')
            ++at;
        if (at >= length)
            return false;
        ++at;
    }
    size_t start = at;
    while (at < length && line[at] != ' ')
        ++at;
    return at < length && boat_net_sdp_is_ip_literal(line + start, at - start);
}

size_t boat_net_sdp_filter_offer(const char *offer, char *out, size_t capacity, unsigned *dropped)
{
    size_t written = 0;
    unsigned removed = 0;
    if (dropped)
        *dropped = 0;
    if (!offer || !out || capacity == 0)
        return SIZE_MAX;
    while (*offer) {
        const char *end = offer;
        while (*end && *end != '\n')
            ++end;
        size_t line = (size_t)(end - offer) + (*end == '\n');
        size_t content = (size_t)(end - offer);
        if (content && offer[content - 1] == '\r')
            --content;
        if (candidate_allowed(offer, content)) {
            if (line > capacity - 1 - written)
                return SIZE_MAX;
            memcpy(out + written, offer, line);
            written += line;
        } else {
            ++removed;
        }
        offer += line;
    }
    out[written] = '\0';
    if (dropped)
        *dropped = removed;
    return written;
}
